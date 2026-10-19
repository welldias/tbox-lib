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
set(TBOX_SOURCES ${TBOX_BASE_SOURCES} ${TBOX_HTML_PARSER_SOURCES} ${TBOX_HTML_XPATH_SOURCES} ${TBOX_CSS_PARSER_SOURCES} ${TBOX_CSS_SELECTOR_SOURCES} ${TBOX_CSS_CASCADE_SOURCES} ${TBOX_STYLE_SOURCES} ${TBOX_FONT_SOURCES} ${TBOX_LAYOUT_SOURCES} ${TBOX_RENDER_SOURCES} ${TBOX_OUTPUT_SOURCES} ${TBOX_CONTEXT_SOURCES} ${TBOX_APP_SOURCES} ${TBOX_IMAGE_SOURCES})

# tbox_css_hsla_to_rgba (css_cascade/tbox_css_color_convert.c) uses fmod(),
# which pre-C99-merge glibc and some other libcs ship in a separate math
# library rather than folding into libc itself.
find_library(TBOX_MATH_LIBRARY m)

# tbox_app_post's queue lock (src/base/tbox_mutex.h): the one piece of the
# library called from other threads.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Fontconfig backs tbox_font_source_fontconfig.c (Fonte/Texto layer), which
# resolves a generic family like "sans-serif" to a real font file on Linux.
# It is optional, same graceful-skip pattern as the Wayland dependency
# detection in example/CMakeLists.txt: absence must never fail configure,
# because the test suite only ever exercises the embedded font backend and
# has to keep building in a CI container without Fontconfig installed --
# when absent, tbox_font_source_fontconfig.c is excluded from TBOX_SOURCES
# below rather than compiled and left to fail.
find_package(PkgConfig QUIET)

set(TBOX_FONTCONFIG_FOUND FALSE)

if(TBOX_ENABLE_FONTCONFIG AND PkgConfig_FOUND)
    pkg_check_modules(FONTCONFIG QUIET fontconfig)
    if(FONTCONFIG_FOUND)
        set(TBOX_FONTCONFIG_FOUND TRUE)
    endif()
endif()

# example/CMakeLists.txt (a sibling add_subdirectory(), added after this one
# by the root CMakeLists.txt) needs this too, to gate the tbox_app_demo
# example the same way tbox_app.c is gated below -- directory-scoped `set()`
# doesn't cross the src/ <-> example/ boundary on its own, so these have to
# be pushed up to the scope both share (same reasoning as TBOX_WAYLAND_FOUND,
# which is why that one is set in the root CMakeLists.txt directly instead).
# The FONTCONFIG_* variables are only ever meaningful once TBOX_FONTCONFIG_FOUND
# is true, but PARENT_SCOPE on an unset variable is harmless either way.
set(TBOX_FONTCONFIG_FOUND ${TBOX_FONTCONFIG_FOUND} PARENT_SCOPE)
set(FONTCONFIG_INCLUDE_DIRS ${FONTCONFIG_INCLUDE_DIRS} PARENT_SCOPE)
set(FONTCONFIG_LIBRARY_DIRS ${FONTCONFIG_LIBRARY_DIRS} PARENT_SCOPE)
set(FONTCONFIG_LIBRARIES ${FONTCONFIG_LIBRARIES} PARENT_SCOPE)

# HarfBuzz backs the text shaper (docs/09-texto-e-fontes.md, "Shaping").
# Optional with the same graceful-skip: exactly one of the two shaper files
# is compiled -- tbox_font_shaper_harfbuzz.c when HarfBuzz is found,
# tbox_font_shaper_simple.c (one glyph per codepoint) otherwise.
set(TBOX_HARFBUZZ_FOUND FALSE)
if(TBOX_ENABLE_HARFBUZZ AND PkgConfig_FOUND)
    pkg_check_modules(HARFBUZZ QUIET harfbuzz)
    if(HARFBUZZ_FOUND)
        set(TBOX_HARFBUZZ_FOUND TRUE)
    endif()
endif()
if(TBOX_HARFBUZZ_FOUND)
    list(REMOVE_ITEM TBOX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/font/tbox_font_shaper_simple.c)
