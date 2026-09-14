file(GLOB_RECURSE TBOX_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
add_executable(tbox_tests ${TBOX_TEST_SOURCES})

# The parser/tree-builder test groups exercise internals that aren't part of
# the public <tbox/tbox.h> API, so the test binary needs the src/ headers and
# must link against the static library specifically: a shared library built
# without explicit export declarations for internal symbols may not expose
# them on all platforms (notably Windows), whereas a static library always
# does.
target_include_directories(tbox_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/external/utf8.h)

# test_font.c loads the vendored Liberation Sans Regular by path (same
# pattern as TBOX_EXAMPLE_HTML_PATH in example/CMakeLists.txt) rather than
# depending on Fontconfig or anything installed on the test machine.
target_compile_definitions(tbox_tests PRIVATE
    TBOX_TEST_LIBERATION_SANS_PATH="${CMAKE_SOURCE_DIR}/external/liberation-sans/LiberationSans-Regular.ttf")

if(TBOX_BUILD_STATIC)
    target_link_libraries(tbox_tests PRIVATE tbox_static tbox_warnings tbox_sanitizers)
else()
    target_link_libraries(tbox_tests PRIVATE tbox tbox_warnings tbox_sanitizers)
endif()

foreach(tbox_test_group arena list vector string debug html_parser_tokenizer html_parser_tree html_xpath css_parser_tokenizer css_parser_parser css_selector css_cascade css_cascade_named_color css_cascade_color_convert css_cascade_color_parse style font)
    add_test(NAME ${tbox_test_group} COMMAND tbox_tests ${tbox_test_group})
endforeach()
