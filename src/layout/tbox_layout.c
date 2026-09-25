#include <tbox/layout.h>

#include <tbox/image.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"
#include "html_parser/tbox_html_entities.h"

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
    .opacity           = 1.0,
};

/* `nowrap` and `pre` never wrap at the box width. */
static bool tbox_layout_white_space_nowrap(const tbox_style *style) {
    return style->white_space == TBOX_STYLE_WHITE_SPACE_NOWRAP || style->white_space == TBOX_STYLE_WHITE_SPACE_PRE;
}

static const tbox_style *tbox_layout_style_or_default(const tbox_style_table *styles, const tbox_html_node *node) {
    const tbox_style *style = tbox_style_table_find(styles, node);
    return style != NULL ? style : &tbox_layout_default_style;
}

static double tbox_layout_constrain_width(const tbox_style *style, double width, double base, double edges);

static bool tbox_layout_is_hidden_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("hidden", 6));
}

static bool tbox_layout_is_image_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("image", 5));
}

static bool tbox_layout_is_password_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("password", 8));
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
 * every other text tag already has. Cells with block children take the
 * normal container path instead, so their nested block boxes are retained. */
static bool tbox_layout_is_text_tag(const tbox_html_node *node) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) {
        return false;
    }
    if (tbox_string_view_equal_cstr(node->element.tag_name, "input")) {
        const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
        return type == NULL ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("text", 4)) ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("email", 5)) ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("number", 6)) ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("password", 8)) ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("search", 6)) ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("tel", 3)) ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("url", 3)) ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("button", 6)) ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("reset", 5)) ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("submit", 6)) ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("date", 4)) ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("time", 4)) ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("week", 4)) ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("month", 5)) ||
               tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("datetime-local", 14));
    }
    if (tbox_string_view_equal_cstr(node->element.tag_name, "select") ||
        tbox_string_view_equal_cstr(node->element.tag_name, "textarea")) return true;

    static const char *const text_tags[] = { "h1", "h2", "h3", "h4", "h5", "h6", "p", "li", "pre", "td", "th", "caption", "button" };
    tbox_string_view tag_name            = node->element.tag_name;
    for (size_t i = 0; i < sizeof(text_tags) / sizeof(text_tags[0]); i++) {
        if (tbox_string_view_equal_cstr(tag_name, text_tags[i])) {
            return true;
        }
    }
    return false;
}

static bool tbox_layout_is_checked_checkbox(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL &&
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("checkbox", 8)) &&
           tbox_html_node_get_attribute(node, tbox_string_view_make("checked", 7)) != NULL;
}

static void tbox_layout_build_checkbox_checkmark(tbox_arena *arena, const tbox_html_node *node,
                                                  const tbox_style *style, tbox_font_face_cache *fonts,
                                                  tbox_layout_box *box) {
    if (!tbox_layout_is_checked_checkbox(node) || fonts == NULL) return;

    tbox_string_view entity = tbox_string_view_make("&checkmark;", 11);
    tbox_string_view checkmark = tbox_html_decode_entities(arena, entity);
    if (checkmark.size != 3) return;

    double size = box->content_box.height;
    if (size > 14.0) size = 14.0;
    if (size <= 0.0) return;
    const tbox_font_face *face = tbox_font_face_cache_get(fonts,
        tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
        style->font_italic, size);
    if (!tbox_font_face_has_glyph(face, 0x2713))
        face = tbox_font_face_cache_get(fonts, tbox_string_view_make("DejaVu Sans", 11),
                                        false, false, size);
    if (!tbox_font_face_has_glyph(face, 0x2713)) return;

    double width = tbox_font_measure_text(face, checkmark);
    double line_height = tbox_font_face_line_height(face);
    tbox_layout_text_run *run = tbox_arena_alloc_zero(arena, sizeof(*run));
    run->rect = (tbox_rect){box->content_box.x + (box->content_box.width - width) / 2.0,
                            box->content_box.y + (box->content_box.height - line_height) / 2.0,
                            width, line_height};
    run->text = checkmark;
    run->font = face;
    run->style = style;
    box->text_runs = run;
    box->text_run_count = 1;
}

static unsigned tbox_layout_textarea_size(const tbox_html_node *node, const char *name, unsigned fallback) {
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr(name));
    if (attribute == NULL || attribute->value.size == 0) return fallback;
    unsigned value = 0;
    for (size_t i = 0; i < attribute->value.size; i++) {
        char c = attribute->value.data[i];
        if (c < '0' || c > '9' || value > 10000) return fallback;
        value = value * 10 + (unsigned)(c - '0');
    }
    return value > 0 && value <= 10000 ? value : fallback;
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
    bool no_space_before; /* continuation of an overflow-wrap split word */

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
            entry->width            = tbox_font_measure_text_spaced(face, word, style->letter_spacing);
            entry->space_width      = tbox_font_measure_text_spaced(face, space, style->letter_spacing) + style->word_spacing;
            entry->image            = NULL;
            entry->image_height     = 0.0;
            entry->hard_break       = false;
            entry->no_space_before  = false;
        }

        while (i < collapsed.size && collapsed.data[i] == ' ') {
            i++;
        }
    }
}

/* Applies `text-transform` codepoint by codepoint (utf8.h's simple case
 * mappings, so no multi-codepoint expansions such as German sharp s).
 * `capitalize` uppercases the first letter after the start of `text` or a
 * whitespace byte -- word boundaries across inline elements are not
 * tracked. Returns `text` itself for NONE or on allocation failure. */
static tbox_string_view tbox_layout_transform_text(tbox_arena *arena, tbox_string_view text,
                                                   tbox_style_text_transform transform) {
    if (transform == TBOX_STYLE_TEXT_TRANSFORM_NONE || text.size == 0 || text.size > SIZE_MAX / 2 - 4)
        return text;
    size_t capacity = text.size * 2 + 4; /* utf8.h's mappings keep byte length; slack is defensive */
    char *out = (char *)tbox_arena_alloc(arena, capacity);
    if (out == NULL) return text;

    char *write = out;
    const char *read = text.data;
    const char *end = text.data + text.size;
    bool word_start = true;
    while (read < end) {
        /* A truncated sequence at the end would read past the view: leave
         * the text alone rather than decode it. */
        if ((size_t)(end - read) < utf8codepointcalcsize((const utf8_int8_t *)read)) return text;
        utf8_int32_t codepoint;
        const char *next = (const char *)utf8codepoint((const utf8_int8_t *)read, &codepoint);
        bool space = codepoint == ' ' || codepoint == '\t' || codepoint == '\n' ||
            codepoint == '\r' || codepoint == '\f';
        if (transform == TBOX_STYLE_TEXT_TRANSFORM_UPPERCASE ||
            (transform == TBOX_STYLE_TEXT_TRANSFORM_CAPITALIZE && word_start))
            codepoint = utf8uprcodepoint(codepoint);
        else if (transform == TBOX_STYLE_TEXT_TRANSFORM_LOWERCASE)
            codepoint = utf8lwrcodepoint(codepoint);
        word_start = space;
        utf8_int8_t *written = utf8catcodepoint((utf8_int8_t *)write, codepoint,
                                                (size_t)(out + capacity - write));
        if (written == NULL) return text;
        write = (char *)written;
        read = next;
    }
    return tbox_string_view_make(out, (size_t)(write - out));
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
    entry->no_space_before  = false;
}

/* Pushes one word glued to whatever precedes it (no break opportunity, no
 * collapsed space) unless `space_before`, which adds a single space's
 * advance and a break opportunity. Empty text is skipped. */
static void tbox_layout_push_preserved_word(tbox_vector *words, tbox_string_view text, bool space_before,
                                            const tbox_font_face *face, const tbox_style *style) {
    if (text.size == 0) return;
    static const tbox_string_view space = { " ", 1 };
    tbox_layout_word *entry = (tbox_layout_word *)tbox_vector_push(words);
    entry->text             = text;
    entry->face             = face;
    entry->style            = style;
    entry->width            = tbox_font_measure_text_spaced(face, text, style->letter_spacing);
    entry->space_width      = space_before ? tbox_font_measure_text_spaced(face, space, style->letter_spacing) +
        style->word_spacing : 0.0;
    entry->image            = NULL;
    entry->image_height     = 0.0;
    entry->hard_break       = false;
    entry->no_space_before  = !space_before;
}

/* One newline-free segment under `pre-wrap`: every space is kept. A gap of
 * k spaces becomes one break opportunity plus k-1 spaces glued to the end
 * of the word before it (so they hang at a line end instead of indenting
 * the next line); leading spaces are glued to the first word and trailing
 * ones to the last. A run's text only ever joins words with ONE space,
 * which is why the extra spaces must live inside the words' own text. */
static void tbox_layout_push_pre_wrap_segment(tbox_vector *words, tbox_string_view segment,
                                              const tbox_font_face *face, const tbox_style *style) {
    size_t start = 0;
    bool space_before = false;
    while (start < segment.size) {
        size_t end = start;
        while (end < segment.size && segment.data[end] == ' ') end++;       /* leading spaces (first word only) */
        while (end < segment.size && segment.data[end] != ' ') end++;       /* the word itself */
        size_t gap_end = end;
        while (gap_end < segment.size && segment.data[gap_end] == ' ') gap_end++;
        /* Keep all but one space of the gap, or the whole gap at the end. */
        size_t word_end = gap_end == segment.size ? gap_end : gap_end - 1;
        if (word_end < end) word_end = end;
        tbox_layout_push_preserved_word(words, tbox_string_view_make(segment.data + start, word_end - start),
                                        space_before, face, style);
        space_before = gap_end < segment.size && gap_end > end;
        start = gap_end;
    }
}

/* Turns a text node's raw text into words per the style's `white-space`:
 * `normal`/`nowrap` collapse every whitespace run, `pre-line` collapses
 * spaces but breaks at each newline, and `pre`/`pre-wrap` keep every space
 * and break at each newline (`pre` as one unbreakable word per line; the
 * text box itself decides whether lines may wrap). `text-transform` is
 * applied before measuring. */
