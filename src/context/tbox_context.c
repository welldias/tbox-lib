#include "tbox_context_internal.h"
#include "tbox_ua_style.h"

static void tbox_context_append_control_paint(tbox_context *ctx, tbox_display_list *list);
static void tbox_context_collect_style_elements(tbox_arena *arena, const tbox_html_node *node, tbox_string_builder *builder);

static void tbox_context_append_control_paint(tbox_context *ctx, tbox_display_list *list) {
    if (ctx->select_fields == NULL && ctx->open_select == NULL && ctx->open_color == NULL && ctx->open_date == NULL && ctx->open_file == NULL && !ctx->has_file_input && !ctx->has_number_input && !ctx->has_range_input && !ctx->has_search_input)
        return;
    tbox_vector items;
    tbox_vector_init(&items, &ctx->frame_arena, sizeof(tbox_paint_op), list->count + 32);
    for (size_t i = 0; i < list->count; i++) {
        tbox_paint_op *op = tbox_vector_push(&items);
        *op               = list->items[i];
    }
    tbox_context_paint_select_arrows(ctx->root, &items);
    tbox_context_paint_number_steppers(ctx->root, &items);
    tbox_context_paint_search_clears(ctx->root, &items);
    tbox_context_paint_ranges(ctx->root, &items);
    tbox_context_paint_file_controls(ctx, ctx->root, &items);
    tbox_context_paint_select_popup(ctx, &items);
    tbox_context_paint_color_popup(ctx, &items);
    tbox_context_paint_date_popup(ctx, &items);
    tbox_context_paint_file_popup(ctx, &items);
    list->items = items.data;
    list->count = items.length;
}

/* pre-order traversal of the WHOLE document tree (starting at
 * tbox_html_document_root -- the real root, which may have several
 * top-level children such as <head> and <body>, not the single "first
 * top-level element" tbox_layout_build isolates for itself in a separate
 * layer -- see ARCHITECTURE.md's v9 "Escopo"), appending the raw text
 * content of every <style> element found onto `builder`, in document
 * order. tbox_html_node_text_content already returns raw (non-entity-
 * decoded) text for a raw-text element like <style>, which is exactly what
 * CSS text needs. Pure function: reads the tree, writes only to `builder`
 * (a parameter) -- no global/static state. */
static void tbox_context_collect_style_elements(tbox_arena *arena, const tbox_html_node *node, tbox_string_builder *builder) {
    if (node == NULL) {
        return;
    }

    if (node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(node->element.tag_name, "style")) {
        tbox_string_view text = tbox_html_node_text_content(arena, node);
        tbox_string_builder_append_view(builder, text);
    }

    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        tbox_context_collect_style_elements(arena, child, builder);
    }
}

tbox_context_options tbox_context_options_default(void) {
    return (tbox_context_options){ .ua_style = tbox_ua_style_config_default() };
}

