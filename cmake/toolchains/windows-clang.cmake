# Windows + LLVM clang (GNU driver) without a Visual Studio developer prompt.
# clang finds MSVC and the Windows SDK through the Visual Studio installer;
# when that registration is missing (e.g. Build Tools installed without it)
# this toolchain points it at the newest MSVC and Windows SDK on disk.
#   cmake -S . -B build-pc-jp -G Ninja --toolchain cmake/toolchains/windows-clang.cmake
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_LINKER_TYPE LLD)

function(_ttyd_newest out pattern)
    file(GLOB _c LIST_DIRECTORIES true ${pattern})
    list(SORT _c COMPARE NATURAL ORDER DESCENDING)
    list(GET _c 0 _v)
    set(${out} ${_v} PARENT_SCOPE)
endfunction()

if(NOT DEFINED ENV{INCLUDE} OR "$ENV{INCLUDE}" STREQUAL "")
    set(_pf86 "$ENV{ProgramFiles\(x86\)}")
    set(_pf "$ENV{ProgramFiles}")
    file(TO_CMAKE_PATH "${_pf86}" _pf86)
    file(TO_CMAKE_PATH "${_pf}" _pf)
    # Build Tools install under Program Files (x86); Visual Studio 2022 itself
    # (e.g. GitHub's windows runners) under Program Files.
    _ttyd_newest(_msvc "${_pf86}/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*;${_pf}/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*")
    _ttyd_newest(_sdkinc "${_pf86}/Windows Kits/10/Include/10.*")
    get_filename_component(_sdkver ${_sdkinc} NAME)
    set(_sdklib "${_pf86}/Windows Kits/10/Lib/${_sdkver}")
    set(_inc "-idirafter \"${_msvc}/include\" -idirafter \"${_sdkinc}/ucrt\" -idirafter \"${_sdkinc}/um\" -idirafter \"${_sdkinc}/shared\" -idirafter \"${_sdkinc}/winrt\" -idirafter \"${_sdkinc}/cppwinrt\"")
    # Resource scripts (.rc) include <windows.h> & co.
    set(CMAKE_RC_FLAGS_INIT "-I \"${_msvc}/include\" -I \"${_sdkinc}/ucrt\" -I \"${_sdkinc}/um\" -I \"${_sdkinc}/shared\"")
    set(_lib "-L\"${_msvc}/lib/x64\" -L\"${_sdklib}/ucrt/x64\" -L\"${_sdklib}/um/x64\"")
    set(CMAKE_C_FLAGS_INIT "${_inc}")
    set(CMAKE_CXX_FLAGS_INIT "${_inc}")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "${_lib}")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_lib}")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_lib}")
    find_program(_rc llvm-rc)
    if(_rc)
        set(CMAKE_RC_COMPILER ${_rc})
    endif()
endif()