static void tbox_layout_push_text_words(tbox_arena *arena, tbox_vector *words, tbox_string_view raw,
                                        const tbox_font_face *face, const tbox_style *style) {
    if (face == NULL) return;
    tbox_string_view text = tbox_layout_transform_text(arena, raw, style->text_transform);
    tbox_style_white_space mode = style->white_space;
    if (mode != TBOX_STYLE_WHITE_SPACE_PRE && mode != TBOX_STYLE_WHITE_SPACE_PRE_WRAP &&
        mode != TBOX_STYLE_WHITE_SPACE_PRE_LINE) {
        tbox_layout_push_words(words, tbox_string_collapse_whitespace(arena, text), face, style);
        return;
    }
    size_t line_start = 0;
    for (size_t i = 0; i <= text.size; i++) {
        if (i < text.size && text.data[i] != '\n') continue;
        size_t line_end = i > line_start && text.data[i - 1] == '\r' ? i - 1 : i;
        tbox_string_view segment = tbox_string_view_make(text.data + line_start, line_end - line_start);
        if (mode == TBOX_STYLE_WHITE_SPACE_PRE_LINE)
            tbox_layout_push_words(words, tbox_string_collapse_whitespace(arena, segment), face, style);
        else if (mode == TBOX_STYLE_WHITE_SPACE_PRE_WRAP)
            tbox_layout_push_pre_wrap_segment(words, segment, face, style);
        else
            tbox_layout_push_preserved_word(words, segment, false, face, style);
        if (i < text.size) tbox_layout_push_hard_break(words, face, style);
        line_start = i + 1;
    }
}

/* Pushes one tbox_layout_word for an <img> element (see
 * tbox_layout_collect_words below) -- carries decoded image data instead of
 * text, sized from CSS/HTML-attribute width/height (`img_style`, the img's
 * OWN resolved tbox_style -- already includes the `<img width/height>`
 * attribute fallback, see tbox_style_resolve_img_dimension_attribute in
 * src/style/tbox_style.c) falling back to the image's intrinsic pixel
 * dimensions on any axis left AUTO or PERCENT (percentage `width` keeps its
 * existing intrinsic-size fallback in this inline path). Its measured width
 * is then constrained by min/max-width against `containing_width`. A missing
 * `src`, or a `src` tbox_image_cache_get can't
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
static void tbox_layout_push_image_word(tbox_arena *arena, const tbox_html_node *img_node, const tbox_style *img_style, const tbox_font_face *context_face, tbox_image_cache *images, double containing_width, tbox_vector *words) {
    const tbox_html_attribute *src_attr = tbox_html_node_get_attribute(img_node, tbox_string_view_make("src", 3));
    const tbox_image *image             = src_attr != NULL ? tbox_image_cache_get(images, src_attr->value) : NULL;

    if (image == NULL) {
        const tbox_html_attribute *alt_attr = tbox_html_node_get_attribute(img_node, tbox_string_view_make("alt", 3));
        if (alt_attr != NULL && alt_attr->value.size > 0) {
            tbox_layout_push_text_words(arena, words, alt_attr->value, context_face, img_style);
        }
        return;
    }

    double width  = img_style->width.kind == TBOX_STYLE_LENGTH_PX ? img_style->width.value : (double)image->width;
    double height = img_style->height.kind == TBOX_STYLE_LENGTH_PX ? img_style->height.value : (double)image->height;
    width = tbox_layout_constrain_width(img_style, width, containing_width, 0.0);

    tbox_layout_word *entry = (tbox_layout_word *)tbox_vector_push(words);
    entry->text             = tbox_string_view_make(NULL, 0);
    entry->face             = context_face;
    entry->style             = img_style;
    entry->width             = width;
    entry->space_width       = context_face != NULL ? tbox_font_measure_text_spaced(context_face, tbox_string_view_make(" ", 1), img_style->letter_spacing) + img_style->word_spacing : 0.0;
    entry->image              = image;
    entry->image_height       = height;
    entry->hard_break          = false;
    entry->no_space_before     = false;
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
static void tbox_layout_collect_words(tbox_arena *arena, const tbox_html_node *first_sibling, const tbox_html_node *end_exclusive, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double containing_width, tbox_vector *words) {
    for (const tbox_html_node *child = first_sibling; child != end_exclusive; child = child->next_sibling) {
        if (tbox_layout_is_hidden_input(child)) continue;
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
            const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
            tbox_layout_push_text_words(arena, words, child->text.text, face, style);
        } else if (child->type == TBOX_HTML_NODE_ELEMENT) {
            const tbox_style *child_style = tbox_layout_style_or_default(styles, child);
            if (tbox_string_view_equal_cstr(child->element.tag_name, "img")) {
                const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
                tbox_layout_push_image_word(arena, child, child_style, face, images, containing_width, words);
            } else if (child_style->display == TBOX_STYLE_DISPLAY_INLINE) {
                if (tbox_layout_has_img_child(child)) {
                    for (const tbox_html_node *grandchild = child->first_child; grandchild != NULL; grandchild = grandchild->next_sibling) {
                        if (grandchild->type == TBOX_HTML_NODE_TEXT) {
                            const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(child_style->font_family), child_style->font_weight_bold, child_style->font_italic, child_style->font_size);
                            tbox_layout_push_text_words(arena, words, grandchild->text.text, face, child_style);
                        } else if (grandchild->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(grandchild->element.tag_name, "img")) {
                            const tbox_style *img_style = tbox_layout_style_or_default(styles, grandchild);
                            const tbox_font_face *face  = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(child_style->font_family), child_style->font_weight_bold, child_style->font_italic, child_style->font_size);
                            tbox_layout_push_image_word(arena, grandchild, img_style, face, images, containing_width, words);
                        }
                        /* else: skipped -- bounded one-level extension, no deeper nesting */
                    }
                } else {
                    tbox_string_view raw       = tbox_html_node_text_content(arena, child);
                    const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(child_style->font_family), child_style->font_weight_bold, child_style->font_italic, child_style->font_size);
                    tbox_layout_push_text_words(arena, words, raw, face, child_style);
                }
            }
        }
        /* else: COMMENT/DOCTYPE, or an ELEMENT that isn't display:inline/img --
         * contributes nothing, same as v0's "not a text tag" treatment. */
    }
}

/* Greedy, per-word line breaking. Optional break-word preprocessing splits
 * only words wider than an empty line into codepoint-aligned pieces before
 * this pass. This loop accumulates words onto the current
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
static double tbox_layout_style_line_height(const tbox_style *style, const tbox_font_face *face) {
    if (style->line_height_kind == TBOX_STYLE_LINE_HEIGHT_NUMBER)
        return style->font_size * style->line_height_value;
    if (style->line_height_kind == TBOX_STYLE_LINE_HEIGHT_PX)
        return style->line_height_value;
    return tbox_font_face_line_height(face);
}

static double tbox_layout_word_line_height(const tbox_layout_word *word) {
    return word->image != NULL ? word->image_height :
        tbox_layout_style_line_height(word->style, word->face);
}

static double tbox_layout_word_ascent(const tbox_layout_word *word) {
    if (word->image != NULL) return word->image_height;
    double leading = (tbox_layout_style_line_height(word->style, word->face) -
        tbox_font_face_line_height(word->face)) / 2.0;
    return tbox_font_face_ascent(word->face) + leading;
}

static void tbox_layout_break_lines(const tbox_layout_word *words, size_t word_count, double available_width, double first_line_indent, bool no_wrap, tbox_vector *lines) {
    if (word_count == 0) {
        return;
    }

    size_t line_start = 0;
    double line_width = first_line_indent;

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

        double prospective = (i == line_start) ? line_width + words[i].width : line_width + words[i].space_width + words[i].width;

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
 * space.
 *
 * `text-top`/`text-bottom` align the run's top/bottom edge with the
 * ascent/descent of `block_face` -- the text box's own font, standing in for
 * the parent's (inline elements have no box of their own to take it from).
 * `run_ascent`/`run_height` are the run's own extent, the same values
 * tbox_layout_build_line_runs already uses for baseline alignment. A length
 * raises the run by that many px; a percentage is of the run's own used
 * line-height. The line box's height is not grown for any of these, same
 * simplification as `sub`/`super`. */