tbox_context *tbox_context_open_with_options(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_context_options options) {
    if (options.control_css != NULL && options.control_css_length == SIZE_MAX)
        return NULL;
    tbox_html_document *document = tbox_html_parse(html, html_length);
    if (document == NULL) {
        return NULL;
    }

    /* gathers every <style> element's raw text from the WHOLE
     * document (not just what Layout later renders) into one concatenated
     * buffer, parsed as a single extra author stylesheet below. `scratch`
     * only needs to survive long enough for tbox_css_parse to copy the
     * concatenated text into its own arena -- destroyed right after. */
    tbox_arena scratch = tbox_arena_create(0);
    tbox_string_builder style_builder;
    tbox_string_builder_init(&style_builder, &scratch, 0);
    tbox_context_collect_style_elements(&scratch, tbox_html_document_root(document), &style_builder);
    tbox_string_view internal_css_text = tbox_string_builder_finish(&style_builder);

    tbox_css_stylesheet *internal_stylesheet = NULL;
    if (internal_css_text.size > 0) {
        internal_stylesheet = tbox_css_parse(internal_css_text.data, internal_css_text.size);
        if (internal_stylesheet == NULL) {
            tbox_arena_destroy(&scratch);
            tbox_html_document_destroy(document);
            return NULL;
        }
    }
    tbox_arena_destroy(&scratch);

    tbox_css_stylesheet *stylesheet = tbox_css_parse(css, css_length);
    if (stylesheet == NULL) {
        tbox_css_stylesheet_destroy(internal_stylesheet);
        tbox_html_document_destroy(document);
        return NULL;
    }

    tbox_css_stylesheet *ua_stylesheet = tbox_ua_stylesheet_create(options.ua_style);
    if (ua_stylesheet == NULL) {
        tbox_css_stylesheet_destroy(stylesheet);
        tbox_css_stylesheet_destroy(internal_stylesheet);
        tbox_html_document_destroy(document);
        return NULL;
    }

    const char *control_css                 = options.control_css != NULL ? options.control_css : tbox_context_default_control_css();
    size_t control_css_length               = options.control_css != NULL ? options.control_css_length : strlen(control_css);
    tbox_css_stylesheet *control_stylesheet = tbox_css_parse(control_css, control_css_length);
    if (control_stylesheet == NULL) {
        tbox_css_stylesheet_destroy(ua_stylesheet);
        tbox_css_stylesheet_destroy(stylesheet);
        tbox_css_stylesheet_destroy(internal_stylesheet);
        tbox_html_document_destroy(document);
        return NULL;
    }

    tbox_context *ctx = (tbox_context *)malloc(sizeof(tbox_context));
    if (ctx == NULL) {
        tbox_css_stylesheet_destroy(control_stylesheet);
        tbox_css_stylesheet_destroy(ua_stylesheet);
        tbox_css_stylesheet_destroy(stylesheet);
        tbox_css_stylesheet_destroy(internal_stylesheet);
        tbox_html_document_destroy(document);
        return NULL;
    }

    ctx->document            = document;
    ctx->stylesheet          = stylesheet;
    ctx->ua_stylesheet       = ua_stylesheet;
    ctx->control_stylesheet  = control_stylesheet;
    ctx->internal_stylesheet = internal_stylesheet;
    ctx->fonts               = fonts;
    ctx->images              = images;
    ctx->root                = NULL;
    ctx->frame_arena         = tbox_arena_create(0);
    ctx->handler_arena       = tbox_arena_create(0);
    tbox_vector_init(&ctx->handlers, &ctx->handler_arena, sizeof(tbox_context_click_binding), 0);
    ctx->next_handler_id = 0;
    ctx->hovered_node    = NULL;
    ctx->focused_node    = NULL;
    ctx->text_fields     = NULL;
    ctx->file_fields     = NULL;
    ctx->select_fields   = NULL;
    ctx->form_defaults   = NULL;
    ctx->open_select     = NULL;
    ctx->open_color      = NULL;
    ctx->color_channel   = 0;
    ctx->open_date       = NULL;
    ctx->date_cursor     = (tbox_date){ 0 };
    ctx->date_hour       = 0;
    ctx->date_minute     = 0;
    ctx->open_file       = NULL;
    ctx->file_directory  = NULL;
    ctx->file_entries    = NULL;
    ctx->file_count = ctx->file_first = ctx->file_highlight = 0;
    ctx->has_file_input                                     = false;
    ctx->has_number_input                                   = false;
    ctx->has_range_input                                    = false;
    ctx->has_search_input                                   = false;
    ctx->range_drag_node                                    = NULL;
    ctx->popup_highlight                                    = NULL;
    ctx->popup_first                                        = 0;
    ctx->viewport_width                                     = 0.0;
    ctx->viewport_height                                    = 0.0;
    ctx->select_handler                                     = NULL;
    ctx->select_userdata                                    = NULL;
    ctx->scroll_states                                      = NULL;
    ctx->scroll_drag_node                                   = NULL;
    ctx->scroll_drag_grab_y                                 = 0.0;
    ctx->scroll_pointer_consumed                            = false;
    ctx->focus_scroll_pending                               = false;
    ctx->input_handler                                      = NULL;
    ctx->input_userdata                                     = NULL;
    ctx->submit_handler                                     = NULL;
    ctx->submit_userdata                                    = NULL;
    ctx->styles                                             = (tbox_style_table){ 0 };

    tbox_context_sanitize_inputs(ctx, (tbox_html_node *)tbox_html_document_root(document));
    tbox_context_capture_form_defaults(ctx, tbox_html_document_root(document));

    return ctx;
}

