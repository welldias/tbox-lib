#include "tbox_layout_internal.h"
#include <stdio.h>
#include <string.h>

/* ---- inline formatting context (word wrap + run merging) ---- */

/* Every word starts zeroed, so fields a pusher doesn't know about stay
 * empty (tbox_vector_push does not clear memory). */
tbox_layout_word *tbox_layout_new_word(tbox_vector *words) {
    tbox_layout_word *word = (tbox_layout_word *)tbox_vector_push(words);
    memset(word, 0, sizeof(*word));
    return word;
}

/* Splits `collapsed` (already whitespace-collapsed: single ' ' separators,
 * no leading/trailing whitespace -- see tbox_string_collapse_whitespace) on
 * ' ' and pushes one tbox_layout_word per non-empty piece onto `words`, all
 * measured against `face`. A NULL `face` (tbox_font_face_cache_get failed
 * for this element's (bold, size) -- e.g. an unloadable font) contributes no
 * words at all rather than crashing on tbox_font_measure_text(NULL, ...). */
void tbox_layout_push_words(tbox_vector *words, tbox_string_view collapsed, const tbox_font_face *face, const tbox_style *style) {
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
            tbox_layout_word *entry = tbox_layout_new_word(words);
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
tbox_string_view tbox_layout_transform_text(tbox_arena *arena, tbox_string_view text, tbox_style_text_transform transform) {
    if (transform == TBOX_STYLE_TEXT_TRANSFORM_NONE || text.size == 0 || text.size > SIZE_MAX / 2 - 4)
        return text;
    size_t capacity = text.size * 2 + 4; /* utf8.h's mappings keep byte length; slack is defensive */
    char *out       = (char *)tbox_arena_alloc(arena, capacity);
    if (out == NULL)
        return text;

    char *write      = out;
    const char *read = text.data;
    const char *end  = text.data + text.size;
    bool word_start  = true;
    while (read < end) {
        /* A truncated sequence at the end would read past the view: leave
         * the text alone rather than decode it. */
        if ((size_t)(end - read) < utf8codepointcalcsize((const utf8_int8_t *)read))
            return text;
        utf8_int32_t codepoint;
        const char *next = (const char *)utf8codepoint((const utf8_int8_t *)read, &codepoint);
        bool space       = codepoint == ' ' || codepoint == '\t' || codepoint == '\n' || codepoint == '\r' || codepoint == '\f';
        if (transform == TBOX_STYLE_TEXT_TRANSFORM_UPPERCASE || (transform == TBOX_STYLE_TEXT_TRANSFORM_CAPITALIZE && word_start))
            codepoint = utf8uprcodepoint(codepoint);
        else if (transform == TBOX_STYLE_TEXT_TRANSFORM_LOWERCASE)
            codepoint = utf8lwrcodepoint(codepoint);
        word_start           = space;
        utf8_int8_t *written = utf8catcodepoint((utf8_int8_t *)write, codepoint, (size_t)(out + capacity - write));
        if (written == NULL)
            return text;
        write = (char *)written;
        read  = next;
    }
    return tbox_string_view_make(out, (size_t)(write - out));
}

/* pushes one tbox_layout_word marking a FORCED end of line --
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
void tbox_layout_push_hard_break(tbox_vector *words, const tbox_font_face *face, const tbox_style *style) {
    tbox_layout_word *entry = tbox_layout_new_word(words);
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
static void tbox_layout_push_preserved_word(tbox_vector *words, tbox_string_view text, bool space_before, const tbox_font_face *face, const tbox_style *style) {
    if (text.size == 0)
        return;
    static const tbox_string_view space = { " ", 1 };
    tbox_layout_word *entry             = tbox_layout_new_word(words);
    entry->text                         = text;
    entry->face                         = face;
    entry->style                        = style;
    entry->width                        = tbox_font_measure_text_spaced(face, text, style->letter_spacing);
    entry->space_width                  = space_before ? tbox_font_measure_text_spaced(face, space, style->letter_spacing) + style->word_spacing : 0.0;
    entry->image                        = NULL;
    entry->image_height                 = 0.0;
    entry->hard_break                   = false;
    entry->no_space_before              = !space_before;
}

/* One newline-free segment under `pre-wrap`: every space is kept. A gap of
 * k spaces becomes one break opportunity plus k-1 spaces glued to the end
 * of the word before it (so they hang at a line end instead of indenting
 * the next line); leading spaces are glued to the first word and trailing
 * ones to the last. A run's text only ever joins words with ONE space,
 * which is why the extra spaces must live inside the words' own text. */
static void tbox_layout_push_pre_wrap_segment(tbox_vector *words, tbox_string_view segment, const tbox_font_face *face, const tbox_style *style) {
    size_t start      = 0;
    bool space_before = false;
    while (start < segment.size) {
        size_t end = start;
        while (end < segment.size && segment.data[end] == ' ')
            end++; /* leading spaces (first word only) */
        while (end < segment.size && segment.data[end] != ' ')
            end++; /* the word itself */
        size_t gap_end = end;
        while (gap_end < segment.size && segment.data[gap_end] == ' ')
            gap_end++;
        /* Keep all but one space of the gap, or the whole gap at the end. */
        size_t word_end = gap_end == segment.size ? gap_end : gap_end - 1;
        if (word_end < end)
            word_end = end;
        tbox_layout_push_preserved_word(words, tbox_string_view_make(segment.data + start, word_end - start), space_before, face, style);
        space_before = gap_end < segment.size && gap_end > end;
        start        = gap_end;
    }
}

/* One newline-free segment under `break-spaces`: preserved spaces take
 * room like any character and a line may break after any of them, so each
 * word carries the spaces that follow it, glued to its neighbors. */
static void tbox_layout_push_break_spaces_segment(tbox_vector *words, tbox_string_view segment, const tbox_font_face *face, const tbox_style *style) {
    size_t start = 0;
    while (start < segment.size) {
        size_t end = start;
        while (end < segment.size && segment.data[end] == ' ')
            end++; /* leading spaces (first word only) */
        while (end < segment.size && segment.data[end] != ' ')
            end++;
        while (end < segment.size && segment.data[end] == ' ')
            end++;
        tbox_layout_push_preserved_word(words, tbox_string_view_make(segment.data + start, end - start), false, face, style);
        start = end;
    }
}

/* Replaces each tab with spaces up to the next tab stop, every `tab-size`
 * columns (codepoints since the last newline); a length tab-size becomes
 * the nearest whole number of the face's spaces. */
tbox_string_view tbox_layout_expand_tabs(tbox_arena *arena, tbox_string_view text, const tbox_style *style, const tbox_font_face *face) {
    if (memchr(text.data, '\t', text.size) == NULL)
        return text;
    double tab = style->tab_size > 0.0 ? style->tab_size : 8.0;
    if (style->tab_size_length && face != NULL) {
        static const tbox_string_view space = { " ", 1 };
        double space_width                  = tbox_font_measure_text_spaced(face, space, style->letter_spacing) + style->word_spacing;
        tab                                 = space_width > 0.0 ? style->tab_size / space_width : 8.0;
    }
    size_t columns = (size_t)(tab + 0.5);
    size_t tabs    = 0;
    for (size_t i = 0; i < text.size; i++)
        if (text.data[i] == '\t')
            tabs++;
    if (columns > 64)
        columns = 64;
    char *out = (char *)tbox_arena_alloc(arena, text.size + tabs * (columns > 0 ? columns : 1));
    if (out == NULL)
        return text;
    size_t length = 0, column = 0;
    for (size_t i = 0; i < text.size; i++) {
        char c = text.data[i];
        if (c == '\t') {
            size_t pad = columns == 0 ? 0 : columns - column % columns;
            memset(out + length, ' ', pad);
            length += pad;
            column += pad;
            continue;
        }
        out[length++] = c;
        if (c == '\n')
            column = 0;
        else if (((unsigned char)c & 0xC0u) != 0x80u)
            column++; /* count codepoints, not continuation bytes */
    }
    return tbox_string_view_make(out, length);
}

/* Soft hyphens (U+00AD) in the words pushed since `first`: invisible, and
 * with `hyphens: manual`/`auto` each one splits its word into glued pieces
 * (break opportunities that show a hyphen when taken); with `none` they
 * are only removed. */
static void tbox_layout_apply_soft_hyphens(tbox_arena *arena, tbox_vector *words, size_t first, const tbox_style *style) {
    static const char shy[2] = { (char)0xC2, (char)0xAD };
    bool any = false;
    for (size_t w = first; w < words->length && !any; w++) {
        const tbox_layout_word *word = (const tbox_layout_word *)tbox_vector_at(words, w);
        for (size_t i = 0; i + 1 < word->text.size; i++)
            if (word->text.data[i] == shy[0] && word->text.data[i + 1] == shy[1])
                any = true;
    }
    if (!any)
        return;
    size_t count               = words->length - first;
    tbox_layout_word *original = (tbox_layout_word *)tbox_arena_alloc(arena, count * sizeof(tbox_layout_word));
    if (original == NULL)
        return;
    memcpy(original, (tbox_layout_word *)words->data + first, count * sizeof(tbox_layout_word));
    words->length = first;
    for (size_t w = 0; w < count; w++) {
        const tbox_layout_word *word = &original[w];
        if (word->hard_break || word->image != NULL || word->atomic != NULL || word->text.size < 2) {
            *tbox_layout_new_word(words) = *word;
            continue;
        }
        if (style->hyphens_none) {
            /* Drop the soft hyphens: a copy without them. */
            char *copy    = (char *)tbox_arena_alloc(arena, word->text.size);
            size_t length = 0;
            for (size_t i = 0; i < word->text.size; i++) {
                if (i + 1 < word->text.size && word->text.data[i] == shy[0] && word->text.data[i + 1] == shy[1]) {
                    i++;
                    continue;
                }
                if (copy != NULL)
                    copy[length++] = word->text.data[i];
            }
            tbox_layout_word *entry = tbox_layout_new_word(words);
            *entry                  = *word;
            if (copy != NULL) {
                entry->text  = tbox_string_view_make(copy, length);
                entry->width = tbox_font_measure_text_spaced(word->face, entry->text, style->letter_spacing);
            }
            continue;
        }
        size_t start = 0;
        bool first_piece = true;
        for (size_t i = 0; i <= word->text.size; i++) {
            bool at_shy = i + 1 < word->text.size && word->text.data[i] == shy[0] && word->text.data[i + 1] == shy[1];
            if (!at_shy && i < word->text.size)
                continue;
            if (i > start || !first_piece) {
                tbox_layout_word *entry  = tbox_layout_new_word(words);
                *entry                   = *word;
                entry->text              = tbox_string_view_make(word->text.data + start, i - start);
                entry->width             = tbox_font_measure_text_spaced(word->face, entry->text, style->letter_spacing);
                entry->soft_hyphen_after = at_shy;
                if (!first_piece) {
                    entry->space_width     = 0.0;
                    entry->no_space_before = true;
                }
                first_piece = false;
            }
            if (at_shy)
                i++;
            start = i + 1;
        }
    }
}

/* Turns a text node's raw text into words per the style's `white-space`:
 * `normal`/`nowrap` collapse every whitespace run, `pre-line` collapses
 * spaces but breaks at each newline, and `pre`/`pre-wrap` keep every space
 * and break at each newline (`pre` as one unbreakable word per line; the
 * text box itself decides whether lines may wrap). `text-transform` is
 * applied before measuring. */
void tbox_layout_push_text_words(tbox_arena *arena, tbox_vector *words, tbox_string_view raw, const tbox_font_face *face, const tbox_style *style) {
    if (face == NULL)
        return;
    tbox_string_view text       = tbox_layout_transform_text(arena, raw, style->text_transform);
    tbox_style_white_space mode = style->white_space;
    if (mode != TBOX_STYLE_WHITE_SPACE_PRE && mode != TBOX_STYLE_WHITE_SPACE_PRE_WRAP && mode != TBOX_STYLE_WHITE_SPACE_PRE_LINE && mode != TBOX_STYLE_WHITE_SPACE_BREAK_SPACES) {
        tbox_layout_push_words(words, tbox_string_collapse_whitespace(arena, text), face, style);
        return;
    }
    if (mode != TBOX_STYLE_WHITE_SPACE_PRE_LINE)
        text = tbox_layout_expand_tabs(arena, text, style, face);
    size_t line_start = 0;
    for (size_t i = 0; i <= text.size; i++) {
        if (i < text.size && text.data[i] != '\n')
            continue;
        size_t line_end          = i > line_start && text.data[i - 1] == '\r' ? i - 1 : i;
        tbox_string_view segment = tbox_string_view_make(text.data + line_start, line_end - line_start);
        if (mode == TBOX_STYLE_WHITE_SPACE_PRE_LINE)
            tbox_layout_push_words(words, tbox_string_collapse_whitespace(arena, segment), face, style);
        else if (mode == TBOX_STYLE_WHITE_SPACE_PRE_WRAP)
            tbox_layout_push_pre_wrap_segment(words, segment, face, style);
        else if (mode == TBOX_STYLE_WHITE_SPACE_BREAK_SPACES)
            tbox_layout_push_break_spaces_segment(words, segment, face, style);
        else
            tbox_layout_push_preserved_word(words, segment, false, face, style);
        if (i < text.size)
            tbox_layout_push_hard_break(words, face, style);
        line_start = i + 1;
    }
}

/* `font-variant: small-caps` over the words pushed since `first`: each
 * run of lowercase letters becomes uppercase in a face 70% of the size
 * (the usual synthesized small capitals), glued to its neighbors inside
 * the same word. */
static void tbox_layout_apply_small_caps(tbox_arena *arena, tbox_vector *words, size_t first, const tbox_style *style, tbox_font_face_cache *fonts) {
    if (words->length <= first)
        return;
    tbox_style small_style        = *style;
    small_style.font_size         = style->font_size * 0.7;
    const tbox_font_face *small   = tbox_layout_style_face(fonts, &small_style);
    if (small == NULL)
        return;
    size_t count                  = words->length - first;
    tbox_layout_word *original    = (tbox_layout_word *)tbox_arena_alloc(arena, count * sizeof(tbox_layout_word));
    if (original == NULL)
        return;
    memcpy(original, (tbox_layout_word *)words->data + first, count * sizeof(tbox_layout_word));
    words->length = first;
    for (size_t w = 0; w < count; w++) {
        const tbox_layout_word *word = &original[w];
        if (word->hard_break || word->image != NULL || word->atomic != NULL || word->text.size == 0) {
            *tbox_layout_new_word(words) = *word;
            continue;
        }
        const char *read = word->text.data, *end = word->text.data + word->text.size;
        bool first_piece = true;
        while (read < end) {
            /* One run of same-case codepoints. */
            const char *start = read;
            bool lower        = false;
            bool started      = false;
            while (read < end) {
                if ((size_t)(end - read) < utf8codepointcalcsize((const utf8_int8_t *)read)) {
                    read = end;
                    break;
                }
                utf8_int32_t codepoint;
                const char *next = (const char *)utf8codepoint((const utf8_int8_t *)read, &codepoint);
                bool is_lower    = utf8uprcodepoint(codepoint) != codepoint;
                if (started && is_lower != lower)
                    break;
                lower   = is_lower;
                started = true;
                read    = next;
            }
            tbox_string_view piece = tbox_string_view_make(start, (size_t)(read - start));
            if (lower)
                piece = tbox_layout_transform_text(arena, piece, TBOX_STYLE_TEXT_TRANSFORM_UPPERCASE);
            tbox_layout_word *entry = tbox_layout_new_word(words);
            *entry                  = *word;
            entry->text             = piece;
            entry->face             = lower ? small : word->face;
            entry->width            = tbox_font_measure_text_spaced(entry->face, piece, style->letter_spacing);
            if (!first_piece) {
                entry->space_width     = 0.0;
                entry->no_space_before = true;
            } else if (lower && word->space_width > 0.0) {
                /* The space before it is drawn in the run's (small) face. */
                static const tbox_string_view space = { " ", 1 };
                entry->space_width                  = tbox_font_measure_text_spaced(small, space, style->letter_spacing) + style->word_spacing;
            }
            first_piece = false;
        }
    }
}

/* Walks the sibling range [first_sibling, end_exclusive) in document order
 * (see ARCHITECTURE.md's algorithm), pushing the words of an inline
 * formatting context: a TEXT child contributes its words in `style`'s face
 * (the text-bearing element itself, or --  -- the container style
 * synthesized for an anonymous box); `<br>` a hard break; `<img>` one image
 * word (tbox_layout_push_image_word). an inline ELEMENT child is
 * walked recursively with ITS OWN style, to any depth (`<b><i>x</i></b>`
 * keeps both), instead of being flattened to its text content one level
 * down. Inside an inline element, any other non-`none` element is walked
 * the same way (its text stays in the line, as before v15); directly under
 * the text-bearing element, a block/none child contributes nothing, and a
 * `display: none` element or hidden input is skipped at any depth.
 *
 * an `inline-block` element, at any depth, becomes one atomic
 * word (tbox_layout_push_atomic_word). `measure_only` (intrinsic sizing)
 * gives it its min/max-content widths instead of building its box.
 *
 * an explicit sibling range, so the SAME mechanism serves an
 * anonymous box's [run_start, run_end) interval (see
 * tbox_layout_build_anonymous_box below) as well as a whole node's
 * children (node->first_child, NULL). */
void tbox_layout_push_atomic_word(tbox_arena *arena, const tbox_html_node *node, const tbox_style *node_style, const tbox_font_face *context_face, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double containing_width, bool measure_only, tbox_vector *words);

/* whether the inline content collected so far could be followed
 * by a space. Words from different nodes are only separated by a space (and
 * a break opportunity) when whitespace really sits between them in the
 * source: `a<b>b</b>` is one word, `a <b>b</b>` two. */

static bool tbox_layout_is_ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* Glues the first word pushed since index `first` to the content before
 * it, unless whitespace separated them; then records the new content. */
static void tbox_layout_join_words(tbox_vector *words, size_t first, bool leading_space, tbox_layout_inline_state *state) {
    if (words->length <= first)
        return;
    tbox_layout_word *word = (tbox_layout_word *)tbox_vector_at(words, first);
    if (state->has_content && !state->trailing_space && !leading_space && !word->hard_break) {
        word->no_space_before = true;
        word->space_width     = 0.0;
    }
    state->has_content    = true;
    state->trailing_space = false;
}

void tbox_layout_collect_words_in(tbox_arena *arena, const tbox_html_node *first_sibling, const tbox_html_node *end_exclusive, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double containing_width, bool inside_inline, bool measure_only, tbox_layout_inline_state *state, tbox_vector *words) {
    for (const tbox_html_node *child = first_sibling; child != end_exclusive; child = child->next_sibling) {
        size_t first = words->length;
        if (tbox_layout_is_hidden_input(child))
            continue;
        /* a <br> child forces a line break -- checked BEFORE the
         * TEXT branch below and independently of the `display == INLINE`
         * gate an ELEMENT child otherwise needs (see ARCHITECTURE.md): <br>
         * does not need `display: inline` for this to work. Uses the
         * enclosing element's face (`style`), same as the TEXT branch. */
        if (child->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(child->element.tag_name, "br")) {
            const tbox_font_face *face = tbox_layout_style_face(fonts, style);
            tbox_layout_push_hard_break(words, face, style);
            state->has_content = state->trailing_space = false;
            continue;
        }

        if (child->type == TBOX_HTML_NODE_TEXT) {
            const tbox_font_face *face = tbox_layout_style_face(fonts, style);
            tbox_string_view text      = child->text.text;
            tbox_layout_push_text_words(arena, words, text, face, style);
            tbox_layout_apply_soft_hyphens(arena, words, first, style);
            if (style->font_small_caps)
                tbox_layout_apply_small_caps(arena, words, first, style, fonts);
            if (text.size == 0)
                continue;
            bool preserved = style->white_space == TBOX_STYLE_WHITE_SPACE_PRE || style->white_space == TBOX_STYLE_WHITE_SPACE_PRE_WRAP || style->white_space == TBOX_STYLE_WHITE_SPACE_BREAK_SPACES;
            bool trailing  = !preserved && tbox_layout_is_ascii_space(text.data[text.size - 1]);
            if (words->length > first) {
                tbox_layout_join_words(words, first, !preserved && tbox_layout_is_ascii_space(text.data[0]), state);
                const tbox_layout_word *last = (const tbox_layout_word *)tbox_vector_at(words, words->length - 1);
                if (last->hard_break)
                    state->has_content = false; /* pre-line/pre newlines */
            }
            if (trailing)
                state->trailing_space = true;
        } else if (child->type == TBOX_HTML_NODE_ELEMENT) {
            const tbox_style *child_style = tbox_layout_style_or_default(styles, child);
            if (tbox_string_view_equal_cstr(child->element.tag_name, "img")) {
                const tbox_font_face *face = tbox_layout_style_face(fonts, style);
                tbox_layout_push_image_word(arena, child, child_style, face, images, containing_width, words);
                tbox_layout_join_words(words, first, false, state);
            } else if (child_style->display == TBOX_STYLE_DISPLAY_INLINE_BLOCK || child_style->display == TBOX_STYLE_DISPLAY_INLINE_FLEX) {
                const tbox_font_face *face = tbox_layout_style_face(fonts, style);
                tbox_layout_push_atomic_word(arena, child, child_style, face, styles, fonts, images, containing_width, measure_only, words);
                tbox_layout_join_words(words, first, false, state);
            } else if (child_style->display == TBOX_STYLE_DISPLAY_INLINE || (inside_inline && child_style->display != TBOX_STYLE_DISPLAY_NONE)) {
                tbox_layout_collect_words_in(arena, child->first_child, NULL, child_style, styles, fonts, images, containing_width, true, measure_only, state, words);
            }
        }
        /* else: COMMENT/DOCTYPE, or a block/none ELEMENT directly under the
         * text-bearing element -- contributes nothing. */
    }
}

void tbox_layout_collect_words(tbox_arena *arena, const tbox_html_node *first_sibling, const tbox_html_node *end_exclusive, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double containing_width, tbox_vector *words) {
    tbox_layout_inline_state state = { false, false };
    tbox_layout_collect_words_in(arena, first_sibling, end_exclusive, style, styles, fonts, images, containing_width, false, false, &state, words);
}