static double tbox_layout_vertical_align_offset(const tbox_style *style, const tbox_font_face *run_face,
                                                double run_ascent, double run_height,
                                                const tbox_font_face *block_face) {
    switch (style->vertical_align) {
    case TBOX_STYLE_VERTICAL_ALIGN_SUB:
        return 0.15 * style->font_size;
    case TBOX_STYLE_VERTICAL_ALIGN_SUPER:
        return -0.35 * style->font_size;
    case TBOX_STYLE_VERTICAL_ALIGN_TEXT_TOP:
        return block_face != NULL ? run_ascent - tbox_font_face_ascent(block_face) : 0.0;
    case TBOX_STYLE_VERTICAL_ALIGN_TEXT_BOTTOM:
        return block_face != NULL ? run_ascent - run_height + tbox_font_face_line_height(block_face) -
            tbox_font_face_ascent(block_face) : 0.0;
    case TBOX_STYLE_VERTICAL_ALIGN_LENGTH:
        if (style->vertical_align_length.kind == TBOX_STYLE_LENGTH_PERCENT)
            return run_face != NULL ? -tbox_layout_style_line_height(style, run_face) *
                style->vertical_align_length.value / 100.0 : 0.0;
        return -style->vertical_align_length.value;
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
static void tbox_layout_build_line_runs(tbox_arena *arena, const tbox_layout_word *words, const tbox_layout_line *line, double line_y, double content_x, const tbox_font_face *block_face, double justify_gap, tbox_vector *runs) {
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

        /* `justify_gap` widens every real word gap (text-align: justify);
         * each such word then opens its own run, since a run's text can
         * only carry single, unwidened spaces. */
        bool justified = justify_gap != 0.0 && i != line->start && word->space_width > 0.0;
        if (i != line->start) {
            cursor_x += word->space_width + (justified ? justify_gap : 0.0);
        }

        /* An image word never merges with a neighbor -- forced by
         * `word->image != NULL` (opening one) OR `run_is_image` (the
         * currently-open run already is one, so THIS word, whatever it is,
         * must start a fresh run) -- same "never merges" treatment a
         * `<mark>` word already gets today via the `style` mismatch, just
         * unconditional here since an image's face/style are otherwise
         * ordinary values that could otherwise coincidentally match. */
        bool new_run = !have_run || word->face != run_face || word->style != run_style || word->image != NULL || run_is_image || word->style->word_spacing != 0.0 || justified;
        if (new_run) {
            if (have_run) {
                tbox_layout_text_run *run = (tbox_layout_text_run *)tbox_vector_push(runs);
                run->rect.x               = content_x + run_start_x;
                double ascent             = run_is_image ? run_image_height : tbox_font_face_ascent(run_face);
                run->rect.y               = line_y + (line->ascent - ascent) + tbox_layout_vertical_align_offset(run_style, run_face, ascent,
                    run_is_image ? run_image_height : tbox_font_face_line_height(run_face), block_face);
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
        } else if (!word->no_space_before) {
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
        run->rect.y               = line_y + (line->ascent - ascent) + tbox_layout_vertical_align_offset(run_style, run_face, ascent,
            run_is_image ? run_image_height : tbox_font_face_line_height(run_face), block_face);
        run->rect.width           = run_end_x - run_start_x;
        run->rect.height          = run_is_image ? run_image_height : line->height;
        run->text                 = tbox_string_builder_finish(&run_builder);
        run->font                 = run_face;
        run->style                = run_style;
        run->image                = run_is_image ? run_image : NULL;
    }
}

static size_t tbox_layout_previous_codepoint(tbox_string_view text) {
    size_t at = text.size;
    if (at == 0) return 0;
    at--;
    while (at > 0 && ((unsigned char)text.data[at] & 0xc0) == 0x80) at--;
    return at;
}

/* Split only text words too wide for an empty line. Each piece is a UTF-8
 * codepoint-aligned view into the original word; continuation pieces carry
 * no preceding space, so both measurement and painted text stay intact. */
static void tbox_layout_split_overlong_words(tbox_arena *arena, tbox_vector *words,
                                              double available_width, double first_indent) {
    if (available_width <= 0.0) return;
    tbox_vector expanded;
    tbox_vector_init(&expanded, arena, sizeof(tbox_layout_word), words->length);
    const tbox_layout_word *source = (const tbox_layout_word *)words->data;
    for (size_t i = 0; i < words->length; i++) {
        const tbox_layout_word *word = &source[i];
        double first_limit = i == 0 ? available_width - first_indent : available_width;
        /* word-break: break-all -- every codepoint becomes its own word
         * glued to the previous one (no space), so the greedy line breaker
         * can break between any two characters and fill each line. Run
         * building merges the pieces back into one run per line. */
        if (word->style->word_break_all && !word->hard_break && word->image == NULL && word->text.size > 0) {
            size_t start = 0;
            while (start < word->text.size) {
                size_t next = start + 1;
                while (next < word->text.size && ((unsigned char)word->text.data[next] & 0xc0) == 0x80) next++;
                tbox_layout_word *part = (tbox_layout_word *)tbox_vector_push(&expanded);
                *part = *word;
                part->text = tbox_string_view_make(word->text.data + start, next - start);
                part->width = tbox_font_measure_text_spaced(word->face, part->text, word->style->letter_spacing);
                part->space_width = start == 0 ? word->space_width : 0.0;
                part->no_space_before = start == 0 ? word->no_space_before : true;
                start = next;
            }
            continue;
        }
        if (word->hard_break || word->image != NULL || word->text.size == 0 ||
            !word->style->overflow_wrap_break_word || word->width <= first_limit) {
            *(tbox_layout_word *)tbox_vector_push(&expanded) = *word;
            continue;
        }
        size_t start = 0;
        while (start < word->text.size) {
            double limit = start == 0 ? first_limit : available_width;
            size_t end = start, best = start;
            double best_width = 0.0;
            while (end < word->text.size) {
                size_t next = end + 1;
                while (next < word->text.size &&
                    ((unsigned char)word->text.data[next] & 0xc0) == 0x80) next++;
                tbox_string_view piece = tbox_string_view_make(word->text.data + start, next - start);
                double measured = tbox_font_measure_text_spaced(word->face, piece,
                    word->style->letter_spacing);
                if (measured > limit && best > start) break;
                best = next;
                best_width = measured;
                end = next;
                if (measured > limit) break; /* one glyph exceeds the line */
            }
            tbox_layout_word *part = (tbox_layout_word *)tbox_vector_push(&expanded);
            *part = *word;
            part->text = tbox_string_view_make(word->text.data + start, best - start);
            part->width = best_width;
            part->space_width = start == 0 ? word->space_width : 0.0;
            part->no_space_before = start != 0;
            start = best;
        }
    }
    *words = expanded;
}

static void tbox_layout_ellipsize_line(tbox_vector *runs, size_t first, double content_x,
                                       double available_width, const tbox_style *style,
                                       const tbox_font_face *face, const tbox_layout_line *line,
                                       double line_y) {
    if (runs->length == first || face == NULL) return;
    tbox_layout_text_run *items = (tbox_layout_text_run *)runs->data;
    tbox_layout_text_run *last = &items[runs->length - 1];
    double right = content_x + available_width;
    if (last->rect.x + last->rect.width <= right) return;

    tbox_string_view ellipsis = tbox_font_face_has_glyph(face, 0x2026) ?
        tbox_string_view_make("\xe2\x80\xa6", 3) : tbox_string_view_make("...", 3);
    double glyph_width = tbox_font_measure_text_spaced(face, ellipsis, style->letter_spacing);
    double limit = right - glyph_width;
    while (runs->length > first) {
        items = (tbox_layout_text_run *)runs->data;
        last = &items[runs->length - 1];
        if (last->image != NULL || last->rect.x >= limit) {
            runs->length--;
            continue;
        }
        while (last->text.size > 0 && last->rect.x +
               tbox_font_measure_text_spaced(last->font, last->text,
                   last->style->letter_spacing) > limit) {
            last->text.size = tbox_layout_previous_codepoint(last->text);
        }
        if (last->text.size == 0) {
            runs->length--;
            continue;
        }
        last->rect.width = tbox_font_measure_text_spaced(last->font, last->text,
            last->style->letter_spacing);
        break;
    }
    tbox_layout_text_run *mark = (tbox_layout_text_run *)tbox_vector_push(runs);
    mark->rect = (tbox_rect){right - glyph_width > content_x ? right - glyph_width : content_x,
        line_y + line->ascent - tbox_font_face_ascent(face), glyph_width, line->height};
    mark->text = ellipsis;
    mark->font = face;
    mark->style = style;
    mark->image = NULL;
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
/* Formats `index` (1-based) as a counter marker plus its trailing '.':
 * alphabetic markers count a..z, aa..zz, ...; roman numerals cover 1-3999
 * and fall back to decimal outside that range, like browsers do. Returns
 * the length written (without a NUL), 0 on failure. */
static size_t tbox_layout_format_list_counter(tbox_style_list_style_type type, size_t index, char *buffer, size_t size) {
    char digits[20];
    size_t count = 0;
    bool upper = type == TBOX_STYLE_LIST_STYLE_UPPER_ALPHA || type == TBOX_STYLE_LIST_STYLE_UPPER_ROMAN;
    if ((type == TBOX_STYLE_LIST_STYLE_LOWER_ALPHA || type == TBOX_STYLE_LIST_STYLE_UPPER_ALPHA) && index > 0) {
        for (size_t n = index; n > 0 && count < sizeof(digits); n = (n - 1) / 26)
            digits[count++] = (char)((upper ? 'A' : 'a') + (n - 1) % 26);
        for (size_t i = 0; i < count / 2; i++) {
            char tmp = digits[i];
            digits[i] = digits[count - 1 - i];
            digits[count - 1 - i] = tmp;
        }
    } else if ((type == TBOX_STYLE_LIST_STYLE_LOWER_ROMAN || type == TBOX_STYLE_LIST_STYLE_UPPER_ROMAN) &&
               index > 0 && index < 4000) {
        static const struct { size_t value; const char *text; } numerals[] = {
            {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"},
            {50, "l"}, {40, "xl"}, {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"},
        };
        size_t n = index;
        for (size_t i = 0; i < sizeof(numerals) / sizeof(numerals[0]); i++) {
            for (; n >= numerals[i].value; n -= numerals[i].value)
                for (const char *c = numerals[i].text; *c != '\0'; c++)
                    digits[count++] = upper ? (char)(*c - 'a' + 'A') : *c;
        }
    } else {
        int written = snprintf(digits, sizeof(digits), "%zu", index);
        if (written <= 0) return 0;
        count = (size_t)written < sizeof(digits) ? (size_t)written : sizeof(digits) - 1;
    }
    if (count + 1 > size) return 0;
    memcpy(buffer, digits, count);
    buffer[count] = '.';
    return count + 1;
}

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

    tbox_style_list_style_type type = style->list_style_type;
    if (type == TBOX_STYLE_LIST_STYLE_AUTO)
        type = parent_is_ul ? TBOX_STYLE_LIST_STYLE_DISC : TBOX_STYLE_LIST_STYLE_DECIMAL;
    if (type == TBOX_STYLE_LIST_STYLE_NONE) {
        return;
    }

    /* Glyph markers fall back to the plain bullet when the face lacks
     * U+25E6 (white bullet) or U+25AA (small black square). */
    if (type == TBOX_STYLE_LIST_STYLE_DISC || type == TBOX_STYLE_LIST_STYLE_CIRCLE ||
        type == TBOX_STYLE_LIST_STYLE_SQUARE) {
        static const tbox_string_view bullet = { "\xE2\x80\xA2", 3 };
        static const tbox_string_view circle = { "\xE2\x97\xA6", 3 };
        static const tbox_string_view square = { "\xE2\x96\xAA", 3 };
        tbox_string_view marker = bullet;
        if (type == TBOX_STYLE_LIST_STYLE_CIRCLE && tbox_font_face_has_glyph(face, 0x25E6)) marker = circle;
        if (type == TBOX_STYLE_LIST_STYLE_SQUARE && tbox_font_face_has_glyph(face, 0x25AA)) marker = square;
        tbox_layout_push_words(words, marker, face, style);
        return;
    }

    /* Counters: this <li>'s 1-based position among its direct <li>
     * siblings (same parent), in document order, never restarting across
     * sibling groups. */
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
    size_t length = tbox_layout_format_list_counter(type, index, buffer, sizeof(buffer));
    if (length == 0) {
        return;
    }

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
    double space_width                  = tbox_font_measure_text_spaced(face, space, style->letter_spacing) + style->word_spacing;

    tbox_string_view text = tbox_layout_transform_text(arena, tbox_html_node_text_content(arena, node),
                                                       style->text_transform);

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
        entry->width            = tbox_font_measure_text_spaced(face, line, style->letter_spacing);
        entry->space_width      = space_width;
        entry->image            = NULL;
        entry->image_height     = 0.0;
        entry->hard_break       = false;
        entry->no_space_before  = false;

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
    /* <pre> keeps its own verbatim path (whole-subtree text in the <pre>'s
     * face) for AUTO/`pre`; any other white-space mode on a <pre> goes
     * through the general per-node collection like every other element. */
    bool is_preformatted = node != NULL && tbox_string_view_equal_cstr(node->element.tag_name, "pre") &&
        (style->white_space == TBOX_STYLE_WHITE_SPACE_AUTO || style->white_space == TBOX_STYLE_WHITE_SPACE_PRE);
    bool is_input = node != NULL && tbox_string_view_equal_cstr(node->element.tag_name, "input");
    bool is_select = node != NULL && (tbox_string_view_equal_cstr(node->element.tag_name, "select") ||
        tbox_string_view_equal_cstr(node->element.tag_name, "textarea"));

    tbox_vector words;
    tbox_vector_init(&words, arena, sizeof(tbox_layout_word), 0);
    if (is_input) {
        const tbox_html_attribute *value = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
        const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
        tbox_string_view label = value != NULL ? value->value :
            type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("reset", 5)) ?
                tbox_string_view_make("Reset", 5) :
            type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("submit", 6)) ?
                tbox_string_view_make("Submit", 6) : tbox_string_view_make(NULL, 0);
        const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
        if (label.size > 0 && face != NULL) {
            tbox_string_view display = label;
            if (tbox_layout_is_password_input(node)) {
                bool bullet = tbox_font_face_has_glyph(face, 0x2022);
                size_t count = 0;
                for (size_t i = 0; i < label.size; i++)
                    if (((unsigned char)label.data[i] & 0xc0) != 0x80) count++;
                size_t unit = bullet ? 3 : 1;
                display = tbox_string_view_make(NULL, 0);
                char *masked = count <= SIZE_MAX / unit ? tbox_arena_alloc(arena, count * unit) : NULL;
                if (masked != NULL) {
                    for (size_t i = 0; i < count; i++) {
                        if (bullet) memcpy(masked + i * unit, "\xe2\x80\xa2", 3);
                        else masked[i] = '*';
                    }
                    display = tbox_string_view_make(masked, count * unit);
                }
            }
            tbox_layout_word *word = (tbox_layout_word *)tbox_vector_push(&words);
            word->text = display;
            word->face = face;
            word->style = style;
            word->width = tbox_font_measure_text_spaced(face, display, style->letter_spacing);
            word->space_width = 0.0;
            word->image = NULL;
            word->image_height = 0.0;
            word->hard_break = false;
            word->no_space_before = false;
        }
    } else if (is_select) {
        /* The selected label is supplied by Context after layout; option
         * descendants do not create boxes or contribute to select height. */
    } else if (is_preformatted) {
        tbox_layout_collect_preformatted_words(arena, node, style, fonts, &words);
    } else {
        if (node != NULL) {
            tbox_layout_push_list_marker(arena, node, style, fonts, &words);
        }
        tbox_layout_collect_words(arena, first_sibling, end_exclusive, style, styles, fonts, images, available_width, &words);
    }

    size_t word_count = tbox_vector_length(&words);
    if (word_count == 0) {
        box->text_runs      = NULL;
        box->text_run_count = 0;

        const tbox_font_face *own_face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
        return own_face != NULL ? tbox_layout_style_line_height(style, own_face) : 0.0;
    }

    tbox_vector lines;
    tbox_vector_init(&lines, arena, sizeof(tbox_layout_line), 0);
    double indent = style->text_indent.kind == TBOX_STYLE_LENGTH_PX ? style->text_indent.value :
        style->text_indent.kind == TBOX_STYLE_LENGTH_PERCENT ?
        available_width * style->text_indent.value / 100.0 : 0.0;
    bool no_wrap = is_preformatted || is_input || tbox_layout_white_space_nowrap(style);
    if (!no_wrap) tbox_layout_split_overlong_words(arena, &words, available_width, indent);
    const tbox_layout_word *word_items = (const tbox_layout_word *)words.data;
    word_count = tbox_vector_length(&words);
    tbox_layout_break_lines(word_items, word_count, available_width, indent,
        no_wrap, &lines);

    tbox_vector runs;
    tbox_vector_init(&runs, arena, sizeof(tbox_layout_text_run), 0);

    size_t line_count                  = tbox_vector_length(&lines);
    const tbox_layout_line *line_items = (const tbox_layout_line *)lines.data;

    const tbox_font_face *block_face = tbox_font_face_cache_get(fonts,
        tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
        style->font_italic, style->font_size);
    double cumulative_y = content_y;
    double total_height = 0.0;
    for (size_t li = 0; li < line_count; li++) {
        const tbox_layout_line *line = &line_items[li];

        size_t runs_before = tbox_vector_length(&runs);
        double justify_gap = 0.0;
        if (style->text_align == TBOX_STYLE_TEXT_ALIGN_JUSTIFY && !no_wrap && li + 1 < line_count &&
            !(line->end < word_count && word_items[line->end].hard_break)) {
            double used = li == 0 ? indent : 0.0;
            size_t gaps = 0;
            for (size_t w = line->start; w < line->end; w++) {
                used += word_items[w].width;
                if (w != line->start) used += word_items[w].space_width;
                if (w != line->start && word_items[w].space_width > 0.0) gaps++;
            }
            if (gaps > 0 && used < available_width) justify_gap = (available_width - used) / (double)gaps;
        }
        tbox_layout_build_line_runs(arena, word_items, line, cumulative_y,
            content_x + (li == 0 ? indent : 0.0), block_face, justify_gap, &runs);
        if (style->text_overflow == TBOX_STYLE_TEXT_OVERFLOW_ELLIPSIS &&
            tbox_layout_white_space_nowrap(style) && style->overflow_y == TBOX_STYLE_OVERFLOW_Y_HIDDEN &&
            !is_input && !is_select) {
            const tbox_font_face *face = tbox_font_face_cache_get(fonts,
                tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
                style->font_italic, style->font_size);
            tbox_layout_ellipsize_line(&runs, runs_before, content_x, available_width,
                style, face, line, cumulative_y);
        }
        size_t runs_after = tbox_vector_length(&runs);

        if ((style->text_align == TBOX_STYLE_TEXT_ALIGN_CENTER || style->text_align == TBOX_STYLE_TEXT_ALIGN_RIGHT) &&
            runs_after > runs_before) {
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

/* Constraints apply to content width; a minimum wins if it exceeds the
 * maximum. Percentages use the containing block's width. */
static double tbox_layout_constrain_width(const tbox_style *style, double width, double base, double edges) {
    if (style->max_width.kind != TBOX_STYLE_LENGTH_AUTO) {
        double maximum = tbox_layout_resolve_edge(style->max_width, base);
        if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX) maximum -= edges;
        if (maximum < 0.0) maximum = 0.0;
        if (width > maximum) width = maximum;
    }
    if (style->min_width.kind != TBOX_STYLE_LENGTH_AUTO) {
        double minimum = tbox_layout_resolve_edge(style->min_width, base);
        if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX) minimum -= edges;
        if (minimum < 0.0) minimum = 0.0;
        if (width < minimum) width = minimum;
    }
    return width;
}

/* Height constraints use the containing block's height only when definite.
 * CSS min-height wins if it exceeds max-height, as with width constraints. */
static double tbox_layout_constrain_height(const tbox_style *style, double height,
                                           double base, bool base_definite, double edges) {
    if (style->max_height.kind == TBOX_STYLE_LENGTH_PX ||
        (style->max_height.kind == TBOX_STYLE_LENGTH_PERCENT && base_definite)) {
        double maximum = tbox_layout_resolve_edge(style->max_height, base);
        if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX) maximum -= edges;
        if (maximum < 0.0) maximum = 0.0;
        if (height > maximum) height = maximum;
    }
    if (style->min_height.kind == TBOX_STYLE_LENGTH_PX ||
        (style->min_height.kind == TBOX_STYLE_LENGTH_PERCENT && base_definite)) {
        double minimum = tbox_layout_resolve_edge(style->min_height, base);
        if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX) minimum -= edges;
        if (minimum < 0.0) minimum = 0.0;
        if (height < minimum) height = minimum;
    }
    return height;
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
        if (tbox_layout_is_hidden_input(child)) return false;
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

        if (tbox_layout_is_hidden_input(node)) continue;

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
 * the inherited fields needed by anonymous text and pointer hit testing
 * from `container_style` (see
 * include/tbox/style.h's "inheritable" comments on each field): `color`,
 * `font_family` (the whole fixed buffer, via memcpy -- not a pointer),
 * and every other field style.h marks inheritable (text layout, text
 * painting, table and form-control properties) except `text_indent`, which
 * only indents the block's own first line, not each anonymous box. This is
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
    anon.overflow_wrap_break_word = container_style->overflow_wrap_break_word;
    anon.pointer_events_none = container_style->pointer_events_none;
    anon.visibility_hidden = container_style->visibility_hidden;
    anon.white_space       = container_style->white_space;
    anon.word_break_all    = container_style->word_break_all;
    anon.text_transform    = container_style->text_transform;
    anon.list_style_type   = container_style->list_style_type;
    anon.word_spacing      = container_style->word_spacing;
    anon.letter_spacing    = container_style->letter_spacing;
    anon.line_height_kind  = container_style->line_height_kind;
    anon.line_height_value = container_style->line_height_value;
    anon.text_underline_offset = container_style->text_underline_offset;
    anon.text_shadow_offset_x  = container_style->text_shadow_offset_x;
    anon.text_shadow_offset_y  = container_style->text_shadow_offset_y;
    anon.text_shadow_blur      = container_style->text_shadow_blur;
    anon.text_shadow_color     = container_style->text_shadow_color;
    anon.caption_side      = container_style->caption_side;
    anon.border_collapse   = container_style->border_collapse;
    anon.border_spacing_x  = container_style->border_spacing_x;
    anon.border_spacing_y  = container_style->border_spacing_y;
    anon.accent_color      = container_style->accent_color;
    anon.caret_color       = container_style->caret_color;

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

/* Original direct-row table path, retained for the existing simple tables.
 * The extended grid path below handles sections, spans, captions and cols. */

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

static double tbox_layout_table_longest_word(const tbox_font_face *face, tbox_string_view text, double letter_spacing);
static void tbox_layout_table_shift_y(tbox_layout_box *box, double amount);

/* Approximate auto table layout with the minimum (longest word) and maximum
 * (unwrapped text) width of each column. Interpolating between those limits
 * lets a long, wrapping cell use available room without taking it from
 * columns whose contents already fit. */
static void tbox_layout_table_compute_column_widths(tbox_arena *arena, const tbox_html_node *table_node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, size_t column_count, double *content_width, double *out_widths) {
    double *min_widths = tbox_arena_alloc_zero(arena, sizeof(double) * column_count);
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
            double padding_left          = tbox_layout_resolve_edge(cell_style->padding[3], *content_width);
            double padding_right         = tbox_layout_resolve_edge(cell_style->padding[1], *content_width);
            double border_x              = tbox_style_border_side_width(cell_style, 1) + tbox_style_border_side_width(cell_style, 3);

            double text_width          = 0.0;
            double word_width          = 0.0;
            tbox_vector words;
            tbox_vector_init(&words, arena, sizeof(tbox_layout_word), 0);
            tbox_layout_collect_words(arena, cell->first_child, NULL, cell_style, styles, fonts, images, *content_width, &words);
            if (words.length > 0) {
                double line_width = 0.0;
                bool first_word = true;
                for (size_t i = 0; i < words.length; i++) {
                    const tbox_layout_word *word = tbox_vector_at(&words, i);
                    if (word->hard_break) {
                        if (line_width > text_width) text_width = line_width;
                        line_width = 0.0;
                        first_word = true;
                        continue;
                    }
                    if (!first_word) line_width += word->space_width;
                    line_width += word->width;
                    if (word->width > word_width) word_width = word->width;
                    first_word = false;
                }
                if (line_width > text_width) text_width = line_width;
            } else {
                const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(cell_style->font_family), cell_style->font_weight_bold, cell_style->font_italic, cell_style->font_size);
                tbox_string_view text = tbox_html_node_text_content(arena, cell);
                if (face != NULL) {
                    text_width = tbox_font_measure_text_spaced(face, text, cell_style->letter_spacing);
                    word_width = tbox_layout_table_longest_word(face, text, cell_style->letter_spacing);
                }
            }

            double edges = padding_left + padding_right + border_x;
            double natural_width = text_width + edges;
            double minimum_width = word_width + edges;
            if (cell_style->min_width.kind != TBOX_STYLE_LENGTH_AUTO) {
                double css_min = tbox_layout_resolve_edge(cell_style->min_width, *content_width) +
                    (cell_style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX ? 0.0 : edges);
                if (minimum_width < css_min) minimum_width = css_min;
                if (natural_width < css_min) natural_width = css_min;
            }
            if (natural_width > out_widths[column_index]) {
                out_widths[column_index] = natural_width;
            }
            if (minimum_width > min_widths[column_index]) {
                min_widths[column_index] = minimum_width;
            }

            column_index++;
        }
    }

    double total_natural = 0.0;
    double total_minimum = 0.0;
    for (size_t i = 0; i < column_count; i++) {
        total_natural += out_widths[i];
        total_minimum += min_widths[i];
    }
    if (*content_width < total_minimum) *content_width = total_minimum;

    if (total_natural <= 0.0) {
        double equal_share = *content_width / (double)column_count;
        for (size_t i = 0; i < column_count; i++) {
            out_widths[i] = equal_share;
        }
        return;
    }

    if (*content_width >= total_minimum && *content_width < total_natural) {
        double fraction = (*content_width - total_minimum) / (total_natural - total_minimum);
        for (size_t i = 0; i < column_count; i++) {
            out_widths[i] = min_widths[i] + fraction * (out_widths[i] - min_widths[i]);
        }
    } else {
        double scale = *content_width / total_natural;
        for (size_t i = 0; i < column_count; i++) {
            out_widths[i] *= scale;
        }
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
 * height: the MAX of every cell's margin_box.height. All cell borders and
 * backgrounds then extend to that height, as in a browser table row. */
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

    for (tbox_layout_box *cell_box = row_box->first_child; cell_box != NULL; cell_box = cell_box->next_sibling) {
        double growth = max_height - cell_box->margin_box.height;
        if (growth <= 0.0) continue;
        const tbox_style *style = cell_box->style;
        double align = style->vertical_align == TBOX_STYLE_VERTICAL_ALIGN_BOTTOM ? growth :
            style->vertical_align == TBOX_STYLE_VERTICAL_ALIGN_MIDDLE ? growth / 2.0 : 0.0;
        for (size_t run = 0; run < cell_box->text_run_count; run++)
            cell_box->text_runs[run].rect.y += align;
        for (tbox_layout_box *child = cell_box->first_child; child != NULL; child = child->next_sibling)
            tbox_layout_table_shift_y(child, align);
        cell_box->margin_box.height += growth;
        cell_box->border_box.height += growth;
        cell_box->padding_box.height += growth;
        cell_box->content_box.height += growth;
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
static double tbox_layout_build_table_children(tbox_arena *arena, const tbox_html_node *table_node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double content_y, double *content_width, tbox_layout_box *table_box, tbox_layout_positioned_context positioned_context) {
    size_t column_count = tbox_layout_table_column_count(table_node);
    if (column_count == 0) {
        return 0.0;
    }

    double *column_widths = (double *)tbox_arena_alloc(arena, sizeof(double) * column_count);
    tbox_layout_table_compute_column_widths(arena, table_node, styles, fonts, images, column_count, content_width, column_widths);

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
            .width           = *content_width,
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

/* The extended table path keeps a small, explicit cell grid. The original
 * path above remains in use for plain direct-row tables. */
#define TBOX_TABLE_MAX_COLUMNS 1000
typedef struct tbox_table_row {
    const tbox_html_node *node, *group;
    size_t group_id;
    tbox_layout_box *box;
    double height, y;
} tbox_table_row;

typedef struct tbox_table_cell {
    const tbox_html_node *node;
    tbox_layout_box *box;
    size_t row, column, rows, columns;
} tbox_table_cell;

typedef struct tbox_table_column {
    const tbox_html_node *node, *group;
    double min_width, width, x;
} tbox_table_column;

static bool tbox_layout_tag(const tbox_html_node *node, const char *name) {
    return node != NULL && node->type == TBOX_HTML_NODE_ELEMENT &&
        tbox_string_view_equal_cstr(node->element.tag_name, name);
}

static bool tbox_layout_table_section(const tbox_html_node *node) {
    return tbox_layout_tag(node, "thead") || tbox_layout_tag(node, "tbody") ||
        tbox_layout_tag(node, "tfoot");
}

static bool tbox_layout_table_cell_node(const tbox_html_node *node) {
    return tbox_layout_tag(node, "td") || tbox_layout_tag(node, "th");
}

static size_t tbox_layout_table_span(const tbox_html_node *node, const char *name, bool zero_valid) {
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr(name));
    if (attribute == NULL || attribute->value.size == 0) return 1;
    size_t value = 0;
    for (size_t i = 0; i < attribute->value.size; i++) {
        char c = attribute->value.data[i];
        if (c < '0' || c > '9') return 1;
        value = value * 10 + (size_t)(c - '0');
        if (value > TBOX_TABLE_MAX_COLUMNS) return TBOX_TABLE_MAX_COLUMNS;
    }
    return value == 0 && !zero_valid ? 1 : value;
}

static double tbox_layout_table_css_width(const tbox_style *style, double base) {
    double width = 0.0;
    if (style->width.kind == TBOX_STYLE_LENGTH_PX) width = style->width.value;
    if (style->width.kind == TBOX_STYLE_LENGTH_PERCENT) {
        width = base * style->width.value / 100.0;
    }
    width = tbox_layout_constrain_width(style, width, base, 0.0);
    return width > 0.0 ? width : 0.0;
}

static void tbox_layout_table_append(tbox_layout_box *parent, tbox_layout_box *child) {
    child->parent = parent;
    if (parent->last_child != NULL) parent->last_child->next_sibling = child;
    else parent->first_child = child;
    parent->last_child = child;
}

static tbox_layout_box *tbox_layout_table_box(tbox_arena *arena, const tbox_html_node *node,
                                               const tbox_style_table *styles, tbox_rect rect) {
    tbox_layout_box *box = tbox_arena_alloc_zero(arena, sizeof(*box));
    box->node = node;
    box->style = tbox_layout_style_or_default(styles, node);
    box->margin_box = box->border_box = box->padding_box = box->content_box = rect;
    return box;
}

static void tbox_layout_table_shift_y(tbox_layout_box *box, double amount) {
    box->margin_box.y += amount;
    box->border_box.y += amount;
    box->padding_box.y += amount;
    box->content_box.y += amount;
    for (size_t i = 0; i < box->text_run_count; i++) box->text_runs[i].rect.y += amount;
    for (tbox_layout_box *child = box->first_child; child != NULL; child = child->next_sibling)
        tbox_layout_table_shift_y(child, amount);
}

static bool tbox_layout_table_extended(const tbox_html_node *table, const tbox_style_table *styles) {
    const tbox_style *style = tbox_layout_style_or_default(styles, table);
    if (style->border_collapse || style->border_spacing_x > 0.0 || style->border_spacing_y > 0.0) return true;
    for (const tbox_html_node *child = table->first_child; child != NULL; child = child->next_sibling) {
        if (tbox_layout_table_section(child) || tbox_layout_tag(child, "caption") ||
            tbox_layout_tag(child, "col") || tbox_layout_tag(child, "colgroup")) return true;
        if (!tbox_layout_tag(child, "tr")) continue;
        if (tbox_layout_style_or_default(styles, child)->display == TBOX_STYLE_DISPLAY_NONE) return true;
        for (const tbox_html_node *cell = child->first_child; cell != NULL; cell = cell->next_sibling)
            if (tbox_layout_table_cell_node(cell) &&
                (tbox_html_node_get_attribute(cell, tbox_string_view_make("colspan", 7)) != NULL ||
                 tbox_html_node_get_attribute(cell, tbox_string_view_make("rowspan", 7)) != NULL ||
                 tbox_layout_style_or_default(styles, cell)->display == TBOX_STYLE_DISPLAY_NONE ||
                 tbox_layout_style_or_default(styles, cell)->width.kind != TBOX_STYLE_LENGTH_AUTO)) return true;
    }
    return false;
}

/* Collapsed borders: the widest `side` (0 top, 1 right, 2 bottom, 3 left)
 * among the boxes meeting at an edge wins; later calls win ties. */
static void tbox_layout_table_choose_border(const tbox_style *style, size_t side, double *width, tbox_css_rgba *color) {
    double side_width = tbox_style_border_side_width(style, side);
    if (style != NULL && side_width >= *width && side_width > 0.0) {
        *width = side_width;
        *color = tbox_style_border_side_color(style, side);
    }
}

static double tbox_layout_table_longest_word(const tbox_font_face *face, tbox_string_view text, double letter_spacing) {
    double longest = 0.0;
    for (size_t start = 0; start < text.size;) {
        while (start < text.size && (text.data[start] == ' ' || text.data[start] == '\t' ||
            text.data[start] == '\n' || text.data[start] == '\r')) start++;
        size_t end = start;
        while (end < text.size && text.data[end] != ' ' && text.data[end] != '\t' &&
            text.data[end] != '\n' && text.data[end] != '\r') end++;
        if (end > start) {
            double width = tbox_font_measure_text_spaced(face, tbox_string_view_make(text.data + start, end - start), letter_spacing);
            if (width > longest) longest = width;
        }
        start = end;
    }
    return longest;
}

static double tbox_layout_build_table_extended(tbox_arena *arena, const tbox_html_node *table,
    const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images,
    double x, double y, double *table_width, tbox_layout_box *table_box,
    tbox_layout_positioned_context positioned_context) {
    const tbox_style *table_style = table_box->style;
    bool collapse = table_style->border_collapse;
    double sx = collapse ? 0.0 : table_style->border_spacing_x;
    double sy = collapse ? 0.0 : table_style->border_spacing_y;
    tbox_vector rows, cells;
    tbox_vector_init(&rows, arena, sizeof(tbox_table_row), 0);
    tbox_vector_init(&cells, arena, sizeof(tbox_table_cell), 0);
    tbox_table_column *columns = tbox_arena_alloc_zero(arena,
        sizeof(*columns) * TBOX_TABLE_MAX_COLUMNS);
    if (columns == NULL) return 0.0;
    size_t declared = 0, group_id = 0;
    const tbox_html_node *caption = NULL;
    for (const tbox_html_node *child = table->first_child; child != NULL; child = child->next_sibling) {
        if (child->type != TBOX_HTML_NODE_ELEMENT) continue;
        if (tbox_layout_style_or_default(styles, child)->display == TBOX_STYLE_DISPLAY_NONE) continue;
        if (tbox_layout_tag(child, "caption")) {
            if (caption == NULL) caption = child;
        } else if (tbox_layout_tag(child, "col") && declared < TBOX_TABLE_MAX_COLUMNS) {
            size_t span = tbox_layout_table_span(child, "span", false);
            for (size_t i = 0; i < span && declared < TBOX_TABLE_MAX_COLUMNS; i++)
                columns[declared++] = (tbox_table_column){.node = child};
        } else if (tbox_layout_tag(child, "colgroup")) {
            size_t before = declared;
            for (const tbox_html_node *col = child->first_child; col != NULL; col = col->next_sibling) {
                if (!tbox_layout_tag(col, "col") ||
                    tbox_layout_style_or_default(styles, col)->display == TBOX_STYLE_DISPLAY_NONE) continue;
                size_t span = tbox_layout_table_span(col, "span", false);
                for (size_t i = 0; i < span && declared < TBOX_TABLE_MAX_COLUMNS; i++)
                    columns[declared++] = (tbox_table_column){.node = col, .group = child};
            }
            if (declared == before) {
                size_t span = tbox_layout_table_span(child, "span", false);
                for (size_t i = 0; i < span && declared < TBOX_TABLE_MAX_COLUMNS; i++)
                    columns[declared++] = (tbox_table_column){.group = child};
            }
        } else if (tbox_layout_tag(child, "tr")) {
            *(tbox_table_row *)tbox_vector_push(&rows) =
                (tbox_table_row){.node = child, .group_id = group_id};
        } else if (tbox_layout_table_section(child)) {
            group_id++;
            for (const tbox_html_node *row = child->first_child; row != NULL; row = row->next_sibling) {
                if (tbox_layout_tag(row, "tr") &&
                    tbox_layout_style_or_default(styles, row)->display != TBOX_STYLE_DISPLAY_NONE)
                    *(tbox_table_row *)tbox_vector_push(&rows) =
                        (tbox_table_row){.node = row, .group = child, .group_id = group_id};
            }
            group_id++;
        }
    }
    size_t occupied_until[TBOX_TABLE_MAX_COLUMNS] = {0};
    size_t column_count = declared;
    size_t previous_group = SIZE_MAX;
    for (size_t r = 0; r < rows.length; r++) {
        tbox_table_row *row = tbox_vector_at(&rows, r);
        if (row->group_id != previous_group) {
            memset(occupied_until, 0, sizeof(occupied_until));
            previous_group = row->group_id;
        }
        size_t group_end = r + 1;
        while (group_end < rows.length &&
            ((tbox_table_row *)tbox_vector_at(&rows, group_end))->group_id == row->group_id) group_end++;
        size_t cursor = 0;
        for (const tbox_html_node *cell = row->node->first_child; cell != NULL; cell = cell->next_sibling) {
            if (!tbox_layout_table_cell_node(cell) ||
                tbox_layout_style_or_default(styles, cell)->display == TBOX_STYLE_DISPLAY_NONE) continue;
            size_t span = tbox_layout_table_span(cell, "colspan", false);
            while (cursor < TBOX_TABLE_MAX_COLUMNS) {
                if (cursor + span > TBOX_TABLE_MAX_COLUMNS) span = TBOX_TABLE_MAX_COLUMNS - cursor;
                bool free = span > 0;
                for (size_t i = 0; i < span; i++)
                    if (occupied_until[cursor + i] > r) { free = false; break; }
                if (free) break;
                cursor++;
            }
            if (cursor >= TBOX_TABLE_MAX_COLUMNS || span == 0) break;
            size_t rowspan = tbox_layout_table_span(cell, "rowspan", true);
            if (rowspan == 0 || rowspan > group_end - r) rowspan = group_end - r;
            for (size_t i = 0; i < span; i++) occupied_until[cursor + i] = r + rowspan;
            *(tbox_table_cell *)tbox_vector_push(&cells) =
                (tbox_table_cell){.node = cell, .row = r, .column = cursor,
                                  .rows = rowspan, .columns = span};
            if (cursor + span > column_count) column_count = cursor + span;
            cursor += span;
        }
    }
    for (size_t c = 0; c < column_count; c++) {
        tbox_table_column *col = &columns[c];
        if (col->group != NULL)
            col->min_width = tbox_layout_table_css_width(tbox_layout_style_or_default(styles, col->group), *table_width);
        if (col->node != NULL) {
            double width = tbox_layout_table_css_width(tbox_layout_style_or_default(styles, col->node), *table_width);
            if (width > 0.0) col->min_width = width;
        }
    }
    for (size_t i = 0; i < cells.length; i++) {
        tbox_table_cell *cell = tbox_vector_at(&cells, i);
        const tbox_style *style = tbox_layout_style_or_default(styles, cell->node);
        double css_width = tbox_layout_table_css_width(style, *table_width);
        double edges = tbox_layout_resolve_edge(style->padding[1], *table_width) +
            tbox_layout_resolve_edge(style->padding[3], *table_width) +
            tbox_style_border_side_width(style, 1) + tbox_style_border_side_width(style, 3);
        double minimum = 0.0;
        const tbox_font_face *face = tbox_font_face_cache_get(fonts,
            tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
            style->font_italic, style->font_size);
        if (face != NULL) {
            double natural = tbox_layout_table_longest_word(face,
                tbox_html_node_text_content(arena, cell->node), style->letter_spacing);
            minimum = natural;
        }
        minimum += edges;
        double css_outer = css_width +
            (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX ? 0.0 : edges);
        if (css_outer > minimum) minimum = css_outer;
        double current = sx * (cell->columns - 1);
        for (size_t c = cell->column; c < cell->column + cell->columns; c++) current += columns[c].min_width;
        if (minimum > current) {
            double each = (minimum - current) / cell->columns;
            for (size_t c = cell->column; c < cell->column + cell->columns; c++) columns[c].min_width += each;
        }
    }
    double minimum_total = sx * (column_count + 1);
    for (size_t c = 0; c < column_count; c++) minimum_total += columns[c].min_width;
    if (minimum_total > *table_width) *table_width = minimum_total;
    double extra = *table_width - minimum_total;
    for (size_t c = 0; c < column_count; c++) {
        columns[c].width = columns[c].min_width + (column_count > 0 ? extra / column_count : 0.0);
        columns[c].x = x + sx;
        if (c > 0) columns[c].x = columns[c - 1].x + columns[c - 1].width + sx;
    }
    tbox_layout_box *caption_box = NULL;
    double caption_height = 0.0;
    if (caption != NULL) {
        tbox_layout_containing_block container = {
            .x = x, .y = y, .width = *table_width, .height = 0.0, .height_definite = false};
        caption_box = tbox_layout_build_element(arena, caption, styles, fonts, images,
            container, y, positioned_context, NULL, 0);
        caption_height = caption_box->margin_box.height;
    }
    for (size_t i = 0; i < cells.length; i++) {
        tbox_table_cell *cell = tbox_vector_at(&cells, i);
        double width = sx * (cell->columns - 1);
        for (size_t c = cell->column; c < cell->column + cell->columns; c++) width += columns[c].width;
        tbox_layout_containing_block container = {
            .x = columns[cell->column].x, .y = 0.0, .width = width,
            .height = 0.0, .height_definite = false};
        /* The non-null marker asks build_element to fit the cell into the grid
         * even when CSS width supplied its earlier column constraint. */
        cell->box = tbox_layout_build_element(arena, cell->node, styles, fonts, images,
            container, 0.0, positioned_context, &width, 0);
        if (cell->rows == 1) {
            tbox_table_row *row = tbox_vector_at(&rows, cell->row);
            if (cell->box->margin_box.height > row->height) row->height = cell->box->margin_box.height;
        }
        if (collapse) cell->box->table_suppress_border = true;
    }
    for (size_t i = 0; i < cells.length; i++) {
        tbox_table_cell *cell = tbox_vector_at(&cells, i);
        if (cell->rows == 1) continue;
        double available = sy * (cell->rows - 1);
        for (size_t r = cell->row; r < cell->row + cell->rows; r++)
            available += ((tbox_table_row *)tbox_vector_at(&rows, r))->height;
        if (cell->box->margin_box.height > available) {
            double increment = (cell->box->margin_box.height - available) / cell->rows;
            for (size_t r = cell->row; r < cell->row + cell->rows; r++)
                ((tbox_table_row *)tbox_vector_at(&rows, r))->height += increment;
        }
    }
    double grid_top = y + (caption_box != NULL && caption_box->style->caption_side == TBOX_STYLE_CAPTION_TOP ?
        caption_height : 0.0);
    double cursor_y = grid_top + sy;
    for (size_t r = 0; r < rows.length; r++) {
        tbox_table_row *row = tbox_vector_at(&rows, r);
        row->y = cursor_y;
        row->box = tbox_layout_table_box(arena, row->node, styles,
            (tbox_rect){x + sx, cursor_y, *table_width - 2.0 * sx, row->height});
        if (collapse) row->box->table_suppress_border = true;
        cursor_y += row->height + sy;
    }
    double grid_height = rows.length > 0 ? cursor_y - grid_top : 0.0;
    for (size_t i = 0; i < cells.length; i++) {
        tbox_table_cell *cell = tbox_vector_at(&cells, i);
        tbox_table_row *row = tbox_vector_at(&rows, cell->row);
        double target = sy * (cell->rows - 1);
        for (size_t r = cell->row; r < cell->row + cell->rows; r++)
            target += ((tbox_table_row *)tbox_vector_at(&rows, r))->height;
        double growth = target - cell->box->margin_box.height;
        if (growth < 0.0) growth = 0.0;
        tbox_layout_table_shift_y(cell->box, row->y);
        const tbox_style *style = cell->box->style;
        double align = style->vertical_align == TBOX_STYLE_VERTICAL_ALIGN_BOTTOM ? growth :
            style->vertical_align == TBOX_STYLE_VERTICAL_ALIGN_MIDDLE ? growth / 2.0 : 0.0;
        for (size_t run = 0; run < cell->box->text_run_count; run++)
            cell->box->text_runs[run].rect.y += align;
        for (tbox_layout_box *child = cell->box->first_child; child != NULL; child = child->next_sibling)
            tbox_layout_table_shift_y(child, align);
        cell->box->margin_box.height += growth;
        cell->box->border_box.height += growth;
        cell->box->padding_box.height += growth;
        cell->box->content_box.height += growth;
        tbox_layout_table_append(row->box, cell->box);
    }
    /* Column backgrounds are laid out before rows so ordinary paint order
     * puts them beneath row and cell backgrounds. */
    for (size_t c = 0; c < column_count;) {
        const tbox_html_node *group = columns[c].group;
        size_t end = c + 1;
        while (group != NULL && end < column_count && columns[end].group == group) end++;
        tbox_layout_box *parent = table_box;
        if (group != NULL) {
            double width = columns[end - 1].x + columns[end - 1].width - columns[c].x;
            tbox_layout_box *group_box = tbox_layout_table_box(arena, group, styles,
                (tbox_rect){columns[c].x, grid_top, width, grid_height});
            group_box->table_suppress_border = true;
            tbox_layout_table_append(table_box, group_box);
            parent = group_box;
        }
        for (size_t index = c; index < end; index++) {
            if (columns[index].node == NULL) continue;
            tbox_layout_box *col_box = tbox_layout_table_box(arena, columns[index].node, styles,
                (tbox_rect){columns[index].x, grid_top, columns[index].width, grid_height});
            col_box->table_suppress_border = true;
            tbox_layout_table_append(parent, col_box);
        }
        c = end;
    }
    if (caption_box != NULL && caption_box->style->caption_side == TBOX_STYLE_CAPTION_TOP)
        tbox_layout_table_append(table_box, caption_box);
    tbox_layout_box *group_box = NULL;
    const tbox_html_node *last_group = NULL;
    for (size_t r = 0; r < rows.length; r++) {
        tbox_table_row *row = tbox_vector_at(&rows, r);
        if (row->group != last_group) {
            group_box = NULL;
            last_group = row->group;
        }
        if (row->group != NULL && group_box == NULL) {
            group_box = tbox_layout_table_box(arena, row->group, styles,
                (tbox_rect){x + sx, row->y, *table_width - 2.0 * sx, 0.0});
            if (collapse) group_box->table_suppress_border = true;
            tbox_layout_table_append(table_box, group_box);
        }
        tbox_layout_table_append(group_box != NULL ? group_box : table_box, row->box);
        if (group_box != NULL) group_box->margin_box.height = group_box->border_box.height =
            group_box->padding_box.height = group_box->content_box.height =
            row->y + row->height - group_box->content_box.y;
    }
    if (caption_box != NULL && caption_box->style->caption_side == TBOX_STYLE_CAPTION_BOTTOM) {
        tbox_layout_table_shift_y(caption_box, grid_height);
        tbox_layout_table_append(table_box, caption_box);
    }
    if (collapse && rows.length > 0 && column_count > 0 &&
        rows.length <= SIZE_MAX / (column_count * sizeof(tbox_table_cell *))) {
        tbox_table_cell **grid = tbox_arena_alloc_zero(arena,
            sizeof(*grid) * rows.length * column_count);
        if (grid != NULL) {
            for (size_t i = 0; i < cells.length; i++) {
                tbox_table_cell *cell = tbox_vector_at(&cells, i);
                for (size_t r = cell->row; r < cell->row + cell->rows; r++)
                    for (size_t c = cell->column; c < cell->column + cell->columns; c++)
                        grid[r * column_count + c] = cell;
            }
            tbox_vector edges;
            tbox_vector_init(&edges, arena, sizeof(tbox_table_edge), 0);
            for (size_t r = 0; r < rows.length; r++) {
                const tbox_table_row *row = tbox_vector_at(&rows, r);
                for (size_t c = 0; c <= column_count; c++) {
                    tbox_table_cell *left = c > 0 ? grid[r * column_count + c - 1] : NULL;
                    tbox_table_cell *right = c < column_count ? grid[r * column_count + c] : NULL;
                    if (left != NULL && left == right) continue;
                    double width = 0.0;
                    tbox_css_rgba color = {0, 0, 0, 255};
                    if (c == 0 || c == column_count) {
                        size_t side = c == 0 ? 3 : 1;
                        tbox_layout_table_choose_border(table_style, side, &width, &color);
                        if (row->group != NULL)
                            tbox_layout_table_choose_border(tbox_layout_style_or_default(styles, row->group),
                                side, &width, &color);
                        tbox_layout_table_choose_border(row->box->style, side, &width, &color);
                    }
                    if (left != NULL) tbox_layout_table_choose_border(left->box->style, 1, &width, &color);
                    if (right != NULL) tbox_layout_table_choose_border(right->box->style, 3, &width, &color);
                    if (width <= 0.0) continue;
                    double boundary = c == column_count ?
                        columns[c - 1].x + columns[c - 1].width : columns[c].x;
                    *(tbox_table_edge *)tbox_vector_push(&edges) = (tbox_table_edge){
                        .rect = {boundary - width / 2.0, row->y, width, row->height}, .color = color};
                }
            }
            for (size_t r = 0; r <= rows.length; r++) {
                for (size_t c = 0; c < column_count; c++) {
                    tbox_table_cell *above = r > 0 ? grid[(r - 1) * column_count + c] : NULL;
                    tbox_table_cell *below = r < rows.length ? grid[r * column_count + c] : NULL;
                    if (above != NULL && above == below) continue;
                    double width = 0.0;
                    tbox_css_rgba color = {0, 0, 0, 255};
                    if (r == 0 || r == rows.length)
                        tbox_layout_table_choose_border(table_style, r == 0 ? 0 : 2, &width, &color);
                    if (r > 0) {
                        const tbox_table_row *row = tbox_vector_at(&rows, r - 1);
                        tbox_layout_table_choose_border(row->box->style, 2, &width, &color);
                        if (r == rows.length || row->group !=
                            ((tbox_table_row *)tbox_vector_at(&rows, r))->group)
                            if (row->group != NULL)
                                tbox_layout_table_choose_border(tbox_layout_style_or_default(styles, row->group),
                                    2, &width, &color);
                    }
                    if (r < rows.length) {
                        const tbox_table_row *row = tbox_vector_at(&rows, r);
                        tbox_layout_table_choose_border(row->box->style, 0, &width, &color);
                        if (r == 0 || row->group !=
                            ((tbox_table_row *)tbox_vector_at(&rows, r - 1))->group)
                            if (row->group != NULL)
                                tbox_layout_table_choose_border(tbox_layout_style_or_default(styles, row->group),
                                    0, &width, &color);
                    }
                    if (above != NULL) tbox_layout_table_choose_border(above->box->style, 2, &width, &color);
                    if (below != NULL) tbox_layout_table_choose_border(below->box->style, 0, &width, &color);
                    if (width <= 0.0) continue;
                    double boundary = r == rows.length ?
                        ((tbox_table_row *)tbox_vector_at(&rows, r - 1))->y +
                        ((tbox_table_row *)tbox_vector_at(&rows, r - 1))->height :
                        ((tbox_table_row *)tbox_vector_at(&rows, r))->y;
                    *(tbox_table_edge *)tbox_vector_push(&edges) = (tbox_table_edge){
                        .rect = {columns[c].x, boundary - width / 2.0, columns[c].width, width},
                        .color = color};
                }
            }
            table_box->table_suppress_border = true;
            table_box->table_edges = edges.data;
            table_box->table_edge_count = edges.length;
        }
    }
    return grid_height + caption_height;
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
        if (tbox_layout_is_hidden_input(child) || child_style->display == TBOX_STYLE_DISPLAY_NONE) {
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
    if (tbox_layout_table_cell_node(node)) {
        for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
            if (child->type != TBOX_HTML_NODE_ELEMENT) continue;
            if (tbox_layout_tag(child, "div") || tbox_layout_tag(child, "p") ||
                tbox_layout_tag(child, "table") || tbox_layout_tag(child, "ul") ||
                tbox_layout_tag(child, "ol") || tbox_layout_tag(child, "section")) {
                is_text_tag = false;
                break;
            }
        }
    }
    bool is_image_input     = tbox_layout_is_image_input(node);
    const tbox_html_attribute *image_src = is_image_input ?
        tbox_html_node_get_attribute(node, tbox_string_view_make("src", 3)) : NULL;
    const tbox_image *input_image = image_src != NULL ? tbox_image_cache_get(images, image_src->value) : NULL;
    const tbox_html_attribute *image_alt = is_image_input && input_image == NULL ?
        tbox_html_node_get_attribute(node, tbox_string_view_make("alt", 3)) : NULL;
    tbox_string_view fallback_text = image_alt != NULL ?
        tbox_string_collapse_whitespace(arena, image_alt->value) : tbox_string_view_make(NULL, 0);
    const tbox_font_face *fallback_face = fallback_text.size > 0 ?
        tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family),
                                 style->font_weight_bold, style->font_italic, style->font_size) : NULL;

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
    bool is_table_row = row_column_widths != NULL && row_column_count > 0 && tbox_layout_tag(node, "tr");

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

    /* NOVO v4: `border` occupies space exactly like padding does, but only when
     * `border-style: solid` actually applies; a declared border-width/color
     * without `solid` (or with the initial `none`) occupies zero space,
     * same as not declaring `border` at all. */
    double border_top    = tbox_style_border_side_width(style, 0);
    double border_right  = tbox_style_border_side_width(style, 1);
    double border_bottom = tbox_style_border_side_width(style, 2);
    double border_left   = tbox_style_border_side_width(style, 3);
    double horizontal_edges = padding_left + padding_right + border_left + border_right;
    double vertical_edges = padding_top + padding_bottom + border_top + border_bottom;

    /* Width: the same rule for every node, text-tag or not -- see
     * ARCHITECTURE.md's clarification that measured text never resizes the
     * box (D4). Border counts against the available width in the AUTO
     * branch exactly like padding does. */
    double content_width;
    switch (style->width.kind) {
    case TBOX_STYLE_LENGTH_PX:
        content_width = style->width.value -
            (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX ? horizontal_edges : 0.0);
        break;
    case TBOX_STYLE_LENGTH_PERCENT:
        content_width = style->width.value / 100.0 * container.width -
            (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX ? horizontal_edges : 0.0);
        break;
    case TBOX_STYLE_LENGTH_AUTO:
    default:
        content_width = container.width - margin_left - margin_right - horizontal_edges;
        /* An absolute/fixed box with both `left` and `right` stretches
         * between them (CSS2.1 10.3.7), instead of taking the whole width. */
        if (is_out_of_flow && style->offset[1].kind != TBOX_STYLE_LENGTH_AUTO &&
            style->offset[3].kind != TBOX_STYLE_LENGTH_AUTO)
            content_width -= tbox_layout_resolve_edge(style->offset[1], container.width) +
                tbox_layout_resolve_edge(style->offset[3], container.width);
        if (is_image_input) {
            content_width = input_image != NULL ? (double)input_image->width :
                fallback_face != NULL ? tbox_font_measure_text(fallback_face, fallback_text) : 16.0;
            if (input_image != NULL && input_image->height > 0) {
                if (style->height.kind == TBOX_STYLE_LENGTH_PX)
                    content_width = (style->height.value -
                        (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX ? vertical_edges : 0.0)) *
                        input_image->width / input_image->height;
                else if (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite)
                    content_width = (style->height.value / 100.0 * container.height -
                        (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX ? vertical_edges : 0.0)) *
                                    input_image->width / input_image->height;
            }
        }
        if (tbox_string_view_equal_cstr(node->element.tag_name, "textarea")) {
            const tbox_font_face *face = tbox_font_face_cache_get(fonts,
                tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
                style->font_italic, style->font_size);
            if (face != NULL) content_width = tbox_layout_textarea_size(node, "cols", 20) *
                tbox_font_measure_text(face, tbox_string_view_make("0", 1));
        }
        break;
    }
    if (row_column_widths != NULL && row_column_count == 0 && tbox_layout_table_cell_node(node))
        content_width = container.width - margin_left - margin_right - padding_left - padding_right -
            border_left - border_right;
    if (content_width < 0.0) content_width = 0.0;
    /* Grid cells take their final width from the column algorithm, where
     * min-width is included as a column constraint. Clamping a cell alone
     * would leave gaps or overlap its neighbors. */
    if (!is_table_row && !tbox_layout_table_cell_node(node))
        content_width = tbox_layout_constrain_width(style, content_width, container.width, horizontal_edges);

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
        double border_box_width = content_width + horizontal_edges;
        double margin_box_width = border_box_width + margin_left + margin_right;
        double margin_box_x     = tbox_layout_resolve_absolute_edge(style->offset[3], style->offset[1], container.x, container.width, margin_box_width);
        double border_box_x     = margin_box_x + margin_left;
        content_x                = border_box_x + border_left + padding_left;

        /* Vertical: `top` non-AUTO resolves outright, no circularity (children
         * are laid out normally afterwards, and an AUTO content_height still
         * just sums them as usual). `top` AUTO but `bottom` non-AUTO needs
         * margin_box.height up front, which is only knowable ahead of the
         * children/text pass when style->height is itself definite (PX, or a
         * PERCENT against an already-definite container) AND this isn't a
         * text tag (its text layout is completed below, so the height is
         * not used for early absolute-position calculations).
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
                if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
                    early_content_height = early_content_height > vertical_edges ?
                        early_content_height - vertical_edges : 0.0;
                early_content_height = tbox_layout_constrain_height(style, early_content_height,
                    container.height, container.height_definite, vertical_edges);
                double early_border_box_height = early_content_height + vertical_edges;
                margin_box_height              = early_border_box_height + margin_top + margin_bottom;
            }
            margin_box_y = tbox_layout_resolve_absolute_edge(style->offset[0], style->offset[2], container.y, container.height, margin_box_height);
        } else {
            margin_box_y = container.y; /* circular top:auto+bottom:defined+height:auto case -- see comment above */
        }

        double border_box_y = margin_box_y + margin_top;
        content_y            = border_box_y + border_top + padding_top;
    } else {
        content_x = container.x + margin_left + padding_left + border_left;
        content_y = cursor_y + margin_top + padding_top + border_top;
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

    /* Same stretch vertically (CSS2.1 10.6.4): auto height with both `top`
     * and `bottom` fills the containing block between them. */
    bool stretch_height = is_out_of_flow && style->height.kind == TBOX_STYLE_LENGTH_AUTO &&
        style->offset[0].kind != TBOX_STYLE_LENGTH_AUTO && style->offset[2].kind != TBOX_STYLE_LENGTH_AUTO &&
        container.height_definite;
    double stretched_height = container.height - margin_top - margin_bottom - vertical_edges -
        tbox_layout_resolve_edge(style->offset[0], container.height) -
        tbox_layout_resolve_edge(style->offset[2], container.height);
    if (stretched_height < 0.0) stretched_height = 0.0;
    double content_height;
    if (is_image_input) {
        content_height = input_image != NULL ? (double)input_image->height :
            fallback_face != NULL ? tbox_font_face_line_height(fallback_face) : 16.0;
        if (style->height.kind == TBOX_STYLE_LENGTH_PX) content_height = style->height.value;
        else if (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite)
            content_height = style->height.value / 100.0 * container.height;
        else if (input_image != NULL && input_image->width > 0)
            content_height = content_width * input_image->height / input_image->width;
        if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX &&
            (style->height.kind == TBOX_STYLE_LENGTH_PX ||
             (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite)))
            content_height = content_height > vertical_edges ? content_height - vertical_edges : 0.0;
    } else if (is_text_tag) {
        /* Text-tag leaf: no child boxes even though the DOM node may have
         * element descendants (e.g. <b> inside a <p>) -- those only
         * contribute words to this box's own text_runs (NOVO v2, real
         * inline formatting context -- see tbox_layout_build_text_runs),
         * never a box of their own. Height is the sum of the wrapped
         * lines' heights (or one face's line-height for empty text) -- see
         * tbox_layout_build_text_runs's doc comment. */
        content_height = tbox_layout_build_text_runs(arena, node, node->first_child, NULL, style, styles, fonts, images, content_x, content_y, content_width, box);
        if (tbox_string_view_equal_cstr(node->element.tag_name, "textarea")) {
            const tbox_font_face *face = tbox_font_face_cache_get(fonts,
                tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
                style->font_italic, style->font_size);
            if (style->height.kind == TBOX_STYLE_LENGTH_PX) content_height = style->height.value;
            else if (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite)
                content_height = style->height.value / 100.0 * container.height;
            else if (face != NULL) content_height = tbox_layout_textarea_size(node, "rows", 2) *
                tbox_font_face_line_height(face);
            if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX &&
                (style->height.kind == TBOX_STYLE_LENGTH_PX ||
                 (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite)))
                content_height = content_height > vertical_edges ? content_height - vertical_edges : 0.0;
        } else if (style->height.kind == TBOX_STYLE_LENGTH_PX ||
                   (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite)) {
            content_height = style->height.kind == TBOX_STYLE_LENGTH_PX ? style->height.value :
                style->height.value / 100.0 * container.height;
            if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
                content_height = content_height > vertical_edges ? content_height - vertical_edges : 0.0;
        }
    } else if (is_table) {
        /* NOVO (table support): a <table>'s content_height is ALWAYS the
         * summed row heights, same "content always dictates height,
         * style->height is never consulted" posture is_text_tag already
         * has above -- no PX/PERCENT `height` branch, no positioned-context
         * threading for descendants (a `position: absolute` box inside a
         * table resolving against the table itself is out of scope). */
        content_height = tbox_layout_table_extended(node, styles) ?
            tbox_layout_build_table_extended(arena, node, styles, fonts, images,
                content_x, content_y, &content_width, box, positioned_context) :
            tbox_layout_build_table_children(arena, node, styles, fonts, images,
                content_x, content_y, &content_width, box, positioned_context);
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
            if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
                content_height = content_height > vertical_edges ? content_height - vertical_edges : 0.0;
            height_definite = true;
            break;
        case TBOX_STYLE_LENGTH_PERCENT:
            if (container.height_definite) {
                content_height  = style->height.value / 100.0 * container.height;
                if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
                    content_height = content_height > vertical_edges ? content_height - vertical_edges : 0.0;
                height_definite = true;
            } else {
                content_height = 0.0; /* placeholder; replaced by the children sum below */
            }
            break;
        case TBOX_STYLE_LENGTH_AUTO:
        default:
            content_height = 0.0; /* placeholder; replaced by the children sum below */
            if (stretch_height) {
                content_height  = stretched_height;
                height_definite = true;
            }
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
        box->scroll_content_height = children_total_height;
        if (!height_definite) {
            content_height = children_total_height;
        }
    }

    if (stretch_height) content_height = stretched_height; /* text boxes too, not only blocks */
    content_height = tbox_layout_constrain_height(style, content_height, container.height,
        container.height_definite, vertical_edges);

    box->content_box.x      = content_x;
    box->content_box.y      = content_y;
    box->content_box.width  = content_width;
    box->content_box.height = content_height;

    /* padding_box is content_box grown back out by padding (note:
     * content_x/content_y already include the left/top border -- see above --
     * so subtracting only padding_left/padding_top here correctly lands on
     * the padding_box edge, between border and padding). NOVO v4:
     * border_box is padding_box grown back out by each side's own border
     * width (0.0 when there's no effective border, preserving the v0-v3
     * identity border_box == padding_box exactly). */
    box->padding_box.x      = content_x - padding_left;
    box->padding_box.y      = content_y - padding_top;
    box->padding_box.width  = content_width + padding_left + padding_right;
    box->padding_box.height = content_height + padding_top + padding_bottom;

    box->border_box.x      = box->padding_box.x - border_left;
    box->border_box.y      = box->padding_box.y - border_top;
    box->border_box.width  = box->padding_box.width + border_left + border_right;
    box->border_box.height = box->padding_box.height + border_top + border_bottom;

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

    if (is_image_input && (input_image != NULL || fallback_face != NULL)) {
        tbox_layout_text_run *run = tbox_arena_alloc_zero(arena, sizeof(*run));
        run->rect = box->content_box;
        run->text = fallback_text;
        run->font = fallback_face;
        run->style = style;
        run->image = input_image;
        box->text_runs = run;
        box->text_run_count = 1;
    }

    tbox_layout_build_checkbox_checkmark(arena, node, style, fonts, box);

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

    if (tbox_layout_is_hidden_input(element) ||
        tbox_layout_style_or_default(styles, element)->display == TBOX_STYLE_DISPLAY_NONE) {
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
