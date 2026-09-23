#include <tbox/layout.h>

#include <tbox/image.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"

/* v0's fallback style for an ELEMENT node missing from `styles` -- should
 * not normally happen (the Style layer resolves every ELEMENT node), but
 * matches tbox_style_resolve's own initial values exactly (display BLOCK,
 * width/height AUTO, margin/padding 0px, color opaque black, background
 * transparent, font-size 16px, font-weight not bold -- the last two NOVO
 * v2) rather than crashing on a defensive gap. */
static const tbox_style tbox_layout_default_style = {
    .display = TBOX_STYLE_DISPLAY_BLOCK,
    .width   = { TBOX_STYLE_LENGTH_AUTO, 0.0 },
    .height  = { TBOX_STYLE_LENGTH_AUTO, 0.0 },
    .margin  = {
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
    },
    .padding = {
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
    },
    .color            = { 0, 0, 0, 255 },
    .background_color = { 0, 0, 0, 0 },
    .font_size         = 16.0,
    .font_weight_bold  = false,
};

static const tbox_style *tbox_layout_style_or_default(const tbox_style_table *styles, const tbox_html_node *node) {
    const tbox_style *style = tbox_style_table_find(styles, node);
    return style != NULL ? style : &tbox_layout_default_style;
}

/* The fixed tag-name list that gets text-box treatment (see
 * ARCHITECTURE.md's "Layout Tree" section) -- checked by plain byte-exact
 * comparison since tbox_html_node tag names are already lowercase ASCII.
 * Unchanged since v0/v1: generalizing this to arbitrary containers (e.g. a
 * <div> with loose text) is explicitly out of scope for v2 too. NOVO
 * (table support): "td"/"th" added -- a table cell reuses this EXACT same
 * word-wrap/text-run machinery (tbox_layout_build_text_runs) as <p>/<li>,
 * with no new code of its own; only its CONTAINING BLOCK differs (a
 * column's own width, not its row's full width -- see
 * tbox_layout_build_table_row_children). Same pre-existing consequence
 * every other text tag already has: a table cell never builds child BOXES
 * of its own, so a <table> nested inside a <td>/<th> is silently dropped,
 * same as a <div> nested inside a <p> already is. */
static bool tbox_layout_is_text_tag(const tbox_html_node *node) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) {
        return false;
    }
    if (tbox_string_view_equal_cstr(node->element.tag_name, "input")) {
        const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
        return type == NULL || tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("text", 4));
    }

    static const char *const text_tags[] = { "h1", "h2", "h3", "h4", "h5", "h6", "p", "li", "pre", "td", "th", "button" };
    tbox_string_view tag_name            = node->element.tag_name;
    for (size_t i = 0; i < sizeof(text_tags) / sizeof(text_tags[0]); i++) {
        if (tbox_string_view_equal_cstr(tag_name, text_tags[i])) {
            return true;
        }
    }
    return false;
}

/* ---- NOVO v2: inline formatting context (word wrap + run merging) ---- */

/* One word from the flat, document-order (word, face) sequence a
 * text-bearing box's direct children contribute (see
 * tbox_layout_collect_words) -- `width`/`space_width` are pre-measured
 * (via tbox_font_measure_text, against `face`) so the greedy wrap pass
 * below never re-measures the same word twice. */
typedef struct tbox_layout_word {
    tbox_string_view text;
    const tbox_font_face *face;
    /* NOVO v13: the SAME tbox_style that already decided `face` above (and
     * ultimately becomes tbox_layout_text_run.style once this word is
     * merged into a run, see tbox_layout_build_line_runs below) -- populated
     * at the exact same call sites that already populate `face`, no new
     * "which style" decision. Never NULL: every pusher of a
     * tbox_layout_word (tbox_layout_push_words, tbox_layout_push_hard_break)
     * requires a non-NULL style, same as `face` already effectively required
     * a non-NULL cache lookup to have produced a word at all. */
    const tbox_style *style;
    double width;       /* tbox_font_measure_text(face, text), OR (image word) the resolved CSS/attribute content width */
    double space_width; /* tbox_font_measure_text(face, " ") -- the gap this word's face would render before it */

    /* An <img> word instead of a text word -- see tbox_layout_push_image_word.
     * NULL for every ordinary word (text or hard-break). `face`/`style`
     * above are still populated for an image word (the surrounding text
     * context's own face/style, purely for `space_width` measurement and
     * run-merge-boundary comparisons -- see tbox_layout_build_line_runs),
     * but `image`/`image_height` are what Layout/Render actually use to
     * size and paint it: `image` is the DECODED image (intrinsic pixel
     * dimensions + pixel data, owned by the tbox_image_cache), `width`
     * above and `image_height` are the RESOLVED CSS content box size for
     * this particular <img> (may differ from `image`'s own intrinsic
     * dimensions -- e.g. an explicit `style="width:600px;height:500px"` on
     * a smaller source image -- Output Display scales when painting, see
     * tbox_raster_image). */
    const tbox_image *image;
    double image_height;

    /* NOVO v11: true for an entry pushed by tbox_layout_push_hard_break --
     * a forced line break (<br>, or the boundary between two physical
     * lines inside <pre>), not a real word. `text`/`width`/`space_width`
     * are all zeroed for such an entry (see tbox_layout_push_hard_break);
     * `face` is still populated, kept only as a line-height fallback for a
     * blank line between two consecutive hard breaks (see
     * tbox_layout_break_lines). Every function that pushes a REAL word
     * (tbox_layout_push_words, tbox_layout_collect_preformatted_words) sets
     * this explicitly to false -- never left to tbox_vector's zero-init,
     * per ARCHITECTURE.md's "v11" section. */
    bool hard_break;
} tbox_layout_word;

/* A half-open range [start, end) into a tbox_layout_word array, one greedy
 * line's worth of words, plus that line's own height (max line-height among
 * the faces actually used by words in this range -- see ARCHITECTURE.md;
 * degenerates to "the one face's line-height" when every word on the line
 * shares a face, the common v2 case). */
typedef struct tbox_layout_line {
    size_t start, end;
    double height;
    /* NOVO v13: the max tbox_font_face_ascent among the faces used by words
     * in [start, end) -- same "max across the line" loop that already
     * computes `height` above (tbox_font_face_line_height), just with
     * tbox_font_face_ascent instead; see tbox_layout_break_lines's three
     * line-closing points. Used by tbox_layout_build_line_runs to align
     * every run's own baseline with this line's dominant one instead of
     * `line_y` (the line's TOP) directly -- see ARCHITECTURE.md's "v13 --
     * Layout Tree" section for why this is needed now (the first time this
     * project has more than one font size on the same line). */
    double ascent;
} tbox_layout_line;

/* Splits `collapsed` (already whitespace-collapsed: single ' ' separators,
 * no leading/trailing whitespace -- see tbox_string_collapse_whitespace) on
 * ' ' and pushes one tbox_layout_word per non-empty piece onto `words`, all
 * measured against `face`. A NULL `face` (tbox_font_face_cache_get failed
 * for this element's (bold, size) -- e.g. an unloadable font) contributes no
 * words at all rather than crashing on tbox_font_measure_text(NULL, ...). */
static void tbox_layout_push_words(tbox_vector *words, tbox_string_view collapsed, const tbox_font_face *face, const tbox_style *style) {
    if (face == NULL) {
        return;
    }

    static const tbox_string_view space = { " ", 1 };

    size_t i = 0;
    while (i < collapsed.size) {
        size_t start = i;
        while (i < collapsed.size && collapsed.data[i] != ' ') {
            i++;
        }

        tbox_string_view word = tbox_string_view_make(collapsed.data + start, i - start);
        if (word.size > 0) {
            tbox_layout_word *entry = (tbox_layout_word *)tbox_vector_push(words);
            entry->text             = word;
            entry->face             = face;
            entry->style            = style;
            entry->width            = tbox_font_measure_text(face, word);
            entry->space_width      = tbox_font_measure_text(face, space);
            entry->image            = NULL;
            entry->image_height     = 0.0;
            entry->hard_break       = false;
        }

        while (i < collapsed.size && collapsed.data[i] == ' ') {
            i++;
        }
    }
}

/* NOVO v11: pushes one tbox_layout_word marking a FORCED end of line --
 * `<br>` (see tbox_layout_collect_words below), or the boundary between two
 * physical lines inside a `<pre>` (see tbox_layout_collect_preformatted_words
 * below) -- reused unchanged by both callers, so the forced-break logic
 * lives in exactly one place. Carries no text (`{NULL, 0}`) and no width
 * (`width`/`space_width` both 0.0), so it never itself renders or advances a
 * line's measured width; `face` is kept purely as tbox_layout_break_lines's
 * line-height fallback for a blank line sandwiched between two consecutive
 * hard breaks (e.g. `<br><br>`), where the normal "max line-height among the
 * words in range" loop has nothing to iterate. See ARCHITECTURE.md's "v11 --
 * Layout Tree -- <br> (quebra forçada)". */
static void tbox_layout_push_hard_break(tbox_vector *words, const tbox_font_face *face, const tbox_style *style) {
    tbox_layout_word *entry = (tbox_layout_word *)tbox_vector_push(words);
    entry->text             = tbox_string_view_make(NULL, 0);
    entry->face             = face;
    entry->style            = style;
    entry->width            = 0.0;
    entry->space_width      = 0.0;
    entry->image            = NULL;
    entry->image_height     = 0.0;
    entry->hard_break       = true;
}

/* Pushes one tbox_layout_word for an <img> element (see
 * tbox_layout_collect_words below) -- carries decoded image data instead of
 * text, sized from CSS/HTML-attribute width/height (`img_style`, the img's
 * OWN resolved tbox_style -- already includes the `<img width/height>`
 * attribute fallback, see tbox_style_resolve_img_dimension_attribute in
 * src/style/tbox_style.c) falling back to the image's intrinsic pixel
 * dimensions on any axis left AUTO or PERCENT (percent is deliberately
 * treated the same as auto here -- resolving it against a containing-block
 * width would need `available_width` threaded all the way into word
 * collection for a case this project's own fixtures never exercise; see
 * ARCHITECTURE.md). A missing `src`, or a `src` tbox_image_cache_get can't
 * decode (bad path, corrupt/unsupported file), falls back to the `alt`
 * attribute's text -- pushed as an ORDINARY text word (tbox_layout_push_words,
 * in `img_style`'s own face/color) rather than an image word, same fallback
 * every real browser shows in place of a broken image (see
 * tests/assets/024.html's `notfound.png`, alt="Image not found" -- the
 * fixture this behavior was added to match). No broken-image ICON is drawn
 * (out of scope, no icon asset anywhere in this engine) -- text only. An
 * absent OR empty `alt` contributes nothing at all, matching a real
 * browser's own "no visible fallback for a purely decorative image"
 * behavior (`alt=""` is the standard way to mark an image decorative).
 *
 * `context_face` is the SURROUNDING text flow's own face (whatever
 * tbox_layout_collect_words would have used for an ordinary word at this
 * exact position) -- used for this word's `space_width` measurement in the
 * successful-decode case, and reused as the alt-text fallback's own face
 * too (an <img> has no font of its own to speak of; the surrounding
 * context's face is the closest sensible choice, and tbox_layout_push_words
 * already no-ops safely if it's NULL). `word->style` is set to `img_style`
 * (the img's OWN resolved style, not the surrounding context's) both for
 * the image case (purely so tbox_layout_build_line_runs' run-merge-boundary
 * `style` comparison still makes sense -- an image word never actually
 * merges with a neighbor regardless, see that function's `new_run` check)
 * and for the alt-text case (so the fallback text picks up the img
 * element's own resolved `color`, exactly like any other inline text
 * would). */
