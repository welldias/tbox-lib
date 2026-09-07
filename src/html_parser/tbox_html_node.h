#ifndef TBOX_HTML_PARSER_NODE_H
#define TBOX_HTML_PARSER_NODE_H

#include <tbox/html_parser.h>

#include "base/tbox_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/* True if tag_name (already lowercase ASCII) names one of the HTML5 void
 * elements (area, base, br, col, embed, hr, img, input, link, meta, param,
 * source, track, wbr) -- elements that never have children and are never
 * pushed onto the tree builder's open-elements stack. */
bool tbox_html_is_void_element(tbox_string_view tag_name);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_HTML_PARSER_NODE_H */
