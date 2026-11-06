#!/usr/bin/env bash
# Linux package of the Qt port (docs/phase-6-packaging-stabilization.md 3.2): AppImage.
#
# Default: built in the ci/docker/appimage.Dockerfile container (Ubuntu 22.04 + official Qt),
# from any host with Docker (Linux, or Windows with Docker Desktop). This is the release
# package: glibc 2.35, it runs on Ubuntu 22.04 and later distributions.
#
# --local: built on this Linux host without Docker (local check of the packaging). Needs Qt
# (QT_DIR, default ~/Qt/<Qt version of the Dockerfile>/gcc_64), CMake, Ninja (or
# CMAKE_MAKE_PROGRAM), pkg-config + gio-2.0, and linuxdeploy, linuxdeploy-plugin-qt,
# linuxdeploy-plugin-appimage in PATH (or LINUXDEPLOY_DIR). The AppImage needs at least the
# glibc of this host.
#
# Usage: ci/package-linux.sh [--local] [--dist DIR]
# Output: dist/notepadpp-qt-<version>-linux-x86_64.AppImage
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist"
IMAGE="npp-qt-appimage"
VOLUME="npp-qt-appimage-build"   # incremental build tree between runs
LOCAL=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dist) DIST="$2"; shift ;;
        --local) LOCAL=1 ;;
        -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1"; exit 2 ;;
    esac
    shift
done

VERSION="$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
[[ -n "$VERSION" ]] || { echo "project VERSION not found"; exit 1; }
OUTPUT="notepadpp-qt-$VERSION-linux-x86_64.AppImage"
mkdir -p "$DIST"
DIST="$(cd "$DIST" && pwd)"

# Build, install into AppDir and run linuxdeploy. Same steps in the container and on the host;
# paths come from SRC (sources), OUT (output folder) and BUILD (build tree).
read -r -d '' PACKAGE_STEPS <<'STEPS' || true
cmake -S "$SRC" -B "$BUILD/release" -G Ninja ${CMAKE_MAKE_PROGRAM:+-DCMAKE_MAKE_PROGRAM="$CMAKE_MAKE_PROGRAM"} \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_DIR" \
      -DNPP_DOCKING_BACKEND=ADS -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
# Elevated save through GVfs admin:// needs gio-2.0 at build time (pkexec only otherwise)
grep -q "^GIO_FOUND:INTERNAL=1" "$BUILD/release/CMakeCache.txt" \
    || { echo "gio-2.0 not found (pkg-config, libglib2.0-dev): the package would lack GVfs elevated save"; exit 1; }
cmake --build "$BUILD/release" -j"$(nproc)" | grep -E "error|warning: unused|FAILED" || true
test -x "$BUILD/release/bin/notepadpp-qt"
rm -rf "$BUILD/AppDir" && DESTDIR="$BUILD/AppDir" cmake --install "$BUILD/release" >/dev/null
cp "$SRC/LICENSE" "$BUILD/AppDir/usr/share/notepadpp-qt/LICENSE.txt"

export QMAKE="$QT_DIR/bin/qmake" LD_LIBRARY_PATH="$QT_DIR/lib"
export EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqoffscreen.so"
export EXTRA_QT_MODULES=waylandclient EXTRA_QT_PLUGINS="wayland-shell-integration;wayland-decoration-client;wayland-graphics-integration-client"
export DEPLOY_PLATFORM_THEMES=1
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION" APPIMAGE_EXTRACT_AND_RUN=1
cd "$BUILD"
rm -f "$OUTPUT"
linuxdeploy --appdir AppDir --plugin qt --output appimage \
    --desktop-file AppDir/usr/share/applications/io.github.asprenas.notepadpp_qt.desktop \
    --icon-file AppDir/usr/share/icons/hicolor/128x128/apps/notepadpp-qt.png 2>&1 \
    | grep -vE "^(\[qt/stdout\]|-- |Deploying|Calling|Setting rpath|Copying file|Creating directory|ERROR: Could not find dependency: lib(gtk|gdk))" | tail -25
