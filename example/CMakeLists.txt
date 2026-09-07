add_executable(html_parser
    html_parser.c
)

target_link_libraries(html_parser
    PRIVATE tbox tbox_warnings tbox_sanitizers
)

target_compile_definitions(html_parser PRIVATE TBOX_EXAMPLE_HTML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/example.html")

set_target_properties(html_parser PROPERTIES OUTPUT_NAME html_parser)

add_executable(html_xpath
    html_xpath.c
)

target_link_libraries(html_xpath
    PRIVATE tbox tbox_warnings tbox_sanitizers
)

target_compile_definitions(html_xpath PRIVATE TBOX_EXAMPLE_HTML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/example.html")

set_target_properties(html_xpath PROPERTIES OUTPUT_NAME html_xpath)
