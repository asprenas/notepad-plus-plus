# Linux build environment for the Qt port (local builds and CI).
#
#   docker build -t npp-qt-linux -f ci/docker/linux.Dockerfile ci/docker
#   docker run --rm -v "$PWD:/src" -w /src npp-qt-linux \
#       bash -c "cmake --preset linux-release && cmake --build --preset linux-release"
#
# Ubuntu 26.04 ships Qt 6.10 (Ubuntu 24.04 only has 6.4, below the 6.5 minimum).
FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git ca-certificates \
        qt6-base-dev qt6-base-private-dev qt6-5compat-dev \
        libxcb1-dev libgl-dev \
        fonts-dejavu-core fonts-noto-cjk \
        xvfb xauth qt6-gtk-platformtheme \
    && rm -rf /var/lib/apt/lists/*