else()
    list(REMOVE_ITEM TBOX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/font/tbox_font_shaper_harfbuzz.c)
    if(TBOX_ENABLE_HARFBUZZ)
        message(WARNING
            "HarfBuzz (pkg-config module 'harfbuzz') not found: text is shaped "
            "one glyph per codepoint (no ligatures or complex scripts).\n"
            "  Arch Linux:    sudo pacman -S harfbuzz\n"
            "  Debian/Ubuntu: sudo apt install libharfbuzz-dev"
        )
    endif()
endif()

set(TBOX_HARFBUZZ_FOUND ${TBOX_HARFBUZZ_FOUND} PARENT_SCOPE)

if(NOT TBOX_FONTCONFIG_FOUND)
    list(REMOVE_ITEM TBOX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/font/tbox_font_source_fontconfig.c)
    if(TBOX_ENABLE_FONTCONFIG)
        message(WARNING
            "Fontconfig (pkg-config module 'fontconfig') not found: "
            "application rendering is unavailable, but the core library "
            "still builds with embedded fonts.\n"
            "  Arch Linux:    sudo pacman -S fontconfig\n"
            "  Debian/Ubuntu: sudo apt install libfontconfig1-dev"
        )
    endif()
endif()

# tbox_backend_wayland.c (Output Display's real Wayland backend) needs
# wayland-client/xkbcommon plus the generated xdg-shell protocol glue --
# TBOX_WAYLAND_FOUND and the associated variables/targets are detected once
# in the root CMakeLists.txt (before add_subdirectory(src) runs), same
# reasoning as TBOX_FONTCONFIG_FOUND above: this can't fail configure when
# absent, since the test suite (tbox_raster_* only, no Wayland) must keep
# building without these system packages installed.
if(NOT TBOX_WAYLAND_FOUND)
    list(REMOVE_ITEM TBOX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/output/tbox_backend_wayland.c)
else()
    list(APPEND TBOX_SOURCES ${TBOX_XDG_SHELL_PROTOCOL_C})
endif()

# Screenshot rendering needs Fontconfig but no window backend. When font
# discovery is absent, linkable application stubs expose that limitation.
if(NOT TBOX_FONTCONFIG_FOUND)
    list(REMOVE_ITEM TBOX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/app/tbox_app.c)
else()
    list(REMOVE_ITEM TBOX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/app/tbox_app_unavailable.c)
endif()

# utf8.h is visible to base/tbox_string.c only: every other file goes
# through src/base/tbox_string.h, and an `#include "utf8.h"` elsewhere fails
# to compile (docs/09-texto-e-fontes.md).
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/base/tbox_string.c
    PROPERTIES INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/external/utf8.h)

option(TBOX_BUILD_STATIC "Build the static tbox library (libtbox.a)" ON)
option(TBOX_BUILD_SHARED "Build the shared tbox library (libtbox.so)" ON)

if(NOT TBOX_BUILD_STATIC AND NOT TBOX_BUILD_SHARED)
    message(FATAL_ERROR "At least one of TBOX_BUILD_STATIC or TBOX_BUILD_SHARED must be ON")
endif()

