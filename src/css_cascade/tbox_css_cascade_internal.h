#ifndef TBOX_CSS_CASCADE_INTERNAL_H
#define TBOX_CSS_CASCADE_INTERNAL_H

#include <tbox/css_cascade.h>

#include "base/tbox_vector.h"

/* Builds `style`'s property filter from its current items, so
 * tbox_css_computed_style_find skips the scan for absent properties. Every
 * code that fills a tbox_css_computed_style calls it once the items are
 * final; adding or renaming items afterwards requires calling it again (a
 * stale filter would hide the new properties). Removing items, or leaving
 * the filter unbuilt, only costs speed. */
void tbox_css_computed_style_index(tbox_css_computed_style *style);

/* One evaluation of a cascade that read more than the element's and its
 * ancestors' tags and attributes (see tbox_css_selector_matches_traced):
 * a selector of `scope` (NULL: unscoped), or, `selector` NULL, a matched
 * rule's @media/@container chain `media`; and what it gave. */
typedef struct tbox_css_cascade_guard {
    const tbox_css_scope *scope;
    const tbox_css_selector *selector;
    const tbox_css_media_condition *media;
    bool matched;
    size_t hops;
} tbox_css_cascade_guard;

/* Every evaluation of one cascade that read more than its key, in order.
 * The caller initializes `guards` (tbox_css_cascade_guard) and sets
 * `complete`; a guard that could not be stored clears it. */
typedef struct tbox_css_cascade_trace {
    tbox_vector guards;
    bool complete;
} tbox_css_cascade_trace;

/* tbox_css_cascade_resolve_in_viewport that records its guards in `trace`
 * (NULL: none). Every other evaluation gives the same for any element with
 * the same tags and attributes up its ancestor chain, so such an element
 * for which the guards still hold (tbox_css_cascade_guards_hold) gets the
 * same result. */
tbox_css_computed_style tbox_css_cascade_resolve_traced(const tbox_css_cascade_source *sources, size_t source_count, const tbox_html_node *node, double viewport_width, double viewport_height, bool reduced_motion, const tbox_css_query_context *query, tbox_css_cascade_trace *trace);

/* Whether each guard gives for `node` (in `query`'s containers) what it
 * gave when recorded. */
bool tbox_css_cascade_guards_hold(const tbox_css_cascade_guard *guards, size_t count, const tbox_html_node *node, double viewport_width, double viewport_height, bool reduced_motion, const tbox_css_query_context *query);

#endif /* TBOX_CSS_CASCADE_INTERNAL_H */