static void tbox_layout_push_image_word(tbox_arena *arena, const tbox_html_node *img_node, const tbox_style *img_style, const tbox_font_face *context_face, tbox_image_cache *images, tbox_vector *words) {
    const tbox_html_attribute *src_attr = tbox_html_node_get_attribute(img_node, tbox_string_view_make("src", 3));
    const tbox_image *image             = src_attr != NULL ? tbox_image_cache_get(images, src_attr->value) : NULL;

    if (image == NULL) {
        const tbox_html_attribute *alt_attr = tbox_html_node_get_attribute(img_node, tbox_string_view_make("alt", 3));
        if (alt_attr != NULL && alt_attr->value.size > 0) {
            tbox_string_view collapsed = tbox_string_collapse_whitespace(arena, alt_attr->value);
            tbox_layout_push_words(words, collapsed, context_face, img_style);
        }
        return;
    }

    double width  = img_style->width.kind == TBOX_STYLE_LENGTH_PX ? img_style->width.value : (double)image->width;
    double height = img_style->height.kind == TBOX_STYLE_LENGTH_PX ? img_style->height.value : (double)image->height;

    tbox_layout_word *entry = (tbox_layout_word *)tbox_vector_push(words);
    entry->text             = tbox_string_view_make(NULL, 0);
    entry->face             = context_face;
    entry->style             = img_style;
    entry->width             = width;
    entry->space_width       = context_face != NULL ? tbox_font_measure_text(context_face, tbox_string_view_make(" ", 1)) : 0.0;
    entry->image              = image;
    entry->image_height       = height;
    entry->hard_break          = false;
}

/* True if `node` (an ELEMENT) has an `<img>` among its OWN direct children
 * -- used by tbox_layout_collect_words below to decide whether an inline
 * wrapper (e.g. `<a>`) needs its narrow one-level img-in-inline handling
 * instead of the ordinary flatten-to-text fast path. */
static bool tbox_layout_has_img_child(const tbox_html_node *node) {
    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        if (child->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(child->element.tag_name, "img")) {
            return true;
        }
    }
    return false;
}

/* Walks the sibling range [first_sibling, end_exclusive) in document order
 * (see ARCHITECTURE.md's algorithm): a TEXT child contributes its own words
 * in `style`'s own face (the h1-h6/p element itself, or -- NOVO v14 -- the
 * container style synthesized for an anonymous box); an `<img>` child
 * contributes one image word (tbox_layout_push_image_word); an ELEMENT
 * child whose OWN resolved style has display == INLINE recurses one level
 * -- via tbox_html_node_text_content, which already folds any further
 * nesting away -- contributing ITS whole text in ITS OWN face (this is how
 * e.g. a <b> renders bold within a regular <p>), UNLESS that inline child
 * itself directly contains an `<img>` (tbox_layout_has_img_child, e.g.
 * `<a href="..."><img ...></a>`), in which case ITS direct children are
 * walked instead with the same TEXT/`<img>` handling one level down (any
 * OTHER element nested in there is skipped, not flattened, not recursed
 * further -- a narrow, bounded extension of the "one level of inline
 * nesting" scope already established, never a general deep-nesting
 * mechanism). Any other child (a BLOCK/NONE element, COMMENT, DOCTYPE) is
 * skipped entirely -- no box, no text, no recursion.
 *
 * NOVO v14: generalized from always iterating `node->first_child` to NULL to
 * an explicit sibling range, so the SAME word-collection mechanism can be
 * reused for an anonymous box's [run_start, run_end) interval (see
 * tbox_layout_build_anonymous_box below) instead of always a whole node's
 * children. The only pre-v14 call site (inside tbox_layout_build_text_runs,
 * for a real text-tag element) passes (node->first_child, NULL, ...) --
 * behavior identical to before. */
static void tbox_layout_collect_words(tbox_arena *arena, const tbox_html_node *first_sibling, const tbox_html_node *end_exclusive, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_vector *words) {
    for (const tbox_html_node *child = first_sibling; child != end_exclusive; child = child->next_sibling) {
        /* NOVO v11: a <br> child forces a line break -- checked BEFORE the
         * TEXT branch below and independently of the `display == INLINE`
         * gate an ELEMENT child otherwise needs (see ARCHITECTURE.md): <br>
         * does not need `display: inline` for this to work. Uses the
         * text-bearing element's OWN face (`style`, not a child style --
         * <br> has no style of its own worth resolving here), same call the
         * TEXT branch below already makes. */
        if (child->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(child->element.tag_name, "br")) {
            const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
            tbox_layout_push_hard_break(words, face, style);
            continue;
        }

        if (child->type == TBOX_HTML_NODE_TEXT) {
            tbox_string_view collapsed = tbox_string_collapse_whitespace(arena, child->text.text);
            const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
            tbox_layout_push_words(words, collapsed, face, style);
        } else if (child->type == TBOX_HTML_NODE_ELEMENT) {
            const tbox_style *child_style = tbox_layout_style_or_default(styles, child);
            if (tbox_string_view_equal_cstr(child->element.tag_name, "img")) {
                const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
                tbox_layout_push_image_word(arena, child, child_style, face, images, words);
            } else if (child_style->display == TBOX_STYLE_DISPLAY_INLINE) {
                if (tbox_layout_has_img_child(child)) {
                    for (const tbox_html_node *grandchild = child->first_child; grandchild != NULL; grandchild = grandchild->next_sibling) {
                        if (grandchild->type == TBOX_HTML_NODE_TEXT) {
                            tbox_string_view collapsed = tbox_string_collapse_whitespace(arena, grandchild->text.text);
                            const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(child_style->font_family), child_style->font_weight_bold, child_style->font_italic, child_style->font_size);
                            tbox_layout_push_words(words, collapsed, face, child_style);
                        } else if (grandchild->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(grandchild->element.tag_name, "img")) {
                            const tbox_style *img_style = tbox_layout_style_or_default(styles, grandchild);
                            const tbox_font_face *face  = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(child_style->font_family), child_style->font_weight_bold, child_style->font_italic, child_style->font_size);
                            tbox_layout_push_image_word(arena, grandchild, img_style, face, images, words);
                        }
                        /* else: skipped -- bounded one-level extension, no deeper nesting */
                    }
                } else {
                    tbox_string_view raw       = tbox_html_node_text_content(arena, child);
                    tbox_string_view collapsed = tbox_string_collapse_whitespace(arena, raw);
                    const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(child_style->font_family), child_style->font_weight_bold, child_style->font_italic, child_style->font_size);
                    tbox_layout_push_words(words, collapsed, face, child_style);
                }
            }
        }
        /* else: COMMENT/DOCTYPE, or an ELEMENT that isn't display:inline/img --
         * contributes nothing, same as v0's "not a text tag" treatment. */
    }
}

/* Greedy, per-word line breaking (CSS `overflow-wrap: normal` -- never
 * breaks mid-word, see ARCHITECTURE.md): accumulates words onto the current
 * line (measuring each one's own advance + a preceding space, both already
 * cached on the word) until the next word wouldn't fit within
 * `available_width`; then closes the line and starts a new one. A single
 * word wider than `available_width` still lands alone on its own line
 * (`i == line_start`, so the fit check is skipped) rather than being force-
 * split -- it simply overflows visually, same policy box width already has
 * for content that's too wide (see ARCHITECTURE.md's D4). Pushes one
 * tbox_layout_line per line onto `lines`, each carrying its own height
 * (the max line-height among the faces used by the words in its range).
 *
 * NOVO v11: `no_wrap` (true for `<pre>`, see
 * tbox_layout_collect_preformatted_words/tbox_layout_build_text_runs) turns
 * off the width-based fit check entirely -- `<pre>` never wraps by width
 * (CSS `white-space: pre`, not `pre-wrap`), only at an explicit hard break.
 * Independently of `no_wrap`, any word with `hard_break == true` (see
 * tbox_layout_push_hard_break above -- `<br>`, or a `<pre>` physical-line
 * boundary) unconditionally closes the current line right there, checked at
 * the TOP of the loop body, before the width fit check even runs: the
 * current line's range is `[line_start, i)` (the hard break itself is never
 * part of any line's word range, on either side); when that range is EMPTY
 * (`i == line_start` -- this break immediately follows another break, e.g.
 * `<br><br>`, or is the very first word), the normal "max line-height among
 * the words in range" loop has nothing to iterate, so
 * `tbox_font_face_line_height(words[i].face)` -- the face
 * tbox_layout_push_hard_break stashed on the break itself purely for this
 * purpose -- is used instead, so a blank line between two consecutive
 * breaks still occupies roughly one line's height rather than 0. The push
 * of the FINAL line, after the loop, is now guarded by
 * `line_start < word_count`: without it, a hard break sitting at the very
 * end of the words (`"texto<br>"`, nothing after) would unconditionally
 * push one more, phantom, empty line that no real browser shows -- the
 * guard means a final line is only pushed when real content actually
 * follows the last break. */
/* An image word's "line-height"/"ascent" contribution, wherever
 * tbox_layout_break_lines would otherwise call tbox_font_face_line_height/
 * tbox_font_face_ascent(word->face): an image, at the default
 * `vertical-align: baseline`, sits with its BOTTOM edge exactly on the
 * line's shared baseline (no descent below it) -- so its own resolved
 * height (`word->image_height`) is both its ascent AND its line-height
 * contribution, unlike a font face's ascent/line-height (which differ by
 * the descent + any line gap). This is exact, not an approximation, for
 * the one vertical-align this project supports on a replaced element. */
static double tbox_layout_word_line_height(const tbox_layout_word *word) {
    return word->image != NULL ? word->image_height : tbox_font_face_line_height(word->face);
}

static double tbox_layout_word_ascent(const tbox_layout_word *word) {
    return word->image != NULL ? word->image_height : tbox_font_face_ascent(word->face);
}

static void tbox_layout_break_lines(const tbox_layout_word *words, size_t word_count, double available_width, bool no_wrap, tbox_vector *lines) {
    if (word_count == 0) {
        return;
    }

    size_t line_start = 0;
    double line_width = 0.0;

    for (size_t i = 0; i < word_count; i++) {
        if (words[i].hard_break) {
            double height = 0.0;
            double ascent = 0.0;
            for (size_t j = line_start; j < i; j++) {
                double face_height = tbox_layout_word_line_height(&words[j]);
                if (face_height > height) {
                    height = face_height;
                }
                double face_ascent = tbox_layout_word_ascent(&words[j]);
                if (face_ascent > ascent) {
                    ascent = face_ascent;
                }
            }
            if (i == line_start) {
                height = tbox_layout_word_line_height(&words[i]);
                ascent = tbox_layout_word_ascent(&words[i]);
            }

            tbox_layout_line *line = (tbox_layout_line *)tbox_vector_push(lines);
            line->start            = line_start;
            line->end              = i;
            line->height           = height;
            line->ascent           = ascent;

            line_start = i + 1;
            line_width = 0.0;
            continue;
        }

        double prospective = (i == line_start) ? words[i].width : line_width + words[i].space_width + words[i].width;

        if (!no_wrap && i > line_start && prospective > available_width) {
            double height = 0.0;
            double ascent = 0.0;
            for (size_t j = line_start; j < i; j++) {
                double face_height = tbox_layout_word_line_height(&words[j]);
                if (face_height > height) {
                    height = face_height;
                }
                double face_ascent = tbox_layout_word_ascent(&words[j]);
                if (face_ascent > ascent) {
                    ascent = face_ascent;
                }
            }

            tbox_layout_line *line = (tbox_layout_line *)tbox_vector_push(lines);
            line->start            = line_start;
            line->end              = i;
            line->height           = height;
            line->ascent           = ascent;

            line_start = i;
            line_width = words[i].width;
        } else {
            line_width = prospective;
        }
    }

    if (line_start < word_count) {
        double height = 0.0;
        double ascent = 0.0;
        for (size_t j = line_start; j < word_count; j++) {
            double face_height = tbox_layout_word_line_height(&words[j]);
            if (face_height > height) {
                height = face_height;
            }
            double face_ascent = tbox_layout_word_ascent(&words[j]);
            if (face_ascent > ascent) {
                ascent = face_ascent;
            }
        }
        tbox_layout_line *line = (tbox_layout_line *)tbox_vector_push(lines);
        line->start            = line_start;
        line->end              = word_count;
        line->height           = height;
        line->ascent           = ascent;
    }
}

