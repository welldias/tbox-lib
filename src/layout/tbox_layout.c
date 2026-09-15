#include <tbox/layout.h>

#include <stddef.h>

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
 * <div> with loose text) is explicitly out of scope for v2 too. */
static bool tbox_layout_is_text_tag(const tbox_html_node *node) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) {
        return false;
    }

    static const char *const text_tags[] = { "h1", "h2", "h3", "h4", "h5", "h6", "p" };
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
    double width;       /* tbox_font_measure_text(face, text) */
    double space_width; /* tbox_font_measure_text(face, " ") -- the gap this word's face would render before it */
} tbox_layout_word;

/* A half-open range [start, end) into a tbox_layout_word array, one greedy
 * line's worth of words, plus that line's own height (max line-height among
 * the faces actually used by words in this range -- see ARCHITECTURE.md;
 * degenerates to "the one face's line-height" when every word on the line
 * shares a face, the common v2 case). */
typedef struct tbox_layout_line {
    size_t start, end;
    double height;
} tbox_layout_line;

/* Splits `collapsed` (already whitespace-collapsed: single ' ' separators,
 * no leading/trailing whitespace -- see tbox_string_collapse_whitespace) on
 * ' ' and pushes one tbox_layout_word per non-empty piece onto `words`, all
 * measured against `face`. A NULL `face` (tbox_font_face_cache_get failed
 * for this element's (bold, size) -- e.g. an unloadable font) contributes no
 * words at all rather than crashing on tbox_font_measure_text(NULL, ...). */
static void tbox_layout_push_words(tbox_vector *words, tbox_string_view collapsed, const tbox_font_face *face) {
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
            entry->width            = tbox_font_measure_text(face, word);
            entry->space_width      = tbox_font_measure_text(face, space);
        }

        while (i < collapsed.size && collapsed.data[i] == ' ') {
            i++;
        }
    }
}

/* Walks `node`'s DIRECT children in document order (see ARCHITECTURE.md's
 * algorithm): a TEXT child contributes its own words in `style`'s own face
 * (the h1-h6/p element itself); an ELEMENT child whose OWN resolved style
 * has display == INLINE recurses one level -- via tbox_html_node_text_content,
 * which already folds any further nesting away -- contributing ITS whole
 * text in ITS OWN face (this is how e.g. a <b> renders bold within a
 * regular <p>). Any other child (a BLOCK/NONE element, COMMENT, DOCTYPE) is
 * skipped entirely -- no box, no text, no recursion -- matching the fixed
 * tag list being the sole gate for "this element gets text content". */
