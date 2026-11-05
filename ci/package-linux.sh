#!/usr/bin/env bash
# Linux package of the Qt port (docs/phase-6-packaging-stabilization.md 3.2): AppImage.
# Built in the ci/docker/appimage.Dockerfile container (Ubuntu 22.04 + official Qt), from any
# host with Docker; GitHub is a repository only.
#
# Usage: ci/package-linux.sh [--dist DIR]
# Output: dist/notepadpp-qt-<version>-linux-x86_64.AppImage
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist"
IMAGE="npp-qt-appimage"
VOLUME="npp-qt-appimage-build"   # incremental build tree between runs
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dist) DIST="$2"; shift ;;
        -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1"; exit 2 ;;
    esac
    shift
done

VERSION="$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
[[ -n "$VERSION" ]] || { echo "project VERSION not found"; exit 1; }
echo "== Notepad++ Qt $VERSION (Linux x86_64 AppImage)"
mkdir -p "$DIST"

docker info >/dev/null 2>&1 || { echo "Docker daemon not running (start Docker Desktop)"; exit 1; }
docker build -q -t "$IMAGE" -f "$ROOT/ci/docker/appimage.Dockerfile" "$ROOT/ci/docker" >/dev/null

src="$ROOT"; out="$DIST"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) src="$(cygpath -m "$ROOT")"; out="$(cygpath -m "$DIST")" ;; esac

MSYS_NO_PATHCONV=1 docker run --rm -v "$src:/src:ro" -v "$out:/out" -v "$VOLUME:/build" -w /build \
    -e VERSION="$VERSION" "$IMAGE" bash -euo pipefail -c '
        cmake -S /src -B /build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_DIR" \
              -DNPP_DOCKING_BACKEND=ADS -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
        cmake --build /build/release -j"$(nproc)" | grep -E "error|warning: unused|FAILED" || true
        test -x /build/release/bin/notepadpp-qt
        rm -rf /build/AppDir && DESTDIR=/build/AppDir cmake --install /build/release >/dev/null
        cp /src/LICENSE /build/AppDir/usr/share/notepadpp-qt/LICENSE.txt

        export QMAKE="$QT_DIR/bin/qmake" LD_LIBRARY_PATH="$QT_DIR/lib"
        export EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqoffscreen.so"
        export EXTRA_QT_MODULES=waylandclient EXTRA_QT_PLUGINS="wayland-shell-integration;wayland-decoration-client;wayland-graphics-integration-client"
        export DEPLOY_PLATFORM_THEMES=1
        export LINUXDEPLOY_OUTPUT_VERSION="$VERSION" APPIMAGE_EXTRACT_AND_RUN=1
        export OUTPUT="notepadpp-qt-$VERSION-linux-x86_64.AppImage"
        cd /build
        rm -f "$OUTPUT"
        linuxdeploy --appdir AppDir --plugin qt --output appimage \
            --desktop-file AppDir/usr/share/applications/notepadpp-qt.desktop \
            --icon-file AppDir/usr/share/icons/hicolor/128x128/apps/notepadpp-qt.png 2>&1 \
            | grep -vE "^(\[qt/stdout\]|-- |Deploying|Calling|Setting rpath|Copying file|Creating directory|ERROR: Could not find dependency: lib(gtk|gdk))" | tail -25
        cp "$OUTPUT" /out/
        chmod 0755 "/out/$OUTPUT"
    '
echo "  AppImage  $DIST/notepadpp-qt-$VERSION-linux-x86_64.AppImage"
