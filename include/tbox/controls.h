#ifndef TBOX_CONTROLS_H
#define TBOX_CONTROLS_H

#include <stdbool.h>
#include <stddef.h>

#include <tbox/app.h>
#include <tbox/html_parser.h>
#include <tbox/string_view.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Form controls driven from code. Each control keeps its state in a
 * different place -- an input's `value` attribute, a textarea's text, a
 * select's choice inside the context, a checkbox's `checked` attribute --
 * and these functions hide which, keeping the context's editing state
 * (caret, open popup, focus) coherent with the change.
 *
 * They take the tbox_app and act on its current document (the context
 * from tbox_app_context); a node from a document replaced by
 * tbox_app_load_from_files must not be passed.
 *
 * Common rules:
 * - UI thread only, like the rest of the app.
 * - NULL app or control: no-op, false, NULL or an empty view; so does a
 *   build without a window backend (no app can be created there).
 * - Input strings are copied. Returned views are borrowed until the next
 *   frame or the next mutation of that control, whichever comes first.
 * - Programmatic changes never call on_input/on_select (as in the DOM).
 *   Every change advances tbox_html_document_revision, so tbox_app draws a
 *   frame for it without tbox_app_request_redraw. */

/* The control's current value:
 * - input: the `value` attribute (checkbox/radio without one: "on");
 * - textarea, output: the text;
 * - select: the chosen option's value (its text when it has no `value`);
 * - button, progress, meter, option: the `value` attribute.
 * Empty for anything else. */
tbox_string_view tbox_control_value(tbox_app *app, const tbox_html_node *control);

/* Sets the value. A text field's caret goes to the end and its selection
 * is cleared. The value is then normalized the way the control would
 * (number/date/time/color formats, range clamped to min/max/step); one it
 * can't hold leaves it empty. A file input only accepts "" (clears the
 * choice). On a select, chooses the first enabled option with that value.
 * Returns false when nothing was applied (not a control, no such option,
 * a non-empty file value). Checkbox/radio: changes `value`, not checked --
 * see tbox_control_set_checked. */
bool tbox_control_set_value(tbox_app *app, tbox_html_node *control, tbox_string_view value);

/* The visible caption: the `value` attribute of input type=button/submit/
 * reset; an option's `label` attribute when set, else its text; the text
 * of any other element (button, label, legend, a, p...). */
tbox_string_view tbox_control_label(tbox_app *app, const tbox_html_node *control);

/* Replaces the caption as tbox_control_label reads it (for a plain element:
 * all its children become one text node). False for inputs that have no
 * caption (text, checkbox...). */
bool tbox_control_set_label(tbox_app *app, tbox_html_node *control, tbox_string_view text);

/* `:disabled` semantics: the `disabled` attribute, a control inside a
 * disabled fieldset (outside its first legend), or an option inside a
 * disabled optgroup. */
bool tbox_control_disabled(const tbox_html_node *control);

/* Adds or removes `disabled`. Disabling the focused control moves focus
 * away and closes its popup. */
void tbox_control_set_disabled(tbox_app *app, tbox_html_node *control, bool disabled);

/* Gives keyboard focus to a focusable control (as Tab would, and scrolls
 * it into view on the next frame). False when it can't take focus:
 * disabled, hidden, not a control. */
bool tbox_control_focus(tbox_app *app, tbox_html_node *control);

/* Removes focus from whatever has it, closing any open popup. */
void tbox_control_blur(tbox_app *app);

/* Checkbox/radio state (the `checked` attribute; `:checked` follows it). */
bool tbox_control_checked(const tbox_html_node *control);

/* Checks or unchecks a checkbox/radio. Checking a radio unchecks the rest
 * of its group (same `name`, same form). False for other elements. */
bool tbox_control_set_checked(tbox_app *app, tbox_html_node *control, bool checked);

/* The checked radio of the group `radio` belongs to (possibly `radio`
 * itself), or NULL when none is checked. */
tbox_html_node *tbox_control_radio_checked(tbox_app *app, const tbox_html_node *radio);

/* Classes, for state the stylesheet shows (a selected row, the active
 * entry of a menu, a file vs. a folder). `name` is one class: false for an
 * empty name or one with whitespace, a NULL app/node, or a node of another
 * document. Adding a present class or removing an absent one is a no-op
 * that still returns true. */
bool tbox_control_has_class(const tbox_html_node *node, tbox_string_view name);
bool tbox_control_set_class(tbox_app *app, tbox_html_node *node, tbox_string_view name, bool on);

/* The `hidden` attribute; the user-agent stylesheet gives `[hidden]`
 * `display: none`, which author CSS that sets `display` on the element
 * overrides (as in browsers). Hiding the focused control, or an element
 * containing it, moves focus away like tbox_control_set_disabled. */
bool tbox_control_hidden(const tbox_html_node *node);
void tbox_control_set_hidden(tbox_app *app, tbox_html_node *node, bool hidden);

/* Items built from markup: a list entry, a card, a table row with icons
 * and buttons. The HTML declares the item once inside a <template>
 * (display: none, its children are ordinary nodes); instantiate deep-copies
 * each of its element children and inserts the copies into `parent`
 * before the element child `before_index` (SIZE_MAX: at the end). Returns
 * the first copy -- fill it with tbox_app_query_in and the functions above
 * -- or NULL when `templ` isn't a <template>, `parent` is inside `templ`,
 * either belongs to another document, or `parent` is a <select>/<optgroup>
 * (use tbox_select_add_option). Ids are copied as they are: keep them out
 * of templates. For a table, pass a <tbody> as `parent` (a <tr> in a
 * template stays a <tr>); tbox_table_row_of/_row_index see the new rows. */
