#!/usr/bin/env bash
# Checks the AppImage in dist/ on fresh containers of several distributions
# (docs/plan/phase-6-packaging-stabilization.md section 5). Each container gets only what a desktop
# already has (X server, Mesa, fontconfig, fonts) - no Qt, no compiler - then:
#   1. the embedded .desktop file is valid and lists the MIME types
#   2. the AppImage starts on the xcb platform (Xvfb) and renders a file (--screenshot)
#   3. the .desktop Exec line opens a file (what a file manager runs for an association)
#   4. "install" (copy to ~/Applications + desktop entry + icon) and "uninstall" (delete)
#
# Usage: ci/test-package-linux.sh [--dist DIR] [image ...]
#        default images: ubuntu:22.04 ubuntu:24.04 debian:12 fedora:41
# Report: build/package/test-linux/report.txt
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist"
IMAGES=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dist) DIST="$2"; shift ;;
        -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) IMAGES+=("$1") ;;
    esac
    shift
done
[[ ${#IMAGES[@]} -gt 0 ]] || IMAGES=(ubuntu:22.04 ubuntu:24.04 debian:12 fedora:41)

VERSION="$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
APPIMAGE="notepadpp-qt-$VERSION-linux-x86_64.AppImage"
[[ -f "$DIST/$APPIMAGE" ]] || { echo "missing $DIST/$APPIMAGE (run ci/package-linux.sh)"; exit 1; }
WORK="$ROOT/build/package/test-linux"
rm -rf "$WORK"; mkdir -p "$WORK"
REPORT="$WORK/report.txt"
echo "Linux package test $APPIMAGE  $(date -Iseconds)" >"$REPORT"

dist="$DIST"; work="$WORK"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) dist="$(cygpath -m "$DIST")"; work="$(cygpath -m "$WORK")" ;; esac

FAILED=0
for image in "${IMAGES[@]}"; do
    tag="${image//[:\/]/-}"
    echo "== $image"
    MSYS_NO_PATHCONV=1 docker run --rm -v "$dist:/dist:ro" -v "$work:/out" -e APPIMAGE_NAME="$APPIMAGE" -e TAG="$tag" \
        "$image" bash -c '
        set -u
        if command -v apt-get >/dev/null; then
            export DEBIAN_FRONTEND=noninteractive
            apt-get update -qq >/dev/null && apt-get install -y -qq --no-install-recommends \
                xvfb xauth libgl1 libegl1 libopengl0 libfontconfig1 libfreetype6 libx11-xcb1 libxkbcommon-x11-0 \
                libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 \
                libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libdbus-1-3 libglib2.0-0 fonts-dejavu-core \
                desktop-file-utils ca-certificates >/dev/null 2>&1
        else
            dnf install -y -q xorg-x11-server-Xvfb xauth libgpg-error mesa-libGL mesa-libEGL libglvnd-opengl fontconfig freetype libxkbcommon-x11 \
                xcb-util-cursor xcb-util-wm xcb-util-image xcb-util-keysyms xcb-util-renderutil dbus-libs glib2 \
                dejavu-sans-fonts desktop-file-utils which >/dev/null 2>&1
        fi
        useradd -m tester
        cp /dist/$APPIMAGE_NAME /home/tester/ && chown tester /home/tester/$APPIMAGE_NAME
        su tester -c "bash -s" <<"EOS" 2>&1
set -u
cd ~
r() { printf "%-56s %s\n" "$1" "$2"; }
app=./$APPIMAGE_NAME
chmod +x $app
printf "hello from %s\n" "$TAG" > sample.txt
export APPIMAGE_EXTRACT_AND_RUN=1 NPP_QT_INSTANCE_KEY=pkgtest

# 1. desktop file inside the AppImage
$app --appimage-extract "*.desktop" >/dev/null 2>&1
d=$(ls squashfs-root/*.desktop 2>/dev/null | head -1)
if [ -n "$d" ] && desktop-file-validate "$d" && grep -q "^MimeType=.*text/plain" "$d"; then r "desktop file valid, MIME types" PASS; else r "desktop file valid, MIME types" FAIL; fi

# 2. xcb platform under Xvfb
out=$(xvfb-run -a -s "-screen 0 1280x800x24" $app --screenshot /tmp/shot.png sample.txt 2>&1)
if [ -s /tmp/shot.png ]; then r "starts on xcb (Xvfb), renders a file" PASS; cp /tmp/shot.png /out/$TAG.png; else r "starts on xcb (Xvfb), renders a file" "FAIL $(echo "$out" | tail -3 | tr "\n" " ")"; fi

# 3. Exec line of the desktop file with a file (file manager association)
exec_line=$(sed -n "s/^Exec=//p" "$d" | head -1)
cmd=${exec_line/notepadpp-qt/$app}
cmd=${cmd/\%F/$HOME/sample.txt}
out=$(xvfb-run -a $cmd --quit-after=2500 2>&1)
if echo "$out" | grep -q "state: tabs=1 "; then r "desktop Exec line opens the file" PASS; else r "desktop Exec line opens the file" "FAIL $(echo "$out" | tail -3 | tr "\n" " ")"; fi
if [ -f ~/.config/notepad++qt/config.xml ]; then r "settings in ~/.config/notepad++qt" PASS; else r "settings in ~/.config/notepad++qt" FAIL; fi

# 4. install / uninstall (AppImage: copy + menu entry; delete)
mkdir -p ~/Applications ~/.local/share/applications ~/.local/share/icons/hicolor/128x128/apps
cp $app ~/Applications/
sed "s|^Exec=notepadpp-qt|Exec=$HOME/Applications/$APPIMAGE_NAME|" "$d" > ~/.local/share/applications/io.github.asprenas.notepadpp_qt.desktop
$app --appimage-extract "usr/share/icons/hicolor/128x128/apps/notepadpp-qt.png" >/dev/null 2>&1
cp squashfs-root/usr/share/icons/hicolor/128x128/apps/notepadpp-qt.png ~/.local/share/icons/hicolor/128x128/apps/ 2>/dev/null
if desktop-file-validate ~/.local/share/applications/io.github.asprenas.notepadpp_qt.desktop && [ -f ~/.local/share/icons/hicolor/128x128/apps/notepadpp-qt.png ]; then r "install: menu entry + icon" PASS; else r "install: menu entry + icon" FAIL; fi
out=$(xvfb-run -a ~/Applications/$APPIMAGE_NAME --quit-after=2000 2>&1)
if echo "$out" | grep -q "state: tabs="; then r "installed copy runs" PASS; else r "installed copy runs" FAIL; fi
rm -f ~/Applications/$APPIMAGE_NAME ~/.local/share/applications/io.github.asprenas.notepadpp_qt.desktop ~/.local/share/icons/hicolor/128x128/apps/notepadpp-qt.png
if [ ! -e ~/Applications/$APPIMAGE_NAME ] && [ ! -e ~/.local/share/applications/io.github.asprenas.notepadpp_qt.desktop ]; then r "uninstall: files removed" PASS; else r "uninstall: files removed" FAIL; fi
EOS
    ' >"$WORK/$tag.log" 2>&1
    {
        echo "-- $image"
        grep -E "PASS|FAIL" "$WORK/$tag.log" || echo "no result (see $tag.log)"
    } | tee -a "$REPORT"
    if ! grep -q "PASS" "$WORK/$tag.log" || grep -q "FAIL" "$WORK/$tag.log"; then FAILED=1; fi
done
echo "report: ${REPORT#$ROOT/}"
exit $FAILED
