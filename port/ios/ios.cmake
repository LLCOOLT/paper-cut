# The iOS app around the ttyd target. Included from CMakeLists.txt when
# CMAKE_SYSTEM_NAME is iOS (toolchain: cmake/toolchains/ios.cmake).
#
# Produces <build>/ttyd-jp.app (or ttyd-us.app), and with the ttyd_ipa target
# an unsigned <build>/ttyd-jp.ipa for sideloading tools, which sign it with
# the user's Apple ID. No game data and no game artwork go into the bundle.

if(TTYD_VERSION STREQUAL "G8ME01")
    set(_ios_region US)
    set(_ios_suffix us)
else()
    set(_ios_region JP)
    set(_ios_suffix jp)
endif()
set(TTYD_IOS_BUNDLE_ID_PREFIX "io.github.ttyd-port" CACHE STRING
    "iOS bundle identifier prefix; .jp or .us is appended (one app per version)")
set(TTYD_IOS_BUNDLE_ID "${TTYD_IOS_BUNDLE_ID_PREFIX}.${_ios_suffix}")
set(TTYD_IOS_DISPLAY_NAME "TTYD ${_ios_region}")
set(TTYD_IOS_VERSION "0.1")
set(TTYD_IOS_BUILD "1")
if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(TTYD_IOS_DEPLOYMENT_TARGET ${CMAKE_OSX_DEPLOYMENT_TARGET})
else()
    set(TTYD_IOS_DEPLOYMENT_TARGET 14.0)
endif()
if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
    set(TTYD_IOS_PLATFORM_NAME iPhoneSimulator)
    set(_ios_default_identity "-")
else()
    set(TTYD_IOS_PLATFORM_NAME iPhoneOS)
    set(_ios_default_identity "")
endif()
# Code signing: "-" = ad-hoc (the simulator's default), empty = unsigned (the
# device default: sideloading tools sign the .ipa), or a signing identity
# ("Apple Development: ...") together with TTYD_IOS_PROVISIONING_PROFILE.
set(TTYD_IOS_CODESIGN_IDENTITY "${_ios_default_identity}" CACHE STRING
    "codesign identity for the app: '-' ad-hoc, empty unsigned, or an identity name")
set(TTYD_IOS_PROVISIONING_PROFILE "" CACHE FILEPATH
    "Provisioning profile embedded when signing with an identity (device builds)")

# Objective-C glue (folders, document picker). A library of its own: the
# game's prefix header must not reach Objective-C (its BOOL is not the SDK's).
add_library(ttyd_ios STATIC ${CMAKE_CURRENT_LIST_DIR}/ios_app.m)
target_include_directories(ttyd_ios PRIVATE ${CMAKE_SOURCE_DIR}/port/include ${CMAKE_SOURCE_DIR}/extern/aurora/include)
target_compile_options(ttyd_ios PRIVATE -fobjc-arc)
target_link_libraries(ttyd_ios PRIVATE ${AURORA_SDL3_TARGET})
target_link_libraries(ttyd PRIVATE ttyd_ios "-framework Foundation" "-framework UIKit"
    "-framework UniformTypeIdentifiers")

# Placeholder icons, generated (port/ios/make_icon.py).
set(_ios_icon_dir ${CMAKE_BINARY_DIR}/ios/icons)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/make_icon.py ${_ios_icon_dir}
    COMMAND_ERROR_IS_FATAL ANY)
set(_ios_icons
    ${_ios_icon_dir}/AppIcon60x60@2x.png
    ${_ios_icon_dir}/AppIcon60x60@3x.png
    ${_ios_icon_dir}/AppIcon76x76@2x~ipad.png
    ${_ios_icon_dir}/AppIcon83.5x83.5@2x~ipad.png)
target_sources(ttyd PRIVATE ${_ios_icons})

configure_file(${CMAKE_CURRENT_LIST_DIR}/Info.plist.in ${CMAKE_BINARY_DIR}/ios/Info.plist @ONLY)
# iOS bundles are flat: the executable, Info.plist and RESOURCE files sit at
# the bundle's top level.
set_target_properties(ttyd PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/ios/Info.plist
    RESOURCE "${_ios_icons}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${TTYD_IOS_BUNDLE_ID})

if(NOT TTYD_IOS_CODESIGN_IDENTITY STREQUAL "")
    add_custom_command(TARGET ttyd POST_BUILD
        COMMAND sh ${CMAKE_CURRENT_LIST_DIR}/codesign.sh "$<TARGET_BUNDLE_DIR:ttyd>"
            "${TTYD_IOS_CODESIGN_IDENTITY}" "${TTYD_IOS_PROVISIONING_PROFILE}"
        VERBATIM
        COMMENT "Signing $<TARGET_BUNDLE_DIR_NAME:ttyd>")
endif()

# ttyd_ipa: <build>/<exe>.ipa (Payload/<exe>.app, zipped) for AltStore,
# SideStore, Sideloadly and the like.
set(_ipa_dir ${CMAKE_BINARY_DIR}/ipa)
add_custom_target(ttyd_ipa
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_ipa_dir}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_ipa_dir}/Payload
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:ttyd>"
        "${_ipa_dir}/Payload/$<TARGET_BUNDLE_DIR_NAME:ttyd>"
    COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_BINARY_DIR}/${TTYD_EXE_NAME}.ipa
    COMMAND ${CMAKE_COMMAND} -E chdir ${_ipa_dir}
        ${CMAKE_COMMAND} -E tar cf ${CMAKE_BINARY_DIR}/${TTYD_EXE_NAME}.ipa --format=zip Payload
    DEPENDS ttyd
    VERBATIM
    COMMENT "Packaging ${TTYD_EXE_NAME}.ipa")