/* NOVO v13: the extra vertical offset (px) `vertical_align: sub`/`super`
 * adds ON TOP OF baseline alignment (see tbox_layout_build_line_runs below)
 * -- `0.0` for the initial BASELINE, the universal case through v12 (zero
 * visual change). `0.15`/`0.35` are fixed fractions of `style->font_size`,
 * an approximation by common visual convention (browsers' usual order of
 * magnitude), not a real OpenType `subs`/`sups` table lookup -- out of scope,
 * see ARCHITECTURE.md's v13 "Fora de escopo". Positive moves DOWN (`sub`),
 * negative moves UP (`super`), matching this project's y-down coordinate
 * space. */
static double tbox_layout_vertical_align_offset(const tbox_style *style) {
    switch (style->vertical_align) {
    case TBOX_STYLE_VERTICAL_ALIGN_SUB:
        return 0.15 * style->font_size;
    case TBOX_STYLE_VERTICAL_ALIGN_SUPER:
        return -0.35 * style->font_size;
    case TBOX_STYLE_VERTICAL_ALIGN_BASELINE:
    default:
        return 0.0;
    }
}

/* Places and merges one line's words into runs, appending them to `runs`.
 * `line_y` is this line's already-computed absolute top (content_y plus
 * every earlier line's height); `content_x` is the text box's content-box
 * left edge. Consecutive words sharing the exact same face AND style
 * (NOVO v13 -- previously face alone) merge into one tbox_layout_text_run
 * (their text joined by single spaces, matching
 * tbox_string_collapse_whitespace's own separator); a new run starts when
 * EITHER changes (a line boundary is handled by the caller looping per
 * line, never straddled by a single run) -- so e.g. a `<mark>` run never
 * merges with an adjacent plain-text run even when both resolve to the
 * IDENTICAL face (no weight/size/family/italic difference declared), since
 * they still need separate `tbox_layout_text_run.style` pointers for
 * Render Pipeline's per-run background/decoration (see ARCHITECTURE.md's
 * "v13 -- Layout Tree" section). The gap between two adjacent runs (the
 * space that would sit between them in the source text) is accounted for in
 * each run's absolute x position but deliberately belongs to neither run's
 * own text/width -- unchanged since before v13.
 *
 * NOVO v13: `rect.y` is no longer `line_y` alone -- every run's baseline is
 * aligned with the LINE's dominant baseline first (`line->ascent -
 * tbox_font_face_ascent(run_face)`, zero when every run on the line shares
 * one face/size, the universal case through v12), then shifted further by
 * `tbox_layout_vertical_align_offset` for `sub`/`super` (zero for the
 * initial BASELINE). `rect.height` still spans the WHOLE line, not the
 * run's own reduced size -- a deliberate simplification, see
 * ARCHITECTURE.md. `run->style` is set to the SAME style that decided
 * `run_face`, for the reasons above. */
static void tbox_layout_build_line_runs(tbox_arena *arena, const tbox_layout_word *words, const tbox_layout_line *line, double line_y, double content_x, tbox_vector *runs) {
    double cursor_x                  = 0.0;
    double run_start_x               = 0.0;
    double run_end_x                 = 0.0;
    const tbox_font_face *run_face   = NULL;
    const tbox_style *run_style      = NULL;
    /* An image word (see tbox_layout_push_image_word) always closes its own
     * singleton run -- `run_is_image`/`run_image`/`run_image_height` are
     * only ever set alongside `run_face`/`run_style` below, at a run's
     * opening word, and read back at that SAME run's closing point. */
    bool run_is_image           = false;
    const tbox_image *run_image = NULL;
    double run_image_height     = 0.0;
    tbox_string_builder run_builder;
    bool have_run = false;

    for (size_t i = line->start; i < line->end; i++) {
        const tbox_layout_word *word = &words[i];

        if (i != line->start) {
            cursor_x += word->space_width;
        }

        /* An image word never merges with a neighbor -- forced by
         * `word->image != NULL` (opening one) OR `run_is_image` (the
         * currently-open run already is one, so THIS word, whatever it is,
         * must start a fresh run) -- same "never merges" treatment a
         * `<mark>` word already gets today via the `style` mismatch, just
         * unconditional here since an image's face/style are otherwise
         * ordinary values that could otherwise coincidentally match. */
        bool new_run = !have_run || word->face != run_face || word->style != run_style || word->image != NULL || run_is_image;
        if (new_run) {
            if (have_run) {
                tbox_layout_text_run *run = (tbox_layout_text_run *)tbox_vector_push(runs);
                run->rect.x               = content_x + run_start_x;
                double ascent             = run_is_image ? run_image_height : tbox_font_face_ascent(run_face);
                run->rect.y               = line_y + (line->ascent - ascent) + tbox_layout_vertical_align_offset(run_style);
                run->rect.width           = run_end_x - run_start_x;
                run->rect.height          = run_is_image ? run_image_height : line->height;
                run->text                 = tbox_string_builder_finish(&run_builder);
                run->font                 = run_face;
                run->style                = run_style;
                run->image                = run_is_image ? run_image : NULL;
            }

            run_start_x      = cursor_x;
            run_face         = word->face;
            run_style        = word->style;
            run_is_image     = word->image != NULL;
            run_image        = word->image;
            run_image_height = word->image_height;
            tbox_string_builder_init(&run_builder, arena, word->text.size + 8);
            have_run = true;
        } else {
            tbox_string_builder_append_byte(&run_builder, ' ');
        }

        tbox_string_builder_append_view(&run_builder, word->text);
        cursor_x += word->width;
        run_end_x = cursor_x;
    }

    if (have_run) {
        tbox_layout_text_run *run = (tbox_layout_text_run *)tbox_vector_push(runs);
        run->rect.x               = content_x + run_start_x;
        double ascent             = run_is_image ? run_image_height : tbox_font_face_ascent(run_face);
        run->rect.y               = line_y + (line->ascent - ascent) + tbox_layout_vertical_align_offset(run_style);
        run->rect.width           = run_end_x - run_start_x;
        run->rect.height          = run_is_image ? run_image_height : line->height;
        run->text                 = tbox_string_builder_finish(&run_builder);
        run->font                 = run_face;
        run->style                = run_style;
        run->image                = run_is_image ? run_image : NULL;
    }
}

/* NOVO v8: prepends the <li> marker word (bullet or number), if any, as the
 * FIRST entry of `words` -- called before tbox_layout_collect_words so the
 * marker always lands ahead of the <li>'s own text. A no-op unless `node` is
 * an ELEMENT `<li>` whose DIRECT parent is an ELEMENT `<ul>` or `<ol>` (see
 * ARCHITECTURE.md "v8 -- Escopo": the marker kind is decided by the parent's
 * tag name alone, never a CSS property, and a <li> outside <ul>/<ol> gets no
 * marker at all). Reuses tbox_layout_push_words for the actual push --
 * measuring/word-splitting the marker exactly like any other word, so it
 * gets the same space_width/line-break treatment as real text, with no
 * duplicated tbox_font_measure_text call here. */
static void tbox_layout_push_list_marker(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_vector *words) {
    if (node->type != TBOX_HTML_NODE_ELEMENT || !tbox_string_view_equal_cstr(node->element.tag_name, "li")) {
        return;
    }

    const tbox_html_node *parent = node->parent;
    if (parent == NULL || parent->type != TBOX_HTML_NODE_ELEMENT) {
        return;
    }

    bool parent_is_ul = tbox_string_view_equal_cstr(parent->element.tag_name, "ul");
    bool parent_is_ol = tbox_string_view_equal_cstr(parent->element.tag_name, "ol");
    if (!parent_is_ul && !parent_is_ol) {
        return;
    }

    /* The marker always uses the <li>'s OWN face -- never a nested <b>/<em>'s
     * -- same call tbox_layout_collect_words already makes for the <li>'s
     * direct TEXT children. */
    const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
    if (face == NULL) {
        return;
    }

    if (parent_is_ul) {
        static const tbox_string_view bullet = { "\xE2\x80\xA2", 3 };
        tbox_layout_push_words(words, bullet, face, style);
        return;
    }

    /* <ol>: count this <li>'s direct <li> siblings (same parent), in
     * document order, up to and including `node` itself -- a plain 1-based
     * position, never restarting across sibling groups. */
    size_t index = 0;
    for (const tbox_html_node *sibling = parent->first_child; sibling != NULL; sibling = sibling->next_sibling) {
        if (sibling->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(sibling->element.tag_name, "li")) {
            index++;
        }
        if (sibling == node) {
            break;
        }
    }

    char buffer[24];
    int written = snprintf(buffer, sizeof(buffer), "%zu.", index);
    if (written <= 0) {
        return;
    }
    size_t length = (size_t)written < sizeof(buffer) ? (size_t)written : sizeof(buffer) - 1;

    /* `tbox_layout_word.text` must point at memory that outlives this call
     * (the rest of the frame) -- `buffer` is a stack array, so the formatted
     * number is copied into `arena` before becoming a tbox_string_view. */
    char *copy = (char *)tbox_arena_alloc(arena, length);
    memcpy(copy, buffer, length);

    tbox_string_view number = tbox_string_view_make(copy, length);
    tbox_layout_push_words(words, number, face, style);
}

/* NOVO v11: `<pre>`'s own word-collection function, called by
 * tbox_layout_build_text_runs INSTEAD of tbox_layout_collect_words (never
 * both -- <pre> has no list marker and no normal word-splitting; see
 * ARCHITECTURE.md "v11 -- Layout Tree -- <pre> (texto verbatim)"). Pulls
 * `node`'s ENTIRE flattened text via tbox_html_node_text_content (same
 * function any nested-inline text tag already uses -- entities still
 * decode, only structure/nested faces are lost, an accepted simplification,
 * see ARCHITECTURE.md's "Escopo") WITHOUT tbox_string_collapse_whitespace:
 * internal whitespace (runs of spaces, tabs) is preserved byte-for-byte.
 * Scans that text byte by byte for '\n': each physical line (the bytes
 * between two consecutive '\n's, or between the start/end and the nearest
 * '\n') becomes exactly ONE tbox_layout_word -- deliberately NOT split on
 * ' ' via tbox_layout_push_words, which is the whole point of `<pre>`: a run
 * of spaces inside one physical line must stay literal within that one
 * word's own text, never treated as separate word boundaries. Between two
 * physical lines, pushes a hard break (tbox_layout_push_hard_break, the
 * exact same function `<br>` uses -- no separate forced-break logic here).
 * A NULL `face` (unloadable font, same guard tbox_layout_push_words already
 * has) contributes no words at all rather than crashing on
 * tbox_font_measure_text(NULL, ...). */