tbox_context *tbox_context_open_with_config(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_ua_style_config config) {
    tbox_context_options options = tbox_context_options_default();
    options.ua_style             = config;
    return tbox_context_open_with_options(html, html_length, css, css_length, fonts, images, options);
}

tbox_context *tbox_context_open(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face_cache *fonts, tbox_image_cache *images) {
    return tbox_context_open_with_config(html, html_length, css, css_length, fonts, images, tbox_ua_style_config_default());
}

void tbox_context_close(tbox_context *ctx) {
    if (ctx == NULL) {
        return;
    }

    for (tbox_text_field *field = ctx->text_fields; field != NULL;) {
        tbox_text_field *next = field->next;
        free(field->value);
        free(field);
        field = next;
    }
    for (tbox_file_field *field = ctx->file_fields; field != NULL;) {
        tbox_file_field *next = field->next;
        free(field->path);
        free(field);
        field = next;
    }
    tbox_context_file_close(ctx);
    for (tbox_select_field *field = ctx->select_fields; field != NULL;) {
        tbox_select_field *next = field->next;
        free(field);
        field = next;
    }
    for (tbox_scroll_state *state = ctx->scroll_states; state != NULL;) {
        tbox_scroll_state *next = state->next;
        free(state);
        state = next;
    }

    /* Each binding owns its compiled query's own arena (see
     * tbox_css_selector_query_destroy) -- distinct from handler_arena,
     * which only backs the `handlers` vector itself. a binding
     * already removed via tbox_context_unbind_click has query == NULL (its
     * query was destroyed there) -- skip it here to avoid a double
     * destroy. */
    size_t handler_count = tbox_vector_length(&ctx->handlers);
    for (size_t i = 0; i < handler_count; i++) {
        tbox_context_click_binding *binding = (tbox_context_click_binding *)tbox_vector_at(&ctx->handlers, i);
        if (binding->query != NULL) {
            tbox_css_selector_query_destroy(binding->query);
        }
    }
    tbox_arena_destroy(&ctx->handler_arena);

    tbox_css_stylesheet_destroy(ctx->ua_stylesheet);
    tbox_css_stylesheet_destroy(ctx->control_stylesheet);
    tbox_css_stylesheet_destroy(ctx->stylesheet);
    tbox_css_stylesheet_destroy(ctx->internal_stylesheet);
    tbox_html_document_destroy(ctx->document);
    tbox_arena_destroy(&ctx->frame_arena);
    free(ctx);
}