if(TBOX_BUILD_STATIC)
    add_library(tbox_static STATIC ${TBOX_SOURCES})
    target_include_directories(tbox_static PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_include_directories(tbox_static PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/stb_image)
    target_link_libraries(tbox_static PRIVATE tbox_warnings tbox_sanitizers freetype Threads::Threads)
    if(TBOX_MATH_LIBRARY)
        target_link_libraries(tbox_static PRIVATE ${TBOX_MATH_LIBRARY})
    endif()
    if(TBOX_FONTCONFIG_FOUND)
        target_include_directories(tbox_static PRIVATE ${FONTCONFIG_INCLUDE_DIRS})
        target_link_directories(tbox_static PRIVATE ${FONTCONFIG_LIBRARY_DIRS})
        target_link_libraries(tbox_static PRIVATE ${FONTCONFIG_LIBRARIES})
    endif()
    if(TBOX_HARFBUZZ_FOUND)
        target_include_directories(tbox_static PRIVATE ${HARFBUZZ_INCLUDE_DIRS})
        target_link_directories(tbox_static PRIVATE ${HARFBUZZ_LIBRARY_DIRS})
        target_link_libraries(tbox_static PRIVATE ${HARFBUZZ_LIBRARIES})
    endif()
    if(TBOX_WAYLAND_FOUND)
        target_compile_definitions(tbox_static PRIVATE TBOX_HAS_WAYLAND=1 TBOX_HAS_WINDOW_BACKEND=1)
        if(WAYLAND_CURSOR_FOUND)
            target_compile_definitions(tbox_static PRIVATE TBOX_HAS_WAYLAND_CURSOR=1)
        endif()
        add_dependencies(tbox_static tbox_wayland_xdg_shell_protocol)
        target_include_directories(tbox_static PRIVATE
            "${TBOX_XDG_SHELL_PROTOCOL_DIR}"
            ${WAYLAND_CLIENT_INCLUDE_DIRS}
            ${XKBCOMMON_INCLUDE_DIRS}
        )
        target_link_directories(tbox_static PRIVATE
            ${WAYLAND_CLIENT_LIBRARY_DIRS}
            ${XKBCOMMON_LIBRARY_DIRS}
        )
        target_link_libraries(tbox_static PRIVATE
            ${WAYLAND_CLIENT_LIBRARIES}
            ${XKBCOMMON_LIBRARIES}
        )
    endif()
    set_target_properties(tbox_static PROPERTIES OUTPUT_NAME tbox)
endif()

if(TBOX_BUILD_SHARED)
    add_library(tbox_shared SHARED ${TBOX_SOURCES})
    target_include_directories(tbox_shared PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_include_directories(tbox_shared PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/stb_image)
    target_link_libraries(tbox_shared PRIVATE tbox_warnings tbox_sanitizers freetype Threads::Threads)
    if(TBOX_MATH_LIBRARY)
        target_link_libraries(tbox_shared PRIVATE ${TBOX_MATH_LIBRARY})
    endif()
    if(TBOX_FONTCONFIG_FOUND)
        target_include_directories(tbox_shared PRIVATE ${FONTCONFIG_INCLUDE_DIRS})
        target_link_directories(tbox_shared PRIVATE ${FONTCONFIG_LIBRARY_DIRS})
        target_link_libraries(tbox_shared PRIVATE ${FONTCONFIG_LIBRARIES})
    endif()
    if(TBOX_HARFBUZZ_FOUND)
        target_include_directories(tbox_shared PRIVATE ${HARFBUZZ_INCLUDE_DIRS})
        target_link_directories(tbox_shared PRIVATE ${HARFBUZZ_LIBRARY_DIRS})
        target_link_libraries(tbox_shared PRIVATE ${HARFBUZZ_LIBRARIES})
    endif()
    if(TBOX_WAYLAND_FOUND)
        target_compile_definitions(tbox_shared PRIVATE TBOX_HAS_WAYLAND=1 TBOX_HAS_WINDOW_BACKEND=1)
        if(WAYLAND_CURSOR_FOUND)
            target_compile_definitions(tbox_shared PRIVATE TBOX_HAS_WAYLAND_CURSOR=1)
        endif()
        add_dependencies(tbox_shared tbox_wayland_xdg_shell_protocol)
        target_include_directories(tbox_shared PRIVATE
            "${TBOX_XDG_SHELL_PROTOCOL_DIR}"
            ${WAYLAND_CLIENT_INCLUDE_DIRS}
            ${XKBCOMMON_INCLUDE_DIRS}
        )
        target_link_directories(tbox_shared PRIVATE
            ${WAYLAND_CLIENT_LIBRARY_DIRS}
            ${XKBCOMMON_LIBRARY_DIRS}
        )
        target_link_libraries(tbox_shared PRIVATE
            ${WAYLAND_CLIENT_LIBRARIES}
            ${XKBCOMMON_LIBRARIES}
        )
    endif()
    set_target_properties(tbox_shared PROPERTIES OUTPUT_NAME tbox)
endif()

# `tbox` is the default target other parts of the build (e.g. the example) link
# against: shared when available, falling back to static otherwise.
if(TBOX_BUILD_SHARED)
    add_library(tbox ALIAS tbox_shared)
else()
    add_library(tbox ALIAS tbox_static)
endif()