tbox_html_node *tbox_control_instantiate(tbox_app *app, const tbox_html_node *templ, tbox_html_node *parent,
                                         size_t before_index);

/* Detaches `node` (an instantiated item or any other element) / every
 * child of `container`, moving focus away first when it is inside. The
 * memory stays in the document's arena until the document is replaced.
 * False for the root element, a node with no parent, another document's node,
 * and <option>/<optgroup> or a <select>/<optgroup> container (use
 * tbox_select_remove_option / tbox_select_clear). On a <table>, clear also
 * drops <thead>/<tfoot>: use tbox_table_clear_body for rows. */
bool tbox_control_remove(tbox_app *app, tbox_html_node *node);
bool tbox_control_clear(tbox_app *app, tbox_html_node *container);

/* <select> options, counted in list order across <optgroup>s (group
 * labels are not options). Indices are positions in that list. */
size_t tbox_select_option_count(const tbox_html_node *select);
tbox_html_node *tbox_select_option_at(const tbox_html_node *select, size_t index);

/* Index of the chosen option, or -1 when the select has no options. */
ptrdiff_t tbox_select_selected_index(tbox_app *app, const tbox_html_node *select);

/* Chooses option `index`; false when out of range or disabled. */
bool tbox_select_set_selected_index(tbox_app *app, tbox_html_node *select, size_t index);

/* Creates `<option value=…>label</option>` and inserts it before option
 * `before_index` (inside that option's optgroup, if any), or last when the
 * index is out of range (pass SIZE_MAX). A `value` with NULL data omits
 * the attribute (the value is then the label). Returns the new option. */
tbox_html_node *tbox_select_add_option(tbox_app *app, tbox_html_node *select, tbox_string_view value,
                                       tbox_string_view label, size_t before_index);

/* Removes option `index`. When it was the chosen one, the choice falls
 * back as at load (the `selected` option, else the first enabled one). */
bool tbox_select_remove_option(tbox_app *app, tbox_html_node *select, size_t index);

/* Removes every option and optgroup. */
void tbox_select_clear(tbox_app *app, tbox_html_node *select);

/* Tables. Rows are counted as HTMLTableElement.rows does: <thead> rows
 * first, then <tbody> rows and <tr> children of the table itself in tree
 * order, then <tfoot> rows; nested tables are not looked into. Cell
 * indices are positions among the row's <td>/<th> children -- DOM order,
 * not grid columns (colspan/rowspan are not resolved). The functions keep
 * no state of their own: they read and edit the tree, so plain DOM
 * mutations mix freely with them. */
size_t tbox_table_row_count(const tbox_html_node *table);
tbox_html_node *tbox_table_row_at(const tbox_html_node *table, size_t index);

/* Creates a <tr> with one <td> per entry of `cells` (count 0: an empty
 * row) and inserts it before row `before_index`, in that row's section.
 * Out of range (pass SIZE_MAX): appended to the last <tbody>, or to the
 * table itself when it has none. Returns the new row. */
tbox_html_node *tbox_table_insert_row(tbox_app *app, tbox_html_node *table, size_t before_index,
                                      const tbox_string_view *cells, size_t count);

/* Removes row `index`; false when out of range. */
bool tbox_table_remove_row(tbox_app *app, tbox_html_node *table, size_t index);

/* Removes every body row (<tr> children of <tbody> or of the table),
 * keeping <caption>, <colgroup>, <thead>, <tfoot> and the <tbody>
 * elements themselves. */
void tbox_table_clear_body(tbox_app *app, tbox_html_node *table);

/* The table's first <thead>/<tfoot>, or NULL. */
tbox_html_node *tbox_table_head(const tbox_html_node *table);
tbox_html_node *tbox_table_foot(const tbox_html_node *table);

size_t tbox_table_cell_count(const tbox_html_node *row);
tbox_html_node *tbox_table_cell_at(const tbox_html_node *row, size_t index);

/* Creates `<td>text</td>` before cell `before_index` (SIZE_MAX: last).
 * Returns the new cell. */
tbox_html_node *tbox_table_insert_cell(tbox_app *app, tbox_html_node *row, size_t before_index,
                                       tbox_string_view text);

/* Removes cell `index`; false when out of range. */
bool tbox_table_remove_cell(tbox_app *app, tbox_html_node *row, size_t index);

/* Text of cell (row, column), as textContent; an empty view when either
 * index is out of range. */
tbox_string_view tbox_table_cell_text(tbox_app *app, const tbox_html_node *table, size_t row, size_t column);

/* Replaces the cell's children with one text node; false when either
 * index is out of range. */
bool tbox_table_set_cell_text(tbox_app *app, tbox_html_node *table, size_t row, size_t column,
                              tbox_string_view text);

/* The nearest <tr> / <td> or <th> at or above `node` (the innermost table
 * wins), or NULL outside one -- for click handlers, which receive the
 * node their selector matched. */
tbox_html_node *tbox_table_row_of(const tbox_html_node *node);
tbox_html_node *tbox_table_cell_of(const tbox_html_node *node);

/* rowIndex (in tbox_table_row_at's numbering) / cellIndex; -1 when the
 * row is not in a table, or the cell not in a row. */
ptrdiff_t tbox_table_row_index(const tbox_html_node *row);
ptrdiff_t tbox_table_cell_index(const tbox_html_node *cell);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CONTROLS_H */