static void tbox_layout_collect_preformatted_words(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_vector *words) {
    const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
    if (face == NULL) {
        return;
    }

    static const tbox_string_view space = { " ", 1 };
    double space_width                  = tbox_font_measure_text(face, space);

    tbox_string_view text = tbox_html_node_text_content(arena, node);

    size_t line_start = 0;
    for (size_t i = 0; i <= text.size; i++) {
        bool at_break = (i == text.size) || (text.data[i] == '\n');
        if (!at_break) {
            continue;
        }

        tbox_string_view line = tbox_string_view_make(text.data + line_start, i - line_start);

        tbox_layout_word *entry = (tbox_layout_word *)tbox_vector_push(words);
        entry->text             = line;
        entry->face             = face;
        entry->style            = style;
        entry->width            = tbox_font_measure_text(face, line);
        entry->space_width      = space_width;
        entry->image            = NULL;
        entry->image_height     = 0.0;
        entry->hard_break       = false;

        if (i < text.size) {
            /* A real '\n' (not the end-of-text sentinel iteration) -- more
             * physical lines follow, so close this one with a hard break. */
            tbox_layout_push_hard_break(words, face, style);
        }

        line_start = i + 1;
    }
}

/* Builds `box`'s text_runs/text_run_count (a leaf box, one of the fixed
 * text tags) and returns its content-box height: the sum of every line's
 * height (NOVO v2 -- replaces v0/v1's single fixed line height), or, for a
 * box whose collected words come out empty (no words at all -- an empty tag
 * after whitespace-collapsing, or every child skipped), the box's OWN
 * face's line-height alone, matching v0/v1's choice to never collapse an
 * empty text box's height to 0.
 *
 * NOVO v11: `<pre>` (`is_preformatted`) takes a whole separate word-
 * collection path (tbox_layout_collect_preformatted_words, never
 * tbox_layout_push_list_marker + tbox_layout_collect_words -- <pre> has no
 * list marker and no normal word/whitespace collapsing to speak of) and is
 * passed to tbox_layout_break_lines as `no_wrap` (only `<pre>` disables
 * width-based wrapping; every other text tag keeps wrapping exactly as
 * before). Also NOVO v11: once a line's runs are built, when
 * `style->text_align` isn't the initial LEFT, that line's just-pushed runs
 * (the `[runs_before, runs_after)` range -- tbox_layout_build_line_runs
 * itself never shifts anything, see ARCHITECTURE.md "v11 -- Layout Tree --
 * aplicação de text-align") are shifted right by the line's leftover
 * width, split evenly for CENTER or given wholly to the left gap for RIGHT
 * -- computed straight from the LAST run just pushed (`rect.x + rect.width`
 * minus `content_x` is exactly this line's rendered width, since runs on
 * one line are laid out left-to-right with no gaps between them and
 * `content_x`). A negative/zero offset (an overflowing line, wider than
 * `available_width`) is left alone -- same "never shift left" policy D4
 * already has for overflow.
 *
 * NOVO v14: `node` may now be NULL (an anonymous box, see
 * tbox_layout_build_anonymous_box -- no real element to speak of), in which
 * case `is_preformatted` is forced false (a <pre> tag can't exist without a
 * node) and tbox_layout_push_list_marker is skipped entirely (a <li> marker
 * makes no sense without a real <li> node either). `first_sibling`/
 * `end_exclusive` replace the implicit `node->first_child`/NULL range
 * tbox_layout_collect_words used to derive on its own -- the only pre-v14
 * call site (inside tbox_layout_build_element, for a real text-tag element)
 * passes (node, node->first_child, NULL, ...), behavior identical to
 * before. */
static double tbox_layout_build_text_runs(tbox_arena *arena, const tbox_html_node *node, const tbox_html_node *first_sibling, const tbox_html_node *end_exclusive, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double content_y, double available_width, tbox_layout_box *box) {
    bool is_preformatted = node != NULL && tbox_string_view_equal_cstr(node->element.tag_name, "pre");
    bool is_input = node != NULL && tbox_string_view_equal_cstr(node->element.tag_name, "input");

    tbox_vector words;
    tbox_vector_init(&words, arena, sizeof(tbox_layout_word), 0);
    if (is_input) {
        const tbox_html_attribute *value = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
        const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
        if (value != NULL && value->value.size > 0 && face != NULL) {
            tbox_layout_word *word = (tbox_layout_word *)tbox_vector_push(&words);
            word->text = value->value;
            word->face = face;
            word->style = style;
            word->width = tbox_font_measure_text(face, value->value);
            word->space_width = 0.0;
            word->image = NULL;
            word->image_height = 0.0;
            word->hard_break = false;
        }
    } else if (is_preformatted) {
        tbox_layout_collect_preformatted_words(arena, node, style, fonts, &words);
    } else {
        if (node != NULL) {
            tbox_layout_push_list_marker(arena, node, style, fonts, &words);
        }
        tbox_layout_collect_words(arena, first_sibling, end_exclusive, style, styles, fonts, images, &words);
    }

    size_t word_count = tbox_vector_length(&words);
    if (word_count == 0) {
        box->text_runs      = NULL;
        box->text_run_count = 0;

        const tbox_font_face *own_face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
        return own_face != NULL ? tbox_font_face_line_height(own_face) : 0.0;
    }

    const tbox_layout_word *word_items = (const tbox_layout_word *)words.data;

    tbox_vector lines;
    tbox_vector_init(&lines, arena, sizeof(tbox_layout_line), 0);
    tbox_layout_break_lines(word_items, word_count, available_width, is_preformatted || is_input, &lines);

    tbox_vector runs;
    tbox_vector_init(&runs, arena, sizeof(tbox_layout_text_run), 0);

    size_t line_count                  = tbox_vector_length(&lines);
    const tbox_layout_line *line_items = (const tbox_layout_line *)lines.data;

    double cumulative_y = content_y;
    double total_height = 0.0;
    for (size_t li = 0; li < line_count; li++) {
        const tbox_layout_line *line = &line_items[li];

        size_t runs_before = tbox_vector_length(&runs);
        tbox_layout_build_line_runs(arena, word_items, line, cumulative_y, content_x, &runs);
        size_t runs_after = tbox_vector_length(&runs);

        if (style->text_align != TBOX_STYLE_TEXT_ALIGN_LEFT && runs_after > runs_before) {
            tbox_layout_text_run *run_items = (tbox_layout_text_run *)runs.data;
            const tbox_layout_text_run *last_run = &run_items[runs_after - 1];
            double line_width = (last_run->rect.x + last_run->rect.width) - content_x;

            double offset = (style->text_align == TBOX_STYLE_TEXT_ALIGN_CENTER) ? (available_width - line_width) / 2.0 : (available_width - line_width);

            if (offset > 0.0) {
                for (size_t ri = runs_before; ri < runs_after; ri++) {
                    run_items[ri].rect.x += offset;
                }
            }
        }

        cumulative_y += line->height;
        total_height += line->height;
    }

    box->text_runs      = (tbox_layout_text_run *)runs.data;
    box->text_run_count = runs.length;
    return total_height;
}

/* ---- Block formatting context (unchanged in spirit since v0, now
 * threading a tbox_font_face_cache instead of a single tbox_font_face) ---- */

/* A margin/padding component ("length"). Percentages on margin and padding
 * -- on every side, including top/bottom -- always resolve against the
 * containing block's WIDTH per CSS2.1 10.2/8.3 (not its height); that's why
 * every call site below passes the same `percent_base` regardless of which
 * edge it's resolving. AUTO is treated as 0: v0 doesn't implement CSS2.1's
 * auto-margin resolution (used for centering an over-constrained box, see
 * 10.3.3), and padding never has a meaningful "auto" in real CSS anyway. */
static double tbox_layout_resolve_edge(tbox_style_length length, double percent_base) {
    switch (length.kind) {
    case TBOX_STYLE_LENGTH_PX:
        return length.value;
    case TBOX_STYLE_LENGTH_PERCENT:
        return length.value / 100.0 * percent_base;
    case TBOX_STYLE_LENGTH_AUTO:
    default:
        return 0.0;
    }
}

/* NOVO v4: resolves one (primary, opposite) pair of `position: relative`
 * offsets per CSS2.1 9.4.3 -- a non-AUTO primary side wins outright; else a
 * non-AUTO opposite side wins, negated (moving by `-opposite` is exactly
 * equivalent to moving by `+primary` when only one side is given); else 0
 * (both AUTO, the common case: `position: relative` with no offset at all
 * moves nothing). Called once for (left, right) against the container's
 * width (always definite) and once for (top, bottom) against its height,
 * where `percent_base_definite` guards against resolving a `%` against an
 * indefinite (AUTO-height) container -- same guard `height: %` already uses
 * in tbox_layout_build_element below -- falling back to 0 instead of
 * producing a bogus/NaN offset. A PX side is never affected by
 * `percent_base_definite` since it doesn't depend on `percent_base` at all. */
static double tbox_layout_resolve_offset(tbox_style_length primary, tbox_style_length opposite, double percent_base, bool percent_base_definite) {
    if (primary.kind != TBOX_STYLE_LENGTH_AUTO) {
        if (primary.kind == TBOX_STYLE_LENGTH_PERCENT && !percent_base_definite) {
            return 0.0;
        }
        return tbox_layout_resolve_edge(primary, percent_base);
    }

    if (opposite.kind != TBOX_STYLE_LENGTH_AUTO) {
        if (opposite.kind == TBOX_STYLE_LENGTH_PERCENT && !percent_base_definite) {
            return 0.0;
        }
        return -tbox_layout_resolve_edge(opposite, percent_base);
    }

    return 0.0;
}

/* The containing block a box is laid out against: the parent's (or the
 * viewport's, for the root) content-box x/width, plus its content height --
 * `height_definite` says whether that height came from an explicit value
 * (PX, or PERCENT against an already-definite container) rather than from
 * AUTO/shrink-to-fit, which is what CSS2.1 10.5 needs to decide whether a
 * child's own `height: %` resolves or itself falls back to AUTO. For a FLOW
 * container the vertical position is instead threaded through explicitly as
 * a running cursor (see tbox_layout_build_children), since siblings advance
 * it independently of anything about the containing block itself -- that is
 * why `y` was absent through v4. NOVO v5: `y` is added back because
 * `absolute`/`fixed` children are NOT placed via a cursor at all -- their
 * containing block is a concrete rect (the nearest positioned ancestor's
 * padding_box, or the viewport -- see tbox_layout_positioned_context below)
 * whose origin is needed on both axes at once. A flow container's `y` is
 * simply left at its default 0.0 and never read. */
typedef struct tbox_layout_containing_block {
    double x;
    double y;
    double width;
    double height;
    bool height_definite;
} tbox_layout_containing_block;

/* NOVO v5: rastreia, durante a recursão top-down da Layout Tree, contra o
 * que um descendente `absolute`/`fixed` deve se posicionar. `nearest_ancestor`
 * é o padding_box do ancestral posicionado mais próximo (relative/absolute/
 * fixed/sticky) já visitado -- ou o viewport inteiro, se nenhum ancestral
 * posicionado existir ainda (mesma regra do CSS: sem ancestral posicionado, o
 * containing block é o initial containing block). `viewport` é sempre o
 * viewport original, nunca atualizado pela recursão -- é o que
 * `position: fixed` usa incondicionalmente, ignorando qualquer
 * `nearest_ancestor` que exista. Ver ARCHITECTURE.md "v5 -- Layout Tree --
 * containing block posicionado". Passado por valor através da recursão
 * (mesmo padrão de tbox_layout_containing_block já existente) -- não é
 * estado global nem estático. */
typedef struct tbox_layout_positioned_context {
    tbox_rect nearest_ancestor;
    tbox_rect viewport;
} tbox_layout_positioned_context;

/* NOVO (table support): `row_column_widths`/`row_column_count`, trailing --
 * NULL/0 at every call site except tbox_layout_build_table_children's own
 * (building a <tr>'s box), where they carry the table's already-computed
 * per-column widths down into this SAME shared function so its dispatch
 * (see the definition below) can take the "table row" branch instead of
 * the generic one. See ARCHITECTURE.md's table-support section. */
