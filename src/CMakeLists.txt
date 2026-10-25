file(GLOB TBOX_SVG_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/svg/*.c)
file(GLOB TBOX_BASE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/base/*.c)
file(GLOB TBOX_HTML_PARSER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/html_parser/*.c)
file(GLOB TBOX_HTML_XPATH_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/html_xpath/*.c)
file(GLOB TBOX_CSS_PARSER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/css_parser/*.c)
file(GLOB TBOX_CSS_SELECTOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/css_selector/*.c)
file(GLOB TBOX_CSS_CASCADE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/css_cascade/*.c)
file(GLOB TBOX_STYLE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/style/*.c)
file(GLOB TBOX_FONT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/font/*.c)
file(GLOB TBOX_LAYOUT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/layout/*.c)
file(GLOB TBOX_RENDER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/render/*.c)
file(GLOB TBOX_OUTPUT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/output/*.c)
file(GLOB TBOX_CONTEXT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/context/*.c)
file(GLOB TBOX_APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/app/*.c)
file(GLOB TBOX_IMAGE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/image/*.c)
set(TBOX_SOURCES ${TBOX_SVG_SOURCES} ${TBOX_BASE_SOURCES} ${TBOX_HTML_PARSER_SOURCES} ${TBOX_HTML_XPATH_SOURCES} ${TBOX_CSS_PARSER_SOURCES} ${TBOX_CSS_SELECTOR_SOURCES} ${TBOX_CSS_CASCADE_SOURCES} ${TBOX_STYLE_SOURCES} ${TBOX_FONT_SOURCES} ${TBOX_LAYOUT_SOURCES} ${TBOX_RENDER_SOURCES} ${TBOX_OUTPUT_SOURCES} ${TBOX_CONTEXT_SOURCES} ${TBOX_APP_SOURCES} ${TBOX_IMAGE_SOURCES})

# The optional dependencies are detected in the root CMakeLists.txt (shared
# with example/ and tests/); here each one only decides which files are
# compiled. Exactly one of the two shaper files is built.
if(TBOX_HARFBUZZ_FOUND)
    list(REMOVE_ITEM TBOX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/font/tbox_font_shaper_simple.c)
else()
    list(REMOVE_ITEM TBOX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/font/tbox_font_shaper_harfbuzz.c)
endif()

if(NOT TBOX_FONTCONFIG_FOUND)
    list(REMOVE_ITEM TBOX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/font/tbox_font_source_fontconfig.c)
endif()

# tbox_backend_wayland.c (Output Display's real Wayland backend) needs
# wayland-client/xkbcommon plus the generated xdg-shell protocol glue. This
# can't fail configure when absent, since the test suite (tbox_raster_* only,
# no Wayland) must keep building without these system packages installed.
if(NOT TBOX_WAYLAND_FOUND)
    list(REMOVE_ITEM TBOX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/output/tbox_backend_wayland.c)
else()
    list(APPEND TBOX_SOURCES ${TBOX_XDG_SHELL_PROTOCOL_C})
endif()

# Screenshot rendering needs Fontconfig but no window backend. When font
# discovery is absent, linkable application stubs expose that limitation.
if(NOT TBOX_FONTCONFIG_FOUND)
    list(REMOVE_ITEM TBOX_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/app/tbox_app.c
        ${CMAKE_CURRENT_SOURCE_DIR}/app/tbox_app_events.c)
else()
    list(REMOVE_ITEM TBOX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/app/tbox_app_unavailable.c)
endif()

# utf8.h is visible to base/tbox_string.c only: every other file goes
# through src/base/tbox_string.h, and an `#include "utf8.h"` elsewhere fails
# to compile (docs/09-texto-e-fontes.md).
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/base/tbox_string.c
    PROPERTIES INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/external/utf8.h)

# The system libraries the build found. Applied to the object library (their
# include paths) and to each final library (linking). All PRIVATE: a shared
# libtbox records them itself, and a static one passes them on to whoever
# links it as link-only dependencies, so consumers never list them by hand.
function(tbox_use_dependencies target)
    target_link_libraries(${target} PRIVATE Freetype::Freetype Threads::Threads)
    if(TBOX_MATH_LIBRARY)
        target_link_libraries(${target} PRIVATE m)
    endif()
    if(TBOX_FONTCONFIG_FOUND)
        target_link_libraries(${target} PRIVATE PkgConfig::TBOX_FONTCONFIG)
    endif()
    if(TBOX_HARFBUZZ_FOUND)
        target_link_libraries(${target} PRIVATE PkgConfig::TBOX_HARFBUZZ)
    endif()
    if(TBOX_WAYLAND_FOUND)
        target_link_libraries(${target} PRIVATE PkgConfig::TBOX_WAYLAND_CLIENT PkgConfig::TBOX_XKBCOMMON)
        if(TBOX_WAYLAND_CURSOR_FOUND)
            target_link_libraries(${target} PRIVATE PkgConfig::TBOX_WAYLAND_CURSOR)
        endif()
    endif()
endfunction()

# Every source is compiled once, position-independent, and both libraries
# are archived/linked from the same objects.
add_library(tbox_objects OBJECT ${TBOX_SOURCES})
set_target_properties(tbox_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(tbox_objects
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/external/stb_image)
target_link_libraries(tbox_objects PRIVATE tbox_warnings tbox_sanitizers)
tbox_use_dependencies(tbox_objects)

if(TBOX_WAYLAND_FOUND)
    target_compile_definitions(tbox_objects PRIVATE TBOX_HAS_WAYLAND=1 TBOX_HAS_WINDOW_BACKEND=1)
    if(TBOX_WAYLAND_CURSOR_FOUND)
        target_compile_definitions(tbox_objects PRIVATE TBOX_HAS_WAYLAND_CURSOR=1)
    endif()
    add_dependencies(tbox_objects tbox_wayland_xdg_shell_protocol)
    target_include_directories(tbox_objects PRIVATE "${TBOX_XDG_SHELL_PROTOCOL_DIR}")
endif()

# tbox_static / tbox_shared, both named libtbox on disk, each with a
# tbox::-namespaced alias that matches the name of its installed, exported
# counterpart (see the root CMakeLists.txt). tbox_sanitizers stays a
# build-tree-only link dependency: it is never installed.
set(TBOX_LIBRARY_TARGETS "")

foreach(tbox_kind IN ITEMS STATIC SHARED)
    if(NOT TBOX_BUILD_${tbox_kind})
        continue()
    endif()
    string(TOLOWER "tbox_${tbox_kind}" tbox_target)

    add_library(${tbox_target} ${tbox_kind} $<TARGET_OBJECTS:tbox_objects>)
    add_library(tbox::${tbox_target} ALIAS ${tbox_target})
    target_include_directories(${tbox_target} PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
    target_link_libraries(${tbox_target} PRIVATE $<BUILD_INTERFACE:tbox_sanitizers>)
    tbox_use_dependencies(${tbox_target})
    set_target_properties(${tbox_target} PROPERTIES
        OUTPUT_NAME tbox
        EXPORT_NAME ${tbox_target})

    list(APPEND TBOX_LIBRARY_TARGETS ${tbox_target})
endforeach()

# 0.x: every minor version may break the ABI, so it is part of the soname.
if(TBOX_BUILD_SHARED)
    set_target_properties(tbox_shared PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
endif()

set(TBOX_LIBRARY_TARGETS ${TBOX_LIBRARY_TARGETS} PARENT_SCOPE)

# `tbox::tbox` is the target consumers (and the examples) link against:
# shared when available, falling back to static otherwise.
if(TBOX_BUILD_SHARED)
    add_library(tbox::tbox ALIAS tbox_shared)
else()
    add_library(tbox::tbox ALIAS tbox_static)
endif()
