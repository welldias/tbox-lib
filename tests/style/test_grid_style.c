#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "style/tbox_style_grid.h"
#include "test_support.h"
#include <string.h>
#include <tbox/style.h>

/* Out-of-line style groups (tbox_style_masks_of, ...) of the styles this
 * file resolves live here, for the whole run. */
static tbox_arena style_arena;

static tbox_style tbox_grid_resolve(const char *css, const tbox_style *parent) {
    tbox_html_document *doc          = tbox_html_parse("<div></div>", 11);
    tbox_css_stylesheet *sheet       = tbox_css_parse(css, strlen(css));
    const tbox_html_node *node       = tbox_html_document_root(doc)->first_child;
    tbox_css_computed_style computed = tbox_css_cascade_resolve_stylesheet(sheet, node);
    tbox_style style                 = tbox_style_resolve_in_viewport(&style_arena, node, parent, &computed, 800, 600);
    tbox_css_computed_style_destroy(&computed);
    tbox_css_stylesheet_destroy(sheet);
    tbox_html_document_destroy(doc);
    return style;
}

int tbox_test_grid_style_run(void) {
    style_arena = tbox_arena_create(0);
    int failures = 0;
    /* The style owns the strings even after the computed style and sheets die. */
    tbox_style style = tbox_grid_resolve("div{display:inline-grid;grid-template:[a] 'top top' 20px [b] 'side main' / 80px 1fr;grid-auto-rows:30px 40px;grid-auto-flow:column dense;place-items:center end;place-self:end start;grid-area:2 / span 2 / -1 / last}", NULL);
    TBOX_TEST_ASSERT(style.display == TBOX_STYLE_DISPLAY_INLINE_GRID);
    TBOX_TEST_ASSERT(strcmp(tbox_style_grid_templates_of(&style)->grid_template_rows, "[a] 20px [b] auto") == 0);
    TBOX_TEST_ASSERT(strcmp(tbox_style_grid_templates_of(&style)->grid_template_columns, "80px 1fr") == 0);
    TBOX_TEST_ASSERT(strcmp(tbox_style_grid_templates_of(&style)->grid_template_areas, "'top top' 'side main'") == 0);
    TBOX_TEST_ASSERT(style.grid_auto_flow_column && style.grid_auto_flow_dense);
    TBOX_TEST_ASSERT(style.justify_items == TBOX_STYLE_FLEX_ALIGN_END && style.align_items == TBOX_STYLE_FLEX_ALIGN_CENTER);
    TBOX_TEST_ASSERT(style.justify_self == TBOX_STYLE_FLEX_ALIGN_START && style.align_self == TBOX_STYLE_FLEX_ALIGN_END);
    TBOX_TEST_ASSERT(tbox_style_grid_placement_of(&style)->grid_row_start.number == 2 && tbox_style_grid_placement_of(&style)->grid_row_end.number == -1);
    TBOX_TEST_ASSERT(tbox_style_grid_placement_of(&style)->grid_column_start.span && tbox_style_grid_placement_of(&style)->grid_column_start.number == 2);
    TBOX_TEST_ASSERT(strcmp(tbox_style_grid_placement_of(&style)->grid_column_end.name, "last") == 0);
    tbox_style parent = tbox_grid_resolve("div{font-size:20px;grid-template-columns:2em 1fr;grid-template-areas:'a a';grid-auto-flow:column dense;grid-area:area;justify-items:center}", NULL);
    style             = tbox_grid_resolve("div{font-size:10px;grid:inherit;grid-area:inherit;justify-items:inherit}", &parent);
    TBOX_TEST_ASSERT(strcmp(tbox_style_grid_templates_of(&style)->grid_template_columns, "2em 1fr") == 0 && tbox_style_grid_templates_of(&style)->grid_units[0].font_size == 20);
    TBOX_TEST_ASSERT(style.grid_auto_flow_column && style.grid_auto_flow_dense);
    TBOX_TEST_ASSERT(strcmp(tbox_style_grid_placement_of(&style)->grid_row_start.name, "area") == 0 && strcmp(tbox_style_grid_placement_of(&style)->grid_column_end.name, "area") == 0);
    TBOX_TEST_ASSERT(style.justify_items == TBOX_STYLE_FLEX_ALIGN_CENTER);
    style = tbox_grid_resolve("div{grid:initial;grid-area:unset;justify-items:initial}", &parent);
    TBOX_TEST_ASSERT(tbox_style_grid_templates_of(&style)->grid_template_columns[0] == '\0' && tbox_style_grid_templates_of(&style)->grid_template_areas[0] == '\0');
    TBOX_TEST_ASSERT(!style.grid_auto_flow_column && !style.grid_auto_flow_dense);
    TBOX_TEST_ASSERT(tbox_style_grid_placement_of(&style)->grid_row_start.number == 0 && tbox_style_grid_placement_of(&style)->grid_row_start.name[0] == '\0');
    TBOX_TEST_ASSERT(style.justify_items == TBOX_STYLE_FLEX_ALIGN_NORMAL);
    style = tbox_grid_resolve("div{grid-template-columns:30px!important;grid:10px / 20px;grid-auto-rows:7px;grid-template:40px / 50px}", NULL);
    TBOX_TEST_ASSERT(strcmp(tbox_style_grid_templates_of(&style)->grid_template_columns, "30px") == 0 && strcmp(tbox_style_grid_templates_of(&style)->grid_template_rows, "40px") == 0);
    TBOX_TEST_ASSERT(strcmp(tbox_style_grid_templates_of(&style)->grid_auto_rows, "7px") == 0);
    style = tbox_grid_resolve("div{grid-auto-rows:7px;grid-auto-flow:column;grid-template-areas:'a';grid:20px / 30px}", NULL);
    TBOX_TEST_ASSERT(tbox_style_grid_templates_of(&style)->grid_auto_rows[0] == '\0' && !style.grid_auto_flow_column && tbox_style_grid_templates_of(&style)->grid_template_areas[0] == '\0');
    style = tbox_grid_resolve("div{--cols:repeat(2, minmax(10px, 1fr));grid-template-columns:var(--cols);grid-area:foo / bar}", NULL);
    TBOX_TEST_ASSERT(strcmp(tbox_style_grid_templates_of(&style)->grid_template_columns, "repeat(2, minmax(10px, 1fr))") == 0);
    TBOX_TEST_ASSERT(strcmp(tbox_style_grid_placement_of(&style)->grid_row_end.name, "foo") == 0 && strcmp(tbox_style_grid_placement_of(&style)->grid_column_end.name, "bar") == 0);
    /* Malformed shorthands leave lower-priority longhands intact. */
    style = tbox_grid_resolve("div{grid-template-columns:30px;grid-template-rows:20px;grid-template:'a a' 'a .';grid-row-start:2;grid-row:0 / 4;grid-auto-flow:row column;grid-column:span -2}", NULL);
    TBOX_TEST_ASSERT(strcmp(tbox_style_grid_templates_of(&style)->grid_template_columns, "30px") == 0 && strcmp(tbox_style_grid_templates_of(&style)->grid_template_rows, "20px") == 0);
    TBOX_TEST_ASSERT(tbox_style_grid_templates_of(&style)->grid_template_areas[0] == '\0' && tbox_style_grid_placement_of(&style)->grid_row_start.number == 2 && !style.grid_auto_flow_column);
    TBOX_TEST_ASSERT(tbox_style_grid_placement_of(&style)->grid_column_start.number == 0 && !tbox_style_grid_placement_of(&style)->grid_column_start.span);
    static const char *const invalid_tracks[] = { "repeat(0,1fr)", "repeat(2,repeat(2,1fr))", "repeat(auto-fill,1fr)", "repeat(auto-fill,10px) repeat(auto-fit,10px)", "minmax(1fr,10px)", "minmax(0,)", "fit-content(-10px)", "[auto] 10px", "10px garbage", "repeat(999999999999999,10px)" };
    tbox_style_units units                    = { 16, 16, 800, 600, false, -1.0, -1.0, 1.0, TBOX_STYLE_FRAME_NONE, 0.0, 0.0 };
    for (size_t i = 0; i < sizeof(invalid_tracks) / sizeof(invalid_tracks[0]); i++) {
        tbox_grid_track_list tracks;
        TBOX_TEST_ASSERT(!tbox_style_grid_parse_tracks(tbox_string_view_from_cstr(invalid_tracks[i]), &units, 400, 10, true, false, &tracks));
    }
    /* Repeated end-line names are retained at every repetition boundary. */
    tbox_grid_track_list tracks;
    TBOX_TEST_ASSERT(tbox_style_grid_parse_tracks(tbox_string_view_from_cstr("[outer] repeat(auto-fill,[s] 50px [e]) [last] 20px"), &units, 200, 10, true, false, &tracks));
    TBOX_TEST_ASSERT(tracks.count == 4);
    int ends = 0;
    for (int i = 0; i < tracks.name_count; i++)
        if (tbox_string_view_equal_cstr(tracks.names[i].name, "e"))
            ends++;
    TBOX_TEST_ASSERT(ends == 3);
    tbox_arena_destroy(&style_arena);
    return failures;
}
