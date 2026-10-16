#include "tbox_context_internal.h"

void tbox_context_set_animation_time(tbox_context *ctx, double seconds) {
    if (ctx != NULL && isfinite(seconds)) ctx->animation_time = seconds;
}

double tbox_context_next_frame_time(const tbox_context *ctx) {
    return ctx != NULL ? ctx->next_frame_time : INFINITY;
}

bool tbox_context_animations_active(const tbox_context *ctx) {
    return ctx != NULL && ctx->next_frame_time < INFINITY;
}

/* The value `track` shows at `now`, heading for `target`. */
static void tbox_transition_current(const tbox_transition_track *track, const double target[4], double now, double out[4]) {
    double fraction = track->duration > 0.0 ? (now - track->start_time) / track->duration : 1.0;
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    tbox_style_animatable_mix(track->start, target, tbox_style_easing_apply(&track->easing, fraction), out);
}

static bool tbox_transition_any(const tbox_style *style) {
    for (size_t i = 0; i < style->transition_count; i++)
        if (style->transitions[i].duration > 0.0 && style->transitions[i].properties != 0) return true;
    return false;
}

/* The values `node` starts its transitions from when it first appears
 * (or leaves display: none): its style with the @starting-style rules in
 * the cascade, against the parent's current style. False when it has no
 * transitions or no @starting-style rule could apply. */
static bool tbox_transition_starting_values(tbox_context *ctx, const tbox_style_entry *entry, const tbox_css_cascade_source *sources, size_t source_count, double viewport_width, double viewport_height, double out[TBOX_STYLE_ANIM_COUNT][4]) {
    if (!ctx->has_starting_style || !tbox_transition_any(&entry->style))
        return false;
    const tbox_style *parent = entry->node->parent != NULL ? tbox_style_table_find(&ctx->styles, entry->node->parent) : NULL;
    tbox_css_computed_style computed = tbox_css_cascade_resolve_starting_style_in_viewport(sources, source_count, entry->node, viewport_width, viewport_height, ctx->prefers_reduced_motion);
    tbox_style starting = tbox_style_resolve_in_viewport(entry->node, parent, &computed, viewport_width, viewport_height);
    tbox_css_computed_style_destroy(&computed);
    for (int p = 0; p < TBOX_STYLE_ANIM_COUNT; p++)
        tbox_style_animatable_read(&starting, (tbox_style_animatable)p, out[p]);
    return true;
}

static void tbox_transition_free(tbox_transition_state *state) {
    free(state->tracks);
    free(state);
}

void tbox_context_free_transitions(tbox_context *ctx) {
    for (tbox_transition_state *state = ctx->transitions; state != NULL;) {
        tbox_transition_state *next = state->next;
        tbox_transition_free(state);
        state = next;
    }
    ctx->transitions = NULL;
}

void tbox_context_apply_transitions(tbox_context *ctx, const tbox_css_cascade_source *sources, size_t source_count, double viewport_width, double viewport_height) {
    ctx->next_frame_time = INFINITY;
    tbox_transition_state **link = &ctx->transitions;
    while (*link != NULL) {
        if (tbox_context_node_attached(ctx, (*link)->node)) link = &(*link)->next;
        else {
            tbox_transition_state *removed = *link;
            *link = removed->next;
            tbox_transition_free(removed);
        }
    }
    double now = ctx->animation_time;
    for (size_t i = 0; i < ctx->styles.count; i++) {
        tbox_style_entry *entry = &ctx->styles.items[i];
        if (entry->style.generated != 0)
            continue; /* ::before/::after nodes are rebuilt every frame: no transitions */
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
        if (appearing) {
            if (state->tracks != NULL)
                for (int p = 0; p < TBOX_STYLE_ANIM_COUNT; p++) state->tracks[p].active = false;
            if (!tbox_transition_starting_values(ctx, entry, sources, source_count, viewport_width, viewport_height, state->values)) {
                for (int p = 0; p < TBOX_STYLE_ANIM_COUNT; p++)
                    tbox_style_animatable_read(&entry->style, (tbox_style_animatable)p, state->values[p]);
                continue;
            }
        }
        for (int p = 0; p < TBOX_STYLE_ANIM_COUNT; p++) {
            tbox_style_animatable property = (tbox_style_animatable)p;
            double target[4];
            tbox_style_animatable_read(&entry->style, property, target);
            if (memcmp(target, state->values[p], sizeof(target)) != 0) {
                double current[4];
                memcpy(current, state->values[p], sizeof(current));
                if (state->tracks != NULL && state->tracks[p].active)
                    tbox_transition_current(&state->tracks[p], state->values[p], now, current);
                const tbox_style_transition *transition = tbox_style_transition_find(&entry->style, property);
                if (transition != NULL && tbox_style_animatable_interpolable(property, current, target)) {
                    if (state->tracks == NULL) state->tracks = calloc(TBOX_STYLE_ANIM_COUNT, sizeof(*state->tracks));
                    if (state->tracks != NULL) {
                        tbox_transition_track *track = &state->tracks[p];
                        memcpy(track->start, current, sizeof(current));
                        track->start_time = now + transition->delay;
                        track->duration   = transition->duration;
                        track->easing     = transition->easing;
                        track->active     = true;
                    }
                } else if (state->tracks != NULL) state->tracks[p].active = false;
                memcpy(state->values[p], target, sizeof(target));
            }
            if (state->tracks == NULL || !state->tracks[p].active) continue;
            tbox_transition_track *track = &state->tracks[p];
            if (now < track->start_time + track->duration) {
                double current[4];
                tbox_transition_current(track, target, now, current);
                tbox_style_animatable_write(&entry->style, property, current);
                /* in its delay the value holds still until start_time */
                tbox_context_wake_at(ctx, now < track->start_time ? track->start_time : -INFINITY);
            } else track->active = false;
        }
    }
}
