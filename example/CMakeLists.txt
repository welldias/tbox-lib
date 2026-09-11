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
