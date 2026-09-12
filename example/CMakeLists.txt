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

# tbox_wayland is a Linux-only Wayland client demo (no X11, no Windows). It
# depends on system Wayland/xkbcommon dev packages that may not be installed,
# so its build is entirely optional: skip it with a warning rather than
# failing configure for everyone else.
set(TBOX_WAYLAND_DEPS_FOUND FALSE)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PkgConfig_FOUND)
    pkg_check_modules(WAYLAND_CLIENT QUIET wayland-client)
    pkg_check_modules(WAYLAND_PROTOCOLS QUIET wayland-protocols)
    pkg_check_modules(XKBCOMMON QUIET xkbcommon)

    # wayland-scanner is a CLI tool, not a linkable library: try pkg-config's
    # variable first, then fall back to a plain PATH search.
    if(WAYLAND_CLIENT_FOUND)
        pkg_get_variable(WAYLAND_SCANNER_EXECUTABLE wayland-scanner wayland_scanner)
    endif()
    if(NOT WAYLAND_SCANNER_EXECUTABLE)
        find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner)
    endif()

    if(WAYLAND_CLIENT_FOUND AND WAYLAND_PROTOCOLS_FOUND AND XKBCOMMON_FOUND AND WAYLAND_SCANNER_EXECUTABLE)
        pkg_get_variable(WAYLAND_PROTOCOLS_PKGDATADIR wayland-protocols pkgdatadir)
        set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_PKGDATADIR}/stable/xdg-shell/xdg-shell.xml")

        if(EXISTS "${XDG_SHELL_XML}")
            set(TBOX_WAYLAND_DEPS_FOUND TRUE)
        else()
            message(WARNING "wayland-protocols found but xdg-shell.xml is missing at "
                             "${XDG_SHELL_XML}; skipping tbox_wayland example.")
        endif()
    endif()
endif()

if(TBOX_WAYLAND_DEPS_FOUND)
    # Generate the xdg-shell protocol glue -- plain wl_surface has no window
    # semantics (title, close, configure/resize acking); xdg-shell is the
    # standard extension that provides it, so its XML has to be turned into
    # C bindings via wayland-scanner at build time.
    set(XDG_SHELL_PROTOCOL_C "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c")
    set(XDG_SHELL_PROTOCOL_H "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h")

    add_custom_command(
        OUTPUT "${XDG_SHELL_PROTOCOL_H}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header
                "${XDG_SHELL_XML}" "${XDG_SHELL_PROTOCOL_H}"
        DEPENDS "${XDG_SHELL_XML}"
        COMMENT "Generating xdg-shell-client-protocol.h from xdg-shell.xml"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${XDG_SHELL_PROTOCOL_C}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code
                "${XDG_SHELL_XML}" "${XDG_SHELL_PROTOCOL_C}"
        DEPENDS "${XDG_SHELL_XML}"
        COMMENT "Generating xdg-shell-protocol.c from xdg-shell.xml"
        VERBATIM
    )

    add_custom_target(tbox_wayland_xdg_shell_protocol
        DEPENDS "${XDG_SHELL_PROTOCOL_C}" "${XDG_SHELL_PROTOCOL_H}"
    )

    # FreeType is fetched and built from source (rather than found on the
    # system) so its build can be forced to produce only a static library,
    # regardless of how the system package happens to be built.
    include(FetchContent)

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG VER-2-14-3
    )
    FetchContent_MakeAvailable(freetype)

    add_executable(tbox_wayland
        tbox_wayland.c
        "${XDG_SHELL_PROTOCOL_C}"
    )

    add_dependencies(tbox_wayland tbox_wayland_xdg_shell_protocol)

    target_include_directories(tbox_wayland PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
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
else()
    message(WARNING
        "Skipping tbox_wayland example: requires the wayland-client, "
        "wayland-protocols and xkbcommon pkg-config modules plus the "
        "wayland-scanner tool.\n"
        "  Arch Linux:    sudo pacman -S wayland wayland-protocols libxkbcommon\n"
        "  Debian/Ubuntu: sudo apt install libwayland-dev wayland-protocols libxkbcommon-dev"
    )
endif()
