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

static tbox_layout_box *tbox_layout_build_element(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_layout_containing_block container, double cursor_y, tbox_layout_positioned_context positioned_context);

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

/* Walks `node`'s ELEMENT children (TEXT/COMMENT/DOCTYPE children never get
 * their own box, see ARCHITECTURE.md), skipping any whose resolved
 * style->display == TBOX_STYLE_DISPLAY_NONE entirely -- no box, no
 * recursion into its subtree, no contribution to the height sum returned.
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
 * `container.x`/`container.y`-based path instead. */
static double tbox_layout_build_children(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_layout_containing_block children_container, double start_y, tbox_layout_box *parent_box, tbox_layout_positioned_context positioned_context) {
    double border_bottom         = start_y;
    double pending_margin_bottom = 0.0;
    tbox_layout_box *previous    = NULL;

    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        if (child->type != TBOX_HTML_NODE_ELEMENT) {
            continue;
        }

        const tbox_style *child_style = tbox_layout_style_or_default(styles, child);
        if (child_style->display == TBOX_STYLE_DISPLAY_NONE) {
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

            tbox_layout_box *child_box = tbox_layout_build_element(arena, child, styles, fonts, out_of_flow_container, border_bottom, positioned_context);
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

        tbox_layout_box *child_box = tbox_layout_build_element(arena, child, styles, fonts, children_container, cursor_y, positioned_context);
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
static tbox_layout_box *tbox_layout_build_element(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_layout_containing_block container, double cursor_y, tbox_layout_positioned_context positioned_context) {
    const tbox_style *style = tbox_layout_style_or_default(styles, node);
    bool is_text_tag        = tbox_layout_is_text_tag(node);

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
        double children_total_height = tbox_layout_build_children(arena, node, styles, fonts, children_container, content_y, box, context_for_children);
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
    return tbox_layout_build_element(arena, element, styles, fonts, viewport, 0.0, root_positioned_context);
}
