#include "tbox_context_internal.h"

void tbox_context_set_animation_time(tbox_context *ctx, double seconds) {
    if (ctx != NULL && isfinite(seconds)) ctx->animation_time = seconds;
}

bool tbox_context_animations_active(const tbox_context *ctx) {
    return ctx != NULL && ctx->animations_active;
}

static void tbox_transition_read(const tbox_style *style, int property, double out[4]) {
    if (property == 2) {
        out[0] = style->opacity;
        out[1] = out[2] = out[3] = 0.0;
    } else {
        tbox_css_rgba color = property == 0 ? style->color : style->background_color;
        out[0] = color.r, out[1] = color.g, out[2] = color.b, out[3] = color.a;
    }
}

static void tbox_transition_write(tbox_style *style, int property, const double value[4]) {
    if (property == 2) {
        style->opacity = value[0];
    } else {
        tbox_css_rgba color = {0};
        color.r = (unsigned char)lround(value[0]);
        color.g = (unsigned char)lround(value[1]);
        color.b = (unsigned char)lround(value[2]);
        color.a = (unsigned char)lround(value[3]);
        if (property == 0) style->color = color;
        else style->background_color = color;
    }
}

static double tbox_transition_bezier(double t, double a, double b) {
    double inv = 1.0 - t;
    return 3.0 * inv * inv * t * a + 3.0 * inv * t * t * b + t * t * t;
}

static double tbox_transition_progress(double progress, unsigned char timing) {
    if (timing == 1 || progress <= 0.0 || progress >= 1.0) return progress;
    double x1 = 0.25, y1 = 0.1, x2 = 0.25, y2 = 1.0;
    if (timing == 2) x1 = 0.42, y1 = 0.0, x2 = 1.0, y2 = 1.0;
    else if (timing == 3) x1 = 0.0, y1 = 0.0, x2 = 0.58, y2 = 1.0;
    else if (timing == 4) x1 = 0.42, y1 = 0.0, x2 = 0.58, y2 = 1.0;
    double low = 0.0, high = 1.0;
    for (int i = 0; i < 16; i++) {
        double mid = (low + high) * 0.5;
        if (tbox_transition_bezier(mid, x1, x2) < progress) low = mid;
        else high = mid;
    }
    return tbox_transition_bezier((low + high) * 0.5, y1, y2);
}

static void tbox_transition_current(const tbox_transition_track *track, double now, double out[4]) {
    double fraction = track->duration > 0.0 ? (now - track->start_time) / track->duration : 1.0;
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    fraction = tbox_transition_progress(fraction, track->timing);
    for (int i = 0; i < 4; i++) out[i] = track->start[i] + (track->target[i] - track->start[i]) * fraction;
}

/* The values `node` starts its transitions from when it first appears
 * (or leaves display: none): its style with the @starting-style rules in
 * the cascade, against the parent's current style. False when it has no
 * transitions or no @starting-style rule could apply. */
static bool tbox_transition_starting_values(tbox_context *ctx, const tbox_style_entry *entry, const tbox_css_cascade_source *sources, size_t source_count, double viewport_width, double viewport_height, double out[3][4]) {
    if (!ctx->has_starting_style)
        return false;
    bool any = false;
    for (int p = 0; p < 3; p++)
        if (entry->style.transition_duration[p] > 0.0) any = true;
    if (!any)
        return false;
    const tbox_style *parent = entry->node->parent != NULL ? tbox_style_table_find(&ctx->styles, entry->node->parent) : NULL;
    tbox_css_computed_style computed = tbox_css_cascade_resolve_starting_style_in_viewport(sources, source_count, entry->node, viewport_width, viewport_height, ctx->prefers_reduced_motion);
    tbox_style starting = tbox_style_resolve_in_viewport(entry->node, parent, &computed, viewport_width, viewport_height);
    tbox_css_computed_style_destroy(&computed);
    for (int p = 0; p < 3; p++)
        tbox_transition_read(&starting, p, out[p]);
    return true;
}

void tbox_context_apply_transitions(tbox_context *ctx, const tbox_css_cascade_source *sources, size_t source_count, double viewport_width, double viewport_height) {
    ctx->animations_active = false;
    tbox_transition_state **link = &ctx->transitions;
    while (*link != NULL) {
        if (tbox_context_node_attached(ctx, (*link)->node)) link = &(*link)->next;
        else {
            tbox_transition_state *removed = *link;
            *link = removed->next;
            free(removed);
        }
    }
    for (size_t i = 0; i < ctx->styles.count; i++) {
        tbox_style_entry *entry = &ctx->styles.items[i];
        tbox_transition_state *state = ctx->transitions;
        while (state != NULL && state->node != entry->node) state = state->next;
        bool hidden = entry->style.display == TBOX_STYLE_DISPLAY_NONE;
        bool appearing = state == NULL ? !hidden : state->hidden && !hidden;
        if (state == NULL) {
            state = calloc(1, sizeof(*state));
            if (state == NULL) continue;
            state->node = entry->node;
            state->next = ctx->transitions;
            ctx->transitions = state;
        }
        state->hidden = hidden;
        /* Appearing: the "previous" values are the @starting-style ones,
         * so the change below starts a transition from them; without
         * such rules the element just takes its values. */
        double starting[3][4];
        if (appearing) {
            bool from_start = tbox_transition_starting_values(ctx, entry, sources, source_count, viewport_width, viewport_height, starting);
            for (int p = 0; p < 3; p++) {
                state->tracks[p].active = false;
                tbox_transition_read(&entry->style, p, state->tracks[p].target);
                if (from_start) memcpy(state->tracks[p].target, starting[p], sizeof(starting[p]));
            }
            if (!from_start) continue;
        }
        for (int p = 0; p < 3; p++) {
            tbox_transition_track *track = &state->tracks[p];
            double target[4], current[4];
            tbox_transition_read(&entry->style, p, target);
            tbox_transition_current(track, ctx->animation_time, current);
            bool changed = false;
            for (int c = 0; c < 4; c++) if (target[c] != track->target[c]) changed = true;
            if (changed) {
                memcpy(track->start, current, sizeof(current));
                memcpy(track->target, target, sizeof(target));
                track->duration = entry->style.transition_duration[p];
                track->start_time = ctx->animation_time + entry->style.transition_delay[p];
                track->timing = entry->style.transition_timing[p];
                track->active = track->duration > 0.0;
            }
            if (track->active && ctx->animation_time < track->start_time + track->duration) {
                tbox_transition_current(track, ctx->animation_time, current);
                tbox_transition_write(&entry->style, p, current);
                ctx->animations_active = true;
            } else track->active = false;
        }
    }
}
