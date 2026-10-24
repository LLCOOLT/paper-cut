# Native PC build of the TTYD decomp. The matching GameCube build is
# configure.py + ninja and is unaffected by this file.
cmake_minimum_required(VERSION 3.25)
project(ttyd_pc C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

# One executable per game version: struct layouts differ between JP and US,
# so the version is fixed at compile time like the matching build's -DVERSION_*.
set(TTYD_VERSIONS G8MJ01 G8ME01)
set(TTYD_VERSION G8MJ01 CACHE STRING "Game version (disc ID): G8MJ01 (JP) or G8ME01 (US)")
set_property(CACHE TTYD_VERSION PROPERTY STRINGS ${TTYD_VERSIONS})
list(FIND TTYD_VERSIONS ${TTYD_VERSION} TTYD_BUILD_VERSION)
if(TTYD_BUILD_VERSION LESS 0)
    message(FATAL_ERROR "TTYD_VERSION must be one of: ${TTYD_VERSIONS}")
endif()
if(TTYD_VERSION STREQUAL "G8ME01")
    set(TTYD_EXE_NAME ttyd-us)
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

set(TTYD_ARCH_FLAG -m64)
if(TTYD_PC_32BIT AND NOT MSVC)
    set(TTYD_ARCH_FLAG -m32)
    add_compile_options(-m32)
    add_link_options(-m32)
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# The C math library is separate (libm) except on Windows.
if(WIN32)
    set(TTYD_LIBM)
else()
    set(TTYD_LIBM m)
endif()

set(TTYD_DEFINES
    TARGET_PC=1 NONMATCHING=1 NDEBUG=1 _USE_MATH_DEFINES=1 _CRT_SECURE_NO_WARNINGS=1
    BUILD_VERSION=${TTYD_BUILD_VERSION} VERSION_${TTYD_VERSION} SDK_REVISION=1
    PORT_GAME_ID="${TTYD_VERSION}")

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
    add_subdirectory(extern/aurora EXCLUDE_FROM_ALL)
    # aurora_overlay: wrappers (#include_next) adding the 2004-SDK declarations,
    # macros and transitive includes the game relies on that Aurora's headers lack.
    set(TTYD_SDK_INCLUDE ${CMAKE_SOURCE_DIR}/port/include/aurora_overlay
        ${CMAKE_SOURCE_DIR}/extern/aurora/include)
    list(APPEND TTYD_DEFINES TTYD_BACKEND_AURORA=1)
else()
    set(TTYD_SDK_INCLUDE ${CMAKE_SOURCE_DIR}/libs/dolsdk2004/include)
endif()

# The card banner/icon and DSP microcode the decomp extracts from the game
# (python configure.py && ninja, with the disc in orig/). Without them (CI,
# or a build before the disc is set up) stand-ins are used: the port only
# needs the disc to run, not to build.
if(EXISTS ${CMAKE_SOURCE_DIR}/build/${TTYD_VERSION}/include/assets/_mariost_banner_tex.inc)
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
        # A call to an undeclared function returns C's implicit int: a pointer
        # or 64-bit result is cut to 32 bits, a narrow one is not extended and
        # f32 arguments go as doubles (e.g. seq_load.c's winZenkakuStr, a US
        # crash). Every call has a prototype; keep it that way.
        -Werror=implicit-function-declaration
        # MWCC accepts these; they are fixed incrementally.
        -Wno-error=int-conversion
        -Wno-error=incompatible-pointer-types)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        list(APPEND TTYD_CFLAGS -Wno-error=incompatible-function-pointer-types)
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
# - TTYD_RELS_STATIC (default on Windows, where a DLL cannot read the
#   executable's data without dllimport): every REL is linked into the
#   executable. tools/port/rel_rename.py renames each REL's global symbols to
#   rel_<area>__<name> so RELs (and the DOL) cannot clash, and each REL's
#   data/bss go to their own grouped sections (.data$rel_<area>_m) between
#   generated start/end markers; linking a REL restores its initial data,
#   clears its bss and runs its _prolog.
# - otherwise each REL is a loadable module (.so/.dylib) loaded on area entry
#   and unloaded on exit; modules resolve game symbols against the executable.
if(WIN32)
    set(_rels_static_default ON)
else()
    set(_rels_static_default OFF)
