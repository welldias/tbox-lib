cmake_minimum_required(VERSION 4.0)

project(tbox
    VERSION 0.1.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Emit build/compile_commands.json so editor tooling (clangd, cpptools/IntelliSense)
# gets accurate per-file include paths and defines without guessing.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(TBOX_BUILD_EXAMPLES "Build the tbox example programs" ON)
option(TBOX_BUILD_TESTS "Build the tbox test suite" ON)
option(TBOX_ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

# Treat every compiler warning as an error across the whole project.
add_library(tbox_warnings INTERFACE)

if(MSVC)
    target_compile_options(tbox_warnings INTERFACE /W4 /WX)
    # The codebase already null-terminates every strncpy/sprintf call by hand;
    # silence MSVC's "unsafe function" deprecation (C4996) for the standard
    # C library calls used here and in bundled third-party headers (stb_image).
    target_compile_definitions(tbox_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(tbox_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

# Opt-in ASan+UBSan instrumentation, shared by the library, the examples and
# the test suite so a single flag rebuilds everything consistently.
add_library(tbox_sanitizers INTERFACE)

if(TBOX_ENABLE_SANITIZERS)
    if(MSVC)
        message(WARNING "TBOX_ENABLE_SANITIZERS has no effect with MSVC; ignoring.")
    else()
        target_compile_options(tbox_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(tbox_sanitizers INTERFACE -fsanitize=address,undefined)
    endif()
endif()

# FreeType is a core dependency of libtbox (the Fonte/Texto layer wraps
# FT_Face for metrics/rasterization), not just an example concern -- it used
# to be fetched only by example/CMakeLists.txt for the tbox_wayland smoke
# test, but now src/ links it too, so it is declared once here, before
# add_subdirectory(src), and reused by both. Fetched and built from source
# (rather than found on the system) so its build can be forced to produce
# only a static library, regardless of how the system package happens to be
# built.
include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-14-3
)
FetchContent_MakeAvailable(freetype)

# Force PIC even though freetype is a static archive: tbox_shared (a .so)
# links it in, and without this its object files aren't relocatable enough
# for a shared object (surfaces as an "R_X86_64_PC32 ... recompile with
# -fPIC" link error the moment anything actually calls into FreeType).
set_target_properties(freetype PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Wayland/xkbcommon/wayland-scanner/xdg-shell protocol codegen detection --
# used to live entirely in example/CMakeLists.txt (only tbox_wayland needed
# it), but src/output/tbox_backend_wayland.c (Output Display's real Wayland
# backend, see ARCHITECTURE.md's "Output Display" section) now needs the
# exact same things. src/ and example/ are sibling add_subdirectory() calls
# below, so variables set inside one don't propagate to the other -- this has
# to live here, one level up, same as FreeType above, so both consumers can
# use the results. Entirely optional, same graceful-skip pattern as
# Fontconfig above: absence must never fail configure, it just means
# tbox_backend_wayland.c and the tbox_wayland example are both excluded from
# this build.
set(TBOX_WAYLAND_FOUND FALSE)

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
            set(TBOX_WAYLAND_FOUND TRUE)
        else()
            message(WARNING "wayland-protocols found but xdg-shell.xml is missing at "
                             "${XDG_SHELL_XML}; skipping the Wayland backend and tbox_wayland example.")
        endif()
    endif()
endif()

if(TBOX_WAYLAND_FOUND)
    # Generate the xdg-shell protocol glue -- plain wl_surface has no window
    # semantics (title, close, configure/resize acking); xdg-shell is the
    # standard extension that provides it, so its XML has to be turned into
    # C bindings via wayland-scanner at build time. Generated once here into
    # the root build directory and shared by both src/ (the real backend) and
    # example/ (the standalone smoke test).
    set(TBOX_XDG_SHELL_PROTOCOL_DIR "${CMAKE_BINARY_DIR}/generated")
    set(TBOX_XDG_SHELL_PROTOCOL_C "${TBOX_XDG_SHELL_PROTOCOL_DIR}/xdg-shell-protocol.c")
    set(TBOX_XDG_SHELL_PROTOCOL_H "${TBOX_XDG_SHELL_PROTOCOL_DIR}/xdg-shell-client-protocol.h")

    file(MAKE_DIRECTORY "${TBOX_XDG_SHELL_PROTOCOL_DIR}")

    add_custom_command(
        OUTPUT "${TBOX_XDG_SHELL_PROTOCOL_H}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header
                "${XDG_SHELL_XML}" "${TBOX_XDG_SHELL_PROTOCOL_H}"
        DEPENDS "${XDG_SHELL_XML}"
        COMMENT "Generating xdg-shell-client-protocol.h from xdg-shell.xml"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${TBOX_XDG_SHELL_PROTOCOL_C}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code
                "${XDG_SHELL_XML}" "${TBOX_XDG_SHELL_PROTOCOL_C}"
        DEPENDS "${XDG_SHELL_XML}"
        COMMENT "Generating xdg-shell-protocol.c from xdg-shell.xml"
        VERBATIM
    )

    add_custom_target(tbox_wayland_xdg_shell_protocol
        DEPENDS "${TBOX_XDG_SHELL_PROTOCOL_C}" "${TBOX_XDG_SHELL_PROTOCOL_H}"
    )
else()
    message(WARNING
        "Skipping the Wayland Output Display backend and the tbox_wayland example: "
        "requires the wayland-client, wayland-protocols and xkbcommon pkg-config "
        "modules plus the wayland-scanner tool.\n"
        "  Arch Linux:    sudo pacman -S wayland wayland-protocols libxkbcommon\n"
        "  Debian/Ubuntu: sudo apt install libwayland-dev wayland-protocols libxkbcommon-dev"
    )
endif()

# OpenCV backs tests/tbox_cmp.c (v10, see ARCHITECTURE.md's "v10 --
# tbox_cmp"), a visual-regression tool that renders each tests/assets/*.html
# fixture and compares it against its paired *.png reference via SSIM.
# Entirely optional, same graceful-skip pattern as Wayland/Fontconfig above:
# absence must never fail configure -- the main test suite (tbox_tests, the
# 21 groups in tests/CMakeLists.txt) never depends on it, only tbox_cmp
# itself (excluded from the build when this is false, see
# tests/CMakeLists.txt).
# Restricted to the three modules tbox_cmp.cpp actually uses (core is
# pulled in transitively by both) -- the full, unrestricted find_package(OpenCV)
# pulls in every module the local install has, including GUI/visualization
# ones (highgui, viz, cvv, hdf) whose own Qt6/VTK/HDF5 dependencies can be
# broken or absent on a given machine without OpenCV itself being unusable
# for what this project needs -- discovered empirically: linking against
# the unrestricted ${OpenCV_LIBS} failed with unresolved Qt6/VTK/HDF5
# symbols from modules tbox_cmp.cpp never calls into.
find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs)
set(TBOX_OPENCV_FOUND ${OpenCV_FOUND})

if(NOT TBOX_OPENCV_FOUND)
    message(WARNING
        "OpenCV not found: tests/tbox_cmp.cpp (visual regression tool) is "
        "excluded from this build -- libtbox and its test suite still "
        "build fine without it.\n"
        "  Arch Linux:    sudo pacman -S opencv\n"
        "  Debian/Ubuntu: sudo apt install libopencv-dev"
    )
else()
    # tests/tbox_cmp.cpp is the one C++ translation unit in this otherwise
    # pure-C project -- OpenCV's C API (IplImage/cvLoadImage/...) no longer
    # exists as of the OpenCV version this project actually builds against
    # (removed upstream well before OpenCV 5), so tbox_cmp.cpp uses OpenCV's
    # modern C++ API (cv::Mat and friends) instead. C++ is only enabled when
    # OpenCV itself is found -- a build with OpenCV absent needs no C++
    # toolchain at all, same graceful-skip spirit as every other optional
    # dependency here.
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

add_subdirectory(src)

if(TBOX_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

if(TBOX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
