# The browser build (TTYD_BACKEND=aurora under Emscripten):
# ttyd-<jp|us|eu>.js/.wasm and the page around them (port/web), in the build
# directory's web/ folder, which is the whole site.
#
# - One thread. Game threads are fibers (port/src/common/os_thread_pc.c),
#   Aurora's workers run inline, and the game yields to the browser at the end
#   of each frame (port/src/web/web_frame.c). With Asyncify the C stack can be
#   put aside for that, for a disc read (a Blob slice) or a WebGPU wait; no
#   SharedArrayBuffer, so no special headers: any static host serves it.
# - Memory as the Node build's: everything at 2 GiB and up (sound names are told
#   from ids by address, include/sfx_name_word.h).
# - Saves and settings: Aurora's user folder (/papercut) is IndexedDB (IDBFS),
#   written back after each save (port/web/papercut.js).
set(TTYD_WEB_DIR ${CMAKE_BINARY_DIR}/web)
set_target_properties(ttyd PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TTYD_WEB_DIR})
target_sources(ttyd PRIVATE ${CMAKE_SOURCE_DIR}/port/src/web/web_pc.c)
target_link_options(ttyd PRIVATE
    -sGLOBAL_BASE=2147483648 -sSTACK_SIZE=8MB
    -sINITIAL_MEMORY=2432MB -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4GB
    -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=1048576
    -sENVIRONMENT=web -sEXIT_RUNTIME=0 -sINVOKE_RUN=0
    -sMODULARIZE=1 -sEXPORT_NAME=createPaperCut
    -sFORCE_FILESYSTEM=1 -lidbfs.js
    "-sEXPORTED_RUNTIME_METHODS=['FS','IDBFS','ENV','callMain','ccall','HEAPU8','addRunDependency','removeRunDependency']"
    -sSTACK_OVERFLOW_CHECK=0
    # function names in traps and crash reports
    --profiling-funcs)
# the page (port/web), its typeface (Fredoka, docs/THIRD_PARTY.md) and Paper Cut's logo, beside the program
file(GLOB _web_page CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/port/web/*)
add_custom_command(TARGET ttyd POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_web_page}
        ${CMAKE_SOURCE_DIR}/port/assets/fonts/Fredoka-Bold.ttf ${CMAKE_SOURCE_DIR}/port/assets/fonts/Fredoka-Medium.ttf
        ${CMAKE_SOURCE_DIR}/port/assets/fonts/OFL.txt ${CMAKE_SOURCE_DIR}/port/assets/icon/papercut-256.png
        ${TTYD_WEB_DIR}
    VERBATIM)
# Save states: the port's files kept with the game's data (TTYD_SNAPSHOT_SECTION,
# CMakeLists.txt) get their segments moved as the game's do (tools/web/snap_cc.py).
if(TTYD_SAVE_STATES)
    set_target_properties(ttyd PROPERTIES C_COMPILER_LAUNCHER
        "${Python3_EXECUTABLE};${CMAKE_SOURCE_DIR}/tools/web/snap_cc.py")
endif()
# Rewind compares every page of the game's memory each capture (no write watch here): 16 bytes a
# load with wasm SIMD, in that file only (every browser with WebGPU has it).
set_property(SOURCE ${CMAKE_SOURCE_DIR}/port/src/aurora/rewind_aurora.c APPEND PROPERTY COMPILE_OPTIONS -msimd128)
# Asyncify: Aurora's GX (command processing, draws, state) never waits for the browser (that is
# only the frame's end, disc reads, fiber switches and fetches), so it is left uninstrumented:
# ~10% faster in Rogueport uncapped (74 -> 82 fps in Edge, 3 runs each). A build with
# -sASSERTIONS=1 stops at once if anything listed there is on the stack when the program waits.
target_link_options(ttyd PRIVATE "-sASYNCIFY_REMOVE=@${CMAKE_SOURCE_DIR}/tools/web/asyncify_remove.json")
set_property(TARGET ttyd APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/tools/web/asyncify_remove.json)
