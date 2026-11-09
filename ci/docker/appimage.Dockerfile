# AppImage build environment (docs/phase-6-packaging-stabilization.md 3.2).
#
# Built on Ubuntu 22.04 (glibc 2.35) with the official Qt binaries (aqtinstall), so the AppImage
# runs on distributions of 2022 and later (Ubuntu 22.04+, Debian 12+, Fedora 36+, ...).
# The regular CI image (linux.Dockerfile, Ubuntu 26.04) is too new for that.
#
#   docker build -t npp-qt-appimage -f ci/docker/appimage.Dockerfile ci/docker
#   (used by ci/package-linux.sh)
FROM ubuntu:22.04

ARG QT_VERSION=6.12.0
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential g++-12 ninja-build pkg-config git ca-certificates wget file patchelf \
        python3-pip python3-venv desktop-file-utils squashfs-tools ruby ruby-dev rpm flatpak \
        libgl1-mesa-dev libegl1 libglib2.0-dev libdbus-1-3 libfontconfig1-dev libfreetype6-dev \
        libxkbcommon-dev libxkbcommon-x11-0 libx11-xcb1 libxcb1-dev libxcb-cursor0 libxcb-icccm4 \
        libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 \
        libxcb-xinerama0 libxcb-xkb1 libxcb-sync1 libxcb-xfixes0 libxcb-shm0 libxcb-util1 \
        libwayland-client0 libwayland-cursor0 libwayland-egl1 \
        libgtk-3-0 xvfb xauth fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 120 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 120

# CMake >= 3.24 and aqtinstall from PyPI (Ubuntu 22.04 has CMake 3.22)
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --no-cache-dir cmake aqtinstall
ENV PATH=/opt/venv/bin:$PATH

RUN gem install --no-document fpm \
    && flatpak remote-add --system --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo \
    && flatpak install --system -y --noninteractive flathub org.freedesktop.Platform//24.08 org.freedesktop.Sdk//24.08

RUN aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 -m qt5compat -O /opt/Qt \
    && rm -rf /opt/Qt/Docs /opt/Qt/Examples
ENV QT_DIR=/opt/Qt/${QT_VERSION}/gcc_64

# linuxdeploy + Qt plugin, extracted (no FUSE inside containers)
RUN mkdir -p /opt/linuxdeploy && cd /opt/linuxdeploy \
    && wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
    && wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage \
    && wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-x86_64.AppImage \
    && chmod +x *.AppImage \
    && for f in *.AppImage; do ./$f --appimage-extract >/dev/null && mv squashfs-root ${f%.AppImage}; done \
    && rm *.AppImage \
    && ln -s /opt/linuxdeploy/linuxdeploy-x86_64/AppRun /usr/local/bin/linuxdeploy \
    && ln -s /opt/linuxdeploy/linuxdeploy-plugin-qt-x86_64/AppRun /usr/local/bin/linuxdeploy-plugin-qt \
    && ln -s /opt/linuxdeploy/linuxdeploy-plugin-appimage-x86_64/AppRun /usr/local/bin/linuxdeploy-plugin-appimage
