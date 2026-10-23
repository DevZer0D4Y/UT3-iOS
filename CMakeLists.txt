# Unreal Tournament 3 - ReXGlue Recompiled Project, iOS build.
#
# The runtime is the iOS port of the ReXGlue SDK, found through rexglue_DIR
# (see CMakePresets.json). SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(ut3 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Keep build-machine paths (they contain the user name) out of the binary:
# source file names end up in log and assert messages.
add_compile_options("-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=ut3")

include(generated/rexglue.cmake)

add_executable(ut3 src/main.cpp src/game_check.cpp)

# The image digest of the default.xex the code was generated from
# (ut3_manifest.toml), for src/game_check.cpp: a disc with any other executable
# is refused instead of run against code that does not match it.
set(UT3_XEX "${CMAKE_CURRENT_SOURCE_DIR}/../game_xex/default.xex")
if(NOT EXISTS "${UT3_XEX}")
    message(FATAL_ERROR "${UT3_XEX} not found: put your copy of the game's default.xex there "
                        "(see README.md).")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${UT3_XEX}")
file(READ "${UT3_XEX}" UT3_XEX_SECURITY_OFFSET OFFSET 16 LIMIT 4 HEX)
math(EXPR UT3_XEX_DIGEST_OFFSET "0x${UT3_XEX_SECURITY_OFFSET} + 0x114")
file(READ "${UT3_XEX}" UT3_XEX_IMAGE_DIGEST OFFSET ${UT3_XEX_DIGEST_OFFSET} LIMIT 20 HEX)
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ut3_xex_id.h"
     CONTENT "#pragma once\n#define UT3_XEX_IMAGE_DIGEST \"${UT3_XEX_IMAGE_DIGEST}\"\n")
target_include_directories(ut3 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# ut3_register.cpp is one flat function of function-pointer registrations
# (~39,000 for this game), run once at startup, in a single basic block. LLVM
# is quadratic on it at -O1 and above, and so is -O0 on arm64, where
# GlobalISel's Localizer pass walks the whole block for every constant. -O0
# with the classic instruction selector compiles it quickly. The -O3
# precompiled header cannot be used at -O0, so it is skipped here.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/ut3_register.cpp
    PROPERTIES COMPILE_OPTIONS "-O0;-mllvm;-global-isel=0" SKIP_PRECOMPILE_HEADERS ON)

if(APPLE)
    # CFLocale, for the game language default (src/ut3_app.h).
    target_link_libraries(ut3 PRIVATE "-framework CoreFoundation")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJCXX)
    target_sources(ut3 PRIVATE src/iso_import.mm src/ios_screen.mm src/touch_layout_ut3.cpp)
    set_source_files_properties(src/iso_import.mm src/ios_screen.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(ut3 PRIVATE "-framework UIKit" "-framework UniformTypeIdentifiers")
    # The GPU plugin is embedded in Frameworks/ below and loaded from there; a
    # loose copy next to the executable would not be covered by the signature.
    rexglue_setup_target(ut3)
else()
    rexglue_setup_target(ut3 GPU_PLUGINS xenos)
endif()

# =============================================================================
#  iOS: a .app bundle that embeds the SDK's runtime dylibs. Game data is not
#  bundled; it goes in the app's Documents/game folder (Files app).
# =============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(UT3_IOS_BUNDLE_ID "com.devz.ut3" CACHE STRING "iOS bundle identifier")
    set_target_properties(ut3 PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${UT3_IOS_BUNDLE_ID}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${UT3_IOS_BUNDLE_ID}"
        # The SDK helper sets @executable_path, right for a flat macOS layout;
        # inside a bundle the dylibs live in Frameworks/.
        INSTALL_RPATH "@executable_path/Frameworks"
        BUILD_WITH_INSTALL_RPATH ON
    )
    # iOS only loads dylibs from inside the signed bundle.
    get_filename_component(UT3_IOS_SDK_LIB "${rexglue_DIR}/../.." ABSOLUTE)
    set(UT3_IOS_EMBED
        "${UT3_IOS_SDK_LIB}/librexruntime.dylib"
        "${UT3_IOS_SDK_LIB}/librexgpu-xenos.dylib"
        "${UT3_IOS_SDK_LIB}/libMoltenVK.dylib")
    add_custom_target(ut3_ios_bundle ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:ut3>/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${UT3_IOS_EMBED}
                "$<TARGET_BUNDLE_CONTENT_DIR:ut3>/Frameworks"
        DEPENDS ut3 ${UT3_IOS_EMBED}
        COMMENT "Refreshing runtime dylibs in ut3.app/Frameworks"
        VERBATIM)

    # App icons. Without an asset catalog iOS resolves icons by file name from
    # the bundle root (CFBundleIcons in Info.plist), so the PNGs go in flat.
    file(GLOB UT3_IOS_ICONS "${CMAKE_CURRENT_SOURCE_DIR}/ios/icons/*.png")
    if(UT3_IOS_ICONS)
        add_custom_command(TARGET ut3_ios_bundle POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${UT3_IOS_ICONS}
                    "$<TARGET_BUNDLE_CONTENT_DIR:ut3>"
            COMMENT "Copying app icons into ut3.app"
            VERBATIM)
    endif()

    # Touch control glyphs (src/touch_layout_ut3.cpp), loaded by name from the
    # bundle root; regenerate with tools/make_touch_icons.swift.
    file(GLOB UT3_TOUCH_ICONS "${CMAKE_CURRENT_SOURCE_DIR}/ios/touch_icons/*.png")
    add_custom_command(TARGET ut3_ios_bundle POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${UT3_TOUCH_ICONS}
                "$<TARGET_BUNDLE_CONTENT_DIR:ut3>"
        COMMENT "Copying touch control glyphs into ut3.app"
        VERBATIM)
endif()