static tbox_layout_box *tbox_layout_build_element(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_layout_containing_block container, double cursor_y, tbox_layout_positioned_context positioned_context, const double *row_column_widths, size_t row_column_count);

/* NOVO v5: resolves an `absolute`/`fixed` box's MARGIN BOX origin on one axis
 * -- a POSITION against the containing block's origin/size, not a delta like
 * tbox_layout_resolve_offset (v4, for `position: relative`) computes. `primary`
 * is `left`/`top`; `opposite` is `right`/`bottom`. Per CSS2.1 10.3.7/10.6.4: a
 * non-AUTO primary side wins outright (`container_origin + resolved(primary)`);
 * else a non-AUTO opposite side positions the box's FAR edge
 * (`container_origin + container_size - margin_box_size - resolved(opposite)`);
 * else (both AUTO) the box falls back to the containing block's own origin --
 * a deliberate simplification (CSS defines this as the element's "static
 * position", which this project does not compute -- see ARCHITECTURE.md "v5
 * -- Escopo deliberadamente contido" and "Layout Tree -- geometria de
 * absolute/fixed" for the vertical axis's additional circular top/bottom/
 * height case, which also lands on this same fallback). `margin_box_size`
 * must already be known by the caller before this is called -- for the
 * horizontal axis that means content_width/border_box.width/margin_box.width
 * are resolved first (never circular here); for the vertical axis, see the
 * caller in tbox_layout_build_element for how the circular case is avoided. */
static double tbox_layout_resolve_absolute_edge(tbox_style_length primary, tbox_style_length opposite, double container_origin, double container_size, double margin_box_size) {
    if (primary.kind != TBOX_STYLE_LENGTH_AUTO) {
        return container_origin + tbox_layout_resolve_edge(primary, container_size);
    }

    if (opposite.kind != TBOX_STYLE_LENGTH_AUTO) {
        return container_origin + container_size - margin_box_size - tbox_layout_resolve_edge(opposite, container_size);
    }

    return container_origin;
}

/* NOVO v14: true when `child` alone would START a new anonymous inline-box
 * sequence inside tbox_layout_build_children (see ARCHITECTURE.md's v14
 * "Algoritmo de tbox_layout_build_children"): non-whitespace-only TEXT, or
 * an ELEMENT whose resolved style is display:inline AND in flow (not
 * absolute/fixed). Whitespace-only TEXT and any other ELEMENT
 * (display:none/block, or out-of-flow) never trigger a sequence on their
 * own -- even though whitespace TEXT and display:none still EXTEND an
 * already-triggered sequence, see tbox_layout_inline_run_end below. */
static bool tbox_layout_is_inline_run_trigger(tbox_arena *arena, const tbox_html_node *child, const tbox_style_table *styles) {
    if (child->type == TBOX_HTML_NODE_TEXT) {
        tbox_string_view collapsed = tbox_string_collapse_whitespace(arena, child->text.text);
        return collapsed.size > 0;
    }

    if (child->type == TBOX_HTML_NODE_ELEMENT) {
        const tbox_style *child_style = tbox_layout_style_or_default(styles, child);
        bool is_out_of_flow            = (child_style->position == TBOX_STYLE_POSITION_ABSOLUTE || child_style->position == TBOX_STYLE_POSITION_FIXED);
        return child_style->display == TBOX_STYLE_DISPLAY_INLINE && !is_out_of_flow;
    }

    return false;
}

/* NOVO v14: scans forward from `run_start` (itself already known to be an
 * inline-run trigger, see tbox_layout_is_inline_run_trigger above) for the
 * first sibling that must NOT be consumed by the sequence -- the first
 * in-flow ELEMENT with display != inline, the first out-of-flow ELEMENT, or
 * the end of the sibling list (NULL, meaning the sequence runs to the last
 * child). TEXT of ANY content (including whitespace-only -- preserves
 * spacing between words/inline elements), ELEMENT display:none, and
 * COMMENT/DOCTYPE nodes are all transparent and extend the sequence without
 * ever starting or ending it on their own. */
static const tbox_html_node *tbox_layout_inline_run_end(const tbox_html_node *run_start, const tbox_style_table *styles) {
    const tbox_html_node *node;
    for (node = run_start; node != NULL; node = node->next_sibling) {
        if (node->type != TBOX_HTML_NODE_ELEMENT) {
            continue; /* TEXT (any content), COMMENT, DOCTYPE: transparent */
        }

        const tbox_style *node_style = tbox_layout_style_or_default(styles, node);
        if (node_style->display == TBOX_STYLE_DISPLAY_NONE) {
            continue; /* transparent, same as display:none anywhere else */
        }

        bool is_out_of_flow = (node_style->position == TBOX_STYLE_POSITION_ABSOLUTE || node_style->position == TBOX_STYLE_POSITION_FIXED);
        if (is_out_of_flow) {
            break; /* terminates, NOT consumed -- built via the out-of-flow path instead */
        }

        if (node_style->display == TBOX_STYLE_DISPLAY_INLINE) {
            continue; /* in-flow inline: extends the sequence */
        }

        break; /* in-flow block terminates, NOT consumed */
    }
    return node;
}

/* NOVO v14: builds ONE anonymous block box (`box->node == NULL`, a
 * convention documented since v2 and already handled safely by the Context
 * layer's hit-test, see src/context/tbox_context.c) covering the sibling
 * range [run_start, run_end) -- a contiguous sequence of loose inline
 * content directly inside a block container (see ARCHITECTURE.md's v14
 * "Escopo" for the full rationale). Reuses the exact same inline formatting
 * mechanism a real text-tag element already uses
 * (tbox_layout_build_text_runs, generalized to accept a sibling range and a
 * NULL `node` earlier in this file) rather than a parallel implementation.
 *
 * The synthesized style starts from tbox_layout_default_style (every
 * NON-inheritable property at its CSS2.1 initial value -- display:block,
 * zero margin/padding/border, transparent background, no position -- so the
 * anonymous box paints nothing of its own and never double-paints the
 * container's background/border, see ARCHITECTURE.md) and then copies ONLY
 * the six INHERITABLE tbox_style fields from `container_style` (see
 * include/tbox/style.h's "inheritable" comments on each field): `color`,
 * `font_family` (the whole fixed buffer, via memcpy -- not a pointer),
 * `font_weight_bold`, `font_italic`, `font_size`, `text_align`. This is
 * exactly what a real, undeclared child element would resolve to against
 * this same parent, computed here without calling back into the Style
 * layer (which already ran and has no entry point for "resolve a style with
 * no node").
 *
 * The returned box has no margin/padding/border of its own (see
 * ARCHITECTURE.md's "Escopo" -- CSS2.1 anonymous boxes never contribute a
 * box model of their own), so its four rects (content/padding/border/margin
 * box) are all identical: `{content_x, cursor_y, available_width, height}`,
 * `height` coming straight out of tbox_layout_build_text_runs. */
static tbox_layout_box *tbox_layout_build_anonymous_box(tbox_arena *arena, const tbox_html_node *run_start, const tbox_html_node *run_end, const tbox_style *container_style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double cursor_y, double available_width) {
    tbox_style anon = tbox_layout_default_style;
    anon.color            = container_style->color;
    memcpy(anon.font_family, container_style->font_family, sizeof(anon.font_family));
    anon.font_weight_bold = container_style->font_weight_bold;
    anon.font_italic      = container_style->font_italic;
    anon.font_size        = container_style->font_size;
    anon.text_align       = container_style->text_align;

    /* `box->style` is a pointer that must outlive this call -- unlike `anon`
     * itself (a local), the synthesized style needs arena-backed storage,
     * same lifetime as every other tbox_style this Layout Tree build
     * produces. */
    tbox_style *anon_style = (tbox_style *)tbox_arena_alloc(arena, sizeof(tbox_style));
    *anon_style            = anon;

    /* Zero-initialized, same pattern as tbox_layout_build_element: parent/
     * first_child/last_child/next_sibling all start NULL, only overwritten
     * by the caller (tbox_layout_build_children) for the sibling links. */
    tbox_layout_box *box = (tbox_layout_box *)tbox_arena_alloc_zero(arena, sizeof(tbox_layout_box));
    box->node             = NULL;
    box->style             = anon_style;

    double height = tbox_layout_build_text_runs(arena, NULL, run_start, run_end, anon_style, styles, fonts, images, content_x, cursor_y, available_width, box);

    tbox_rect rect      = { content_x, cursor_y, available_width, height };
    box->content_box = rect;
    box->padding_box = rect;
    box->border_box  = rect;
    box->margin_box  = rect;

    return box;
}

/* ---- NOVO (table support): <table>/<tr>/<th>/<td> -- see ARCHITECTURE.md's
 * table-support section for the full column algorithm rationale. Direct
 * children only: a <tr> must be a direct child of <table>, a <th>/<td> a
 * direct child of <tr> -- no <thead>/<tbody>/<tfoot>, out of scope. ---- */

/* Counts `table_node`'s columns: the MAX, across every direct <tr> child,
 * of that row's own direct <th>/<td> element-child count. A ragged table
 * (rows with fewer cells than the widest one) is tolerated -- those rows
 * simply leave their trailing columns empty, never an error. */
static size_t tbox_layout_table_column_count(const tbox_html_node *table_node) {
    size_t max_count = 0;

    for (const tbox_html_node *row = table_node->first_child; row != NULL; row = row->next_sibling) {
        if (row->type != TBOX_HTML_NODE_ELEMENT || !tbox_string_view_equal_cstr(row->element.tag_name, "tr")) {
            continue;
        }

        size_t count = 0;
        for (const tbox_html_node *cell = row->first_child; cell != NULL; cell = cell->next_sibling) {
            if (cell->type == TBOX_HTML_NODE_ELEMENT && (tbox_string_view_equal_cstr(cell->element.tag_name, "td") || tbox_string_view_equal_cstr(cell->element.tag_name, "th"))) {
                count++;
            }
        }
        if (count > max_count) {
            max_count = count;
        }
    }

    return max_count;
}

/* Fills `out_widths[0..column_count)` with each column's own width, summing
 * to EXACTLY `content_width` (the table's own already-resolved content
 * width -- same "AUTO always fills the container" rule, D4, every other
 * block box already has). Two passes: first, for every cell, its "natural"
 * width -- its own flattened text content (tbox_html_node_text_content,
 * same "fold nested markup into one plain-text measurement" approximation
 * already used for an inline child's own text, see
 * tbox_layout_collect_words) measured at the cell's own resolved face, plus
 * its own padding/border -- and the MAX natural width per column across
 * every row; then a single uniform scale so the per-column maxes sum to
 * exactly `content_width`. A per-cell explicit `width` is NOT consulted
 * (only measured text) -- documented scope limitation, same class as images
 * not preserving aspect ratio on a single explicit axis. Falls back to an
 * equal share per column if every cell measured a natural width of 0 (e.g.
 * every cell is empty), avoiding a divide-by-zero in the scale step. This
 * is a deliberate simplification of CSS2.1's real automatic table layout
 * algorithm (separate min-content/max-content tracking per column, only
 * growing proportionally past the sum of max-contents) -- out of scope. */
