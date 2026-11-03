# iOS, arm64, with the Xcode command line tools on a Mac:
#   cmake -S . -B build-ios-jp -G Ninja --toolchain cmake/toolchains/ios.cmake \
#     -DTTYD_VERSION=G8MJ01 -DCMAKE_BUILD_TYPE=RelWithDebInfo   [-DTTYD_IOS_PLATFORM=SIMULATOR]
#   ninja -C build-ios-jp ttyd ttyd_ipa
# TTYD_IOS_PLATFORM: OS (a device, default) or SIMULATOR (Apple silicon Macs).
# The Ninja generator is the tested path; the static RELs post-process object
# files in custom commands, which -G Xcode does not support reliably.
# Needs: Xcode, CMake >= 3.25, Ninja, Python 3, Homebrew LLVM (llvm-nm,
# llvm-objcopy, llvm-ar for the RELs) and Rust with the iOS targets (Aurora's
# disc reader, nod): rustup target add aarch64-apple-ios aarch64-apple-ios-sim
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "")
# 14.0: Aurora's prebuilt Dawn for iOS targets 14.0, and UILaunchScreen.
set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE STRING "")

if(NOT DEFINED TTYD_IOS_PLATFORM)
    if(DEFINED ENV{TTYD_IOS_PLATFORM})
        set(TTYD_IOS_PLATFORM $ENV{TTYD_IOS_PLATFORM})
    else()
        set(TTYD_IOS_PLATFORM OS)
    endif()
endif()
set(TTYD_IOS_PLATFORM ${TTYD_IOS_PLATFORM} CACHE STRING "OS (device) or SIMULATOR")
# try_compile projects re-read this file without the cache.
set(ENV{TTYD_IOS_PLATFORM} ${TTYD_IOS_PLATFORM})
if(TTYD_IOS_PLATFORM STREQUAL "SIMULATOR")
    set(CMAKE_OSX_SYSROOT iphonesimulator CACHE STRING "")
    set(_ttyd_sdk iphonesimulator)
    set(_ttyd_rust_target aarch64-apple-ios-sim)
elseif(TTYD_IOS_PLATFORM STREQUAL "OS")
    set(CMAKE_OSX_SYSROOT iphoneos CACHE STRING "")
    set(_ttyd_sdk iphoneos)
    set(_ttyd_rust_target aarch64-apple-ios)
else()
    message(FATAL_ERROR "TTYD_IOS_PLATFORM must be OS or SIMULATOR")
endif()

# nod (Rust, built through Corrosion): the matching Rust target.
set(Rust_CARGO_TARGET ${_ttyd_rust_target} CACHE STRING "")

# Find libraries, headers and packages in the iOS SDK only, so a macOS build
# of a dependency (Homebrew) is never picked up for iOS; programs on the host.
execute_process(COMMAND xcrun --sdk ${_ttyd_sdk} --show-sdk-path
    OUTPUT_VARIABLE _ttyd_sdk_path OUTPUT_STRIP_TRAILING_WHITESPACE)
if(_ttyd_sdk_path)
    set(CMAKE_FIND_ROOT_PATH ${_ttyd_sdk_path})
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# Frameworks are found in the SDK the same way.
set(CMAKE_FIND_FRAMEWORK FIRST)
# pkg-config (Aurora's fallback for zlib/libpng) must not offer the host's.
set(ENV{PKG_CONFIG_LIBDIR} "${_ttyd_sdk_path}/usr/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")

# Aurora's dependencies: built from source (SDL3, nod) or Aurora's prebuilt
# Dawn for iOS (devices only; the simulator builds Dawn from source).
set(AURORA_SDL3_PROVIDER vendor CACHE STRING "")
set(AURORA_NOD_PROVIDER vendor CACHE STRING "")
if(TTYD_IOS_PLATFORM STREQUAL "SIMULATOR")
    set(AURORA_DAWN_PROVIDER vendor CACHE STRING "")
    set(TINT_BUILD_IR_BINARY OFF CACHE BOOL "")
else()
    set(AURORA_DAWN_PROVIDER package CACHE STRING "")
endif()
set(AURORA_SDL3_LINKAGE static CACHE STRING "")
set(AURORA_NOD_LINKAGE static CACHE STRING "")
set(AURORA_DAWN_LINKAGE static CACHE STRING "")
