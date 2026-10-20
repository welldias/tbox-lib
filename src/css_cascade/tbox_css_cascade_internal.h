#ifndef TBOX_CSS_CASCADE_INTERNAL_H
#define TBOX_CSS_CASCADE_INTERNAL_H

#include <tbox/css_cascade.h>

/* Builds `style`'s property filter from its current items, so
 * tbox_css_computed_style_find skips the scan for absent properties. Every
 * code that fills a tbox_css_computed_style calls it once the items are
 * final; adding or renaming items afterwards requires calling it again (a
 * stale filter would hide the new properties). Removing items, or leaving
 * the filter unbuilt, only costs speed. */
void tbox_css_computed_style_index(tbox_css_computed_style *style);

#endif /* TBOX_CSS_CASCADE_INTERNAL_H */
