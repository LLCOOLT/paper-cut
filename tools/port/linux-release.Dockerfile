# The Linux x86-64 machine for the release's Linux packages and local CI's
# Linux job (tools/port/linux_container.py runs commands in it): Ubuntu with
# clang, the build tools and the development packages Aurora's SDL3 build
# needs (docs/PC_PORT.md, "Linux"); gcc-multilib for the 32-bit C headers the
# layout check (gen_bedecode.py --check, a unit test) compiles against. The
# checkout is mounted at /src.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates git curl clang lld llvm cmake ninja-build python3 pkg-config \
        gcc-multilib \
        libasound2-dev libpulse-dev libaudio-dev libfribidi-dev libjack-dev libsndio-dev \
        libx11-dev libxext-dev libx11-xcb-dev libxrandr-dev libxcursor-dev libxfixes-dev \
        libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libdrm-dev libgbm-dev \
        libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev \
        libudev-dev libthai-dev libpipewire-0.3-dev libwayland-dev libdecor-0-dev liburing-dev \
    && rm -rf /var/lib/apt/lists/*
# the mounted checkout belongs to another user: git (CMake's version lookups,
# gen_statics) must still read it
RUN git config --system --add safe.directory '*'
