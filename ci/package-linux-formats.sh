#!/usr/bin/env bash
# Run inside the packaging image after linuxdeploy has populated AppDir.
set -euo pipefail

: "${BUILD:?}" "${OUT:?}" "${VERSION:?}"
app_id=io.github.asprenas.notepadpp_qt
name=notepadpp-qt
base="$name-$VERSION-linux-x86_64"
appdir="$BUILD/AppDir"
work="$BUILD/formats"
# Keep the same bundled Qt and relative paths as the AppImage, including AppRun.
tar -C "$BUILD" -czf "$OUT/$base.tar.gz" AppDir

# The deb, rpm and Flatpak formats are deferred to a future release.
: <<'DEFERRED_FORMATS'
rm -rf "$work"
mkdir -p "$work/root/opt/$name" "$work/root/usr/bin" \
    "$work/root/usr/share/applications" "$work/root/usr/share/icons/hicolor/128x128/apps"
rm -f "$OUT/$base."{deb,rpm,flatpak,tar.gz}

# Keep the same bundled Qt and relative paths as the AppImage, including AppRun.
tar -C "$BUILD" -czf "$OUT/$base.tar.gz" AppDir
cp "$OUT/$base.AppImage" "$work/root/opt/$name/$name.AppImage"
printf '%s\n' '#!/bin/sh' 'export APPIMAGE_EXTRACT_AND_RUN=1' \
    'exec /opt/notepadpp-qt/notepadpp-qt.AppImage "$@"' > "$work/root/usr/bin/$name"
chmod 755 "$work/root/usr/bin/$name"
cp "$appdir/usr/share/applications/$app_id.desktop" "$work/root/usr/share/applications/"
cp "$appdir/usr/share/icons/hicolor/128x128/apps/$name.png" "$work/root/usr/share/icons/hicolor/128x128/apps/"

for format in deb rpm; do
    fpm -s dir -t "$format" -n "$name" -v "$VERSION" -a amd64 \
        --license GPL-3.0-or-later --maintainer 'Notepad++ Qt port contributors' \
        --url https://github.com/asprenas/notepad-plus-plus \
        --description 'Notepad++ Qt source code editor' \
        -C "$work/root" -p "$OUT/$base.$format" .
done

# Export an offline Flatpak bundle using the shared freedesktop runtime.
flatpak build-init --arch=x86_64 "$work/flatpak" "$app_id" \
    org.freedesktop.Sdk org.freedesktop.Platform 24.08
cp -a "$appdir/usr/." "$work/flatpak/files/"
mkdir -p "$work/flatpak/files/share/applications" "$work/flatpak/files/share/icons/hicolor/128x128/apps"
sed 's/^Icon=notepadpp-qt$/Icon=io.github.asprenas.notepadpp_qt/' \
    "$appdir/usr/share/applications/$app_id.desktop" > "$work/flatpak/files/share/applications/$app_id.desktop"
cp "$appdir/usr/share/icons/hicolor/128x128/apps/$name.png" \
    "$work/flatpak/files/share/icons/hicolor/128x128/apps/$app_id.png"
flatpak build-finish --command="$name" --share=ipc --share=network \
    --socket=fallback-x11 --socket=wayland --device=dri --filesystem=host "$work/flatpak"
flatpak build-export --arch=x86_64 "$work/repo" "$work/flatpak" stable
flatpak build-bundle --arch=x86_64 --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo \
    "$work/repo" "$OUT/$base.flatpak" "$app_id" stable
DEFERRED_FORMATS
for format in AppImage tar.gz; do
    test -s "$OUT/$base.$format"
done
