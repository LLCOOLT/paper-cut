# Paper Cut: the native build of the TTYD decomp. The matching GameCube build is
# configure.py + ninja and is unaffected by this file.
cmake_minimum_required(VERSION 3.25)
# macOS: 12.0 (Aurora's prebuilt Dawn is built for 12) unless asked otherwise; must
# precede project(). Cross builds (iOS) set their own in the toolchain file.
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET AND NOT DEFINED CMAKE_TOOLCHAIN_FILE
   AND NOT DEFINED CMAKE_SYSTEM_NAME AND NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
    set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0 CACHE STRING "Minimum macOS version")
endif()
project(ttyd_pc C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # tools/port/gen_statics.py reads it

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

# One executable per game version: struct layouts differ between JP, US and
# PAL, so the version is fixed at compile time like the matching build's
# -DVERSION_*. The list's order is configure.py's (index = BUILD_VERSION).
set(TTYD_VERSIONS G8MJ01 G8ME01 G8MP01)
set(TTYD_VERSION G8MJ01 CACHE STRING "Game version (disc ID): G8MJ01 (JP), G8ME01 (US) or G8MP01 (PAL)")
set_property(CACHE TTYD_VERSION PROPERTY STRINGS ${TTYD_VERSIONS})
list(FIND TTYD_VERSIONS ${TTYD_VERSION} TTYD_BUILD_VERSION)
if(TTYD_BUILD_VERSION LESS 0)
    message(FATAL_ERROR "TTYD_VERSION must be one of: ${TTYD_VERSIONS}")
endif()
if(TTYD_VERSION STREQUAL "G8ME01")
    set(TTYD_EXE_NAME ttyd-us)
elseif(TTYD_VERSION STREQUAL "G8MP01")
    set(TTYD_EXE_NAME ttyd-eu)
else()
    set(TTYD_EXE_NAME ttyd-jp)
endif()
# aurora: Aurora (extern/aurora) provides GX on WebGPU, VI/PAD via SDL3,
#         DVD from the disc image, CARD, THP, OS. The real target.
# headless: port/src/headless shims, no window or dependencies; for CI and
#           bring-up on machines that cannot fetch Aurora's prebuilts.
set(TTYD_BACKEND aurora CACHE STRING "Platform backend: aurora or headless")
set_property(CACHE TTYD_BACKEND PROPERTY STRINGS aurora headless)
# 32-bit is only kept for the headless bring-up build.
option(TTYD_PC_32BIT "Build a 32-bit executable (headless only)" OFF)
option(TTYD_PTRWIDTH_WARNINGS "Report only int<->pointer width warnings (audit build)" OFF)
option(TTYD_FORTIFY "Keep the C library's _FORTIFY_SOURCE checks in game code (audit build)" OFF)

# --- iOS (cmake/toolchains/ios.cmake; docs/PC_PORT.md, "iOS") ---
# Aurora only. An iOS app may only run code signed into its bundle (no
# dlopen of RELs), so the RELs are always linked in. Bundle, Info.plist,
# icons and the Objective-C glue: port/ios/ios.cmake.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(TTYD_IOS ON)
    if(NOT TTYD_BACKEND STREQUAL "aurora")
        message(FATAL_ERROR "iOS builds need TTYD_BACKEND=aurora")
    endif()
    set(TTYD_RELS_STATIC ON CACHE BOOL "Link the RELs into the executable" FORCE)
    enable_language(OBJC)
else()
    set(TTYD_IOS OFF)
endif()
# On-screen touch controls (port/touch): touches as Aurora's virtual PAD
# status, drawn with Aurora's Dear ImGui. On by default on iOS and Android;
# Aurora only.
if(TTYD_IOS OR ANDROID)
    set(_touch_default ON)
else()
    set(_touch_default OFF)
endif()
option(TTYD_TOUCH_OVERLAY "On-screen touch controls (Aurora backend)" ${_touch_default})

set(TTYD_ARCH_FLAG -m64)
if(TTYD_PC_32BIT AND NOT MSVC)
    set(TTYD_ARCH_FLAG -m32)
    add_compile_options(-m32)
    add_link_options(-m32)
endif()

# --- Web (Emscripten: emcmake cmake ...; docs/PC_PORT.md, "Web") ---
# wasm32: pointers and script words are 32 bits, as on the GameCube. Headless
# in Node for now (the game, its threads and the disc through Node's file
# system); the browser build with Aurora comes later.
if(EMSCRIPTEN)
    set(TTYD_WEB ON)
    if(NOT TTYD_BACKEND STREQUAL "headless")
        message(FATAL_ERROR "Web builds are headless only for now: -DTTYD_BACKEND=headless")
    endif()
    # Each game thread is a host thread (port/src/common/os_thread_pc.c).
    add_compile_options(-pthread)
    add_link_options(-pthread)
else()
    set(TTYD_WEB OFF)
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# The C math library is separate (libm) except on Windows.
if(WIN32)
    set(TTYD_LIBM)
else()
    set(TTYD_LIBM m)
endif()

# --- Linux ---
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # A position-independent executable: every host address the game uses must
    # be 2 GiB or higher (portCheckHostAddress, include/sfx_name_word.h), and a
    # non-PIE executable is loaded at 4 MiB.
    include(CheckPIESupported)
    check_pie_supported()
    # glibc declares dladdr, dl_iterate_phdr and REG_RIP (crash reports) only
    # with _GNU_SOURCE; only this file gets it (it hides nothing the game uses).
    set_source_files_properties(${CMAKE_SOURCE_DIR}/port/src/common/crash_pc.c
        PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE)
endif()

set(TTYD_DEFINES
    TARGET_PC=1 NONMATCHING=1 NDEBUG=1 _USE_MATH_DEFINES=1 _CRT_SECURE_NO_WARNINGS=1
    BUILD_VERSION=${TTYD_BUILD_VERSION} VERSION_${TTYD_VERSION} SDK_REVISION=1
    PORT_GAME_ID="${TTYD_VERSION}")
# As configure.py: PAL is built from the US code, so it defines VERSION_G8ME01
# too (the library code under libs/ tests it; VERSION_G8MP01 marks PAL)
if(TTYD_VERSION STREQUAL "G8MP01")
    list(APPEND TTYD_DEFINES VERSION_G8ME01)
endif()

if(TTYD_BACKEND STREQUAL "aurora")
    # extern/aurora is upstream Aurora; the port's fixes to it are patches
    # (port/patches/aurora, `git format-patch` from a local Aurora branch),
    # applied to its working tree here once each: one already applied is
    # skipped, one that no longer applies stops the configure.
    find_package(Git REQUIRED)
    file(GLOB _aurora_patches ${CMAKE_SOURCE_DIR}/port/patches/aurora/*.patch)
    list(SORT _aurora_patches)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_aurora_patches})
    foreach(_patch ${_aurora_patches})
        get_filename_component(_name ${_patch} NAME)
        execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check ${_patch}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/extern/aurora
            RESULT_VARIABLE _applied OUTPUT_QUIET ERROR_QUIET)
        if(NOT _applied EQUAL 0)
            execute_process(COMMAND ${GIT_EXECUTABLE} apply --whitespace=nowarn ${_patch}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/extern/aurora
                RESULT_VARIABLE _result ERROR_VARIABLE _error)
            if(NOT _result EQUAL 0)
                message(FATAL_ERROR "port/patches/aurora/${_name} does not apply to extern/aurora:\n${_error}")
            endif()
            message(STATUS "Aurora: applied ${_name}")
        endif()
    endforeach()
    if(ANDROID)
        # Android: SDL3 built from source (Aurora requires it there; port/android
        # compiles SDL's Java glue from the same tree, FETCHCONTENT_SOURCE_DIR_SDL),
        # linked statically into libmain.so with Dawn (prebuilt, arm64) and nod.
        set(AURORA_SDL3_PROVIDER vendor CACHE STRING "")
        set(AURORA_SDL3_LINKAGE static CACHE STRING "")
        # Not the build machine's libraries: its pkg-config and SQLite are for
        # the host, whose headers the NDK cannot use. Aurora then fetches and
        # builds what it needs (sqlite3) for the target.
        set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig ON)
        set(CMAKE_DISABLE_FIND_PACKAGE_SQLite3 ON)
    endif()
    if(APPLE)
        # A self-contained app: not Homebrew's or MacPorts' libraries (fmt,
        # abseil, zstd, libpng, ... are found there once any formula pulls them
        # in, and the app would only run on Macs that have them). Aurora then
        # fetches and builds its own, statically, as on Windows; zlib, sqlite3
        # and the frameworks come from the SDK.
        set(_ttyd_ignore_prefix ${CMAKE_IGNORE_PREFIX_PATH})
        list(APPEND CMAKE_IGNORE_PREFIX_PATH /opt/homebrew /usr/local /opt/local)
        set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig ON)
    endif()
    # Aurora's arithmetic (its matrices, GX's projections...) without fused
    # multiply-add, as the game's (TTYD_CFLAGS): arm64 would otherwise round a
    # partner's steps a bit apart from x86-64, and an online game between a
    # Mac and a PC go out of step. (MSVC: Windows is x86-64 only, unfused.)
    if(NOT MSVC)
        add_compile_options(-ffp-contract=off)
    endif()
    add_subdirectory(extern/aurora EXCLUDE_FROM_ALL)
    # The matrix functions' trigonometry the game's (MSL's fdlibm,
    # port/include/port/fdlibm_pc.h), the same on every computer for online
    # co-op (port/patches/aurora/0019).
    target_compile_definitions(aurora_mtx PRIVATE AURORA_MTX_GAME_TRIG=1)
    # nod built by Cargo (Android) on a Windows machine whose Rust is the
    # windows-gnu toolchain: Cargo builds the build scripts and proc macros for
    # that host, and rustc makes their raw-dylib import libraries (getrandom)
    # with a `dlltool` from PATH, which rustup does not provide (its own one
    # needs GNU `as`). LLVM's llvm-dlltool (the NDK's, beside llvm-ar) does the
    # job when it is called dlltool: a copy goes on PATH for Cargo.
    if(CMAKE_HOST_WIN32 AND TARGET nod AND COMMAND corrosion_set_env_vars
            AND "$CACHE{Rust_CARGO_HOST_TARGET_CACHED}" MATCHES "windows-gnu")
        find_program(TTYD_HOST_DLLTOOL dlltool)
        get_filename_component(_llvm_bin "${CMAKE_AR}" DIRECTORY)
        find_program(TTYD_LLVM_DLLTOOL llvm-dlltool HINTS "${_llvm_bin}")
        if(NOT TTYD_HOST_DLLTOOL AND TTYD_LLVM_DLLTOOL)
            set(_dlltool_dir ${CMAKE_BINARY_DIR}/rust-host-tools)
            file(MAKE_DIRECTORY ${_dlltool_dir})
            file(COPY_FILE ${TTYD_LLVM_DLLTOOL} ${_dlltool_dir}/dlltool.exe ONLY_IF_DIFFERENT)
            # Corrosion puts these first in its `cmake -E env` command.
            corrosion_set_env_vars(nod --modify "PATH=path_list_prepend:${_dlltool_dir}")
        endif()
    endif()
    if(APPLE)
        set(CMAKE_IGNORE_PREFIX_PATH ${_ttyd_ignore_prefix})
        unset(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig)
    endif()
    if(TTYD_IOS AND TARGET cargo-build_nod AND COMMAND corrosion_set_env_vars)
        # nod's C dependencies (zlib, zstd, ... through cc-rs) otherwise target
        # the SDK's iOS version, not the app's minimum.
        if(CMAKE_OSX_DEPLOYMENT_TARGET)
            set(_nod_ios_min ${CMAKE_OSX_DEPLOYMENT_TARGET})
        else()
            set(_nod_ios_min 14.0)
        endif()
        corrosion_set_env_vars(nod IPHONEOS_DEPLOYMENT_TARGET=${_nod_ios_min})
    endif()
    # aurora_overlay: wrappers (#include_next) adding the 2004-SDK declarations,
    # macros and transitive includes the game relies on that Aurora's headers lack.
    set(TTYD_SDK_INCLUDE ${CMAKE_SOURCE_DIR}/port/include/aurora_overlay
        ${CMAKE_SOURCE_DIR}/extern/aurora/include)
    # PORT_MODS: mods (port/include/port/mods.h), Aurora only.
    list(APPEND TTYD_DEFINES TTYD_BACKEND_AURORA=1 PORT_MODS=1)
else()
    set(TTYD_SDK_INCLUDE ${CMAKE_SOURCE_DIR}/libs/dolsdk2004/include)
endif()

# The card banner/icon and DSP microcode the decomp extracts from the game
# (python configure.py && ninja, with the disc in orig/). Without them (CI,
# or a build before the disc is set up) stand-ins are used: the port only
# needs the disc to run, not to build.
# TTYD_NO_DISC_ASSETS: the stand-ins even when the disc's are there (release
# builds: nothing from the disc may ship; tools/port/release.py).
option(TTYD_NO_DISC_ASSETS "Never build in assets extracted from the disc" OFF)
if(NOT TTYD_NO_DISC_ASSETS AND EXISTS ${CMAKE_SOURCE_DIR}/build/${TTYD_VERSION}/include/assets/_mariost_banner_tex.inc)
    set(TTYD_ASSET_INCLUDE ${CMAKE_SOURCE_DIR}/build/${TTYD_VERSION}/include)
else()
    message(STATUS "No extracted assets in build/${TTYD_VERSION}/include: using stand-ins (blank save banner)")
    set(TTYD_ASSET_INCLUDE ${CMAKE_SOURCE_DIR}/port/include/no_disc_assets)
endif()

set(TTYD_INCLUDES
    ${CMAKE_SOURCE_DIR}/port/include
    ${CMAKE_SOURCE_DIR}/include
    ${TTYD_SDK_INCLUDE}
    ${CMAKE_SOURCE_DIR}/libs/musyx/include
    ${TTYD_ASSET_INCLUDE}
    ${CMAKE_BINARY_DIR}/generated)

# Asset headers from the user's own disc (placeholders if orig/ is empty).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/port/extract_assets.py ${CMAKE_BINARY_DIR}/generated
    --version ${TTYD_VERSION})

if(MSVC)
    set(TTYD_CFLAGS /FI${CMAKE_SOURCE_DIR}/port/include/port_prefix.h /J)
else()
    set(TTYD_CFLAGS
        -include ${CMAKE_SOURCE_DIR}/port/include/port_prefix.h
        # Bare "texPalette.h" & co. resolve to include/sdk, as with MWCC.
        -iquote ${CMAKE_SOURCE_DIR}/include/sdk
        # gnu89 inline: a plain 'inline' function also gets an external
        # definition, as with MWCC (C11 inline would leave none at -O0).
        -funsigned-char -fno-strict-aliasing -fwrapv -fcommon -fgnu89-inline
        # No fused multiply-add: arm64 (Apple, Android) would otherwise round
        # differently from x86-64, and runs (golden audio hashes) would differ.
        -ffp-contract=off
        # A call to an undeclared function returns C's implicit int: a pointer
        # or 64-bit result is cut to 32 bits, a narrow one is not extended and
        # f32 arguments go as doubles (e.g. seq_load.c's winZenkakuStr, a US
        # crash). Every call has a prototype; keep it that way.
        -Werror=implicit-function-declaration
        # MWCC accepts these; they are fixed incrementally.
        -Wno-error=int-conversion
        -Wno-error=incompatible-pointer-types)
    # The game copies strings past the end of char fields into padding that
    # nothing reads (mobjEntry: "MOBJ_TreasureBox" into animName[16]), as the
    # GameCube allows. Apple clang, Android's bionic and some Linux distros
    # turn _FORTIFY_SOURCE on by default and abort on those copies; it is off
    # in game code, as on Windows. TTYD_FORTIFY=ON keeps it on: a map sweep
    # then lists every overflowing copy (each must land in unread padding).
    if(NOT TTYD_FORTIFY)
        list(APPEND TTYD_CFLAGS -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        list(APPEND TTYD_CFLAGS -Wno-error=incompatible-function-pointer-types)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # GCC 14 makes the same MWCC-accepted constructs (and implicit int,
        # return mismatches) errors; -fpermissive keeps them warnings.
        list(APPEND TTYD_CFLAGS -fpermissive)
    endif()
    # TTYD_PTRWIDTH_WARNINGS: only the 64-bit pointer-truncation warnings
    # (an audit build); otherwise the decomp's code builds silently.
    if(TTYD_PTRWIDTH_WARNINGS)
        list(APPEND TTYD_CFLAGS -Wno-everything -Wpointer-to-int-cast -Wint-to-pointer-cast
            -Wvoid-pointer-to-int-cast -Wint-conversion -Wcast-function-type-mismatch
            -Wimplicit-function-declaration -Wunsequenced -Wsizeof-pointer-memaccess
            -Wincompatible-pointer-types -Wcast-function-type-strict -Wshorten-64-to-32
            -Wformat)
    else()
        list(APPEND TTYD_CFLAGS -w)
    endif()
endif()

# --- Game code: exactly the objects configure.py builds ---
# Regenerate with tools/port/gen_sources.py after editing configure.py.
include(${CMAKE_SOURCE_DIR}/port/sources_${TTYD_VERSION}.cmake)

# Japanese text in string literals must be Shift-JIS, as sjiswrap makes it for
# MWCC: sources with non-ASCII text are compiled from a copy with those
# literals rewritten as Shift-JIS escapes (tools/port/sjis_src.py).
set(_all_game_sources ${TTYD_DOL_SOURCES})
foreach(rel ${TTYD_RELS})
    list(APPEND _all_game_sources ${TTYD_REL_${rel}_SOURCES})
endforeach()
file(WRITE ${CMAKE_BINARY_DIR}/sjis_candidates.txt "${_all_game_sources}")
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/port/sjis_src.py --list
    @${CMAKE_BINARY_DIR}/sjis_candidates.txt
    OUTPUT_VARIABLE _sjis_sources OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "\r" "" _sjis_sources "${_sjis_sources}")
string(REPLACE "\n" ";" _sjis_sources "${_sjis_sources}")
function(ttyd_sjis_sources listvar)
    set(out)
    foreach(src ${${listvar}})
        file(TO_CMAKE_PATH "${src}" src)
        list(FIND _sjis_sources "${src}" idx)
        if(idx GREATER -1)
            file(RELATIVE_PATH rel ${CMAKE_SOURCE_DIR} ${src})
            set(gen ${CMAKE_BINARY_DIR}/sjis/${rel})
            get_filename_component(srcdir ${src} DIRECTORY)
            add_custom_command(OUTPUT ${gen}
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/port/sjis_src.py ${src} ${gen}
                DEPENDS ${src} ${CMAKE_SOURCE_DIR}/tools/port/sjis_src.py
                COMMENT "Shift-JIS literals: ${rel}")
            # quoted includes still resolve next to the original
            set_source_files_properties(${gen} PROPERTIES COMPILE_OPTIONS "-iquote;${srcdir}")
            list(APPEND out ${gen})
        else()
            list(APPEND out ${src})
        endif()
    endforeach()
    set(${listvar} ${out} PARENT_SCOPE)
endfunction()
ttyd_sjis_sources(TTYD_DOL_SOURCES)
foreach(rel ${TTYD_RELS})
    ttyd_sjis_sources(TTYD_REL_${rel}_SOURCES)
endforeach()

# Mods reach the game's file statics too (a GameCube mod by address; see
# tools/port/gen_statics.py): a source with statics is compiled through a
# wrapper that includes it and exports a pointer to each,
# port_static__<path>__<name>.
set(_statics_list ${CMAKE_SOURCE_DIR}/port/statics_${TTYD_VERSION}.txt)
if("PORT_MODS=1" IN_LIST TTYD_DEFINES AND NOT EXISTS ${_statics_list})
    message(WARNING "No ${_statics_list}: mods won't reach the game's statics "
        "(configure, then run tools/port/gen_statics.py --build <this build>)")
endif()
if("PORT_MODS=1" IN_LIST TTYD_DEFINES AND EXISTS ${_statics_list})
    file(STRINGS ${_statics_list} _statics_lines REGEX "^[^#]")
    foreach(_line ${_statics_lines})
        string(REPLACE " " ";" _parts "${_line}")
        list(POP_FRONT _parts _src)
        string(MAKE_C_IDENTIFIER "${_src}" _id)
        string(REGEX REPLACE "_c$" "" _id "${_id}")
        set(_statics_of_${_id} ${_parts})
    endforeach()
    function(ttyd_static_exports listvar)
        set(out)
        foreach(src ${${listvar}})
            # a Shift-JIS copy (build/sjis/...) stands for its original
            file(RELATIVE_PATH rel ${CMAKE_BINARY_DIR}/sjis ${src})
            if(rel MATCHES "^\\.\\.")
                file(RELATIVE_PATH rel ${CMAKE_SOURCE_DIR} ${src})
            endif()
            string(MAKE_C_IDENTIFIER "${rel}" id)
            string(REGEX REPLACE "_c$" "" id "${id}")
            if(NOT DEFINED _statics_of_${id})
                list(APPEND out ${src})
                continue()
            endif()
            set(body "/* Generated by CMakeLists.txt: ${rel} and its statics' addresses, for mods. */\n")
            string(APPEND body "#include \"${src}\"\n\n")
            foreach(name ${_statics_of_${id}})
                string(APPEND body "void* const port_static__${id}__${name} = (void*)&${name};\n")
            endforeach()
            set(gen ${CMAKE_BINARY_DIR}/statics/${rel})
            file(CONFIGURE OUTPUT ${gen} CONTENT "${body}" @ONLY)
            get_filename_component(srcdir ${CMAKE_SOURCE_DIR}/${rel} DIRECTORY)
            # (OBJECT_DEPENDS: a Shift-JIS copy is generated before it's included)
            set_source_files_properties(${gen} PROPERTIES COMPILE_OPTIONS "-iquote;${srcdir}" OBJECT_DEPENDS ${src})
            list(APPEND out ${gen})
        endforeach()
        set(${listvar} ${out} PARENT_SCOPE)
    endfunction()
    ttyd_static_exports(TTYD_DOL_SOURCES)
    foreach(rel ${TTYD_RELS})
        ttyd_static_exports(TTYD_REL_${rel}_SOURCES)
    endforeach()
endif()
# The same list for the mods' scripts to find a static by name
# (port/mods/mods.cpp): one string per source, its path and its statics.
if("PORT_MODS=1" IN_LIST TTYD_DEFINES)
    set(_index "/* Generated by CMakeLists.txt from port/statics_${TTYD_VERSION}.txt. */\n")
    string(APPEND _index "const char* const portStaticsIndex[] = {\n")
    foreach(_line ${_statics_lines})
        string(APPEND _index "    \"${_line}\",\n")
    endforeach()
    string(APPEND _index "    0,\n};\n")
    file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/statics_index.c CONTENT "${_index}" @ONLY)
endif()

function(ttyd_game_target target)
    target_compile_definitions(${target} PRIVATE ${TTYD_DEFINES})
    target_include_directories(${target} PRIVATE ${TTYD_INCLUDES})
    target_compile_options(${target} PRIVATE ${TTYD_CFLAGS})
endfunction()

add_library(ttyd_dol OBJECT ${TTYD_DOL_SOURCES})
ttyd_game_target(ttyd_dol)
# The backend owns main() (port/src/*/main_*.c) and calls the game's as ttyd_main.
set_property(SOURCE ${CMAKE_SOURCE_DIR}/src/main.c TARGET_DIRECTORY ttyd_dol
    APPEND PROPERTY COMPILE_DEFINITIONS main=ttyd_main)

# On the GameCube only one REL is loaded at a time and it starts from fresh
# data on every load. Two ways to get that on PC (port/src/common/rel_pc.c):
# - TTYD_RELS_STATIC (default with clang: Windows, where a DLL cannot read the
#   executable's data without dllimport; Apple, where iOS cannot load code at
#   run time and macOS takes the same path; Linux and Android): every REL is
#   linked into the executable. tools/port/rel_rename.py renames each REL's
#   global symbols to rel_<area>__<name> so RELs (and the DOL) cannot clash,
#   and each REL's data/bss go to sections of their own (per object format,
#   below); linking a REL restores its initial data, clears its bss and runs
#   its _prolog. On Apple this needs LLVM's llvm-objcopy (brew install llvm).
# - otherwise each REL is a loadable module (.so/.dylib) loaded on area entry
#   and unloaded on exit; modules resolve game symbols against the executable.
if(WIN32 OR APPLE OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    # Windows, Apple (iOS cannot load code at run time) and ELF with clang.
    set(_rels_static_default ON)
else()
    set(_rels_static_default OFF)
endif()
option(TTYD_RELS_STATIC "Link the RELs into the executable" ${_rels_static_default})
set(TTYD_REL_TARGETS)
set(TTYD_REL_ARCHIVES)
set(TTYD_REL_STATIC_SOURCES)
if(TTYD_RELS_STATIC)
    # Where each REL's data and bss lie (rel_pc.c restores and clears them):
    # - COFF (Windows): grouped sections .data$rel_<area>_{a,m,z}, which the
    #   linker sorts by suffix, with marker variables in _a and _z.
    # - ELF (Linux, Android): one section per REL, rel_<area>_data and
    #   rel_<area>_bss; the linker defines __start_<name>/__stop_<name> around
    #   sections named like C identifiers. A marker variable in each keeps the
    #   section (and so the symbols) even when the REL has no data or bss.
    # - Mach-O (macOS, iOS): __DATA,__rd_<area> and __DATA,__rb_<area> (at
    #   most 16 characters), bounded by the linker's section$start/section$end
    #   symbols; one byte in each keeps it present.
    # All need clang (#pragma clang section).
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "TTYD_RELS_STATIC needs clang (#pragma clang section); use -DTTYD_RELS_STATIC=OFF")
    endif()
    # - wasm (web): like ELF, rel_<area>_data and rel_<area>_bss bounded by
    #   __start_/__stop_; clang ignores the pragma for wasm, so rel_rename.py
    #   (tools/port/wasm_redefine_syms.py) moves the REL's segments there.
    if(WIN32)
        set(_rel_format COFF)
    elseif(APPLE)
        set(_rel_format MACHO)
    elseif(EMSCRIPTEN)
        set(_rel_format WASM)
    else()
        set(_rel_format ELF)
    endif()
    get_filename_component(_llvm_bin ${CMAKE_C_COMPILER} DIRECTORY)
    # Linux distributions install clang as a link into /usr/lib/llvm-<N>/bin;
    # GNU binutils' nm/objcopy/ar take the same options (rel_rename.py).
    file(REAL_PATH ${CMAKE_C_COMPILER} _cc_real)
    get_filename_component(_llvm_bin_real ${_cc_real} DIRECTORY)
    set(_llvm_hints ${_llvm_bin} ${_llvm_bin_real})
    if(EMSCRIPTEN)
        # emcc is a driver in emscripten/; its LLVM is in ../bin.
        list(APPEND _llvm_hints ${EMSCRIPTEN_ROOT_PATH}/../bin)
    endif()
    if(APPLE)
        # Xcode has no llvm-objcopy and Apple's nm/ar differ: Homebrew's LLVM
        # (keg-only, not on PATH), nm and ar from the same place.
        list(APPEND _llvm_hints /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
        find_program(TTYD_LLVM_OBJCOPY llvm-objcopy HINTS ${_llvm_hints} NO_CMAKE_FIND_ROOT_PATH)
        if(NOT TTYD_LLVM_OBJCOPY)
            message(FATAL_ERROR "TTYD_RELS_STATIC needs llvm-objcopy, which Xcode does not ship: "
                "brew install llvm (or -DTTYD_LLVM_OBJCOPY=<path>, or -DTTYD_RELS_STATIC=OFF for REL modules)")
        endif()
        get_filename_component(_objcopy_bin ${TTYD_LLVM_OBJCOPY} DIRECTORY)
        set(_llvm_hints ${_objcopy_bin})
        set(_nm_names llvm-nm)
        set(_objcopy_names llvm-objcopy)
        set(_ar_names llvm-ar)
    else()
        set(_nm_names llvm-nm nm)
        set(_objcopy_names llvm-objcopy objcopy)
        set(_ar_names llvm-ar ar)
    endif()
    find_program(TTYD_LLVM_NM NAMES ${_nm_names} HINTS ${_llvm_hints} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
    find_program(TTYD_LLVM_OBJCOPY NAMES ${_objcopy_names} HINTS ${_llvm_hints} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
    find_program(TTYD_LLVM_AR NAMES ${_ar_names} HINTS ${_llvm_hints} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
    set(_relgen ${CMAKE_BINARY_DIR}/generated/rels)
    set(_table_decls "")
    set(_table_rows "")
    foreach(rel ${TTYD_RELS})
        if(_rel_format STREQUAL "ELF")
            file(WRITE ${_relgen}/rel_${rel}_sections.h.tmp
                "/* Generated by CMakeLists.txt: REL ${rel}'s data in its own sections. */\n"
                "#pragma clang section data=\"rel_${rel}_data\" bss=\"rel_${rel}_bss\"\n")
            file(WRITE ${_relgen}/rel_${rel}_bounds.c.tmp
                "/* Generated by CMakeLists.txt: keeps REL ${rel}'s sections (and their __start_/__stop_). */\n"
                "#pragma clang section data=\"rel_${rel}_data\" bss=\"rel_${rel}_bss\"\n"
                "char rel_${rel}_data_keep = 1;\nchar rel_${rel}_bss_keep;\n")
            set_source_files_properties(${_relgen}/rel_${rel}_bounds.c PROPERTIES COMPILE_OPTIONS -fno-common)
            string(APPEND _table_decls
                "extern char __start_rel_${rel}_data[] PORT_REL_HIDDEN, __stop_rel_${rel}_data[] PORT_REL_HIDDEN,"
                " __start_rel_${rel}_bss[] PORT_REL_HIDDEN, __stop_rel_${rel}_bss[] PORT_REL_HIDDEN;\n"
                "#define rel_${rel}_data_start (*__start_rel_${rel}_data)\n#define rel_${rel}_data_end (*__stop_rel_${rel}_data)\n"
                "#define rel_${rel}_bss_start (*__start_rel_${rel}_bss)\n#define rel_${rel}_bss_end (*__stop_rel_${rel}_bss)\n")
        elseif(_rel_format STREQUAL "WASM")
            file(WRITE ${_relgen}/rel_${rel}_sections.h.tmp
                "/* Generated by CMakeLists.txt: wasm sections are set by rel_rename.py. */\n")
            file(WRITE ${_relgen}/rel_${rel}_bounds.c.tmp
                "/* Generated by CMakeLists.txt: REL ${rel}'s sections are never empty. */\n"
                "__attribute__((used, section(\"rel_${rel}_data\"))) char rel_${rel}_data_keep = 1;\n"
                "__attribute__((used, section(\"rel_${rel}_bss\"))) char rel_${rel}_bss_keep;\n")
            string(APPEND _table_decls
                "extern char __start_rel_${rel}_data[] PORT_REL_HIDDEN, __stop_rel_${rel}_data[] PORT_REL_HIDDEN,"
                " __start_rel_${rel}_bss[] PORT_REL_HIDDEN, __stop_rel_${rel}_bss[] PORT_REL_HIDDEN;\n"
                "#define rel_${rel}_data_start (*__start_rel_${rel}_data)\n#define rel_${rel}_data_end (*__stop_rel_${rel}_data)\n"
                "#define rel_${rel}_bss_start (*__start_rel_${rel}_bss)\n#define rel_${rel}_bss_end (*__stop_rel_${rel}_bss)\n")
        elseif(_rel_format STREQUAL "MACHO")
            file(WRITE ${_relgen}/rel_${rel}_sections.h.tmp
                "/* Generated by CMakeLists.txt: REL ${rel}'s data in its own sections. */\n"
                "#pragma clang section data=\"__DATA,__rd_${rel}\" bss=\"__DATA,__rb_${rel}\"\n")
            file(WRITE ${_relgen}/rel_${rel}_bounds.c.tmp
                "/* Generated by CMakeLists.txt: REL ${rel}'s sections are never empty. */\n"
                "__attribute__((used, section(\"__DATA,__rd_${rel}\"))) char rel_${rel}_data_pad = 1;\n"
                "__attribute__((used, section(\"__DATA,__rb_${rel}\"))) char rel_${rel}_bss_pad = 0;\n")
            string(APPEND _table_decls
                "extern char rel_${rel}_data_start __asm(\"section$start$__DATA$__rd_${rel}\");\n"
                "extern char rel_${rel}_data_end __asm(\"section$end$__DATA$__rd_${rel}\");\n"
                "extern char rel_${rel}_bss_start __asm(\"section$start$__DATA$__rb_${rel}\");\n"
                "extern char rel_${rel}_bss_end __asm(\"section$end$__DATA$__rb_${rel}\");\n")
        else()
            file(WRITE ${_relgen}/rel_${rel}_sections.h.tmp
                "/* Generated by CMakeLists.txt: REL ${rel}'s data in its own grouped sections. */\n"
                "#pragma clang section data=\".data$rel_${rel}_m\" bss=\".bss$rel_${rel}_m\"\n")
            file(WRITE ${_relgen}/rel_${rel}_bounds.c.tmp
                "/* Generated by CMakeLists.txt: markers around REL ${rel}'s data and bss. */\n"
                "#pragma clang section data=\".data$rel_${rel}_a\" bss=\".bss$rel_${rel}_a\"\n"
                "char rel_${rel}_data_start = 1;\nchar rel_${rel}_bss_start;\n"
                "#pragma clang section data=\".data$rel_${rel}_z\" bss=\".bss$rel_${rel}_z\"\n"
                "char rel_${rel}_data_end = 1;\nchar rel_${rel}_bss_end;\n")
            string(APPEND _table_decls
                "extern char rel_${rel}_data_start, rel_${rel}_data_end, rel_${rel}_bss_start, rel_${rel}_bss_end;\n")
        endif()
        configure_file(${_relgen}/rel_${rel}_sections.h.tmp ${_relgen}/rel_${rel}_sections.h COPYONLY)
        configure_file(${_relgen}/rel_${rel}_bounds.c.tmp ${_relgen}/rel_${rel}_bounds.c COPYONLY)
        list(APPEND TTYD_REL_STATIC_SOURCES ${_relgen}/rel_${rel}_bounds.c)
        string(APPEND _table_decls
            "extern void rel_${rel}___prolog(void);\nextern void rel_${rel}___epilog(void);\n")
        set(_bounds_row "&rel_${rel}_data_start, &rel_${rel}_data_end, &rel_${rel}_bss_start, &rel_${rel}_bss_end")
        string(APPEND _table_rows
            "    {\"${rel}\", rel_${rel}___prolog, rel_${rel}___epilog, ${_bounds_row}},\n")

        add_library(rel_${rel} OBJECT ${TTYD_REL_${rel}_SOURCES} ${CMAKE_SOURCE_DIR}/port/rel_ctors.c)
        ttyd_game_target(rel_${rel})
        target_compile_definitions(rel_${rel} PRIVATE PORT_REL_NAME=${rel} PORT_RELS_STATIC=1)
        # -fno-common: tentative definitions must be real (renamable) symbols.
        target_compile_options(rel_${rel} PRIVATE -fno-common "SHELL:-include ${_relgen}/rel_${rel}_sections.h")
        if(EXISTS ${CMAKE_SOURCE_DIR}/rels/${rel}/include)
            target_include_directories(rel_${rel} PRIVATE ${CMAKE_SOURCE_DIR}/rels/${rel}/include)
        endif()
        set(_archive ${CMAKE_BINARY_DIR}/rel/rel_${rel}${CMAKE_STATIC_LIBRARY_SUFFIX})
        add_custom_command(OUTPUT ${_archive}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/port/rel_rename.py
                --rel ${rel} --out ${_archive} --nm ${TTYD_LLVM_NM} --objcopy ${TTYD_LLVM_OBJCOPY}
                --ar ${TTYD_LLVM_AR} $<TARGET_OBJECTS:rel_${rel}>
            DEPENDS rel_${rel} $<TARGET_OBJECTS:rel_${rel}> ${CMAKE_SOURCE_DIR}/tools/port/rel_rename.py
            COMMAND_EXPAND_LISTS
            COMMENT "Renaming REL ${rel}'s symbols")
        list(APPEND TTYD_REL_ARCHIVES ${_archive})
    endforeach()
    file(WRITE ${_relgen}/rel_static_table.c.tmp
        "/* Generated by CMakeLists.txt: the RELs linked into this executable. */\n"
        "#include \"port/rel_pc.h\"\n\n"
        "#define PORT_REL_HIDDEN __attribute__((visibility(\"hidden\")))\n${_table_decls}\n"
        "const PortRelStatic portRelStaticTable[] = {\n${_table_rows}};\n"
        "const int portRelStaticCount = sizeof(portRelStaticTable) / sizeof(portRelStaticTable[0]);\n")
    configure_file(${_relgen}/rel_static_table.c.tmp ${_relgen}/rel_static_table.c COPYONLY)
    list(APPEND TTYD_REL_STATIC_SOURCES ${_relgen}/rel_static_table.c)
    add_custom_target(ttyd_rels DEPENDS ${TTYD_REL_ARCHIVES})
else()
    foreach(rel ${TTYD_RELS})
        add_library(rel_${rel} MODULE ${TTYD_REL_${rel}_SOURCES} ${CMAKE_SOURCE_DIR}/port/rel_ctors.c
            ${CMAKE_SOURCE_DIR}/port/rel_export.c)
        ttyd_game_target(rel_${rel})
        target_compile_definitions(rel_${rel} PRIVATE PORT_REL_NAME=${rel})
        if(EXISTS ${CMAKE_SOURCE_DIR}/rels/${rel}/include)
            target_include_directories(rel_${rel} PRIVATE ${CMAKE_SOURCE_DIR}/rels/${rel}/include)
        endif()
        set_target_properties(rel_${rel} PROPERTIES PREFIX "" OUTPUT_NAME ${rel}
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/rel
            C_VISIBILITY_PRESET hidden)
        target_link_libraries(rel_${rel} PRIVATE ttyd)
        list(APPEND TTYD_REL_TARGETS rel_${rel})
    endforeach()
    add_custom_target(ttyd_rels ALL DEPENDS ${TTYD_REL_TARGETS})
endif()

# --- Platform layer replacing the Dolphin SDK ---
file(GLOB TTYD_PORT_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/port/src/common/*.c)
# The GameCube's maths library (MSL's fdlibm), the same results on every host
# (online co-op's lockstep): its sources with port/include/port/fdlibm_pc.h
# forced in front, for their fdlibm.h (port_prefix.h routes the game to it).
file(GLOB TTYD_FDLIBM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/libs/msl/src/MSL_C.PPCEABI.bare.H.a/[eksw]_*.c")
if(MSVC)
    set(_fdlibm_include /FI${CMAKE_SOURCE_DIR}/port/include/port/fdlibm_pc.h)
else()
    set(_fdlibm_include -include ${CMAKE_SOURCE_DIR}/port/include/port/fdlibm_pc.h)
endif()
set_source_files_properties(${TTYD_FDLIBM_SOURCES} PROPERTIES COMPILE_OPTIONS "${_fdlibm_include}")
list(APPEND TTYD_PORT_SOURCES ${TTYD_FDLIBM_SOURCES})
if(TTYD_BACKEND STREQUAL "headless")
    file(GLOB_RECURSE headless_sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/port/src/headless/*.c)
    # Hardware-independent SDK sources that build unchanged on PC.
    file(GLOB sdk_portable CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/libs/dolsdk2004/src/mtx/*.c
        ${CMAKE_SOURCE_DIR}/libs/dolsdk2004/src/gd/*.c)
    list(APPEND TTYD_PORT_SOURCES ${headless_sources} ${sdk_portable}
        ${CMAKE_SOURCE_DIR}/libs/dolsdk2004/src/os/OSAlloc.c)
else()
    file(GLOB_RECURSE aurora_sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/port/src/aurora/*.c)
    list(APPEND TTYD_PORT_SOURCES ${aurora_sources})
endif()
if(ANDROID)
    # Android glue: logcat, crash reports, lifecycle (port/android/native; the
    # Gradle project is port/android).
    file(GLOB android_sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/port/android/native/*.c)
    list(APPEND TTYD_PORT_SOURCES ${android_sources})
endif()

# MusyX sound system, built from the decompiled source with its PC target.
file(STRINGS ${CMAKE_SOURCE_DIR}/libs/musyx/CMakeLists.txt musyx_lines REGEX "^ +src/musyx/.*\\.c$")
set(TTYD_MUSYX_SOURCES)
foreach(line ${musyx_lines})
    string(STRIP "${line}" line)
    list(APPEND TTYD_MUSYX_SOURCES ${CMAKE_SOURCE_DIR}/libs/musyx/${line})
endforeach()
# PC hardware layer: hw_pc.c (output, IRQ lock), hw_pcmixer.c (DSP command list
# executed in software), hw_aramdma.c (ARAM on the port's host buffer, portAramPtr).
# An object library so every MusyX symbol is in the executable for the RELs.
add_library(musyx_pc OBJECT ${TTYD_MUSYX_SOURCES})
ttyd_game_target(musyx_pc)
# Quote-only: musyx/sal.h must not shadow the system <sal.h> (MSVC vcruntime.h).
target_compile_options(musyx_pc PRIVATE -iquote ${CMAKE_SOURCE_DIR}/libs/musyx/include/musyx)
if(TTYD_BACKEND STREQUAL "aurora")
    # SDL3 audio stream output (headers only here; the executable links SDL via Aurora).
    target_link_libraries(musyx_pc PRIVATE ${AURORA_SDL3_TARGET})
endif()

# Lua 5.4 for Lua mods (docs/LUA_MODDING.md; libs/lua, docs/THIRD_PARTY.md):
# the core and the safe standard libraries only. io, os, package (C
# modules), debug and the stand-alone interpreters are not built at all.
# Linked statically everywhere (headless too), no dynamic loading.
set(TTYD_LUA_SOURCES)
foreach(_f lapi lauxlib lbaselib lcode lcorolib lctype ldebug ldo ldump lfunc lgc llex lmathlib lmem lobject
           lopcodes lparser lstate lstring lstrlib ltable ltablib ltm lundump lutf8lib lvm lzio)
    list(APPEND TTYD_LUA_SOURCES ${CMAKE_SOURCE_DIR}/libs/lua/src/${_f}.c)
endforeach()
add_library(lua STATIC ${TTYD_LUA_SOURCES})
target_include_directories(lua PUBLIC ${CMAKE_SOURCE_DIR}/libs/lua/src)
# fixed hash and sort seeds, for online play's lockstep (libs/lua/lua_port.h)
target_compile_options(lua PRIVATE -include ${CMAKE_SOURCE_DIR}/libs/lua/lua_port.h -w)
target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS=1)
set_target_properties(lua PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

# Save states (port/src/aurora/savestate_aurora.c; Aurora with static RELs):
# the game's data and bss go to their own sections (port_prefix.h), copied as
# a whole with MEM1, ARAM, the RELs' sections and the save-state heap
# (port/src/common/snapheap_pc.c). -fno-common: tentative definitions must be
# real symbols to be placed. The port's decoders' state goes there too (the
# game's data points into what they decode); MusyX's output layer stays out
# (the SDL stream, the audio thread's buffers).
if(TTYD_BACKEND STREQUAL "aurora" AND TTYD_RELS_STATIC)
    set(TTYD_SAVE_STATES ON)
    foreach(_t ttyd_dol musyx_pc)
        target_compile_definitions(${_t} PRIVATE TTYD_SAVE_STATES=1 TTYD_SNAPSHOT_SECTION=1)
        target_compile_options(${_t} PRIVATE -fno-common)
    endforeach()
    set_property(SOURCE ${CMAKE_SOURCE_DIR}/libs/musyx/src/musyx/runtime/hw_pc.c TARGET_DIRECTORY musyx_pc
        APPEND PROPERTY COMPILE_DEFINITIONS TTYD_NO_SNAPSHOT=1)
    foreach(_f anim_pc tpl_pc map_pc camroad_pc gx_arrays_pc snapshot_bounds_pc luigi_pc costume_pc skin_pc battlex_pc enemy_variant_pc)
        set_property(SOURCE ${CMAKE_SOURCE_DIR}/port/src/common/${_f}.c APPEND PROPERTY
            COMPILE_DEFINITIONS TTYD_SNAPSHOT_SECTION=1)
        set_property(SOURCE ${CMAKE_SOURCE_DIR}/port/src/common/${_f}.c APPEND PROPERTY COMPILE_OPTIONS -fno-common)
    endforeach()
endif()

if(ANDROID)
    # Android: SDLActivity loads the game as libmain.so and calls its SDL_main.
    add_library(ttyd SHARED $<TARGET_OBJECTS:ttyd_dol> $<TARGET_OBJECTS:musyx_pc> ${TTYD_PORT_SOURCES}
        ${TTYD_REL_STATIC_SOURCES})
else()
    add_executable(ttyd $<TARGET_OBJECTS:ttyd_dol> $<TARGET_OBJECTS:musyx_pc> ${TTYD_PORT_SOURCES}
        ${TTYD_REL_STATIC_SOURCES})
endif()
if(TTYD_RELS_STATIC)
    add_dependencies(ttyd ttyd_rels)
    target_compile_definitions(ttyd PRIVATE PORT_RELS_STATIC=1)
    # Every REL object, though nothing references it by name.
    if(_rel_format STREQUAL "ELF")
        # The REL sections are reached only through __start_/__stop_ (hidden,
        # so a shared libmain.so resolves them locally), which --gc-sections
        # would no longer keep alive.
        target_link_libraries(ttyd PRIVATE -Wl,--whole-archive ${TTYD_REL_ARCHIVES} -Wl,--no-whole-archive)
        target_link_options(ttyd PRIVATE "LINKER:-z,nostart-stop-gc")
    elseif(_rel_format STREQUAL "WASM")
        target_link_libraries(ttyd PRIVATE -Wl,--whole-archive ${TTYD_REL_ARCHIVES} -Wl,--no-whole-archive)
    else()
        foreach(_archive ${TTYD_REL_ARCHIVES})
            if(_rel_format STREQUAL "MACHO")
                target_link_options(ttyd PRIVATE "LINKER:-force_load,${_archive}")
            else()
                target_link_options(ttyd PRIVATE "LINKER:/WHOLEARCHIVE:${_archive}")
            endif()
        endforeach()
    endif()
    set_property(TARGET ttyd APPEND PROPERTY LINK_DEPENDS ${TTYD_REL_ARCHIVES})
endif()
if(ANDROID)
    set_target_properties(ttyd PROPERTIES OUTPUT_NAME main)
    # aurora::main's SDL_main (lib/main.cpp) is only referenced from Java: keep it.
    # liblog: logcat output and crash reports (port/android/native).
    target_link_options(ttyd PRIVATE "LINKER:--undefined=SDL_main")
    target_link_libraries(ttyd PRIVATE log android)
else()
    set_target_properties(ttyd PROPERTIES OUTPUT_NAME ${TTYD_EXE_NAME})
endif()
# Paper Cut's icon (port/assets/icon, tools/port/make_icons.py): Windows'
# executable icon from a resource script; macOS's in the bundle (below)
if(WIN32)
    enable_language(RC)
    target_sources(ttyd PRIVATE ${CMAKE_SOURCE_DIR}/port/windows/papercut.rc)
    # the resource compiler doesn't report the .ico as a dependency: a new icon
    # alone rebuilds the resources only with this
    set_source_files_properties(${CMAKE_SOURCE_DIR}/port/windows/papercut.rc PROPERTIES
        OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/port/assets/icon/papercut.ico)
    # "Paper Cut.exe", what the player starts: it starts ttyd-us.exe, whose
    # launcher starts the disc's version (tools/port/release.py packages the
    # three builds together); a window program, no console
    if(TTYD_BACKEND STREQUAL "aurora")
        add_executable(papercut WIN32 ${CMAKE_SOURCE_DIR}/port/windows/papercut_start.c
            ${CMAKE_SOURCE_DIR}/port/windows/papercut.rc)
        set_target_properties(papercut PROPERTIES OUTPUT_NAME "Paper Cut")
        target_compile_definitions(papercut PRIVATE UNICODE _UNICODE)
        target_link_options(papercut PRIVATE -municode)
    endif()
endif()
if(NOT TTYD_RELS_STATIC)
    # REL modules link against the executable's symbols.
    set_target_properties(ttyd PROPERTIES ENABLE_EXPORTS ON WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
target_compile_definitions(ttyd PRIVATE ${TTYD_DEFINES})
if(TTYD_SAVE_STATES)
    target_compile_definitions(ttyd PRIVATE TTYD_SAVE_STATES=1)
    if(APPLE)
        target_link_libraries(ttyd PRIVATE compression) # LZ4 for the states kept in memory (elsewhere, the port's own)
    endif()
endif()
target_include_directories(ttyd PRIVATE ${TTYD_INCLUDES} ${CMAKE_BINARY_DIR}/generated)
target_compile_options(ttyd PRIVATE ${TTYD_CFLAGS})
target_link_libraries(ttyd PRIVATE ${TTYD_LIBM} ${CMAKE_DL_LIBS})
if(APPLE)
    # the JP release's Shift-JIS text as UTF-8 for the second screen (port/src/common/companion_pc.c)
    target_link_libraries(ttyd PRIVATE iconv)
endif()
if(TTYD_WEB)
    # Memory: sound names are told from ids by address (include/sfx_name_word.h),
    # so everything the game uses lies at 2 GiB and up, as on GameCube (data
    # first, then the stack and the heap); the 2 GiB below are never touched.
    # Node: the disc and saves through the host file system (NODERAWFS); game
    # threads come from a pool made at startup, since the main thread blocks
    # on them before the event loop could start a worker.
    target_link_options(ttyd PRIVATE
        -sGLOBAL_BASE=2147483648 -sSTACK_SIZE=8MB
        -sINITIAL_MEMORY=2560MB -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4GB
        -sPTHREAD_POOL_SIZE=8 -sDEFAULT_PTHREAD_STACK_SIZE=512KB
        -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1
        # function names in traps and crash reports
        --profiling-funcs)
endif()
if(WIN32)
    target_link_libraries(ttyd PRIVATE dbghelp ws2_32) # ws2_32: LiveSplit (port/src/common/livesplit_pc.c)
endif()
if(TTYD_BACKEND STREQUAL "aurora")
    target_link_libraries(ttyd PRIVATE aurora::core aurora::gx aurora::gd aurora::vi
        aurora::pad aurora::si aurora::os aurora::mtx aurora::card aurora::dvd
        aurora::thp aurora::main)
    # SDL3 directly: the disc picker's file dialog and message box (disc_aurora.c).
    target_link_libraries(ttyd PRIVATE ${AURORA_SDL3_TARGET})
    # Mbed TLS (Apache-2.0, a fixed release): online co-op's relay over wss://
    # (port/src/common/online_ws_pc.c), the same TLS on every platform
    if(NOT EMSCRIPTEN)
        include(FetchContent)
        set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
        set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
        set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
        FetchContent_Declare(mbedtls
            URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
            URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        # (Windows: clang with MSVC's ABI "simulates" MSVC, and Mbed TLS would add
        # cl's /W3 /utf-8, which the clang driver takes for files: clang's flags)
        set(_ttyd_simulate ${CMAKE_C_SIMULATE_ID})
        set(CMAKE_C_SIMULATE_ID "")
        FetchContent_MakeAvailable(mbedtls)
        set(CMAKE_C_SIMULATE_ID ${_ttyd_simulate})
        target_link_libraries(ttyd PRIVATE mbedtls mbedx509 mbedcrypto)
        # libjuice (MPL-2.0, a fixed release; sm64coopdx's too): the direct UDP
        # path (ICE: STUN, hole punching, IPv6), port/src/common/online_ice_pc.c
        # (its BUILD_SHARED_LIBS option defaults to ON, and would reach every
        # library after it: off, as the build always was)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(NO_SERVER ON CACHE BOOL "" FORCE)
        set(NO_TESTS ON CACHE BOOL "" FORCE)
        FetchContent_Declare(libjuice
            URL https://github.com/paullouisageneau/libjuice/archive/refs/tags/v1.7.4.tar.gz
            URL_HASH SHA256=95c088862aa1b88b73d62aa99577135010eb768fcafb252d13aa85a8515ef798
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE EXCLUDE_FROM_ALL)
        FetchContent_MakeAvailable(libjuice)
        target_link_libraries(ttyd PRIVATE LibJuice::LibJuiceStatic)
        # the relays' addresses, in order (port/relay, docs/ONLINE_RELAY.md); empty:
        # rooms off, connecting by address only ($TTYD_ONLINE_RELAY overrides it, for tests)
        set(TTYD_ONLINE_RELAY "wss://papercut-relay.llcoolt.workers.dev" CACHE STRING
            "Online co-op's relays, wss://<name>.<subdomain>.workers.dev, separated by spaces")
        if(TTYD_ONLINE_RELAY)
            target_compile_definitions(ttyd PRIVATE PORT_ONLINE_RELAY_URL="${TTYD_ONLINE_RELAY}")
        endif()
    endif()
    # Windows: SDL3, Dawn (+ the DXC DLLs it loads), nod, libpng, zlib next to the exe.
    include(${CMAKE_SOURCE_DIR}/extern/aurora/cmake/AuroraCopyRuntimeDLLs.cmake)
    aurora_copy_runtime_dlls(ttyd)
    # The settings page (F1 / Back / three-finger tap), ImGui: C++ like the
    # touch overlay, built without the game's prefix header.
    # The launcher's typeface (port/assets/fonts, Fredoka: docs/THIRD_PARTY.md),
    # embedded as C arrays so no file ships beside the executable.
    function(ttyd_embed_file in symbol out)
        file(READ ${in} _hex HEX)
        string(LENGTH "${_hex}" _len)
        math(EXPR _len "${_len} / 2")
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _hex "${_hex}")
        file(WRITE ${out}.tmp "/* GENERATED from ${in} by CMakeLists.txt. Do not edit. */\n"
            "const unsigned char ${symbol}[] = {${_hex}};\nconst unsigned int ${symbol}_size = ${_len};\n")
        file(COPY_FILE ${out}.tmp ${out} ONLY_IF_DIFFERENT)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${in})
    endfunction()
    ttyd_embed_file(${CMAKE_SOURCE_DIR}/port/assets/fonts/Fredoka-Bold.ttf portFontBold
        ${CMAKE_BINARY_DIR}/generated/font_bold.c)
    ttyd_embed_file(${CMAKE_SOURCE_DIR}/port/assets/fonts/Fredoka-Medium.ttf portFontMedium
        ${CMAKE_BINARY_DIR}/generated/font_medium.c)
    # an object library: a feature's settings file registers its controls from a
    # static initializer, which a static library would leave out unreferenced
    # Features' settings files (port/settings/features/*.cpp): each declares its
    # settings and adds its controls to the page, without editing shared files.
    file(GLOB TTYD_SETTINGS_FEATURES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/port/settings/features/*.cpp)
    # the window's icon: Paper Cut's logo, 64x64 RGBA (tools/port/make_icons.py)
    ttyd_embed_file(${CMAKE_SOURCE_DIR}/port/assets/icon/papercut-64.rgba portIcon64
        ${CMAKE_BINARY_DIR}/generated/icon64.c)
    # Luigi's animation pack, our own work (port/src/aurora/luigi_model_aurora.c, docs/LUIGI_ANIMS.md)
    ttyd_embed_file(${CMAKE_SOURCE_DIR}/port/assets/luigi/luigi_anims.bin portLuigiPack
        ${CMAKE_BINARY_DIR}/generated/luigi_pack.c)
    # Paper Cut's logo, for the artwork of Add to Steam (port/settings/features/steam_shortcut.cpp)
    ttyd_embed_file(${CMAKE_SOURCE_DIR}/port/assets/icon/papercut-512.png portLogo512
        ${CMAKE_BINARY_DIR}/generated/logo512.c)
    # the community controller mappings (port/settings/features/controllers.cpp, docs/THIRD_PARTY.md)
    ttyd_embed_file(${CMAKE_SOURCE_DIR}/port/assets/controllers/gamecontrollerdb.txt portControllerDb
        ${CMAKE_BINARY_DIR}/generated/controller_db.c)
    add_library(ttyd_settings OBJECT ${CMAKE_SOURCE_DIR}/port/settings/settings_ui.cpp
        ${CMAKE_SOURCE_DIR}/port/settings/launcher_ui.cpp ${CMAKE_SOURCE_DIR}/port/settings/launcher_style.cpp
        ${CMAKE_SOURCE_DIR}/port/settings/launcher_popup.cpp
        ${CMAKE_SOURCE_DIR}/port/settings/button_glyphs.cpp
        ${CMAKE_SOURCE_DIR}/port/settings/http.cpp ${CMAKE_SOURCE_DIR}/port/settings/tr.cpp
        # plain HTTP on the local network (save transfer); the QR code (libs/qrcodegen, docs/THIRD_PARTY.md)
        ${CMAKE_SOURCE_DIR}/port/settings/lan_http.cpp ${CMAKE_SOURCE_DIR}/libs/qrcodegen/qrcodegen.c
        ${CMAKE_BINARY_DIR}/generated/font_bold.c ${CMAKE_BINARY_DIR}/generated/font_medium.c
        ${CMAKE_BINARY_DIR}/generated/icon64.c ${CMAKE_BINARY_DIR}/generated/luigi_pack.c
        ${CMAKE_BINARY_DIR}/generated/controller_db.c ${CMAKE_BINARY_DIR}/generated/logo512.c
        ${TTYD_SETTINGS_FEATURES})
    target_include_directories(ttyd_settings PRIVATE ${CMAKE_SOURCE_DIR}/port/include)
    target_compile_features(ttyd_settings PRIVATE cxx_std_20)
    # The port's version, shown on the settings page (tools/port/release.py sets it).
    set(TTYD_PORT_VERSION "dev" CACHE STRING "The port's version (releases: e.g. 0.1.0)")
    target_compile_definitions(ttyd_settings PRIVATE _CRT_SECURE_NO_WARNINGS=1 PORT_VERSION="${TTYD_PORT_VERSION}"
        PORT_GAME_ID="${TTYD_VERSION}")
    # Discord status (port/settings/features/discord.cpp, docs/DISCORD_STATUS.md): the
    # Discord application's ID; empty: the feature is built in but does nothing, its setting hidden
    set(TTYD_DISCORD_APP_ID "1567620424886657085" CACHE STRING "Discord status's Discord application ID (Paper Cut's; empty: off)")
    target_compile_definitions(ttyd_settings PRIVATE PORT_DISCORD_APP_ID="${TTYD_DISCORD_APP_ID}")
    # zlib: unpacking an update's zip (port/settings/features/update_install.cpp)
    target_link_libraries(ttyd_settings PRIVATE aurora::core aurora::pad ZLIB::ZLIB ${AURORA_SDL3_TARGET})
    # Luigi's faces for Play as Luigi: the art recipes, run on the player's disc at the start
    # (port/src/aurora/art_recipes_aurora.c, port/assets/luigi/art/RECIPES.md), and the drawn PNGs that
    # stand in where they can't be made, our own work, by their path under port/assets/luigi
    # (port/src/aurora/luigi_faces_aurora.c)
    set(_la_root ${CMAKE_SOURCE_DIR}/port/assets/luigi)
    file(GLOB_RECURSE _la_assets CONFIGURE_DEPENDS RELATIVE ${_la_root} ${_la_root}/art/icons/*.png)
    file(GLOB _la_recipes CONFIGURE_DEPENDS RELATIVE ${_la_root} ${_la_root}/art/recipes/*.recipe)
    list(APPEND _la_assets ${_la_recipes})
    list(SORT _la_assets)
    set(_la_c "/* GENERATED from port/assets/luigi/art by CMakeLists.txt. Do not edit. */\n")
    set(_la_index "")
    set(_la_n 0)
    foreach(_rel ${_la_assets})
        file(READ ${_la_root}/${_rel} _hex HEX)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_la_root}/${_rel})
        string(LENGTH "${_hex}" _len)
        math(EXPR _len "${_len} / 2")
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _hex "${_hex}")
        string(APPEND _la_c "static const unsigned char a${_la_n}[] = {${_hex}};\n")
        string(APPEND _la_index "    {\"${_rel}\", a${_la_n}, ${_len}},\n")
        math(EXPR _la_n "${_la_n} + 1")
    endforeach()
    string(APPEND _la_c "const struct { const char* path; const unsigned char* data; unsigned int size; } "
        "portLuigiArtTable[] = {\n${_la_index}    {0, 0, 0},\n};\n")
    file(WRITE ${CMAKE_BINARY_DIR}/generated/luigi_art.c.tmp "${_la_c}")
    file(COPY_FILE ${CMAKE_BINARY_DIR}/generated/luigi_art.c.tmp ${CMAKE_BINARY_DIR}/generated/luigi_art.c
        ONLY_IF_DIFFERENT)
    target_sources(ttyd_settings PRIVATE ${CMAKE_BINARY_DIR}/generated/luigi_art.c)
    # the "what's new" page's list (port/include/port/whats_new.h), from this version's release notes
    file(GLOB _wn_notes CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/docs/release_notes/*.md)
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/generated/whats_new.c
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/port/whats_new.py ${CMAKE_SOURCE_DIR}/docs/release_notes
            ${TTYD_PORT_VERSION} ${CMAKE_BINARY_DIR}/generated/whats_new.c
        DEPENDS ${_wn_notes} ${CMAKE_SOURCE_DIR}/tools/port/whats_new.py
        COMMENT "The what's new page's list" VERBATIM)
    target_sources(ttyd_settings PRIVATE ${CMAKE_BINARY_DIR}/generated/whats_new.c)
    target_link_libraries(ttyd PRIVATE ttyd_settings)
    if(WIN32)
        # HTTPS for the update check (port/settings/http.cpp)
        target_link_libraries(ttyd_settings PRIVATE winhttp ws2_32 bcrypt) # (+ the save transfer's sockets, its token)
    endif()
    # Mods (port/include/port/mods.h): C++, built without the game's prefix header.
    # A code mod's library for this build is in its lib/<platform>/.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" OR CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(_mod_arch arm64)
    else()
        set(_mod_arch x64)
    endif()
    if(ANDROID)
        set(TTYD_MOD_PLATFORM android-${_mod_arch})
    elseif(TTYD_IOS)
        set(TTYD_MOD_PLATFORM ios)
    elseif(APPLE)
        set(TTYD_MOD_PLATFORM macos)
    elseif(WIN32)
        set(TTYD_MOD_PLATFORM windows-${_mod_arch})
    else()
        set(TTYD_MOD_PLATFORM linux-${_mod_arch})
    endif()
    add_library(ttyd_mods STATIC ${CMAKE_SOURCE_DIR}/port/mods/mods.cpp ${CMAKE_SOURCE_DIR}/port/mods/mod_api.cpp
        ${CMAKE_SOURCE_DIR}/port/mods/evt_asm.cpp ${CMAKE_BINARY_DIR}/generated/statics_index.c
        ${CMAKE_SOURCE_DIR}/port/mods/lua_host.cpp
        ${CMAKE_SOURCE_DIR}/port/archipelago/ap_client.cpp ${CMAKE_SOURCE_DIR}/port/archipelago/websocket.cpp
        ${CMAKE_SOURCE_DIR}/port/archipelago/ttyd_ap.cpp ${CMAKE_SOURCE_DIR}/port/archipelago/ttyd_rando.cpp
        ${CMAKE_SOURCE_DIR}/port/archipelago/rando_gen.cpp ${CMAKE_SOURCE_DIR}/port/archipelago/open_world.cpp)
    target_include_directories(ttyd_mods PRIVATE ${CMAKE_SOURCE_DIR}/port/include)
    target_compile_features(ttyd_mods PRIVATE cxx_std_20)
    target_compile_definitions(ttyd_mods PRIVATE _CRT_SECURE_NO_WARNINGS=1 PORT_GAME_ID="${TTYD_VERSION}"
        TTYD_MOD_PLATFORM="${TTYD_MOD_PLATFORM}")
    target_link_libraries(ttyd_mods PRIVATE aurora::core aurora::gx aurora::dvd ZLIB::ZLIB ${AURORA_SDL3_TARGET})
    # Lua mods (port/mods/lua_host.cpp; the game's side, port/src/aurora/lua_game_aurora.c, in ttyd)
    target_link_libraries(ttyd_mods PRIVATE lua)
    target_link_libraries(ttyd PRIVATE lua)
    if(WIN32)
        target_link_libraries(ttyd_mods PRIVATE winhttp) # Archipelago's WebSocket (port/archipelago/websocket.cpp)
    endif()
    target_link_libraries(ttyd PRIVATE ttyd_mods)
    target_link_libraries(ttyd_settings PRIVATE ttyd_mods)

    # Code mods built into the game (docs/MODDING.md, "Code mods"; the only
    # way on iOS): folders with a mod.json and src/*.c, compiled with the
    # game (its headers and prefix), each entry point named after its id.
    set(TTYD_BUILTIN_MODS "" CACHE STRING "Code mods built into the game: folders, ';'-separated")
    set(_builtin_decls "")
    set(_builtin_rows "")
    set(_builtin_count 0)
    foreach(_dir ${TTYD_BUILTIN_MODS})
        get_filename_component(_dir ${_dir} ABSOLUTE BASE_DIR ${CMAKE_SOURCE_DIR})
        file(READ ${_dir}/mod.json _json)
        string(JSON _id GET ${_json} id)
        string(MAKE_C_IDENTIFIER ${_id} _cid)
        file(GLOB_RECURSE _srcs CONFIGURE_DEPENDS ${_dir}/src/*.c)
        target_sources(ttyd PRIVATE ${_srcs})
        set_source_files_properties(${_srcs} TARGET_DIRECTORY ttyd PROPERTIES COMPILE_DEFINITIONS TTYD_MOD_BUILTIN=${_cid})
        string(APPEND _builtin_decls "int ttyd_mod_init_${_cid}(const void* api);\n")
        # its mod.json too (name, options) for when it has no folder
        string(REPLACE "\\" "\\\\" _json "${_json}")
        string(REPLACE "\"" "\\\"" _json "${_json}")
        string(REPLACE "\r" "" _json "${_json}")
        string(REPLACE "\n" "\\n\"\n        \"" _json "${_json}")
        string(APPEND _builtin_rows "    {\"${_id}\", ttyd_mod_init_${_cid},\n        \"${_json}\"},\n")
        math(EXPR _builtin_count "${_builtin_count} + 1")
    endforeach()
    file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/builtin_mods.c CONTENT
"/* GENERATED by CMakeLists.txt from TTYD_BUILTIN_MODS (port/mods/mod_api.cpp). */
#include <stddef.h>
typedef struct PortBuiltinMod {
    const char* id;
    int (*init)(const void* api);
    const char* json; /* its mod.json */
} PortBuiltinMod;
@_builtin_decls@const PortBuiltinMod portBuiltinMods[] = {
@_builtin_rows@    {NULL, NULL, NULL},
};
const int portBuiltinModCount = @_builtin_count@;
" @ONLY)
    target_sources(ttyd PRIVATE ${CMAKE_BINARY_DIR}/generated/builtin_mods.c)

    # Code mods as libraries call the game's functions: the executable
    # exports them. Windows: a .def from every game object and REL archive,
    # the port's own, and the SDK's (Aurora's GX, OS, VI... a ported
    # GameCube mod draws and times with them) (tools/port/gen_exports.py), which also makes the import
    # library mods link against (ttyd-<jp|us>.lib); ELF and Mach-O: the
    # dynamic symbol table.
    if(WIN32)
        set(_sdk_archives)
        foreach(_lib core gx gd vi pad si os mtx card dvd)
            list(APPEND _sdk_archives $<TARGET_FILE:aurora::${_lib}>)
        endforeach()
        file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/generated/export_inputs.txt
            CONTENT "$<JOIN:$<TARGET_OBJECTS:ttyd_dol>;$<TARGET_OBJECTS:musyx_pc>;$<TARGET_OBJECTS:ttyd>;${TTYD_REL_ARCHIVES};${_sdk_archives},\n>\n")
        set(_def ${CMAKE_BINARY_DIR}/generated/ttyd_exports.def)
        add_custom_command(TARGET ttyd PRE_LINK
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/port/gen_exports.py --nm ${TTYD_LLVM_NM}
                --out ${_def} ${CMAKE_BINARY_DIR}/generated/export_inputs.txt
            VERBATIM)
        target_link_options(ttyd PRIVATE "LINKER:/DEF:${_def}")
        # a changed input list relinks (the def is written at link time)
        set_property(TARGET ttyd APPEND PROPERTY LINK_DEPENDS ${CMAKE_BINARY_DIR}/generated/export_inputs.txt)
    endif()
    set_target_properties(ttyd PROPERTIES ENABLE_EXPORTS ON)

    # A code mod as a library: the game's headers, flags and prefix (so its
    # structs have the game's layout), linked against the game. Output in
    # <build>/mods/<target>.<dll|so|dylib>, for the mod's lib/<platform>/.
    function(ttyd_add_mod target)
        add_library(${target} MODULE ${ARGN})
        # the game's defines, includes and flags; C++ sources without the C-only ones
        target_compile_definitions(${target} PRIVATE ${TTYD_DEFINES})
        target_include_directories(${target} PRIVATE ${TTYD_INCLUDES})
        set(_cxx_flags ${TTYD_CFLAGS})
        list(REMOVE_ITEM _cxx_flags -fgnu89-inline -fcommon -Werror=implicit-function-declaration)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C>:${TTYD_CFLAGS}>"
            "$<$<COMPILE_LANGUAGE:CXX>:${_cxx_flags}>")
        target_link_libraries(${target} PRIVATE ttyd)
        set_target_properties(${target} PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/mods
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/mods)
    endfunction()
    # TTYD_LIBRARY_MODS: mod folders (each with src/*.c) to build as
    # libraries, <build>/mods/<folder>.<ext>; the example is always built.
    set(TTYD_LIBRARY_MODS "" CACHE STRING "Code mods to build as libraries: folders, ';'-separated")
    if(NOT ANDROID AND NOT TTYD_IOS)
        foreach(_dir ${CMAKE_SOURCE_DIR}/examples/mods/hello-native ${TTYD_LIBRARY_MODS})
            get_filename_component(_dir ${_dir} ABSOLUTE BASE_DIR ${CMAKE_SOURCE_DIR})
            get_filename_component(_name ${_dir} NAME)
            if(EXISTS ${_dir}/CMakeLists.txt)
                # a mod with its own build: it calls ttyd_add_mod itself
                add_subdirectory(${_dir} ${CMAKE_BINARY_DIR}/mod_builds/${_name})
            else()
                file(GLOB_RECURSE _srcs CONFIGURE_DEPENDS ${_dir}/src/*.c ${_dir}/src/*.cpp)
                if(NOT TARGET ${_name})
                    ttyd_add_mod(${_name} ${_srcs})
                endif()
            endif()
        endforeach()
    endif()
    if(TTYD_TOUCH_OVERLAY)
        # C++ and SDL/ImGui only: built without the game's prefix header.
        add_library(ttyd_touch STATIC ${CMAKE_SOURCE_DIR}/port/touch/touch_overlay.cpp)
        target_include_directories(ttyd_touch PRIVATE ${CMAKE_SOURCE_DIR}/port/include)
        target_compile_features(ttyd_touch PRIVATE cxx_std_20)
        target_compile_definitions(ttyd_touch PRIVATE _CRT_SECURE_NO_WARNINGS=1)
        target_link_libraries(ttyd_touch PRIVATE aurora::core ${AURORA_SDL3_TARGET})
        target_link_libraries(ttyd PRIVATE ttyd_touch)
        target_compile_definitions(ttyd PRIVATE PORT_TOUCH_OVERLAY=1)
    endif()
    if(TTYD_IOS)
        include(${CMAKE_SOURCE_DIR}/port/ios/ios.cmake)
    endif()
    if(APPLE)
        # Save sync through iCloud Drive (port/apple/cloud_apple.m), and iOS's
        # HTTPS (port/apple/http_apple.m, for port/settings/http.cpp). Its own
        # library: the game's prefix header must not reach Objective-C.
        enable_language(OBJC)
        # And text to speech (port/apple/speech_apple.m, AVSpeechSynthesizer).
        add_library(ttyd_cloud STATIC ${CMAKE_SOURCE_DIR}/port/apple/cloud_apple.m
            ${CMAKE_SOURCE_DIR}/port/apple/http_apple.m ${CMAKE_SOURCE_DIR}/port/apple/speech_apple.m)
        target_include_directories(ttyd_cloud PRIVATE ${CMAKE_SOURCE_DIR}/port/include)
        target_compile_options(ttyd_cloud PRIVATE -fobjc-arc)
        target_link_libraries(ttyd PRIVATE ttyd_cloud "-framework Foundation" "-framework AVFoundation")
    endif()
    if(NOT ANDROID)
        # Save sync: iCloud Drive on Apple devices (above), any folder on Windows and Linux
        # (port/src/aurora/folder_sync_aurora.c). The settings page has its section too.
        target_compile_definitions(ttyd PRIVATE PORT_CLOUD_SYNC=1)
        target_compile_definitions(ttyd_settings PRIVATE PORT_CLOUD_SYNC=1)
    endif()
    # Aurora's pipeline seed (tools/port/pipeline_cache.py): every GX pipeline
    # the game uses, built at launch so no draw waits for one (on a slow GPU a
    # model drawn for the first time was skipped until its pipeline was ready:
    # invisible cutscene characters on an iPhone). Made from runs on the
    # user's own disc, so it is bundled when present, never committed. Aurora
    # reads it from SDL_GetBasePath(): an iOS bundle's root, a macOS app's
    # Contents/Resources, elsewhere the executable's folder.
    set(TTYD_PIPELINE_SEED ${CMAKE_SOURCE_DIR}/data/initial_pipeline_cache.db CACHE FILEPATH
        "Bundled pipeline seed (initial_pipeline_cache.db), used when it exists")
    if(EXISTS ${TTYD_PIPELINE_SEED})
        get_filename_component(_seed_name ${TTYD_PIPELINE_SEED} NAME)
        if(NOT _seed_name STREQUAL "initial_pipeline_cache.db")
            message(FATAL_ERROR "TTYD_PIPELINE_SEED must be named initial_pipeline_cache.db")
        endif()
        if(APPLE)
            target_sources(ttyd PRIVATE ${TTYD_PIPELINE_SEED})
            if(TTYD_IOS)
                set_property(TARGET ttyd APPEND PROPERTY RESOURCE ${TTYD_PIPELINE_SEED})
            else()
                set_source_files_properties(${TTYD_PIPELINE_SEED} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
            endif()
            # a new seed must re-sign the bundle, which happens after the link
            set_property(TARGET ttyd APPEND PROPERTY LINK_DEPENDS ${TTYD_PIPELINE_SEED})
        elseif(NOT ANDROID)
            add_custom_command(TARGET ttyd POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TTYD_PIPELINE_SEED} $<TARGET_FILE_DIR:ttyd>)
        endif()
        message(STATUS "Bundling the pipeline seed ${TTYD_PIPELINE_SEED}")
    endif()
endif()

# --- macOS: the Aurora build is an .app bundle (ttyd-jp.app / ttyd-us.app) ---
# Info.plist from port/macos/Info.plist.in: name, bundle id, Retina, minimum
# macOS, and the disc image types Finder may open with it (disc_aurora.c).
# User data (cards, settings, disc.txt, pipeline cache) is Aurora's
# SDL_GetPrefPath: ~/Library/Application Support/ttyd/.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND TTYD_BACKEND STREQUAL "aurora")
    if(TTYD_VERSION STREQUAL "G8ME01")
        set(TTYD_MACOS_REGION US)
    elseif(TTYD_VERSION STREQUAL "G8MP01")
        set(TTYD_MACOS_REGION EU)
    else()
        set(TTYD_MACOS_REGION JP)
    endif()
    set(TTYD_MACOS_BUNDLE_ID_PREFIX org.ttyd-port CACHE STRING "macOS bundle identifier prefix")
    set(TTYD_MACOS_APP_NAME "Paper Cut") # one app for every version (tools/port/release.py)
    set(TTYD_MACOS_BUNDLE_ID ${TTYD_MACOS_BUNDLE_ID_PREFIX}.${TTYD_EXE_NAME})
    set(TTYD_MACOS_MIN_VERSION ${CMAKE_OSX_DEPLOYMENT_TARGET})
    if(NOT TTYD_MACOS_MIN_VERSION)
        set(TTYD_MACOS_MIN_VERSION 12.0)
    endif()
    # CFBundleShortVersionString: the release's version (TTYD_PORT_VERSION, e.g.
    # 0.2.0) when it is one, else 0.2 (as port/ios/ios.cmake)
    if(TTYD_PORT_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
        set(TTYD_MACOS_VERSION "${TTYD_PORT_VERSION}")
    else()
        set(TTYD_MACOS_VERSION "0.2")
    endif()
    configure_file(${CMAKE_SOURCE_DIR}/port/macos/Info.plist.in ${CMAKE_BINARY_DIR}/Info.plist @ONLY)
    # the app's icon (CFBundleIconFile), in Contents/Resources
    set(_icns ${CMAKE_SOURCE_DIR}/port/assets/icon/papercut.icns)
    target_sources(ttyd PRIVATE ${_icns})
    set_source_files_properties(${_icns} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(ttyd PROPERTIES MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/Info.plist)
    if(TTYD_RELS_STATIC)
        # Seal the bundle (ad hoc) now that Info.plist is in it; the linker
        # only signs the executable.
        add_custom_command(TARGET ttyd POST_BUILD
            COMMAND codesign --force --sign - $<TARGET_BUNDLE_DIR:ttyd>
            COMMENT "Signing ${TTYD_EXE_NAME}.app (ad hoc)")
    else()
        # REL modules go in the bundle, next to the executable (rel_pc.c).
        foreach(rel ${TTYD_RELS})
            set_target_properties(rel_${rel} PROPERTIES
                LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${TTYD_EXE_NAME}.app/Contents/MacOS/rel)
        endforeach()
    endif()
    # cmake --install: copy any non-system dylib the build linked (e.g. a
    # Homebrew SDL3) into the bundle, then sign it again.
    install(TARGETS ttyd BUNDLE DESTINATION .)
    install(CODE "
        include(BundleUtilities)
        set(_app \"\${CMAKE_INSTALL_PREFIX}/${TTYD_EXE_NAME}.app\")
        fixup_bundle(\"\${_app}\" \"\" \"/opt/homebrew/lib;/usr/local/lib\")
        execute_process(COMMAND codesign --force --deep --sign - \"\${_app}\" COMMAND_ERROR_IS_FATAL ANY)
    ")
endif()

# --- Host unit tests (no disc, no window): ctest or run the binaries ---
# bedecode_test: the big-endian decoder and the TPL path on a synthetic TPL.
enable_testing()
add_executable(bedecode_test EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/port/bedecode_test.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode_gen_tpl.c
    ${CMAKE_SOURCE_DIR}/port/src/common/tpl_pc.c)
target_compile_definitions(bedecode_test PRIVATE ${TTYD_DEFINES} PORT_BEDECODE_STANDALONE=1)
target_include_directories(bedecode_test PRIVATE ${TTYD_INCLUDES})
target_compile_options(bedecode_test PRIVATE ${TTYD_CFLAGS})
add_test(NAME bedecode_test COMMAND bedecode_test)
# anim_test: anim model decoding (filemgr type 5) on a synthetic model.
add_executable(anim_test EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/port/anim_test.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode_gen_anim.c
    ${CMAKE_SOURCE_DIR}/port/src/common/anim_pc.c)
target_compile_definitions(anim_test PRIVATE ${TTYD_DEFINES} PORT_BEDECODE_STANDALONE=1)
target_include_directories(anim_test PRIVATE ${TTYD_INCLUDES})
target_compile_options(anim_test PRIVATE ${TTYD_CFLAGS})
add_test(NAME anim_test COMMAND anim_test)
# loaders_test: bespoke loaders (font, camera road, card saves, U8 archive).
add_executable(loaders_test EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/port/loaders_test.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode_gen_font.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode_gen_camroad.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode_gen_cardsave.c
    ${CMAKE_SOURCE_DIR}/port/src/common/font_pc.c
    ${CMAKE_SOURCE_DIR}/port/src/common/camroad_pc.c
    ${CMAKE_SOURCE_DIR}/port/src/common/cardsave_pc.c
    ${CMAKE_SOURCE_DIR}/src/sdk/arc.c)
target_compile_definitions(loaders_test PRIVATE ${TTYD_DEFINES} PORT_BEDECODE_STANDALONE=1)
target_include_directories(loaders_test PRIVATE ${TTYD_INCLUDES})
target_compile_options(loaders_test PRIVATE ${TTYD_CFLAGS})
add_test(NAME loaders_test COMMAND loaders_test)
# art_recipe_test: the art recipe runner on an extracted disc (tests/port/art_recipe_check.py
# compares it with the authoring tool, port/assets/luigi/art/tools/recipes.py).
add_executable(art_recipe_test EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/port/art_recipe_test.c
    ${CMAKE_SOURCE_DIR}/port/src/common/art_recipe.c)
target_compile_definitions(art_recipe_test PRIVATE ${TTYD_DEFINES})
target_include_directories(art_recipe_test PRIVATE ${TTYD_INCLUDES})
target_compile_options(art_recipe_test PRIVATE ${TTYD_CFLAGS})
# Generated descriptor tables must match their specs and the headers.
add_test(NAME bedecode_generated_fresh
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/port/gen_bedecode.py --check)

# map_test: map 'd' files (relocation, chunk decoding, meshes, rebase) on a
# synthetic big-endian map, hitdrv's geometry accessors, TPL detach/attach.
add_executable(map_test EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/port/map_test.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode_gen_map.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode_gen_tpl.c
    ${CMAKE_SOURCE_DIR}/port/src/common/map_pc.c
    ${CMAKE_SOURCE_DIR}/port/src/common/tpl_pc.c
    ${CMAKE_SOURCE_DIR}/port/src/common/gx_arrays_pc.c)
target_compile_definitions(map_test PRIVATE ${TTYD_DEFINES} PORT_BEDECODE_STANDALONE=1)
target_include_directories(map_test PRIVATE ${TTYD_INCLUDES})
target_compile_options(map_test PRIVATE ${TTYD_CFLAGS})
target_link_libraries(map_test PRIVATE ${TTYD_LIBM})
add_test(NAME map_test COMMAND map_test)

# The extracted disc the tests below read: data/<version>, or orig/<version>
# when the disc was extracted beside its image (files/ and sys/ there)
set(TTYD_TEST_DISC ${CMAKE_SOURCE_DIR}/data/${TTYD_VERSION})
if(NOT EXISTS ${TTYD_TEST_DISC}/files AND EXISTS ${CMAKE_SOURCE_DIR}/orig/${TTYD_VERSION}/files)
    set(TTYD_TEST_DISC ${CMAKE_SOURCE_DIR}/orig/${TTYD_VERSION})
endif()

# disc_sweep: every anim/map/camroad/tpl/font file of the extracted disc
# (TTYD_TEST_DISC) through the real loaders, all failures in one report.
# Skipped when the disc is not there.
add_executable(disc_sweep EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/port/disc_sweep.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode_gen_anim.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode_gen_map.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode_gen_tpl.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode_gen_camroad.c
    ${CMAKE_SOURCE_DIR}/port/src/common/bedecode_gen_font.c
    ${CMAKE_SOURCE_DIR}/port/src/common/anim_pc.c
    ${CMAKE_SOURCE_DIR}/port/src/common/map_pc.c
    ${CMAKE_SOURCE_DIR}/port/src/common/tpl_pc.c
    ${CMAKE_SOURCE_DIR}/port/src/common/camroad_pc.c
    ${CMAKE_SOURCE_DIR}/port/src/common/font_pc.c
    ${CMAKE_SOURCE_DIR}/port/src/common/gx_arrays_pc.c)
target_compile_definitions(disc_sweep PRIVATE ${TTYD_DEFINES} PORT_BEDECODE_STANDALONE=1)
target_include_directories(disc_sweep PRIVATE ${TTYD_INCLUDES})
target_compile_options(disc_sweep PRIVATE ${TTYD_CFLAGS})
target_link_libraries(disc_sweep PRIVATE ${TTYD_LIBM})
add_test(NAME disc_sweep COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/port/disc_sweep.py
    $<TARGET_FILE:disc_sweep> ${TTYD_TEST_DISC})
set_tests_properties(disc_sweep PROPERTIES SKIP_RETURN_CODE 77)
# map_sweep: boot every map of the game's map table headless ($TTYD_MAP warp),
# all failures grouped by cause. Minutes, not seconds: `ctest -L slow`.
# Headless, each map's audio is also compared with its recorded hash.
if(TTYD_BACKEND STREQUAL "headless")
    set(_map_sweep_golden --golden ${CMAKE_SOURCE_DIR}/tests/port/golden/audio_${TTYD_VERSION}.json)
endif()
add_test(NAME map_sweep COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/port/map_sweep.py
    $<TARGET_FILE:ttyd> ${TTYD_TEST_DISC} ${_map_sweep_golden})
set_tests_properties(map_sweep PROPERTIES SKIP_RETURN_CODE 77 LABELS slow TIMEOUT 3600)

# dtk_test: the ADP decoder, ring buffer and mixer of DVD audio streaming.
add_executable(dtk_test EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/port/dtk_test.c
    ${CMAKE_SOURCE_DIR}/port/src/common/dtk_core.c)
target_compile_definitions(dtk_test PRIVATE ${TTYD_DEFINES})
target_include_directories(dtk_test PRIVATE ${TTYD_INCLUDES})
target_compile_options(dtk_test PRIVATE ${TTYD_CFLAGS})
target_link_libraries(dtk_test PRIVATE ${TTYD_LIBM})
add_test(NAME dtk_test COMMAND dtk_test)
# musyx_swap_test: big-endian MusyX proj/pool/sdir/song/stream headers.
add_executable(musyx_swap_test EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/port/musyx_swap_test.c
    ${CMAKE_SOURCE_DIR}/port/src/common/musyx_swap.c)
target_compile_definitions(musyx_swap_test PRIVATE ${TTYD_DEFINES})
target_include_directories(musyx_swap_test PRIVATE ${TTYD_INCLUDES})
target_compile_options(musyx_swap_test PRIVATE ${TTYD_CFLAGS})
add_test(NAME musyx_swap_test COMMAND musyx_swap_test)
# rando_test: the built-in randomizer's seed generator (port/archipelago/rando_gen.cpp):
# deterministic, every item kept, the vanilla story's moves safe, the open world beatable.
add_executable(rando_test EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tests/port/rando_test.cpp
    ${CMAKE_SOURCE_DIR}/port/archipelago/rando_gen.cpp)
target_compile_features(rando_test PRIVATE cxx_std_20)
add_test(NAME rando_test COMMAND rando_test)
# musyx_mixer_test: MusyX PC layer (ADPCM decode, software DSP mixer, end to end
# through sndInit/sndFXStart). Headless only: on Aurora, hw_pc.c drives an SDL device.
if(TTYD_BACKEND STREQUAL "headless")
    add_executable(musyx_mixer_test EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tests/port/musyx_mixer_test.c $<TARGET_OBJECTS:musyx_pc>)
    target_compile_definitions(musyx_mixer_test PRIVATE ${TTYD_DEFINES})
    target_include_directories(musyx_mixer_test PRIVATE ${TTYD_INCLUDES})
    target_compile_options(musyx_mixer_test PRIVATE ${TTYD_CFLAGS})
    target_link_libraries(musyx_mixer_test PRIVATE ${TTYD_LIBM})
    add_test(NAME musyx_mixer_test COMMAND musyx_mixer_test)
    # musyx_seq_sweep: every song of the disc's pmario.slib, swapped like the
    # game does, walked statically and played through the sequencer until it
    # ends or loops twice, with the sequencer's cursors checked as it runs.
    # Skipped when the disc is not there.
    add_executable(musyx_seq_sweep EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tests/port/musyx_seq_sweep.c
        ${CMAKE_SOURCE_DIR}/port/src/common/musyx_swap.c $<TARGET_OBJECTS:musyx_pc>)
    target_compile_definitions(musyx_seq_sweep PRIVATE ${TTYD_DEFINES})
    target_include_directories(musyx_seq_sweep PRIVATE ${TTYD_INCLUDES})
    target_compile_options(musyx_seq_sweep PRIVATE ${TTYD_CFLAGS})
    target_link_libraries(musyx_seq_sweep PRIVATE ${TTYD_LIBM})
    add_test(NAME musyx_seq_sweep COMMAND musyx_seq_sweep
        ${TTYD_TEST_DISC}/files)
    set_tests_properties(musyx_seq_sweep PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)
endif()
# dtk_core.c locks with pthreads on POSIX hosts.
find_package(Threads)
if(Threads_FOUND)
    target_link_libraries(ttyd PRIVATE Threads::Threads)
    target_link_libraries(dtk_test PRIVATE Threads::Threads)
    if(TARGET musyx_mixer_test)
        target_link_libraries(musyx_mixer_test PRIVATE Threads::Threads)
        target_link_libraries(musyx_seq_sweep PRIVATE Threads::Threads)
    endif()
endif()
