#include <stdio.h>
#include <string.h>
#include <tbox/tbox.h>

/* Driving a <table> from code: add and remove rows, click a cell to select
 * its row, click a status cell to toggle it, and add or drop a numbering
 * column. The footer keeps a live count. */
static const char *demo_html =
    "<body><h1>Tables from code</h1>"
    "<p><input id='name' placeholder='Name'> <input id='email' placeholder='E-mail'>"
    " <button id='add'>Add</button> <button id='add-top'>Add at top</button></p>"
    "<p><button id='remove'>Remove selected</button> <button id='clear'>Clear</button>"
    " <button id='numbers'>Numbering</button></p>"
    "<table id='people'>"
    "<thead><tr><th>Name</th><th>E-mail</th><th>Status</th></tr></thead>"
    "<tbody>"
    "<tr><td>Ana</td><td>ana@example.com</td><td>active</td></tr>"
    "<tr><td>Bruno</td><td>bruno@example.com</td><td>inactive</td></tr>"
    "<tr><td>Carla</td><td>carla@example.com</td><td>active</td></tr>"
    "</tbody>"
    "<tfoot><tr><td colspan='3'></td></tr></tfoot>"
    "</table>"
    "<p id='status'>Click a cell to select its row; click a status to toggle it.</p></body>";
static const char *demo_css =
    "body { margin: 24px; font-family: sans-serif; }"
    "table { border-collapse: collapse; margin-top: 8px; }"
    "th, td { border: 1px solid #c8c8cc; padding: 4px 10px; text-align: left; }"
    "thead th { background: #eef0f4; }"
    "tfoot td { color: #555; font-style: italic; }"
    "tbody td { cursor: pointer; }"
    "tbody tr:hover { background: #f6f7fa; }"
    "tbody tr.selected { background: #cfe0ff; }";

/* The demo's state, handed to every handler as userdata. Nodes stay valid
 * while the document is the same; `selected` is cleared whenever its row
 * leaves the table. */
typedef struct table_demo {
    tbox_html_node *table;
    tbox_html_node *selected;
    bool numbered;
} table_demo;

static tbox_html_node *by_id(tbox_app *app, const char *id) {
    return tbox_app_get_element_by_id(app, tbox_string_view_make(id, strlen(id)));
}

static tbox_string_view text(const char *cstr) {
    return tbox_string_view_make(cstr, strlen(cstr));
}

static void set_status(tbox_app *app, const char *message) {
    tbox_control_set_label(app, by_id(app, "status"), text(message));
}

/* Row indices count the <thead> row first (as table.rows does): body rows
 * run from 1 to row_count - 2, the last row being the footer's. */
static size_t first_body_row(const table_demo *demo) {
    return tbox_table_head(demo->table) != NULL ? 1 : 0;
}

static size_t body_row_count(const table_demo *demo) {
    size_t rows = tbox_table_row_count(demo->table) - first_body_row(demo);
    return tbox_table_foot(demo->table) != NULL ? rows - 1 : rows;
}

/* The name column moves right while the numbering column is shown. */
static size_t name_column(const table_demo *demo) {
    return demo->numbered ? 1 : 0;
}

/* Renumbers the body and rewrites the footer after any change. */
static void refresh(tbox_app *app, table_demo *demo) {
    char buffer[48];
    size_t first = first_body_row(demo), count = body_row_count(demo);
    for (size_t i = 0; demo->numbered && i < count; i++) {
        snprintf(buffer, sizeof(buffer), "%zu", i + 1);
        tbox_table_set_cell_text(app, demo->table, first + i, 0, text(buffer));
    }
    snprintf(buffer, sizeof(buffer), "%zu %s", count, count == 1 ? "person" : "people");
    tbox_table_set_cell_text(app, demo->table, tbox_table_row_count(demo->table) - 1, 0, text(buffer));
}

static void select_row(table_demo *demo, tbox_html_node *row) {
    if (demo->selected != NULL)
        tbox_html_node_remove_class(demo->selected->owner_document, demo->selected, text("selected"));
    demo->selected = row;
    if (row != NULL) tbox_html_node_add_class(row->owner_document, row, text("selected"));
}

static void add_row(tbox_app *app, table_demo *demo, size_t before_index) {
    tbox_html_node *name = by_id(app, "name"), *email = by_id(app, "email");
    tbox_string_view cells[] = {tbox_control_value(app, name), tbox_control_value(app, email), text("active")};
    if (cells[0].size == 0) {
        set_status(app, "Type a name first.");
        tbox_control_focus(app, name);
        return;
    }
    tbox_html_node *row = tbox_table_insert_row(app, demo->table, before_index, cells, 3);
    if (demo->numbered) tbox_table_insert_cell(app, row, 0, text(""));
    select_row(demo, row);
    tbox_control_set_value(app, name, text(""));
    tbox_control_set_value(app, email, text(""));
    tbox_control_focus(app, name);
    refresh(app, demo);
    set_status(app, "Row added.");
}