static void tbox_layout_collect_words(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_vector *words) {
    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        if (child->type == TBOX_HTML_NODE_TEXT) {
            tbox_string_view collapsed = tbox_string_collapse_whitespace(arena, child->text.text);
            const tbox_font_face *face = tbox_font_face_cache_get(fonts, style->font_weight_bold, style->font_size);
            tbox_layout_push_words(words, collapsed, face);
        } else if (child->type == TBOX_HTML_NODE_ELEMENT) {
            const tbox_style *child_style = tbox_layout_style_or_default(styles, child);
            if (child_style->display == TBOX_STYLE_DISPLAY_INLINE) {
                tbox_string_view raw       = tbox_html_node_text_content(arena, child);
                tbox_string_view collapsed = tbox_string_collapse_whitespace(arena, raw);
                const tbox_font_face *face = tbox_font_face_cache_get(fonts, child_style->font_weight_bold, child_style->font_size);
                tbox_layout_push_words(words, collapsed, face);
            }
        }
        /* else: COMMENT/DOCTYPE, or an ELEMENT that isn't display:inline --
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
 * (the max line-height among the faces used by the words in its range). */
static void tbox_layout_break_lines(const tbox_layout_word *words, size_t word_count, double available_width, tbox_vector *lines) {
    if (word_count == 0) {
        return;
    }

    size_t line_start = 0;
    double line_width = 0.0;

    for (size_t i = 0; i < word_count; i++) {
        double prospective = (i == line_start) ? words[i].width : line_width + words[i].space_width + words[i].width;

        if (i > line_start && prospective > available_width) {
            double height = 0.0;
            for (size_t j = line_start; j < i; j++) {
                double face_height = tbox_font_face_line_height(words[j].face);
                if (face_height > height) {
                    height = face_height;
                }
            }

            tbox_layout_line *line = (tbox_layout_line *)tbox_vector_push(lines);
            line->start            = line_start;
            line->end              = i;
            line->height           = height;

            line_start = i;
            line_width = words[i].width;
        } else {
            line_width = prospective;
        }
    }

    double height = 0.0;
    for (size_t j = line_start; j < word_count; j++) {
        double face_height = tbox_font_face_line_height(words[j].face);
        if (face_height > height) {
            height = face_height;
        }
    }
    tbox_layout_line *line = (tbox_layout_line *)tbox_vector_push(lines);
    line->start            = line_start;
    line->end              = word_count;
    line->height           = height;
}

/* Places and merges one line's words into runs, appending them to `runs`.
 * `line_y` is this line's already-computed absolute top (content_y plus
 * every earlier line's height); `content_x` is the text box's content-box
 * left edge. Consecutive words sharing the exact same face merge into one
 * tbox_layout_text_run (their text joined by single spaces, matching
 * tbox_string_collapse_whitespace's own separator); a new run starts only
 * when the face changes (a line boundary is handled by the caller looping
 * per line, never straddled by a single run). The gap between two adjacent
 * runs of different faces (the space that would sit between them in the
 * source text) is accounted for in each run's absolute x position but
 * deliberately belongs to neither run's own text/width -- see
 * ARCHITECTURE.md: color/other run-level properties never vary within one
 * box anyway, so there is nothing visually lost by not assigning that gap a
 * face of its own. */
static void tbox_layout_build_line_runs(tbox_arena *arena, const tbox_layout_word *words, const tbox_layout_line *line, double line_y, double content_x, tbox_vector *runs) {
    double cursor_x                = 0.0;
    double run_start_x             = 0.0;
    double run_end_x               = 0.0;
    const tbox_font_face *run_face = NULL;
    tbox_string_builder run_builder;
    bool have_run = false;

    for (size_t i = line->start; i < line->end; i++) {
        const tbox_layout_word *word = &words[i];

        if (i != line->start) {
            cursor_x += word->space_width;
        }

        bool new_run = !have_run || word->face != run_face;
        if (new_run) {
            if (have_run) {
                tbox_layout_text_run *run = (tbox_layout_text_run *)tbox_vector_push(runs);
                run->rect.x               = content_x + run_start_x;
                run->rect.y               = line_y;
                run->rect.width           = run_end_x - run_start_x;
                run->rect.height          = line->height;
                run->text                 = tbox_string_builder_finish(&run_builder);
                run->font                 = run_face;
            }

            run_start_x = cursor_x;
            run_face    = word->face;
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
        run->rect.y               = line_y;
        run->rect.width           = run_end_x - run_start_x;
        run->rect.height          = line->height;
        run->text                 = tbox_string_builder_finish(&run_builder);
        run->font                 = run_face;
    }
}

/* Builds `box`'s text_runs/text_run_count (a leaf box, one of the fixed
 * text tags) and returns its content-box height: the sum of every line's
 * height (NOVO v2 -- replaces v0/v1's single fixed line height), or, for a
 * box whose collected words come out empty (no words at all -- an empty tag
 * after whitespace-collapsing, or every child skipped), the box's OWN
 * face's line-height alone, matching v0/v1's choice to never collapse an
 * empty text box's height to 0. */
static double tbox_layout_build_text_runs(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, double content_x, double content_y, double available_width, tbox_layout_box *box) {
    tbox_vector words;
    tbox_vector_init(&words, arena, sizeof(tbox_layout_word), 0);
    tbox_layout_collect_words(arena, node, style, styles, fonts, &words);

    size_t word_count = tbox_vector_length(&words);
    if (word_count == 0) {
        box->text_runs      = NULL;
        box->text_run_count = 0;

        const tbox_font_face *own_face = tbox_font_face_cache_get(fonts, style->font_weight_bold, style->font_size);
        return own_face != NULL ? tbox_font_face_line_height(own_face) : 0.0;
    }

    const tbox_layout_word *word_items = (const tbox_layout_word *)words.data;

    tbox_vector lines;
    tbox_vector_init(&lines, arena, sizeof(tbox_layout_line), 0);
    tbox_layout_break_lines(word_items, word_count, available_width, &lines);

    tbox_vector runs;
    tbox_vector_init(&runs, arena, sizeof(tbox_layout_text_run), 0);

    size_t line_count                  = tbox_vector_length(&lines);
    const tbox_layout_line *line_items = (const tbox_layout_line *)lines.data;

    double cumulative_y = content_y;
    double total_height = 0.0;
    for (size_t li = 0; li < line_count; li++) {
        const tbox_layout_line *line = &line_items[li];
        tbox_layout_build_line_runs(arena, word_items, line, cumulative_y, content_x, &runs);
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

/* The containing block a box is laid out against: the parent's (or the
 * viewport's, for the root) content-box x/width, plus its content height --
 * `height_definite` says whether that height came from an explicit value
 * (PX, or PERCENT against an already-definite container) rather than from
 * AUTO/shrink-to-fit, which is what CSS2.1 10.5 needs to decide whether a
 * child's own `height: %` resolves or itself falls back to AUTO. There is
 * no `y` here: the vertical position within the containing block is instead
 * threaded through explicitly as a running cursor (see
 * tbox_layout_build_children), since siblings advance it independently of
 * anything about the containing block itself. */
typedef struct tbox_layout_containing_block {
    double x;
    double width;
    double height;
    bool height_definite;
} tbox_layout_containing_block;

static tbox_layout_box *tbox_layout_build_element(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_layout_containing_block container, double cursor_y);

/* Walks `node`'s ELEMENT children (TEXT/COMMENT/DOCTYPE children never get
 * their own box, see ARCHITECTURE.md), skipping any whose resolved
 * style->display == TBOX_STYLE_DISPLAY_NONE entirely -- no box, no
 * recursion into its subtree, no contribution to the height sum returned.
 * Stacks the rest vertically starting at `start_y` (no margin collapsing:
 * each box's own margin is applied independently -- see the module doc
 * comment), linking them onto `parent_box`'s first_child/last_child/
 * next_sibling. Returns the sum of every built child's margin_box.height,
 * for the parent's own AUTO/shrink-to-fit height. */
static double tbox_layout_build_children(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_layout_containing_block children_container, double start_y, tbox_layout_box *parent_box) {
    double cursor_y           = start_y;
    double total_height       = 0.0;
    tbox_layout_box *previous = NULL;

    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        if (child->type != TBOX_HTML_NODE_ELEMENT) {
            continue;
        }

        const tbox_style *child_style = tbox_layout_style_or_default(styles, child);
        if (child_style->display == TBOX_STYLE_DISPLAY_NONE) {
            continue;
        }

        tbox_layout_box *child_box = tbox_layout_build_element(arena, child, styles, fonts, children_container, cursor_y);
        child_box->parent          = parent_box;
        if (previous == NULL) {
            parent_box->first_child = child_box;
        } else {
            previous->next_sibling = child_box;
        }
        parent_box->last_child = child_box;
        previous               = child_box;

        cursor_y += child_box->margin_box.height;
        total_height += child_box->margin_box.height;
    }

    return total_height;
}

/* Builds and positions the box for one ELEMENT `node` (already known to not
 * be display:none -- the caller checks that before recursing, see
 * tbox_layout_build_children and tbox_layout_build) against `container`
 * (its parent's, or the viewport's, content box) with its margin_box's top
 * edge at `cursor_y`. */
static tbox_layout_box *tbox_layout_build_element(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_layout_containing_block container, double cursor_y) {
    const tbox_style *style = tbox_layout_style_or_default(styles, node);

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

    /* Width: the same rule for every node, text-tag or not -- see
     * ARCHITECTURE.md's clarification that measured text never resizes the
     * box (D4). There is no border in v0 (always 0), so border/padding box
     * width differ from content width only by padding. */
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
        content_width = container.width - margin_left - margin_right - padding_left - padding_right;
        break;
    }

    double content_x = container.x + margin_left + padding_left;
    double content_y = cursor_y + margin_top + padding_top;

    double content_height;
    if (tbox_layout_is_text_tag(node)) {
        /* Text-tag leaf: no child boxes even though the DOM node may have
         * element descendants (e.g. <b> inside a <p>) -- those only
         * contribute words to this box's own text_runs (NOVO v2, real
         * inline formatting context -- see tbox_layout_build_text_runs),
         * never a box of their own. Height is the sum of the wrapped
         * lines' heights (or one face's line-height for empty text) -- see
         * tbox_layout_build_text_runs's doc comment. */
        content_height = tbox_layout_build_text_runs(arena, node, style, styles, fonts, content_x, content_y, content_width, box);
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

        tbox_layout_containing_block children_container = {
            .x               = content_x,
            .width           = content_width,
            .height          = content_height,
            .height_definite = height_definite,
        };
        double children_total_height = tbox_layout_build_children(arena, node, styles, fonts, children_container, content_y, box);
        if (!height_definite) {
            content_height = children_total_height;
        }
    }

    box->content_box.x      = content_x;
    box->content_box.y      = content_y;
    box->content_box.width  = content_width;
    box->content_box.height = content_height;

    /* padding_box/border_box are identical in v0 (border is always 0
     * width); both are content_box grown back out by padding. */
    box->padding_box.x      = content_x - padding_left;
    box->padding_box.y      = content_y - padding_top;
    box->padding_box.width  = content_width + padding_left + padding_right;
    box->padding_box.height = content_height + padding_top + padding_bottom;
    box->border_box         = box->padding_box;

    /* margin_box is border_box grown back out by margin -- its x/y land
     * back on (container.x, cursor_y) exactly, per the geometry above. */
    box->margin_box.x      = box->border_box.x - margin_left;
    box->margin_box.y      = box->border_box.y - margin_top;
    box->margin_box.width  = box->border_box.width + margin_left + margin_right;
    box->margin_box.height = box->border_box.height + margin_top + margin_bottom;

    return box;
}

tbox_layout_box *tbox_layout_build(tbox_arena *arena, const tbox_html_node *root, const tbox_style_table *styles, tbox_font_face_cache *fonts, double viewport_width, double viewport_height) {
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
        .width           = viewport_width,
        .height          = viewport_height,
        .height_definite = true, /* the viewport's height is always a concrete number */
    };
    return tbox_layout_build_element(arena, element, styles, fonts, viewport, 0.0);
}
