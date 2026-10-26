# tbox_cmp.cpp (v10, see docs/dev/historico.md's "v10 -- tbox_cmp") is a separate
# executable with its own main() -- a *.cpp file, so this *.c-only glob
# never picks it up in the first place (no exclusion needed, unlike when it
# was still tbox_cmp.c). Built on its own, conditionally, further down this
# file.
file(GLOB_RECURSE TBOX_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

# The app integration test has its own main and simulated backend; the
# measuring tools (further down) are programs of their own too.
list(REMOVE_ITEM TBOX_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_app_lifecycle.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tbox_bench.c ${CMAKE_CURRENT_SOURCE_DIR}/tbox_frame_dump.c)
add_executable(tbox_tests ${TBOX_TEST_SOURCES})

# The parser/tree-builder test groups exercise internals that aren't part of
# the public <tbox/tbox.h> API, so the test binary needs the src/ headers and
# must link against the static library specifically: a shared library built
# without explicit export declarations for internal symbols may not expose
# them on all platforms (notably Windows), whereas a static library always
# does.
target_include_directories(tbox_tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests ${PROJECT_SOURCE_DIR}/external/stb_image)

# test_font.c loads the vendored Liberation Sans Regular by path (same
# pattern as TBOX_EXAMPLE_HTML_PATH in example/CMakeLists.txt) rather than
# depending on Fontconfig or anything installed on the test machine.
target_compile_definitions(tbox_tests PRIVATE
    TBOX_TEST_LIBERATION_SANS_PATH="${PROJECT_SOURCE_DIR}/external/liberation-sans/LiberationSans-Regular.ttf")

# test_image.c decodes real fixtures already on disk (tests/assets/black.png/
# yellow.png, the same ones tests/assets/html-images.html's <img> regression fixture
# uses) rather than a synthetic image, so it needs an absolute base_dir to
# join against regardless of ctest's own working directory.
target_compile_definitions(tbox_tests PRIVATE
    TBOX_TEST_COMPLEX_SHAPING=$<BOOL:${TBOX_HARFBUZZ_FOUND}>
    TBOX_TEST_ASSETS_DIR="${PROJECT_SOURCE_DIR}/tests/assets")

if(TBOX_BUILD_STATIC)
    target_link_libraries(tbox_tests PRIVATE tbox_static tbox_warnings tbox_sanitizers)
else()
    target_link_libraries(tbox_tests PRIVATE tbox::tbox tbox_warnings tbox_sanitizers)
endif()

foreach(tbox_test_group arena list vector string number bidi debug html_parser_tokenizer html_parser_tree html_xpath css_parser_tokenizer css_parser_parser css_selector css_cascade css_cascade_named_color css_cascade_color_convert css_cascade_color_parse style font image layout render output_raster context container_queries)
    add_test(NAME ${tbox_test_group} COMMAND tbox_tests ${tbox_test_group})
endforeach()

# tbox_cmp (v10, see docs/dev/historico.md's "v10 -- tbox_cmp") captures each
# tests/assets/*.html fixture in headless Firefox and renders it via
# tbox_app_screenshot_from_files (v7), then compares the two with OpenCV --
# needs OpenCV and Fontconfig to build, Firefox to run. Wayland is unnecessary because screenshots render
# without a window. This tool remains optional and outside ctest.
if(TBOX_OPENCV_FOUND AND TBOX_FONTCONFIG_FOUND)
    add_executable(tbox_cmp tbox_cmp.cpp)

    if(TBOX_BUILD_STATIC)
        target_link_libraries(tbox_cmp PRIVATE tbox_static tbox_warnings tbox_sanitizers)
    else()
        target_link_libraries(tbox_cmp PRIVATE tbox::tbox tbox_warnings tbox_sanitizers)
    endif()

    # libtbox brings its own system dependencies (link-only from the static
    # archive), so only OpenCV is listed here.
    target_link_libraries(tbox_cmp PRIVATE ${OpenCV_LIBS})
    target_include_directories(tbox_cmp PRIVATE ${OpenCV_INCLUDE_DIRS})

    set_target_properties(tbox_cmp PROPERTIES OUTPUT_NAME tbox_cmp)

    # Deliberately NOT registered via add_test()/ctest -- the maintainer
    # wants this run manually, on demand (`./tbox_cmp [assets_dir]`), not
    # as part of the automated `ctest` run. Several assets deliberately
    # exercise HTML/CSS tbox doesn't implement yet (see docs/dev/historico.md's
    # v10 "Escopo") and are expected to report DIFERENTE today, which is
    # exactly the kind of result that shouldn't silently gate CI.
endif()

# Exercise the production event loop without Wayland or installed fonts.
# The explicit app objects supply their public symbols; the static archive
# supplies the rest of tbox, without extracting its real backend/app objects.
if(TBOX_FONTCONFIG_FOUND AND TBOX_BUILD_STATIC)
    add_executable(tbox_app_lifecycle_tests test_app_lifecycle.c
        ${PROJECT_SOURCE_DIR}/src/app/tbox_app.c
        ${PROJECT_SOURCE_DIR}/src/app/tbox_app_events.c)
    target_compile_definitions(tbox_app_lifecycle_tests PRIVATE
        TBOX_HAS_WINDOW_BACKEND=1 TBOX_TEST_ASSETS_DIR="${PROJECT_SOURCE_DIR}/tests/assets")
    target_include_directories(tbox_app_lifecycle_tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(tbox_app_lifecycle_tests PRIVATE tbox_static tbox_warnings tbox_sanitizers)
    add_test(NAME app_lifecycle COMMAND tbox_app_lifecycle_tests)
endif()

# Measuring tools (docs/12-ferramentas-e-testes.md): tbox_bench times a
# frame and its memory, tbox_frame_dump signs every frame of a set of
# documents to compare two builds. They read context internals (the frame
# arena, the box tree), hence the static library, and use the vendored
# font so results do not depend on the machine's fonts. ctest only checks
# that they still run; their numbers are read by hand.
if(TBOX_BUILD_STATIC)
    foreach(tbox_tool tbox_bench tbox_frame_dump)
        add_executable(${tbox_tool} ${tbox_tool}.c)
        target_include_directories(${tbox_tool} PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(${tbox_tool} PRIVATE
            TBOX_TOOL_FONT_PATH="${PROJECT_SOURCE_DIR}/external/liberation-sans/LiberationSans-Regular.ttf")
        target_link_libraries(${tbox_tool} PRIVATE tbox_static tbox_warnings tbox_sanitizers)
    endforeach()
    add_test(NAME tool_bench COMMAND tbox_bench --table 20 --frames 1)
    add_test(NAME tool_frame_dump COMMAND tbox_frame_dump --hover-step 300 ${CMAKE_CURRENT_SOURCE_DIR}/assets/html-tables-styled.html)
endif()