static void tbox_layout_table_compute_column_widths(tbox_arena *arena, const tbox_html_node *table_node, const tbox_style_table *styles, tbox_font_face_cache *fonts, size_t column_count, double content_width, double *out_widths) {
    for (size_t i = 0; i < column_count; i++) {
        out_widths[i] = 0.0;
    }

    for (const tbox_html_node *row = table_node->first_child; row != NULL; row = row->next_sibling) {
        if (row->type != TBOX_HTML_NODE_ELEMENT || !tbox_string_view_equal_cstr(row->element.tag_name, "tr")) {
            continue;
        }

        size_t column_index = 0;
        for (const tbox_html_node *cell = row->first_child; cell != NULL; cell = cell->next_sibling) {
            if (cell->type != TBOX_HTML_NODE_ELEMENT || (!tbox_string_view_equal_cstr(cell->element.tag_name, "td") && !tbox_string_view_equal_cstr(cell->element.tag_name, "th"))) {
                continue;
            }
            if (column_index >= column_count) {
                break;
            }

            const tbox_style *cell_style = tbox_layout_style_or_default(styles, cell);
            double padding_left          = tbox_layout_resolve_edge(cell_style->padding[3], content_width);
            double padding_right         = tbox_layout_resolve_edge(cell_style->padding[1], content_width);
            double effective_border      = (cell_style->border_style == TBOX_STYLE_BORDER_STYLE_SOLID) ? cell_style->border_width : 0.0;

            const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(cell_style->font_family), cell_style->font_weight_bold, cell_style->font_italic, cell_style->font_size);
            double text_width          = 0.0;
            if (face != NULL) {
                tbox_string_view text = tbox_html_node_text_content(arena, cell);
                text_width             = tbox_font_measure_text(face, text);
            }

            double natural_width = text_width + padding_left + padding_right + 2.0 * effective_border;
            if (natural_width > out_widths[column_index]) {
                out_widths[column_index] = natural_width;
            }

            column_index++;
        }
    }

    double total_natural = 0.0;
    for (size_t i = 0; i < column_count; i++) {
        total_natural += out_widths[i];
    }

    if (total_natural <= 0.0) {
        double equal_share = content_width / (double)column_count;
        for (size_t i = 0; i < column_count; i++) {
            out_widths[i] = equal_share;
        }
        return;
    }

    double scale = content_width / total_natural;
    for (size_t i = 0; i < column_count; i++) {
        out_widths[i] *= scale;
    }
}

/* Lays out one <tr>'s direct <th>/<td> children SIDE BY SIDE (not stacked
 * -- unlike every other container in this file, a table row is a
 * horizontal formatting context) at x-offsets derived from
 * `column_widths`. Each cell is built via tbox_layout_build_element itself
 * -- cells are text tags (see tbox_layout_is_text_tag), so this is the
 * EXACT same per-box machinery (margin/padding/border/content resolution,
 * then tbox_layout_build_text_runs for wrapped text) every other text tag
 * already uses, just called with `container.width = column_widths[i]`
 * instead of the row's own full width. Returns the row's own content
 * height: the MAX of every cell's margin_box.height -- a row's height is
 * dictated by its tallest cell; shorter cells keep their own natural
 * height (no cross-cell vertical stretch to fill the row -- documented
 * simplification, same class as images not preserving aspect ratio). */
static double tbox_layout_build_table_row_children(tbox_arena *arena, const tbox_html_node *row_node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double content_y, const double *column_widths, size_t column_count, tbox_layout_box *row_box, tbox_layout_positioned_context positioned_context) {
    double max_height          = 0.0;
    double cursor_x            = content_x;
    size_t column_index        = 0;
    tbox_layout_box *previous  = NULL;

    for (const tbox_html_node *cell = row_node->first_child; cell != NULL; cell = cell->next_sibling) {
        if (cell->type != TBOX_HTML_NODE_ELEMENT || (!tbox_string_view_equal_cstr(cell->element.tag_name, "td") && !tbox_string_view_equal_cstr(cell->element.tag_name, "th"))) {
            continue;
        }
        if (column_index >= column_count) {
            break; /* defensive only -- column_count is already the max across every row, so this row can never exceed it */
        }

        const tbox_style *cell_style = tbox_layout_style_or_default(styles, cell);
        if (cell_style->display == TBOX_STYLE_DISPLAY_NONE) {
            cursor_x += column_widths[column_index];
            column_index++;
            continue;
        }

        tbox_layout_containing_block cell_container = {
            .x               = cursor_x,
            .y               = content_y,
            .width           = column_widths[column_index],
            .height          = 0.0,
            .height_definite = false,
        };

        tbox_layout_box *cell_box = tbox_layout_build_element(arena, cell, styles, fonts, images, cell_container, content_y, positioned_context, NULL, 0);
        cell_box->parent          = row_box;
        if (previous == NULL) {
            row_box->first_child = cell_box;
        } else {
            previous->next_sibling = cell_box;
        }
        row_box->last_child = cell_box;
        previous            = cell_box;

        if (cell_box->margin_box.height > max_height) {
            max_height = cell_box->margin_box.height;
        }

        cursor_x += column_widths[column_index];
        column_index++;
    }

    return max_height;
}

/* Lays out `table_node`'s direct <tr> children STACKED vertically (like any
 * other block-level sibling sequence, but bespoke here rather than reusing
 * tbox_layout_build_children -- a table row never collapses margins with
 * anything and never triggers the v14 loose-inline-content mechanism,
 * neither of which apply inside a table). Computes `column_count`/
 * `column_widths` ONCE (tbox_layout_table_column_count/
 * tbox_layout_table_compute_column_widths above) before building any row,
 * since every row's cells need the SAME column widths to stay aligned.
 * Each row's own box is built via tbox_layout_build_element too, passing
 * `column_widths`/`column_count` through its trailing parameters so IT
 * takes the "table row" branch (see that function's dispatch). Returns the
 * table's own content height: the sum of every row's margin_box.height. */
static double tbox_layout_build_table_children(tbox_arena *arena, const tbox_html_node *table_node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double content_y, double content_width, tbox_layout_box *table_box, tbox_layout_positioned_context positioned_context) {
    size_t column_count = tbox_layout_table_column_count(table_node);
    if (column_count == 0) {
        return 0.0;
    }

    double *column_widths = (double *)tbox_arena_alloc(arena, sizeof(double) * column_count);
    tbox_layout_table_compute_column_widths(arena, table_node, styles, fonts, column_count, content_width, column_widths);

    double cursor_y           = content_y;
    tbox_layout_box *previous = NULL;

    for (const tbox_html_node *row = table_node->first_child; row != NULL; row = row->next_sibling) {
        if (row->type != TBOX_HTML_NODE_ELEMENT || !tbox_string_view_equal_cstr(row->element.tag_name, "tr")) {
            continue;
        }

        const tbox_style *row_style = tbox_layout_style_or_default(styles, row);
        if (row_style->display == TBOX_STYLE_DISPLAY_NONE) {
            continue;
        }

        tbox_layout_containing_block row_container = {
            .x               = content_x,
            .y               = content_y,
            .width           = content_width,
            .height          = 0.0,
            .height_definite = false,
        };

        tbox_layout_box *row_box = tbox_layout_build_element(arena, row, styles, fonts, images, row_container, cursor_y, positioned_context, column_widths, column_count);
        row_box->parent          = table_box;
        if (previous == NULL) {
            table_box->first_child = row_box;
        } else {
            previous->next_sibling = row_box;
        }
        table_box->last_child = row_box;
        previous              = row_box;

        cursor_y += row_box->margin_box.height;
    }

    return cursor_y - content_y;
}

/* Walks `node`'s children (NOVO v14: TEXT children are no longer always
 * ignored, see below), skipping any ELEMENT whose resolved style->display
 * == TBOX_STYLE_DISPLAY_NONE entirely -- no box, no recursion into its
 * subtree, no contribution to the height sum returned.
 *
 * NOVO v4: adjacent siblings now collapse their touching margins (CSS2.1
 * 8.3.1's sibling case -- see ARCHITECTURE.md) instead of always summing
 * them. In place of a single running `cursor_y`, this tracks `border_bottom`
 * (the y just past the last positioned sibling's OWN border_box -- not its
 * margin_box: its bottom margin hasn't been "spent" yet, so it stays free to
 * collapse with the next sibling's top margin) and `pending_margin_bottom`
 * (that unspent margin; 0.0 before the first child, which is why the first
 * child's own top margin never collapses with anything -- out of scope here,
 * unchanged since v0-v3). Before building each child, its OWN top margin is
 * resolved (against `children_container.width`, the same base every margin
 * already resolves against) so the gap can be decided ahead of the call: when
 * both the pending bottom margin and this child's top margin are >= 0, the
 * gap collapses to `max(pending_margin_bottom, child_margin_top)`; otherwise
 * (either one negative -- CSS2.1's full negative-margin algorithm is out of
 * scope, see ARCHITECTURE.md) the pair falls back to today's behavior, a
 * plain sum of the two. Once the child is built, `border_bottom`/
 * `pending_margin_bottom` are refreshed for the next iteration -- deriving
 * `border_bottom` as `cursor_y + child_box->margin_box.height -
 * child_margin_bottom` rather than reading `child_box->border_box.y`
 * directly: a `position: relative` child's border_box/margin_box carry its
 * visual offset (see below), but `cursor_y` (the static flow position this
 * child was placed at) and `margin_box.height` (unaffected by an x/y-only
 * offset) are not, so this keeps margin collapsing computed entirely in the
 * same unshifted flow coordinate space v0-v3 always used -- a
 * `position: relative` sibling still never disturbs where the next sibling
 * lands. The last child's pending bottom margin never collapses with
 * anything after it (parent/last-child collapsing is out of scope), so it's
 * added in full to the returned total.
 *
 * NOVO v5: `positioned_context` (see tbox_layout_positioned_context above)
 * is threaded through so descendants deeper in the recursion know what
 * `absolute`/`fixed` boxes must position against. Before building each
 * child, `child_style->position` is now ALSO peeked (same pattern as
 * `margin[0]` above): `ABSOLUTE`/`FIXED` children are built against a
 * containing block carved out of `positioned_context.nearest_ancestor`
 * (ABSOLUTE) or `positioned_context.viewport` (FIXED) instead of
 * `children_container` -- and, critically, take NO part in flow at all:
 * `border_bottom`/`pending_margin_bottom` are left untouched by them (they
 * never collapse margins with any sibling, in or out of flow) and their
 * height is not added to the returned total (they never count toward the
 * parent's auto-height) -- exactly CSS2.1's rule that out-of-flow boxes do
 * not participate in the block formatting context they're removed from.
 * They are still linked into `parent_box->first_child`/`last_child`/
 * `next_sibling` normally: they remain children of the same DOM parent in
 * the layout tree, just with different geometry (see ARCHITECTURE.md). The
 * `cursor_y` passed to tbox_layout_build_element for them is whatever
 * `border_bottom` currently holds -- a valid double, required by the
 * function's signature, but never actually used for their geometry since
 * `style->position != STATIC/RELATIVE/STICKY` there takes the
 * `container.x`/`container.y`-based path instead.
 *
 * NOVO v14: gains `container_style` -- `node`'s OWN already-resolved style
 * (the caller, tbox_layout_build_element, already has this as its local
 * `style`), used exclusively to synthesize an eventual anonymous box's style
 * (see tbox_layout_build_anonymous_box above), never for `node`'s ELEMENT
 * children (those keep resolving their OWN style via `styles`, unchanged).
 * Before deciding whether a child is skipped/out-of-flow/in-flow-block (the
 * three pre-v14 cases, all unchanged in behavior), each child is first
 * checked for whether it TRIGGERS a loose-inline-content sequence
 * (tbox_layout_is_inline_run_trigger: non-whitespace TEXT, or an in-flow
 * ELEMENT with display:inline -- see ARCHITECTURE.md's v14 "Algoritmo").
 * When it does, tbox_layout_inline_run_end finds the end of that sequence
 * (the first in-flow non-inline ELEMENT, the first out-of-flow ELEMENT, or
 * NULL), tbox_layout_build_anonymous_box builds ONE box for the whole
 * [run_start, run_end) range, and the outer loop resumes at `run_end` --
 * never `child->next_sibling` -- so the entire sequence is consumed in one
 * step. The anonymous box is linked into `parent_box->first_child`/
 * `last_child`/`next_sibling` exactly like any other child box (preserving
 * document order against real block siblings around it) and advances
 * `border_bottom` by its own height with NO margin contribution (it has
 * none, see ARCHITECTURE.md's "Escopo") -- `pending_margin_bottom` is
 * cleared afterwards, the same way a real block's own (here: zero) bottom
 * margin already clears it for the next sibling. A child that does NOT
 * trigger a sequence falls through to the pre-v14 behavior unchanged:
 * whitespace-only TEXT and display:none ELEMENTs are skipped with no box;
 * out-of-flow and in-flow-block ELEMENTs build via tbox_layout_build_element
 * exactly as before. */