test -f "$OUTPUT"
cp "$OUTPUT" "$OUT/"
chmod 0755 "$OUT/$OUTPUT"
STEPS

if [[ $LOCAL -eq 1 ]]; then
    [[ "$(uname -s)" == Linux ]] || { echo "--local builds on a Linux host only"; exit 2; }
    QT_VERSION="$(sed -nE 's/^ARG QT_VERSION=(.*)$/\1/p' "$ROOT/ci/docker/appimage.Dockerfile")"
    QT_DIR="${QT_DIR:-$HOME/Qt/$QT_VERSION/gcc_64}"
    echo "== Notepad++ Qt $VERSION (Linux x86_64 AppImage, local build, Qt $QT_DIR)"
    [[ -x "$QT_DIR/bin/qmake" ]] || { echo "Qt not found: set QT_DIR (directory with bin/qmake)"; exit 1; }
    [[ -n "${LINUXDEPLOY_DIR:-}" ]] && PATH="$LINUXDEPLOY_DIR:$PATH"
    missing=()
    for tool in cmake pkg-config linuxdeploy linuxdeploy-plugin-qt linuxdeploy-plugin-appimage; do
        command -v "$tool" >/dev/null || missing+=("$tool")
    done
    if [[ -z "${CMAKE_MAKE_PROGRAM:-}" ]] && ! command -v ninja >/dev/null; then missing+=("ninja (or CMAKE_MAKE_PROGRAM)"); fi
    [[ ${#missing[@]} -eq 0 ]] || { echo "missing: ${missing[*]}"; exit 1; }
    echo "   glibc $(ldd --version | head -1 | awk '{print $NF}') (the AppImage runs on this glibc or later; release packages: Docker)"
    BUILD="$ROOT/build/package/linux-local"
    mkdir -p "$BUILD"
    SRC="$ROOT" OUT="$DIST" BUILD="$BUILD" QT_DIR="$QT_DIR" VERSION="$VERSION" OUTPUT="$OUTPUT" \
        CMAKE_MAKE_PROGRAM="${CMAKE_MAKE_PROGRAM:-}" PATH="$PATH" bash -euo pipefail -c "$PACKAGE_STEPS"
else
    echo "== Notepad++ Qt $VERSION (Linux x86_64 AppImage, Docker)"
    if ! docker info >/dev/null 2>&1; then
        case "$(uname -s)" in
            MINGW*|MSYS*|CYGWIN*) echo "Docker daemon not running (start Docker Desktop)" ;;
            *) if [[ -S /var/run/docker.sock && ! -w /var/run/docker.sock ]]; then
                   echo "no permission on /var/run/docker.sock: add the user to the docker group (sudo usermod -aG docker \$USER, then log in again)"
               else
                   echo "Docker daemon not running (sudo systemctl start docker)"
               fi
               echo "or build on this host without Docker: ci/package-linux.sh --local" ;;
        esac
        exit 1
    fi
    docker build -q -t "$IMAGE" -f "$ROOT/ci/docker/appimage.Dockerfile" "$ROOT/ci/docker" >/dev/null

    src="$ROOT"; out="$DIST"
    case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) src="$(cygpath -m "$ROOT")"; out="$(cygpath -m "$DIST")" ;; esac

    # The container runs as root: on a Linux host the AppImage is handed back to the caller's user
    owner=()
    [[ "$(uname -s)" == Linux ]] && owner=(-e HOST_OWNER="$(id -u):$(id -g)")
    MSYS_NO_PATHCONV=1 docker run --rm -v "$src:/src:ro" -v "$out:/out" -v "$VOLUME:/build" -w /build \
        -e SRC=/src -e OUT=/out -e BUILD=/build -e VERSION="$VERSION" -e OUTPUT="$OUTPUT" "${owner[@]}" \
        "$IMAGE" bash -euo pipefail -c "$PACKAGE_STEPS"'
            if [ -n "${HOST_OWNER:-}" ]; then chown "$HOST_OWNER" "$OUT/$OUTPUT"; fi'
fi
echo "  AppImage  $DIST/$OUTPUT"