void tbox_context_run_frame(tbox_context *ctx, double viewport_width, double viewport_height, tbox_display_list *out_list) {
    /* Invalidates everything Style/Layout/Render produced last frame in
     * one shot -- no per-layer _destroy to call (see "Convenções" in
     * ARCHITECTURE.md). Must happen before ctx->root is overwritten below:
     * the old tree lives in this same arena. */
    tbox_arena_reset(&ctx->frame_arena);
    ctx->styles = (tbox_style_table){ 0 };

    if (ctx->focused_node != NULL && !tbox_context_node_attached(ctx, ctx->focused_node)) {
        ctx->focused_node = NULL;
    }
    if (ctx->open_select != NULL && !tbox_context_node_attached(ctx, ctx->open_select)) {
        ctx->open_select     = NULL;
        ctx->popup_highlight = NULL;
    }
    if (ctx->open_color != NULL && (!tbox_context_node_attached(ctx, ctx->open_color) || !tbox_context_is_color_input(ctx->open_color) || tbox_html_node_get_attribute(ctx->open_color, tbox_string_view_make("disabled", 8)) != NULL))
        ctx->open_color = NULL;
    if (ctx->open_date != NULL && (!tbox_context_node_attached(ctx, ctx->open_date) || !tbox_context_is_calendar_input(ctx->open_date) || tbox_html_node_get_attribute(ctx->open_date, tbox_string_view_make("disabled", 8)) != NULL))
        ctx->open_date = NULL;
    if (ctx->open_file != NULL && (!tbox_context_node_attached(ctx, ctx->open_file) || !tbox_context_is_file_input(ctx->open_file) || tbox_html_node_get_attribute(ctx->open_file, tbox_string_view_make("disabled", 8)) != NULL))
        tbox_context_file_close(ctx);

    const tbox_html_node *root = tbox_html_document_root(ctx->document);
    ctx->has_file_input        = false;
    ctx->has_number_input      = false;
    ctx->has_range_input       = false;
    ctx->has_search_input      = false;
    tbox_context_sanitize_inputs(ctx, (tbox_html_node *)root);

    /* must happen before every tbox_style_resolve_tree call,
     * unconditionally (not only on ticks where the hover state actually
     * changed) -- the cascade needs to see the CURRENT hover state every
     * time it resolves styles, not just as of whenever it last changed.
     * See tbox_context_update_hover and <tbox/css_selector.h>'s
     * tbox_css_selector_set_hover_context for the full sequencing
     * contract this call fulfills. */
    tbox_css_selector_set_hover_context(ctx->hovered_node);
    tbox_css_selector_set_focus_context(ctx->focused_node);

    /* three cascade sources -- the user-agent stylesheet, the
     * external author stylesheet, and  the internal stylesheet
     * assembled from every <style> element found in the document -- replacing
     * the 1-element placeholder array TASKS.md's Tarefa 1/Tarefa 3 left here.
     * Order in this array does not affect cascade priority BETWEEN DIFFERENT
     * origins (tbox_css_cascade_resolve already ranks by origin internally
     * regardless of array order) -- true for USER_AGENT vs. AUTHOR here, as
     * before. It is NO LONGER true between `stylesheet` and
     * `internal_stylesheet` specifically: both carry the SAME origin
     * (TBOX_CSS_ORIGIN_AUTHOR), so when a property ties in specificity
     * between the two, tbox_css_cascade_wins_or_ties's ">=" tie-break makes
     * whichever comes LAST in this array win -- `internal_stylesheet` is
     * placed after `stylesheet` on purpose, so an embedded <style> wins ties
     * against the external CSS (see ARCHITECTURE.md's v9 "Escopo"). */
    tbox_css_cascade_source sources[3] = {
        { ctx->ua_stylesheet,       TBOX_CSS_ORIGIN_USER_AGENT },
        { ctx->stylesheet,          TBOX_CSS_ORIGIN_AUTHOR     },
        { ctx->internal_stylesheet, TBOX_CSS_ORIGIN_AUTHOR     },
    };
    tbox_style_table styles = tbox_style_resolve_tree_in_viewport(&ctx->frame_arena, root, sources, 3, viewport_width, viewport_height);
    ctx->styles             = styles;

    /* NULL for an empty document (e.g. no ELEMENT to lay out) -- tracked
     * so tbox_context_hit_test has something to search (or not) between
     * frames. */
    ctx->root = tbox_layout_build(&ctx->frame_arena, root, &styles, ctx->fonts, ctx->images, viewport_width, viewport_height);
    tbox_context_apply_scroll(ctx, ctx->root);
    tbox_context_reveal_focused(ctx);
    tbox_context_apply_sticky(ctx, viewport_width, viewport_height);
    tbox_context_sync_select_boxes(ctx, ctx->root);
    tbox_context_layout_textareas(ctx, ctx->root);
    ctx->viewport_width  = viewport_width;
    ctx->viewport_height = viewport_height;

    /* tbox_render_build_display_list already treats a NULL root as "empty
     * subtree", producing {NULL, 0} -- no special-casing needed here. */
    *out_list = tbox_render_build_display_list(&ctx->frame_arena, ctx->root);
    tbox_context_paint_text_input_caret(ctx, out_list);
    tbox_context_paint_textarea_caret(ctx, out_list);
    tbox_context_append_scrollbars(ctx, out_list);
    tbox_context_append_control_paint(ctx, out_list);
}

void tbox_context_on_input(tbox_context *ctx, tbox_context_input_handler handler, void *userdata) {
    if (ctx == NULL)
        return;
    ctx->input_handler  = handler;
    ctx->input_userdata = userdata;
}

void tbox_context_on_submit(tbox_context *ctx, tbox_context_submit_handler handler, void *userdata) {
    if (ctx == NULL)
        return;
    ctx->submit_handler  = handler;
    ctx->submit_userdata = userdata;
}

tbox_html_document *tbox_context_document(tbox_context *ctx) {
    if (ctx == NULL) {
        return NULL;
    }
    return ctx->document;
}
