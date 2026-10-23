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

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CONTROLS_H */
