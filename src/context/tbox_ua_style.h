#ifndef TBOX_CONTEXT_UA_STYLE_H
#define TBOX_CONTEXT_UA_STYLE_H

#include <tbox/context.h>
#include <tbox/css_parser.h>

/* Internal factory. Each context owns its stylesheet, since config varies. */
tbox_css_stylesheet *tbox_ua_stylesheet_create(tbox_ua_style_config config);

#endif /* TBOX_CONTEXT_UA_STYLE_H */