endif()
option(TTYD_RELS_STATIC "Link the RELs into the executable" ${_rels_static_default})
set(TTYD_REL_TARGETS)
set(TTYD_REL_ARCHIVES)
set(TTYD_REL_STATIC_SOURCES)
if(TTYD_RELS_STATIC)
    get_filename_component(_llvm_bin ${CMAKE_C_COMPILER} DIRECTORY)
    find_program(TTYD_LLVM_NM llvm-nm HINTS ${_llvm_bin} REQUIRED)
    find_program(TTYD_LLVM_OBJCOPY llvm-objcopy HINTS ${_llvm_bin} REQUIRED)
    find_program(TTYD_LLVM_AR llvm-ar HINTS ${_llvm_bin} REQUIRED)
    set(_relgen ${CMAKE_BINARY_DIR}/generated/rels)
    set(_table_decls "")
    set(_table_rows "")
    foreach(rel ${TTYD_RELS})
        file(WRITE ${_relgen}/rel_${rel}_sections.h.tmp
            "/* Generated by CMakeLists.txt: REL ${rel}'s data in its own grouped sections. */\n"
            "#pragma clang section data=\".data$rel_${rel}_m\" bss=\".bss$rel_${rel}_m\"\n")
        configure_file(${_relgen}/rel_${rel}_sections.h.tmp ${_relgen}/rel_${rel}_sections.h COPYONLY)
        file(WRITE ${_relgen}/rel_${rel}_bounds.c.tmp
            "/* Generated by CMakeLists.txt: markers around REL ${rel}'s data and bss. */\n"
            "#pragma clang section data=\".data$rel_${rel}_a\" bss=\".bss$rel_${rel}_a\"\n"
            "char rel_${rel}_data_start = 1;\nchar rel_${rel}_bss_start;\n"
            "#pragma clang section data=\".data$rel_${rel}_z\" bss=\".bss$rel_${rel}_z\"\n"
            "char rel_${rel}_data_end = 1;\nchar rel_${rel}_bss_end;\n")
        configure_file(${_relgen}/rel_${rel}_bounds.c.tmp ${_relgen}/rel_${rel}_bounds.c COPYONLY)
        list(APPEND TTYD_REL_STATIC_SOURCES ${_relgen}/rel_${rel}_bounds.c)
        string(APPEND _table_decls
            "extern void rel_${rel}___prolog(void);\nextern void rel_${rel}___epilog(void);\n"
            "extern char rel_${rel}_data_start, rel_${rel}_data_end, rel_${rel}_bss_start, rel_${rel}_bss_end;\n")
        string(APPEND _table_rows
            "    {\"${rel}\", rel_${rel}___prolog, rel_${rel}___epilog, &rel_${rel}_data_start, &rel_${rel}_data_end,"
            " &rel_${rel}_bss_start, &rel_${rel}_bss_end},\n")

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
        "#include \"port/rel_pc.h\"\n\n${_table_decls}\n"
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

add_executable(ttyd $<TARGET_OBJECTS:ttyd_dol> $<TARGET_OBJECTS:musyx_pc> ${TTYD_PORT_SOURCES}
    ${TTYD_REL_STATIC_SOURCES})
if(TTYD_RELS_STATIC)
    add_dependencies(ttyd ttyd_rels)
    target_compile_definitions(ttyd PRIVATE PORT_RELS_STATIC=1)
    foreach(_archive ${TTYD_REL_ARCHIVES})
        target_link_options(ttyd PRIVATE "LINKER:/WHOLEARCHIVE:${_archive}")
    endforeach()
    set_property(TARGET ttyd APPEND PROPERTY LINK_DEPENDS ${TTYD_REL_ARCHIVES})
endif()
set_target_properties(ttyd PROPERTIES OUTPUT_NAME ${TTYD_EXE_NAME})
if(NOT TTYD_RELS_STATIC)
    # REL modules link against the executable's symbols.
    set_target_properties(ttyd PROPERTIES ENABLE_EXPORTS ON WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
target_compile_definitions(ttyd PRIVATE ${TTYD_DEFINES})
target_include_directories(ttyd PRIVATE ${TTYD_INCLUDES} ${CMAKE_BINARY_DIR}/generated)
target_compile_options(ttyd PRIVATE ${TTYD_CFLAGS})
target_link_libraries(ttyd PRIVATE ${TTYD_LIBM} ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(ttyd PRIVATE dbghelp)
endif()
if(TTYD_BACKEND STREQUAL "aurora")
    target_link_libraries(ttyd PRIVATE aurora::core aurora::gx aurora::gd aurora::vi
        aurora::pad aurora::si aurora::os aurora::mtx aurora::card aurora::dvd
        aurora::thp aurora::main)
    # SDL3 directly: the disc picker's file dialog and message box (disc_aurora.c).
    target_link_libraries(ttyd PRIVATE ${AURORA_SDL3_TARGET})
    # Windows: SDL3, Dawn (+ the DXC DLLs it loads), nod, libpng, zlib next to the exe.
    include(${CMAKE_SOURCE_DIR}/extern/aurora/cmake/AuroraCopyRuntimeDLLs.cmake)
    aurora_copy_runtime_dlls(ttyd)
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

# disc_sweep: every anim/map/camroad/tpl/font file of the extracted disc
# (data/<version>) through the real loaders, all failures in one report.
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
    $<TARGET_FILE:disc_sweep> ${CMAKE_SOURCE_DIR}/data/${TTYD_VERSION})
set_tests_properties(disc_sweep PROPERTIES SKIP_RETURN_CODE 77)
# map_sweep: boot every map of the game's map table headless ($TTYD_MAP warp),
# all failures grouped by cause. Minutes, not seconds: `ctest -L slow`.
# Headless, each map's audio is also compared with its recorded hash.
if(TTYD_BACKEND STREQUAL "headless")
    set(_map_sweep_golden --golden ${CMAKE_SOURCE_DIR}/tests/port/golden/audio_${TTYD_VERSION}.json)
endif()
add_test(NAME map_sweep COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/port/map_sweep.py
    $<TARGET_FILE:ttyd> ${CMAKE_SOURCE_DIR}/data/${TTYD_VERSION} ${_map_sweep_golden})
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
        ${CMAKE_SOURCE_DIR}/data/${TTYD_VERSION}/files)
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