static bool on_add(tbox_app *app, tbox_html_node *node, void *userdata) {
    (void)node;
    table_demo *demo = userdata;
    /* Before the footer row: the end of the last <tbody>. */
    add_row(app, demo, SIZE_MAX);
    return false;
}

static bool on_add_top(tbox_app *app, tbox_html_node *node, void *userdata) {
    (void)node;
    table_demo *demo = userdata;
    add_row(app, demo, first_body_row(demo));
    return false;
}

static bool on_remove(tbox_app *app, tbox_html_node *node, void *userdata) {
    (void)node;
    table_demo *demo = userdata;
    ptrdiff_t index  = tbox_table_row_index(demo->selected);
    if (index < 0) {
        set_status(app, "Select a row first.");
        return false;
    }
    tbox_table_remove_row(app, demo->table, (size_t)index);
    demo->selected = NULL;
    refresh(app, demo);
    set_status(app, "Row removed.");
    return false;
}

static bool on_clear(tbox_app *app, tbox_html_node *node, void *userdata) {
    (void)node;
    table_demo *demo = userdata;
    tbox_table_clear_body(app, demo->table);
    demo->selected = NULL;
    refresh(app, demo);
    set_status(app, "Table cleared.");
    return false;
}

/* Inserts or removes cell 0 in the header and body rows, and widens or
 * narrows the footer's colspan to match. */
static bool on_numbers(tbox_app *app, tbox_html_node *node, void *userdata) {
    (void)node;
    table_demo *demo = userdata;
    demo->numbered   = !demo->numbered;
    size_t last      = tbox_table_row_count(demo->table) - 1;
    for (size_t i = 0; i < last; i++) {
        tbox_html_node *row = tbox_table_row_at(demo->table, i);
        if (!demo->numbered)
            tbox_table_remove_cell(app, row, 0);
        else if (i == 0)
            tbox_html_node_insert_before(row, tbox_html_element_create(row->owner_document, text("th")),
                                         tbox_table_cell_at(row, 0));
        else
            tbox_table_insert_cell(app, row, 0, text(""));
    }
    if (demo->numbered) tbox_table_set_cell_text(app, demo->table, 0, 0, text("#"));
    tbox_html_node *foot_cell = tbox_table_cell_at(tbox_table_row_at(demo->table, last), 0);
    tbox_html_node_set_attribute(foot_cell->owner_document, foot_cell, text("colspan"), text(demo->numbered ? "4" : "3"));
    refresh(app, demo);
    set_status(app, demo->numbered ? "Numbering on." : "Numbering off.");
    return false;
}

/* Bound to "#people tbody td": `cell` is the clicked cell. */
static bool on_cell(tbox_app *app, tbox_html_node *cell, void *userdata) {
    table_demo *demo    = userdata;
    tbox_html_node *row = tbox_table_row_of(cell);
    size_t row_index    = (size_t)tbox_table_row_index(row);
    size_t column       = (size_t)tbox_table_cell_index(cell);
    select_row(demo, row);

    if (column == name_column(demo) + 2) {
        bool active = tbox_string_view_equal_cstr(tbox_table_cell_text(app, demo->table, row_index, column), "active");
        tbox_table_set_cell_text(app, demo->table, row_index, column, text(active ? "inactive" : "active"));
    }

    tbox_string_view name  = tbox_table_cell_text(app, demo->table, row_index, name_column(demo));
    tbox_string_view value = tbox_table_cell_text(app, demo->table, row_index, column);
    char message[160];
    snprintf(message, sizeof(message), "Row %zu (%.*s), column %zu: %.*s", row_index - first_body_row(demo) + 1,
             (int)name.size, name.data, column, (int)value.size, value.data);
    set_status(app, message);
    return false;
}

int main(void) {
    if (!tbox_app_backend_available()) {
        fputs("No interactive window backend in this build\n", stderr);
        return 1;
    }
    tbox_app *app = tbox_app_create(demo_html, demo_css, 720, 480);
    if (app == NULL) return 1;
    table_demo demo = {.table = by_id(app, "people")};
    refresh(app, &demo);
    tbox_app_on_click(app, "#add", 4, on_add, &demo, NULL);
    tbox_app_on_click(app, "#add-top", 8, on_add_top, &demo, NULL);
    tbox_app_on_click(app, "#remove", 7, on_remove, &demo, NULL);
    tbox_app_on_click(app, "#clear", 6, on_clear, &demo, NULL);
    tbox_app_on_click(app, "#numbers", 8, on_numbers, &demo, NULL);
    tbox_app_on_click(app, "#people tbody td", 16, on_cell, &demo, NULL);
    tbox_app_run(app);
    tbox_app_close(app);
    return 0;
}