static double tbox_layout_build_children(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_layout_containing_block children_container, double start_y, tbox_layout_box *parent_box, tbox_layout_positioned_context positioned_context, const tbox_style *container_style) {
    double border_bottom         = start_y;
    double pending_margin_bottom = 0.0;
    tbox_layout_box *previous    = NULL;

    /* NOVO v14: `<script>`/`<style>` are HTML5 "raw text" elements -- the
     * HTML parser already treats them specially, tokenizing their content
     * as opaque text rather than markup (see
     * src/html_parser/tbox_html_tree_builder.c). Their one TEXT child (CSS/
     * script source) must never become visible page content -- browsers
     * never render it, and tbox_context_collect_style_elements (Context
     * layer) already reads `<style>`'s raw text straight from the DOM for
     * the cascade, independently of the Layout Tree. Before v14 this held
     * "for free" (loose TEXT was ignored everywhere); the inline-run
     * detection below would otherwise now wrap that source text in an
     * anonymous box like any other loose text. Scoped to exactly these two
     * tags -- every other element keeps the new v14 behavior unchanged. */
    bool is_raw_text_container = tbox_string_view_equal_cstr(node->element.tag_name, "script") || tbox_string_view_equal_cstr(node->element.tag_name, "style");

    for (const tbox_html_node *child = node->first_child; child != NULL;) {
        if (child->type != TBOX_HTML_NODE_ELEMENT && child->type != TBOX_HTML_NODE_TEXT) {
            /* COMMENT/DOCTYPE: never a box, never a sequence trigger. */
            child = child->next_sibling;
            continue;
        }

        if (!is_raw_text_container && tbox_layout_is_inline_run_trigger(arena, child, styles)) {
            const tbox_html_node *run_start = child;
            const tbox_html_node *run_end   = tbox_layout_inline_run_end(run_start, styles);

            double cursor_y             = border_bottom + pending_margin_bottom;
            tbox_layout_box *child_box  = tbox_layout_build_anonymous_box(arena, run_start, run_end, container_style, styles, fonts, images, children_container.x, cursor_y, children_container.width);
            child_box->parent           = parent_box;
            if (previous == NULL) {
                parent_box->first_child = child_box;
            } else {
                previous->next_sibling = child_box;
            }
            parent_box->last_child = child_box;
            previous               = child_box;

            border_bottom          = cursor_y + child_box->margin_box.height;
            pending_margin_bottom  = 0.0;

            child = run_end;
            continue;
        }

        if (child->type == TBOX_HTML_NODE_TEXT) {
            /* Whitespace-only TEXT that never triggered a sequence above:
             * contributes nothing -- unchanged since before v14. */
            child = child->next_sibling;
            continue;
        }

        const tbox_style *child_style = tbox_layout_style_or_default(styles, child);
        if (child_style->display == TBOX_STYLE_DISPLAY_NONE) {
            child = child->next_sibling;
            continue;
        }

        if (child_style->position == TBOX_STYLE_POSITION_ABSOLUTE || child_style->position == TBOX_STYLE_POSITION_FIXED) {
            tbox_rect basis = (child_style->position == TBOX_STYLE_POSITION_ABSOLUTE) ? positioned_context.nearest_ancestor : positioned_context.viewport;
            tbox_layout_containing_block out_of_flow_container = {
                .x               = basis.x,
                .y               = basis.y,
                .width           = basis.width,
                .height          = basis.height,
                .height_definite = true, /* a concrete rect -- always definite, see tbox_layout_positioned_context */
            };

            tbox_layout_box *child_box = tbox_layout_build_element(arena, child, styles, fonts, images, out_of_flow_container, border_bottom, positioned_context, NULL, 0);
            child_box->parent          = parent_box;
            if (previous == NULL) {
                parent_box->first_child = child_box;
            } else {
                previous->next_sibling = child_box;
            }
            parent_box->last_child = child_box;
            previous               = child_box;

            /* Deliberately NOT touching border_bottom/pending_margin_bottom:
             * an out-of-flow child never collapses margins with, or advances
             * the cursor for, any sibling -- see doc comment above. */
            child = child->next_sibling;
            continue;
        }

        double child_margin_top = tbox_layout_resolve_edge(child_style->margin[0], children_container.width);

        double cursor_y;
        if (pending_margin_bottom >= 0.0 && child_margin_top >= 0.0) {
            double gap = pending_margin_bottom > child_margin_top ? pending_margin_bottom : child_margin_top;
            cursor_y   = border_bottom + gap - child_margin_top;
        } else {
            cursor_y = border_bottom + pending_margin_bottom;
        }

        tbox_layout_box *child_box = tbox_layout_build_element(arena, child, styles, fonts, images, children_container, cursor_y, positioned_context, NULL, 0);
        child_box->parent          = parent_box;
        if (previous == NULL) {
            parent_box->first_child = child_box;
        } else {
            previous->next_sibling = child_box;
        }
        parent_box->last_child = child_box;
        previous               = child_box;

        double child_margin_bottom = tbox_layout_resolve_edge(child_style->margin[2], children_container.width);
        border_bottom              = cursor_y + child_box->margin_box.height - child_margin_bottom;
        pending_margin_bottom      = child_margin_bottom;

        child = child->next_sibling;
    }

    return (border_bottom - start_y) + pending_margin_bottom;
}

/* Builds and positions the box for one ELEMENT `node` (already known to not
 * be display:none -- the caller checks that before recursing, see
 * tbox_layout_build_children and tbox_layout_build) against `container`
 * (its parent's, or the viewport's, content box -- NOVO v5: or, for an
 * `absolute`/`fixed` box, the positioned containing block tbox_layout_build_children
 * already carved out for it) with its margin_box's top edge at `cursor_y`
 * (flow boxes only -- an `absolute`/`fixed` box ignores `cursor_y` entirely
 * and positions itself against `container.x`/`container.y` instead, see
 * below). `positioned_context` (NOVO v5) is what this box's OWN descendants,
 * if any, will use to position themselves if they turn out to be
 * `absolute`/`fixed` -- see tbox_layout_positioned_context above. */
