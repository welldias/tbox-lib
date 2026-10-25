cmake_minimum_required(VERSION 3.25...4.0)

project(tbox
    VERSION 0.1.0
    LANGUAGES C
)

# Directory-scoped: these reach tbox's own subdirectories only, never the
# project that pulls tbox in with add_subdirectory()/FetchContent.
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Settings that belong to whoever owns the build: applied only when tbox is
# the root project, so a parent project keeps its own build type and
# compile database choice.
if(PROJECT_IS_TOP_LEVEL)
    # Emit build/compile_commands.json so editor tooling (clangd, cpptools/IntelliSense)
    # gets accurate per-file include paths and defines without guessing.
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    endif()
endif()

# Examples, tests, -Werror and install rules are on by default only for
# tbox's own build; a parent project opts in explicitly.
option(TBOX_BUILD_EXAMPLES "Build the tbox example programs" ${PROJECT_IS_TOP_LEVEL})
option(TBOX_BUILD_TESTS "Build the tbox test suite" ${PROJECT_IS_TOP_LEVEL})
option(TBOX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ${PROJECT_IS_TOP_LEVEL})
option(TBOX_INSTALL "Generate the tbox install rules and CMake package" ${PROJECT_IS_TOP_LEVEL})
option(TBOX_ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(TBOX_ENABLE_WAYLAND "Build the Linux Wayland window backend when available" ON)
option(TBOX_ENABLE_FONTCONFIG "Use Fontconfig for application font discovery when available" ON)
option(TBOX_ENABLE_HARFBUZZ "Use HarfBuzz for text shaping when available" ON)

# tbox's own build produces both libraries (the test suite needs the static
# one). As a subproject only one is built, following BUILD_SHARED_LIBS like
# any other add_library() without an explicit type.
if(PROJECT_IS_TOP_LEVEL)
    set(TBOX_DEFAULT_STATIC ON)
    set(TBOX_DEFAULT_SHARED ON)
elseif(BUILD_SHARED_LIBS)
    set(TBOX_DEFAULT_STATIC OFF)
    set(TBOX_DEFAULT_SHARED ON)
else()
    set(TBOX_DEFAULT_STATIC ON)
    set(TBOX_DEFAULT_SHARED OFF)
endif()
option(TBOX_BUILD_STATIC "Build the static tbox library (libtbox.a)" ${TBOX_DEFAULT_STATIC})
option(TBOX_BUILD_SHARED "Build the shared tbox library (libtbox.so)" ${TBOX_DEFAULT_SHARED})

if(NOT TBOX_BUILD_STATIC AND NOT TBOX_BUILD_SHARED)
    message(FATAL_ERROR "At least one of TBOX_BUILD_STATIC or TBOX_BUILD_SHARED must be ON")
endif()

# The project's warning set; -Werror only with TBOX_WARNINGS_AS_ERRORS, so a
# consumer on a newer compiler isn't broken by a warning tbox's CI never saw.
add_library(tbox_warnings INTERFACE)

if(MSVC)
    target_compile_options(tbox_warnings INTERFACE /W4 $<$<BOOL:${TBOX_WARNINGS_AS_ERRORS}>:/WX>)
    # The codebase already null-terminates every strncpy/sprintf call by hand;
    # silence MSVC's "unsafe function" deprecation (C4996) for the standard
    # C library calls used here and in bundled third-party headers (stb_image).
    target_compile_definitions(tbox_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(tbox_warnings INTERFACE -Wall -Wextra -Wpedantic $<$<BOOL:${TBOX_WARNINGS_AS_ERRORS}>:-Werror>)
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

# System dependencies. Detected once here, before any add_subdirectory(), so
# src/, example/ and tests/ all see the same results and imported targets
# (an imported target is visible in the directory that creates it and
# below). The pkg-config prefixes carry TBOX_ so neither the variables nor
# the PkgConfig::TBOX_* targets clash with a parent project's own
# pkg_check_modules() calls.

# FreeType is required for font metrics, rasterization and outlines.
# Use the system development package; the imported target supplies its
# include paths and library to src/, examples and tools.
find_package(Freetype REQUIRED)

# tbox_app_post's queue lock (src/base/tbox_mutex.h): the one piece of the
# library called from other threads.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# tbox_css_hsla_to_rgba (css_cascade/tbox_css_color_convert.c) uses fmod(),
# which pre-C99-merge glibc and some other libcs ship in a separate math
# library rather than folding into libc itself. Linked by name (-lm) so an
# installed static library doesn't carry this machine's absolute path.
find_library(TBOX_MATH_LIBRARY m)

find_package(PkgConfig QUIET)

# Fontconfig backs tbox_font_source_fontconfig.c (Fonte/Texto layer), which
# resolves a generic family like "sans-serif" to a real font file on Linux.
# It is optional: absence must never fail configure, because the test suite
# only ever exercises the embedded font backend and has to keep building in
# a CI container without Fontconfig installed -- when absent,
# tbox_font_source_fontconfig.c and the app files that need it are excluded
# from the library in src/CMakeLists.txt.
set(TBOX_FONTCONFIG_FOUND FALSE)

if(TBOX_ENABLE_FONTCONFIG AND PkgConfig_FOUND)
    pkg_check_modules(TBOX_FONTCONFIG QUIET IMPORTED_TARGET fontconfig)
endif()

if(TBOX_ENABLE_FONTCONFIG AND NOT TBOX_FONTCONFIG_FOUND)
    message(WARNING
        "Fontconfig (pkg-config module 'fontconfig') not found: "
        "application rendering is unavailable, but the core library "
        "still builds with embedded fonts.\n"
        "  Arch Linux:    sudo pacman -S fontconfig\n"
        "  Debian/Ubuntu: sudo apt install libfontconfig1-dev"
    )
endif()

# HarfBuzz backs the text shaper (docs/09-texto-e-fontes.md, "Shaping").
# Optional with the same graceful-skip: exactly one of the two shaper files
# is compiled -- tbox_font_shaper_harfbuzz.c when HarfBuzz is found,
# tbox_font_shaper_simple.c (one glyph per codepoint) otherwise.
set(TBOX_HARFBUZZ_FOUND FALSE)

if(TBOX_ENABLE_HARFBUZZ AND PkgConfig_FOUND)
    pkg_check_modules(TBOX_HARFBUZZ QUIET IMPORTED_TARGET harfbuzz)
endif()

if(TBOX_ENABLE_HARFBUZZ AND NOT TBOX_HARFBUZZ_FOUND)
    message(WARNING
        "HarfBuzz (pkg-config module 'harfbuzz') not found: text is shaped "
        "one glyph per codepoint (no ligatures or complex scripts).\n"
        "  Arch Linux:    sudo pacman -S harfbuzz\n"
        "  Debian/Ubuntu: sudo apt install libharfbuzz-dev"
    )
endif()

# Wayland/xkbcommon/wayland-scanner/xdg-shell protocol codegen detection,
# shared by src/output/tbox_backend_wayland.c (Output Display's real Wayland
# backend, see docs/dev/historico.md's "Output Display" section) and the
# example/tbox_wayland smoke test. Entirely optional, same graceful-skip
# pattern as Fontconfig above: absence must never fail configure, it just
# means tbox_backend_wayland.c and the tbox_wayland example are both
# excluded from this build.
set(TBOX_WAYLAND_FOUND FALSE)

if(TBOX_ENABLE_WAYLAND AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PkgConfig_FOUND)
    pkg_check_modules(TBOX_WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
    pkg_check_modules(TBOX_WAYLAND_PROTOCOLS QUIET wayland-protocols)
    pkg_check_modules(TBOX_XKBCOMMON QUIET IMPORTED_TARGET xkbcommon)
    # Optional: themed pointer cursors (CSS `cursor`). Without it the window
    # keeps whatever cursor the compositor shows.
    pkg_check_modules(TBOX_WAYLAND_CURSOR QUIET IMPORTED_TARGET wayland-cursor)

    # wayland-scanner is a CLI tool, not a linkable library: try pkg-config's
    # variable first, then fall back to a plain PATH search.
    if(TBOX_WAYLAND_CLIENT_FOUND)
        pkg_get_variable(WAYLAND_SCANNER_EXECUTABLE wayland-scanner wayland_scanner)
    endif()
    if(NOT WAYLAND_SCANNER_EXECUTABLE)
        find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner)
    endif()

    if(TBOX_WAYLAND_CLIENT_FOUND AND TBOX_WAYLAND_PROTOCOLS_FOUND AND TBOX_XKBCOMMON_FOUND AND WAYLAND_SCANNER_EXECUTABLE)
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
    # tbox's build directory and shared by both src/ (the real backend) and
    # example/ (the standalone smoke test).
    set(TBOX_XDG_SHELL_PROTOCOL_DIR "${PROJECT_BINARY_DIR}/generated")
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
elseif(TBOX_ENABLE_WAYLAND)
    message(WARNING
        "Skipping the Wayland Output Display backend and the tbox_wayland example: "
        "requires the wayland-client, wayland-protocols and xkbcommon pkg-config "
        "modules plus the wayland-scanner tool.\n"
        "  Arch Linux:    sudo pacman -S wayland wayland-protocols libxkbcommon\n"
        "  Debian/Ubuntu: sudo apt install libwayland-dev wayland-protocols libxkbcommon-dev"
    )
endif()

# OpenCV backs tests/tbox_cmp.cpp (v10, see docs/dev/historico.md's "v10 --
# tbox_cmp"), a visual-regression tool that renders each tests/assets/*.html
# fixture and compares it against a headless Firefox capture of the same file.
# Looked for only when the test suite is built, and optional even then: the
# main test suite (tbox_tests, the groups in tests/CMakeLists.txt) never
# depends on it, only tbox_cmp itself (excluded from the build when this is
# false, see tests/CMakeLists.txt).
# Restricted to the three modules tbox_cmp.cpp actually uses (core is
# pulled in transitively by both) -- the full, unrestricted find_package(OpenCV)
# pulls in every module the local install has, including GUI/visualization
# ones (highgui, viz, cvv, hdf) whose own Qt6/VTK/HDF5 dependencies can be
# broken or absent on a given machine without OpenCV itself being unusable
# for what this project needs -- discovered empirically: linking against
# the unrestricted ${OpenCV_LIBS} failed with unresolved Qt6/VTK/HDF5
# symbols from modules tbox_cmp.cpp never calls into.
set(TBOX_OPENCV_FOUND FALSE)

if(TBOX_BUILD_TESTS)
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
endif()

add_subdirectory(src)

if(TBOX_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

if(TBOX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install rules and CMake package: `find_package(tbox CONFIG)` followed by
# `target_link_libraries(app PRIVATE tbox::tbox)`, the same target name an
# add_subdirectory()/FetchContent consumer links against.
if(TBOX_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(TBOX_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/tbox")

    install(TARGETS ${TBOX_LIBRARY_TARGETS} EXPORT tboxTargets)
    install(DIRECTORY include/tbox DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(EXPORT tboxTargets
        NAMESPACE tbox::
        FILE tboxTargets.cmake
        DESTINATION "${TBOX_INSTALL_CMAKEDIR}"
    )

    # Plain ON/OFF for the template's if()s: a pkg-config *_FOUND can be
    # empty, and wayland-cursor only counts when the backend was built.
    foreach(tbox_dep IN ITEMS FONTCONFIG HARFBUZZ WAYLAND)
        if(TBOX_${tbox_dep}_FOUND)
            set(TBOX_CONFIG_${tbox_dep} ON)
        else()
            set(TBOX_CONFIG_${tbox_dep} OFF)
        endif()
    endforeach()
    if(TBOX_WAYLAND_FOUND AND TBOX_WAYLAND_CURSOR_FOUND)
        set(TBOX_CONFIG_WAYLAND_CURSOR ON)
    else()
        set(TBOX_CONFIG_WAYLAND_CURSOR OFF)
    endif()

    configure_package_config_file(cmake/tboxConfig.cmake.in
        "${PROJECT_BINARY_DIR}/tboxConfig.cmake"
        INSTALL_DESTINATION "${TBOX_INSTALL_CMAKEDIR}"
    )
    # 0.x: the API still changes between minor versions.
    write_basic_package_version_file("${PROJECT_BINARY_DIR}/tboxConfigVersion.cmake"
        COMPATIBILITY SameMinorVersion
    )
    install(FILES
        "${PROJECT_BINARY_DIR}/tboxConfig.cmake"
        "${PROJECT_BINARY_DIR}/tboxConfigVersion.cmake"
        DESTINATION "${TBOX_INSTALL_CMAKEDIR}"
    )
endif()

# Static analysis: `cmake --build build --target cppcheck`. Only in tbox's
# own build -- the target name is generic, and a parent project may well
# have its own. Optional, same graceful-skip pattern as the other tools
# above -- without cppcheck the target simply doesn't exist. tbox.cppcheck.in
# is configured into the build directory so the project file points at this
# build's compile_commands.json regardless of where the build directory
# lives. It reads a deduplicated copy of the compile database
# (cmake/tbox_cppcheck_compile_db.cmake), so a file compiled by more than
# one target is analysed once.
# The threadsafety addon (static locals, MT-unsafe libc calls) needs
# Python 3 at analysis time. Findings are written as
# plain text to <build>/cppcheck-report.txt instead of the terminal; any
# finding still fails the target (--error-exitcode), so it can gate CI.
if(PROJECT_IS_TOP_LEVEL)
    find_program(TBOX_CPPCHECK_EXECUTABLE cppcheck)

    if(TBOX_CPPCHECK_EXECUTABLE)
        # cppcheck resolves project-file paths relative to the project file
        # itself (even absolute ones), so the source dir is passed relative too.
        file(RELATIVE_PATH TBOX_CPPCHECK_SOURCE_DIR "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}")
        string(REGEX REPLACE "/$" "" TBOX_CPPCHECK_SOURCE_DIR "${TBOX_CPPCHECK_SOURCE_DIR}")
        if(TBOX_CPPCHECK_SOURCE_DIR STREQUAL "")
            set(TBOX_CPPCHECK_SOURCE_DIR ".")
        endif()
        file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/cppcheck")
        configure_file(tbox.cppcheck.in "${PROJECT_BINARY_DIR}/tbox.cppcheck" @ONLY)

        cmake_host_system_information(RESULT TBOX_CPPCHECK_JOBS QUERY NUMBER_OF_LOGICAL_CORES)

        add_custom_command(
            OUTPUT "${PROJECT_BINARY_DIR}/cppcheck-compile_commands.json"
            COMMAND "${CMAKE_COMMAND}"
                    "-DINPUT=${PROJECT_BINARY_DIR}/compile_commands.json"
                    "-DOUTPUT=${PROJECT_BINARY_DIR}/cppcheck-compile_commands.json"
                    -P "${PROJECT_SOURCE_DIR}/cmake/tbox_cppcheck_compile_db.cmake"
            DEPENDS "${PROJECT_BINARY_DIR}/compile_commands.json"
                    "${PROJECT_SOURCE_DIR}/cmake/tbox_cppcheck_compile_db.cmake"
            COMMENT "Deduplicating the compile database for cppcheck"
            VERBATIM
        )

        add_custom_target(cppcheck
            COMMAND "${TBOX_CPPCHECK_EXECUTABLE}"
                    "--project=${PROJECT_BINARY_DIR}/tbox.cppcheck"
                    --enable=warning,style,performance,portability
                    --inline-suppr
                    --error-exitcode=1
                    "--output-file=${PROJECT_BINARY_DIR}/cppcheck-report.txt"
                    --quiet
                    -j ${TBOX_CPPCHECK_JOBS}
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            DEPENDS "${PROJECT_BINARY_DIR}/cppcheck-compile_commands.json"
            COMMENT "Running cppcheck static analysis (report: ${PROJECT_BINARY_DIR}/cppcheck-report.txt)"
            USES_TERMINAL
            VERBATIM
        )
    else()
        message(STATUS "cppcheck not found: the 'cppcheck' target is unavailable.")
    endif()
endif()
