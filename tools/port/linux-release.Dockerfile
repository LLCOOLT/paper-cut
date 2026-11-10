# The Linux x86-64 machine for the release's Linux packages and local CI's
# Linux job (tools/port/linux_container.py runs commands in it): Ubuntu with
# clang, the build tools and the development packages Aurora's SDL3 build
# needs (docs/PC_PORT.md, "Linux"); gcc-multilib for the 32-bit C headers the
# layout check (gen_bedecode.py --check, a unit test) compiles against. The
# checkout is mounted at /src.
#
# Ubuntu 22.04, the oldest LTS still in standard support: a program links
# against the C library it is built on and runs only where that one or a
# newer one is, so the packages (the zip, the AppImage and the Flatpak, all
# from the same build) run on 22.04 and everything newer (glibc 2.35). The
# AppImage catalog tests on that release. What 22.04 itself is too old for
# comes from elsewhere:
# - clang 18 (apt.llvm.org; 22.04 has 14), as 24.04 had;
# - GCC 13's C++ library (the toolchain PPA, built on 22.04): the prebuilt
#   Dawn needs it, and it is linked in statically (release.py,
#   TTYD_STATIC_CXX_RUNTIME), so no newer libstdc++ is asked of the system;
# - CMake 3.28 (Kitware's build; 22.04 has 3.22, Aurora needs 3.25);
# - Python 3.12 (deadsnakes), the tools' Python as on 24.04.
# tools/port/linux_packages.py fails the release if a binary still asks for a
# newer glibc or libstdc++ than 22.04's.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl gnupg software-properties-common \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && add-apt-repository -y ppa:deadsnakes/ppa \
    && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm.gpg \
    && echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/jammy/ llvm-toolchain-jammy-18 main" \
        > /etc/apt/sources.list.d/llvm-18.list \
    && apt-get update && apt-get install -y --no-install-recommends \
        git clang-18 lld-18 llvm-18 libstdc++-13-dev ninja-build python3.12 pkg-config binutils \
        gcc-multilib \
        libasound2-dev libpulse-dev libaudio-dev libfribidi-dev libjack-dev libsndio-dev \
        libx11-dev libxext-dev libx11-xcb-dev libxrandr-dev libxcursor-dev libxfixes-dev \
        libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libdrm-dev libgbm-dev \
        libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev \
        libudev-dev libthai-dev libpipewire-0.3-dev libwayland-dev libdecor-0-dev liburing-dev \
        flatpak file desktop-file-utils zsync \
    && rm -rf /var/lib/apt/lists/*
# clang, clang++, ld.lld, llvm-ar... by their plain names; python3 is 3.12 for
# the tools (the system's 3.10 stays /usr/bin/python3, for apt's own scripts)
ENV PATH=/usr/lib/llvm-18/bin:$PATH
RUN ln -s /usr/bin/python3.12 /usr/local/bin/python3 \
    && curl -fsSL https://github.com/Kitware/CMake/releases/download/v3.28.6/cmake-3.28.6-linux-x86_64.tar.gz \
        | tar -xz -C /opt \
    && ln -s /opt/cmake-3.28.6-linux-x86_64/bin/cmake /opt/cmake-3.28.6-linux-x86_64/bin/ctest /usr/local/bin/
# clang must take GCC 13's C++ library (the newest GCC installed), not 22.04's own
RUN clang++ -v 2>&1 | grep -q "Selected GCC installation: /usr/lib/gcc/x86_64-linux-gnu/13$"
# the AppImage's tool (tools/port/linux_packages.py), a fixed release; run
# extracted (APPIMAGE_EXTRACT_AND_RUN), as a container has no FUSE; it stops
# without desktop-file-validate (desktop-file-utils, above), and writes the
# .zsync AppImageUpdate reads only with zsyncmake (zsync, above)
RUN curl -fsSL -o /usr/local/bin/appimagetool \
        https://github.com/AppImage/appimagetool/releases/download/1.9.0/appimagetool-x86_64.AppImage \
    && chmod +x /usr/local/bin/appimagetool
# the mounted checkout belongs to another user: git (CMake's version lookups,
# gen_statics) must still read it
RUN git config --system --add safe.directory '*'
