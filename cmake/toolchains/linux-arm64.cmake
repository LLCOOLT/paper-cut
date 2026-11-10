# Linux ARM64 (aarch64), cross-compiled with clang and lld against a Debian
# sysroot: the handheld build (PortMaster: docs/HANDHELDS.md). From Windows,
# macOS or an x86-64 Linux machine alike:
#   python tools/port/arm64_sysroot.py
#   cmake -S . -B build-arm64-us -G Ninja --toolchain cmake/toolchains/linux-arm64.cmake \
#         -DTTYD_VERSION=G8ME01 -DTTYD_BACKEND=aurora -DCMAKE_BUILD_TYPE=Release
#
# The sysroot (tools/port/arm64_sysroot.py) holds glibc 2.31 and GCC 14's C++
# library: the executable runs on any firmware with glibc 2.31 or newer, the
# C++ library and GCC's runtime linked in (-static-libstdc++ -static-libgcc),
# the few newer C-library names they use provided by
# port/src/aurora/glibc_compat.c. SDL3 is built from
# source (its KMSDRM, Wayland and X11 video drivers, loading their libraries
# at run time) and linked in; Dawn and nod are their prebuilt aarch64
# packages, linked in statically too.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(TTYD_ARM64_TRIPLE aarch64-linux-gnu)

if(NOT TTYD_ARM64_SYSROOT)
    get_filename_component(TTYD_ARM64_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/../../build-arm64-sysroot" ABSOLUTE)
endif()
set(TTYD_ARM64_SYSROOT "${TTYD_ARM64_SYSROOT}" CACHE PATH "The ARM64 sysroot (tools/port/arm64_sysroot.py)")
if(NOT EXISTS "${TTYD_ARM64_SYSROOT}/usr/include/stdio.h")
    message(FATAL_ERROR "No ARM64 sysroot at ${TTYD_ARM64_SYSROOT}: run tools/port/arm64_sysroot.py")
endif()
set(CMAKE_SYSROOT "${TTYD_ARM64_SYSROOT}")
# (the try_compile projects get the same)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TTYD_ARM64_SYSROOT)

if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER clang)
endif()
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER clang++)
endif()
set(CMAKE_C_COMPILER_TARGET ${TTYD_ARM64_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${TTYD_ARM64_TRIPLE})
set(CMAKE_ASM_COMPILER_TARGET ${TTYD_ARM64_TRIPLE})
set(CMAKE_LINKER_TYPE LLD)
find_program(CMAKE_AR NAMES llvm-ar)
find_program(CMAKE_RANLIB NAMES llvm-ranlib)
find_program(CMAKE_STRIP NAMES llvm-strip)
find_program(CMAKE_OBJCOPY NAMES llvm-objcopy)

# Cortex-A53 and up (Allwinner H700, Rockchip RK3326/RK3566/RK3588, ...):
# ARMv8.0-A, so no LSE atomics in line (GCC's runtime picks them at run time)
# (--target in the flags too: libpng runs the compiler itself with only CMAKE_C_FLAGS)
set(_ttyd_arm64_flags "--target=${TTYD_ARM64_TRIPLE} -mcpu=cortex-a53")
set(CMAKE_C_FLAGS_INIT "${_ttyd_arm64_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_ttyd_arm64_flags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Two of the build machine's programs, as Python scripts through a wrapper in
# the build folder where the machine has none (Windows):
#  - pkg-config over the sysroot (SDL3's driver checks): tools/port/pkg_config.py,
#    always (a host's own would look at the host's libraries);
#  - wayland-scanner (SDL3's Wayland driver, ROCKNIX's Sway sessions):
#    tools/port/wayland_scanner.py, when the machine has no wayland-scanner.
if(Python3_EXECUTABLE)
    set(_ttyd_python "${Python3_EXECUTABLE}")
elseif(CMAKE_HOST_WIN32)
    set(_ttyd_python py)
else()
    set(_ttyd_python python3)
endif()
function(_ttyd_host_tool name script args out)
    get_filename_component(_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../tools/port/${script}" ABSOLUTE)
    if(CMAKE_HOST_WIN32)
        set(_wrapper "${CMAKE_BINARY_DIR}/arm64-${name}.cmd")
        file(TO_NATIVE_PATH "${_ttyd_python}" _python)
        string(ASCII 13 _cr)
        file(WRITE "${_wrapper}" "@\"${_python}\" \"${_script}\" ${args} %*${_cr}\n")
    else()
        set(_wrapper "${CMAKE_BINARY_DIR}/arm64-${name}")
        file(WRITE "${_wrapper}" "#!/bin/sh\nexec \"${_ttyd_python}\" \"${_script}\" ${args} \"$@\"\n")
        file(CHMOD "${_wrapper}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
    endif()
    set(${out} "${_wrapper}" PARENT_SCOPE)
endfunction()
if(CMAKE_BINARY_DIR)
    if(NOT PKG_CONFIG_EXECUTABLE)
        _ttyd_host_tool(pkg-config pkg_config.py "--sysroot \"${TTYD_ARM64_SYSROOT}\"" _ttyd_wrapper)
        set(PKG_CONFIG_EXECUTABLE "${_ttyd_wrapper}" CACHE FILEPATH "pkg-config over the ARM64 sysroot" FORCE)
    endif()
    find_program(_ttyd_wayland_scanner wayland-scanner NO_CMAKE_FIND_ROOT_PATH)
    if(NOT WAYLAND_SCANNER AND NOT _ttyd_wayland_scanner)
        _ttyd_host_tool(wayland-scanner wayland_scanner.py "" _ttyd_wrapper)
        set(WAYLAND_SCANNER "${_ttyd_wrapper}" CACHE FILEPATH "wayland-scanner (tools/port/wayland_scanner.py)" FORCE)
    endif()
endif()

# SDL3 from source, statically (no aarch64 package; a firmware's own SDL3, if
# it has one at all, may be older than Aurora's)
set(AURORA_SDL3_PROVIDER vendor CACHE STRING "")
set(AURORA_SDL3_LINKAGE static CACHE STRING "")
# nod (Rust) and Dawn as their prebuilt aarch64 packages: building nod here
# would need a Rust cross toolchain
set(AURORA_NOD_PROVIDER package CACHE STRING "")
set(AURORA_DAWN_PROVIDER package CACHE STRING "")
