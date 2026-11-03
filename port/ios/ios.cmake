# The iOS app around the ttyd target. Included from CMakeLists.txt when
# CMAKE_SYSTEM_NAME is iOS (toolchain: cmake/toolchains/ios.cmake).
#
# Produces <build>/ttyd-jp.app (or ttyd-us.app, ttyd-eu.app), and with the ttyd_ipa target
# an unsigned <build>/ttyd-jp.ipa for sideloading tools, which sign it with
# the user's Apple ID. No game data and no game artwork go into the bundle.

if(TTYD_VERSION STREQUAL "G8ME01")
    set(_ios_suffix us)
elseif(TTYD_VERSION STREQUAL "G8MP01")
    set(_ios_suffix eu)
else()
    set(_ios_suffix jp)
endif()
set(TTYD_IOS_BUNDLE_ID_PREFIX "io.github.ttyd-port" CACHE STRING
    "iOS bundle identifier prefix; .jp, .us or .eu is appended (one app per version)")
set(TTYD_IOS_BUNDLE_ID "${TTYD_IOS_BUNDLE_ID_PREFIX}.${_ios_suffix}")
# the name under the icon: Paper Cut, whichever version (the launcher names it)
set(TTYD_IOS_DISPLAY_NAME "Paper Cut" CACHE STRING "The app's name on the Home Screen")
# the release's version when it is one (TTYD_PORT_VERSION, e.g. 0.2.0), else 0.2
if(TTYD_PORT_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
    set(TTYD_IOS_VERSION "${TTYD_PORT_VERSION}")
else()
    set(TTYD_IOS_VERSION "0.2")
endif()
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

# Icons: Paper Cut's logo (port/assets/icon/ios, made by tools/port/make_icons.py
# from shapes, no game art).
set(_ios_icon_dir ${CMAKE_SOURCE_DIR}/port/assets/icon/ios)
set(_ios_icons
    ${_ios_icon_dir}/AppIcon60x60@2x.png
    ${_ios_icon_dir}/AppIcon60x60@3x.png
    ${_ios_icon_dir}/AppIcon76x76@2x~ipad.png
    ${_ios_icon_dir}/AppIcon83.5x83.5@2x~ipad.png)
target_sources(ttyd PRIVATE ${_ios_icons})
# The bundle is signed after the link (below): a changed icon must relink, or
# the copied resource breaks the existing signature.
set_property(TARGET ttyd APPEND PROPERTY LINK_DEPENDS ${_ios_icons})

# The app's page in the Settings app (the widescreen switch).
set(_ios_settings ${CMAKE_CURRENT_LIST_DIR}/Settings.bundle/Root.plist)
target_sources(ttyd PRIVATE ${_ios_settings})
set_source_files_properties(${_ios_settings} PROPERTIES MACOSX_PACKAGE_LOCATION Settings.bundle)
set_property(TARGET ttyd APPEND PROPERTY LINK_DEPENDS ${_ios_settings})

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