static tbox_layout_box *tbox_layout_build_element(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_layout_containing_block container, double cursor_y, tbox_layout_positioned_context positioned_context, const double *row_column_widths, size_t row_column_count) {
    const tbox_style *style = tbox_layout_style_or_default(styles, node);
    bool is_text_tag        = tbox_layout_is_text_tag(node);

    /* NOVO (table support): dispatched by TAG NAME, not style->display --
     * <table>/<tr> already default to the v0 BLOCK fallback (correct for
     * how their PARENT treats them as an ordinary in-flow block child; no
     * new UA stylesheet rule is even required for this), and neither ever
     * needs a different `display` value. `is_table_row` is true exactly
     * when `row_column_widths` is non-NULL, which only ever happens at the
     * ONE call site inside tbox_layout_build_table_children -- a <tr> built
     * any other way (there is none, today) would fall through to the
     * generic container branch below instead. */
    bool is_table     = node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(node->element.tag_name, "table");
    bool is_table_row = row_column_widths != NULL;

    /* NOVO v5: RELATIVE/ABSOLUTE/FIXED/STICKY all count as "positioned" for
     * being a containing block (see tbox_layout_positioned_context), but
     * only ABSOLUTE/FIXED are actually placed by resolving left/right/top/
     * bottom against `container` instead of flowing at `cursor_y` -- RELATIVE
     * and STICKY (== RELATIVE, see ARCHITECTURE.md's scope) still flow
     * normally and only shift visually, unchanged since v4. */
    bool is_out_of_flow = (style->position == TBOX_STYLE_POSITION_ABSOLUTE || style->position == TBOX_STYLE_POSITION_FIXED);

    /* Zero-initialized: text_runs/text_run_count/parent/first_child/
     * last_child/next_sibling all start at their empty/NULL default and are
     * only overwritten where this function (or tbox_layout_build_children,
     * for the sibling links) has something to put there. */
    tbox_layout_box *box = (tbox_layout_box *)tbox_arena_alloc_zero(arena, sizeof(tbox_layout_box));
    box->node            = node;
    box->style           = style;

    double margin_top    = tbox_layout_resolve_edge(style->margin[0], container.width);
    double margin_right  = tbox_layout_resolve_edge(style->margin[1], container.width);
    double margin_bottom = tbox_layout_resolve_edge(style->margin[2], container.width);
    double margin_left   = tbox_layout_resolve_edge(style->margin[3], container.width);

    double padding_top    = tbox_layout_resolve_edge(style->padding[0], container.width);
    double padding_right  = tbox_layout_resolve_edge(style->padding[1], container.width);
    double padding_bottom = tbox_layout_resolve_edge(style->padding[2], container.width);
    double padding_left   = tbox_layout_resolve_edge(style->padding[3], container.width);

    /* NOVO v4: `border` occupies space exactly like padding does (there is
     * no `box-sizing: border-box` -- see ARCHITECTURE.md) -- but only when
     * `border-style: solid` actually applies; a declared border-width/color
     * without `solid` (or with the initial `none`) occupies zero space,
     * same as not declaring `border` at all. */
    double effective_border = (style->border_style == TBOX_STYLE_BORDER_STYLE_SOLID) ? style->border_width : 0.0;

    /* Width: the same rule for every node, text-tag or not -- see
     * ARCHITECTURE.md's clarification that measured text never resizes the
     * box (D4). Border counts against the available width in the AUTO
     * branch exactly like padding does. */
    double content_width;
    switch (style->width.kind) {
    case TBOX_STYLE_LENGTH_PX:
        content_width = style->width.value;
        break;
    case TBOX_STYLE_LENGTH_PERCENT:
        content_width = style->width.value / 100.0 * container.width;
        break;
    case TBOX_STYLE_LENGTH_AUTO:
    default:
        content_width = container.width - margin_left - margin_right - padding_left - padding_right - 2.0 * effective_border;
        break;
    }

    double content_x;
    double content_y;

    if (is_out_of_flow) {
        /* NOVO v5: absolute/fixed geometry -- resolve the MARGIN BOX
         * position against `container` (already the right positioned
         * containing block by construction, see tbox_layout_build_children)
         * instead of flowing at cursor_y. See ARCHITECTURE.md "Layout Tree
         * -- geometria de absolute/fixed" and tbox_layout_resolve_absolute_edge
         * above for the full rationale. */

        /* Horizontal: never circular -- content_width above is already
         * resolved against container.width regardless of position, so
         * margin_box.width is known outright before positioning. */
        double border_box_width = content_width + padding_left + padding_right + 2.0 * effective_border;
        double margin_box_width = border_box_width + margin_left + margin_right;
        double margin_box_x     = tbox_layout_resolve_absolute_edge(style->offset[3], style->offset[1], container.x, container.width, margin_box_width);
        double border_box_x     = margin_box_x + margin_left;
        content_x                = border_box_x + effective_border + padding_left;

        /* Vertical: `top` non-AUTO resolves outright, no circularity (children
         * are laid out normally afterwards, and an AUTO content_height still
         * just sums them as usual). `top` AUTO but `bottom` non-AUTO needs
         * margin_box.height up front, which is only knowable ahead of the
         * children/text pass when style->height is itself definite (PX, or a
         * PERCENT against an already-definite container) AND this isn't a
         * text tag (a text tag ignores style->height entirely -- its height
         * always comes from laid-out text, see tbox_layout_build_text_runs
         * below -- so it can never be "known early" for this purpose).
         * Otherwise -- both AUTO, or the genuinely circular
         * top:auto+bottom:defined+height:auto case -- falls back to the
         * containing block's own origin, the simplification documented in
         * ARCHITECTURE.md's "Escopo deliberadamente contido" and "Layout
         * Tree -- geometria de absolute/fixed". */
        bool top_auto           = style->offset[0].kind == TBOX_STYLE_LENGTH_AUTO;
        bool bottom_auto        = style->offset[2].kind == TBOX_STYLE_LENGTH_AUTO;
        bool height_known_early = !is_text_tag &&
                                  (style->height.kind == TBOX_STYLE_LENGTH_PX ||
                                   (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite));

        double margin_box_y;
        if (!top_auto || bottom_auto || height_known_early) {
            double margin_box_height = 0.0; /* only read by tbox_layout_resolve_absolute_edge's opposite-side branch, taken below */
            if (top_auto && !bottom_auto && height_known_early) {
                double early_content_height = (style->height.kind == TBOX_STYLE_LENGTH_PX)
                    ? style->height.value
                    : style->height.value / 100.0 * container.height;
                double early_border_box_height = early_content_height + padding_top + padding_bottom + 2.0 * effective_border;
                margin_box_height              = early_border_box_height + margin_top + margin_bottom;
            }
            margin_box_y = tbox_layout_resolve_absolute_edge(style->offset[0], style->offset[2], container.y, container.height, margin_box_height);
        } else {
            margin_box_y = container.y; /* circular top:auto+bottom:defined+height:auto case -- see comment above */
        }

        double border_box_y = margin_box_y + margin_top;
        content_y            = border_box_y + effective_border + padding_top;
    } else {
        content_x = container.x + margin_left + padding_left + effective_border;
        content_y = cursor_y + margin_top + padding_top + effective_border;
    }

    /* NOVO v4: `position: relative` -- a pure visual-coordinate shift, no
     * new containing-block concept (confirmed in ARCHITECTURE.md). Applied
     * to content_x/content_y BEFORE anything below derives from them --
     * children_container (so the whole subtree shifts automatically) and
     * content_box/padding_box/border_box/margin_box (all built from
     * content_x/content_y further down). Deliberately NOT applied to
     * cursor_y/margin_box.height as seen by the sibling loop in
     * tbox_layout_build_children: those are computed independently of
     * content_x/content_y (cursor_y is the function's own parameter, and
     * margin_box.height only ever depends on content_height/padding/
     * border/margin, never x/y), so a `position: relative` box never
     * disturbs the normal flow of any other element, exactly as CSS
     * specifies. NOVO v5: `position: sticky` takes this exact same branch --
     * ARCHITECTURE.md documents `sticky` as an exact synonym of `relative`
     * (no scrollport anywhere in the project for a "stuck" threshold to ever
     * cross), so it must resolve to IDENTICAL geometry given the same
     * offsets, not just similar. */
    if (style->position == TBOX_STYLE_POSITION_RELATIVE || style->position == TBOX_STYLE_POSITION_STICKY) {
        double dx = tbox_layout_resolve_offset(style->offset[3], style->offset[1], container.width, true);
        double dy = tbox_layout_resolve_offset(style->offset[0], style->offset[2], container.height, container.height_definite);
        content_x += dx;
        content_y += dy;
    }

    double content_height;
    if (is_text_tag) {
        /* Text-tag leaf: no child boxes even though the DOM node may have
         * element descendants (e.g. <b> inside a <p>) -- those only
         * contribute words to this box's own text_runs (NOVO v2, real
         * inline formatting context -- see tbox_layout_build_text_runs),
         * never a box of their own. Height is the sum of the wrapped
         * lines' heights (or one face's line-height for empty text) -- see
         * tbox_layout_build_text_runs's doc comment. */
        content_height = tbox_layout_build_text_runs(arena, node, node->first_child, NULL, style, styles, fonts, images, content_x, content_y, content_width, box);
    } else if (is_table) {
        /* NOVO (table support): a <table>'s content_height is ALWAYS the
         * summed row heights, same "content always dictates height,
         * style->height is never consulted" posture is_text_tag already
         * has above -- no PX/PERCENT `height` branch, no positioned-context
         * threading for descendants (a `position: absolute` box inside a
         * table resolving against the table itself is out of scope). */
        content_height = tbox_layout_build_table_children(arena, node, styles, fonts, images, content_x, content_y, content_width, box, positioned_context);
    } else if (is_table_row) {
        /* NOVO (table support): same posture as the <table> branch above --
         * content always dictates a row's height. */
        content_height = tbox_layout_build_table_row_children(arena, node, styles, fonts, images, content_x, content_y, row_column_widths, row_column_count, box, positioned_context);
    } else {
        /* Container node: recurse into ELEMENT children first (their
         * containing block is this node's own content box), then decide
         * this node's own height. PX is definite outright; PERCENT is
         * definite only when the containing block's own height is (CSS2.1
         * 10.5 -- percentage height against an auto-height container
         * computes to auto, the common v0 case); anything else (AUTO, or
         * that PERCENT fallback) is shrink-to-fit: the sum of every child's
         * margin_box.height, which requires children to be laid out first. */
        bool height_definite = false;
        switch (style->height.kind) {
        case TBOX_STYLE_LENGTH_PX:
            content_height  = style->height.value;
            height_definite = true;
            break;
        case TBOX_STYLE_LENGTH_PERCENT:
            if (container.height_definite) {
                content_height  = style->height.value / 100.0 * container.height;
                height_definite = true;
            } else {
                content_height = 0.0; /* placeholder; replaced by the children sum below */
            }
            break;
        case TBOX_STYLE_LENGTH_AUTO:
        default:
            content_height = 0.0; /* placeholder; replaced by the children sum below */
            break;
        }

        /* NOVO v5: decide what positioned_context THIS box's own descendants
         * see, per ARCHITECTURE.md "Layout Tree -- containing block
         * posicionado" -- any non-STATIC position (RELATIVE/ABSOLUTE/FIXED/
         * STICKY all count as "positioned" for this purpose, same as CSS)
         * makes this box's own padding_box the nearest_ancestor passed down
         * to tbox_layout_build_children; `viewport` never changes. Computed
         * here, before recursing into children, from content_x/content_y/
         * content_width/content_height as known AT THIS POINT -- for a
         * positioned box whose OWN height is AUTO, that means `content_height`
         * is still the 0.0 placeholder above (its real, children-summed
         * value isn't known until after tbox_layout_build_children returns,
         * which is too late: children need nearest_ancestor to build
         * themselves). A documented simplification, not a bug: an
         * absolute/fixed descendant resolving `bottom` against such an
         * auto-height ancestor sees a too-small containing block. Not
         * exercised by this version's minimum test cases (which use a
         * definite-height or default-flow positioned ancestor). */
        tbox_layout_positioned_context context_for_children = positioned_context;
        if (style->position != TBOX_STYLE_POSITION_STATIC) {
            tbox_rect padding_box_now = {
                .x      = content_x - padding_left,
                .y      = content_y - padding_top,
                .width  = content_width + padding_left + padding_right,
                .height = content_height + padding_top + padding_bottom,
            };
            context_for_children.nearest_ancestor = padding_box_now;
        }

        tbox_layout_containing_block children_container = {
            .x               = content_x,
            .y               = content_y,
            .width           = content_width,
            .height          = content_height,
            .height_definite = height_definite,
        };
        double children_total_height = tbox_layout_build_children(arena, node, styles, fonts, images, children_container, content_y, box, context_for_children, style);
        if (!height_definite) {
            content_height = children_total_height;
        }
    }

    box->content_box.x      = content_x;
    box->content_box.y      = content_y;
    box->content_box.width  = content_width;
    box->content_box.height = content_height;

    /* padding_box is content_box grown back out by padding (note:
     * content_x/content_y already include effective_border -- see above --
     * so subtracting only padding_left/padding_top here correctly lands on
     * the padding_box edge, between border and padding). NOVO v4:
     * border_box is padding_box grown back out by effective_border on all 4
     * sides (0.0 when there's no effective border, preserving the v0-v3
     * identity border_box == padding_box exactly). */
    box->padding_box.x      = content_x - padding_left;
    box->padding_box.y      = content_y - padding_top;
    box->padding_box.width  = content_width + padding_left + padding_right;
    box->padding_box.height = content_height + padding_top + padding_bottom;

    box->border_box.x      = box->padding_box.x - effective_border;
    box->border_box.y      = box->padding_box.y - effective_border;
    box->border_box.width  = box->padding_box.width + 2.0 * effective_border;
    box->border_box.height = box->padding_box.height + 2.0 * effective_border;

    /* margin_box is border_box grown back out by margin -- its x/y land back
     * on (container.x, cursor_y) exactly for a flow box, per the geometry
     * above -- or, NOVO v5, on (margin_box_x, margin_box_y) as resolved by
     * tbox_layout_resolve_absolute_edge for an absolute/fixed box, by the
     * same border_box.x = margin_box.x + margin_left relation used
     * everywhere else in this file. */
    box->margin_box.x      = box->border_box.x - margin_left;
    box->margin_box.y      = box->border_box.y - margin_top;
    box->margin_box.width  = box->border_box.width + margin_left + margin_right;
    box->margin_box.height = box->border_box.height + margin_top + margin_bottom;

    return box;
}

tbox_layout_box *tbox_layout_build(tbox_arena *arena, const tbox_html_node *root, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double viewport_width, double viewport_height) {
    if (root == NULL) {
        return NULL;
    }

    /* A DOCUMENT node is transparent -- it is never itself styled (the
     * Style layer skips it, same as TEXT/COMMENT/DOCTYPE) -- so build the
     * one box for its first ELEMENT child instead (typically <html> in a
     * full document). A caller may also pass an ELEMENT node directly, to
     * lay out one fragment in isolation. Anything else (a bare TEXT/
     * COMMENT/DOCTYPE root) has nothing to lay out. */
    const tbox_html_node *element = NULL;
    if (root->type == TBOX_HTML_NODE_DOCUMENT) {
        for (const tbox_html_node *child = root->first_child; child != NULL; child = child->next_sibling) {
            if (child->type == TBOX_HTML_NODE_ELEMENT) {
                element = child;
                break;
            }
        }
    } else if (root->type == TBOX_HTML_NODE_ELEMENT) {
        element = root;
    }

    if (element == NULL) {
        return NULL;
    }

    if (tbox_layout_style_or_default(styles, element)->display == TBOX_STYLE_DISPLAY_NONE) {
        return NULL;
    }

    tbox_layout_containing_block viewport = {
        .x               = 0.0,
        .y               = 0.0,
        .width           = viewport_width,
        .height          = viewport_height,
        .height_definite = true, /* the viewport's height is always a concrete number */
    };

    /* NOVO v5: no positioned ancestor exists yet at the root -- both
     * `nearest_ancestor` and `viewport` start out as the same initial
     * containing block (CSS2.1's rule: with no positioned ancestor, an
     * absolute box's containing block is the initial containing block). See
     * tbox_layout_positioned_context above. */
    tbox_rect viewport_rect = { .x = 0.0, .y = 0.0, .width = viewport_width, .height = viewport_height };
    tbox_layout_positioned_context root_positioned_context = {
        .nearest_ancestor = viewport_rect,
        .viewport         = viewport_rect,
    };
    return tbox_layout_build_element(arena, element, styles, fonts, images, viewport, 0.0, root_positioned_context, NULL, 0);
}
