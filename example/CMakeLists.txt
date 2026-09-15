# Every example below shares the same two fixtures -- example/index.html and
# example/style.css -- so there is exactly one HTML document and one
# stylesheet to keep up to date as the library grows new example material.

add_executable(html_parser
    html_parser.c
)

target_link_libraries(html_parser
    PRIVATE tbox tbox_warnings tbox_sanitizers
)

target_compile_definitions(html_parser PRIVATE TBOX_EXAMPLE_HTML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/index.html")

set_target_properties(html_parser PROPERTIES OUTPUT_NAME html_parser)

add_executable(html_xpath
    html_xpath.c
)

target_link_libraries(html_xpath
    PRIVATE tbox tbox_warnings tbox_sanitizers
)

target_compile_definitions(html_xpath PRIVATE TBOX_EXAMPLE_HTML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/index.html")

set_target_properties(html_xpath PROPERTIES OUTPUT_NAME html_xpath)

add_executable(css_parser
    css_parser.c
)

target_link_libraries(css_parser
    PRIVATE tbox tbox_warnings tbox_sanitizers
)

target_compile_definitions(css_parser PRIVATE TBOX_EXAMPLE_CSS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/style.css")

set_target_properties(css_parser PROPERTIES OUTPUT_NAME css_parser)

add_executable(css_selector
    css_selector.c
)

target_link_libraries(css_selector
    PRIVATE tbox tbox_warnings tbox_sanitizers
)

target_compile_definitions(css_selector PRIVATE TBOX_EXAMPLE_HTML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/index.html" TBOX_EXAMPLE_CSS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/style.css")

set_target_properties(css_selector PROPERTIES OUTPUT_NAME css_selector)

add_executable(css_style_by_node
    css_style_by_node.c
)

target_link_libraries(css_style_by_node
    PRIVATE tbox tbox_warnings tbox_sanitizers
)

target_compile_definitions(css_style_by_node PRIVATE TBOX_EXAMPLE_HTML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/index.html" TBOX_EXAMPLE_CSS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/style.css")

set_target_properties(css_style_by_node PROPERTIES OUTPUT_NAME css_style_by_node)

add_executable(css_cascade
    css_cascade.c
)

target_link_libraries(css_cascade
    PRIVATE tbox tbox_warnings tbox_sanitizers
)

target_compile_definitions(css_cascade PRIVATE TBOX_EXAMPLE_HTML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/index.html" TBOX_EXAMPLE_CSS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/style.css")

set_target_properties(css_cascade PROPERTIES OUTPUT_NAME css_cascade)

add_executable(css_cascade_origins
    css_cascade_origins.c
)

target_link_libraries(css_cascade_origins
    PRIVATE tbox tbox_warnings tbox_sanitizers
)

target_compile_definitions(css_cascade_origins PRIVATE
    TBOX_EXAMPLE_HTML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/index.html"
    TBOX_EXAMPLE_CSS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/style.css"
    TBOX_EXAMPLE_USER_AGENT_CSS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/user-agent.css"
    TBOX_EXAMPLE_USER_CSS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/user.css"
)

set_target_properties(css_cascade_origins PROPERTIES OUTPUT_NAME css_cascade_origins)

# tbox_wayland is a Linux-only Wayland client demo (no X11, no Windows). Its
# dependency detection (wayland-client/wayland-protocols/xkbcommon/
# wayland-scanner) and the xdg-shell protocol codegen it needs now live in
# the root CMakeLists.txt (TBOX_WAYLAND_FOUND, TBOX_XDG_SHELL_PROTOCOL_C/_H,
# the tbox_wayland_xdg_shell_protocol target) rather than here, because
# src/output/tbox_backend_wayland.c (Output Display's real Wayland backend)
# needs the exact same things and src/ can't see variables set in this
# sibling directory -- see the root CMakeLists.txt for the detection itself
# and its graceful-skip warning when these packages aren't installed. This
# example's own C source and behavior are unchanged; only how it's wired up
# to those already-detected results is different.
if(TBOX_WAYLAND_FOUND)
    # FreeType is declared once in the root CMakeLists.txt (a core libtbox
    # dependency now, not just an example concern) and made available before
    # add_subdirectory(example) runs, so the `freetype` target already exists
    # here -- just link it.
    add_executable(tbox_wayland
        tbox_wayland.c
        "${TBOX_XDG_SHELL_PROTOCOL_C}"
    )

    add_dependencies(tbox_wayland tbox_wayland_xdg_shell_protocol)

    target_include_directories(tbox_wayland PRIVATE
        "${TBOX_XDG_SHELL_PROTOCOL_DIR}"
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS}
    )
    target_link_directories(tbox_wayland PRIVATE
        ${WAYLAND_CLIENT_LIBRARY_DIRS}
        ${XKBCOMMON_LIBRARY_DIRS}
    )
    target_link_libraries(tbox_wayland
        PRIVATE tbox tbox_warnings tbox_sanitizers
                freetype
                ${WAYLAND_CLIENT_LIBRARIES}
                ${XKBCOMMON_LIBRARIES}
    )

    set_target_properties(tbox_wayland PROPERTIES OUTPUT_NAME tbox_wayland)
endif()

# tbox_app_demo exercises the Application layer's single public entry point,
# tbox_app_open() (src/app/tbox_app.c) -- the v0 vertical slice end to end,
# through the real library API rather than tbox_wayland's own low-level
# Wayland/FreeType calls. tbox_app.c is only compiled into libtbox when BOTH
# TBOX_WAYLAND_FOUND and TBOX_FONTCONFIG_FOUND (see src/CMakeLists.txt --
# TBOX_FONTCONFIG_FOUND is pushed up to this scope from there, the same
# reason TBOX_WAYLAND_FOUND itself lives in the root CMakeLists.txt), so this
# example is gated on the same combination -- linking against tbox_app_open
# with either dependency missing would fail at link time, not just be
# pointless.
if(TBOX_WAYLAND_FOUND AND TBOX_FONTCONFIG_FOUND)
    add_executable(tbox_app_demo
        tbox_app.c
    )

    target_link_libraries(tbox_app_demo
        PRIVATE tbox tbox_warnings tbox_sanitizers
                freetype
                ${WAYLAND_CLIENT_LIBRARIES}
                ${XKBCOMMON_LIBRARIES}
                ${FONTCONFIG_LIBRARIES}
    )

    target_include_directories(tbox_app_demo PRIVATE
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS}
        ${FONTCONFIG_INCLUDE_DIRS}
    )
    target_link_directories(tbox_app_demo PRIVATE
        ${WAYLAND_CLIENT_LIBRARY_DIRS}
        ${XKBCOMMON_LIBRARY_DIRS}
        ${FONTCONFIG_LIBRARY_DIRS}
    )

    set_target_properties(tbox_app_demo PROPERTIES OUTPUT_NAME tbox_app_demo)
endif()
