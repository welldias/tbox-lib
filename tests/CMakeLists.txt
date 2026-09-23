# tbox_cmp.cpp (v10, see ARCHITECTURE.md's "v10 -- tbox_cmp") is a separate
# executable with its own main() -- a *.cpp file, so this *.c-only glob
# never picks it up in the first place (no exclusion needed, unlike when it
# was still tbox_cmp.c). Built on its own, conditionally, further down this
# file.
file(GLOB_RECURSE TBOX_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

add_executable(tbox_tests ${TBOX_TEST_SOURCES})

# The parser/tree-builder test groups exercise internals that aren't part of
# the public <tbox/tbox.h> API, so the test binary needs the src/ headers and
# must link against the static library specifically: a shared library built
# without explicit export declarations for internal symbols may not expose
# them on all platforms (notably Windows), whereas a static library always
# does.
target_include_directories(tbox_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/external/utf8.h ${CMAKE_SOURCE_DIR}/external/stb_image)

# test_font.c loads the vendored Liberation Sans Regular by path (same
# pattern as TBOX_EXAMPLE_HTML_PATH in example/CMakeLists.txt) rather than
# depending on Fontconfig or anything installed on the test machine.
target_compile_definitions(tbox_tests PRIVATE
    TBOX_TEST_LIBERATION_SANS_PATH="${CMAKE_SOURCE_DIR}/external/liberation-sans/LiberationSans-Regular.ttf")

# test_image.c decodes real fixtures already on disk (tests/assets/black.png/
# yellow.png, the same ones tests/assets/024.html's <img> regression fixture
# uses) rather than a synthetic image, so it needs an absolute base_dir to
# join against regardless of ctest's own working directory.
target_compile_definitions(tbox_tests PRIVATE
    TBOX_TEST_ASSETS_DIR="${CMAKE_SOURCE_DIR}/tests/assets")

if(TBOX_BUILD_STATIC)
    target_link_libraries(tbox_tests PRIVATE tbox_static tbox_warnings tbox_sanitizers)
else()
    target_link_libraries(tbox_tests PRIVATE tbox tbox_warnings tbox_sanitizers)
endif()

foreach(tbox_test_group arena list vector string debug html_parser_tokenizer html_parser_tree html_xpath css_parser_tokenizer css_parser_parser css_selector css_cascade css_cascade_named_color css_cascade_color_convert css_cascade_color_parse style font image layout render output_raster context)
    add_test(NAME ${tbox_test_group} COMMAND tbox_tests ${tbox_test_group})
endforeach()

# tbox_cmp (v10, see ARCHITECTURE.md's "v10 -- tbox_cmp") renders each
# tests/assets/*.html fixture via tbox_app_screenshot_from_files (v7) and
# compares it against its paired *.png reference via OpenCV/SSIM -- needs
# OpenCV and Fontconfig. Wayland is unnecessary because screenshots render
# without a window. This tool remains optional and outside ctest.
if(TBOX_OPENCV_FOUND AND TBOX_FONTCONFIG_FOUND)
    add_executable(tbox_cmp tbox_cmp.cpp)

    if(TBOX_BUILD_STATIC)
        target_link_libraries(tbox_cmp PRIVATE tbox_static tbox_warnings tbox_sanitizers)
    else()
        target_link_libraries(tbox_cmp PRIVATE tbox tbox_warnings tbox_sanitizers)
    endif()

    target_link_libraries(tbox_cmp PRIVATE
        freetype
        ${WAYLAND_CLIENT_LIBRARIES}
        ${XKBCOMMON_LIBRARIES}
        ${FONTCONFIG_LIBRARIES}
        ${OpenCV_LIBS}
    )
    target_include_directories(tbox_cmp PRIVATE
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS}
        ${FONTCONFIG_INCLUDE_DIRS}
        ${OpenCV_INCLUDE_DIRS}
    )
    target_link_directories(tbox_cmp PRIVATE
        ${WAYLAND_CLIENT_LIBRARY_DIRS}
        ${XKBCOMMON_LIBRARY_DIRS}
        ${FONTCONFIG_LIBRARY_DIRS}
    )

    set_target_properties(tbox_cmp PROPERTIES OUTPUT_NAME tbox_cmp)

    # Deliberately NOT registered via add_test()/ctest -- the maintainer
    # wants this run manually, on demand (`./tbox_cmp [assets_dir]`), not
    # as part of the automated `ctest` run. Several assets deliberately
    # exercise HTML/CSS tbox doesn't implement yet (see ARCHITECTURE.md's
    # v10 "Escopo") and are expected to report DIFERENTE today, which is
    # exactly the kind of result that shouldn't silently gate CI.
endif()
