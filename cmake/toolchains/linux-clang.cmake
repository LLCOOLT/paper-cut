# Linux (x86_64 or arm64) with clang, and lld when it is installed.
#   cmake -S . -B build-pc-jp -G Ninja --toolchain cmake/toolchains/linux-clang.cmake
# clang is the tested compiler: the RELs are linked into the executable with
# '#pragma clang section' (TTYD_RELS_STATIC, CMakeLists.txt). A versioned
# clang works too: -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18.
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER clang)
endif()
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER clang++)
endif()
find_program(_ttyd_ld_lld NAMES ld.lld)
if(_ttyd_ld_lld)
    set(CMAKE_LINKER_TYPE LLD)
endif()
