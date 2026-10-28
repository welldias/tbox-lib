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
static void tbox_transition_current(tbox_style_animatable property, const tbox_transition_track *track, const double target[TBOX_STYLE_ANIM_VALUE_SIZE], double now, double out[TBOX_STYLE_ANIM_VALUE_SIZE]) {
    double fraction = track->duration > 0.0 ? (now - track->start_time) / track->duration : 1.0;
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    tbox_style_animatable_mix(property, track->start, target, tbox_style_easing_apply(&track->easing, fraction), out);
}

static bool tbox_transition_any(const tbox_style *style) {
    const tbox_style_animation_lists *lists = tbox_style_animation_lists_of(style);
    for (size_t i = 0; i < lists->transition_count; i++)
        if (lists->transitions[i].duration > 0.0 && lists->transitions[i].properties != 0) return true;
    return false;
}

/* The values `node` starts its transitions from when it first appears
 * (or leaves display: none): its style with the @starting-style rules in
 * the cascade, against the parent's current style. False when it has no
 * transitions or no @starting-style rule could apply. */
static bool tbox_transition_starting_values(tbox_context *ctx, const tbox_style_entry *entry, const tbox_css_cascade_source *sources, size_t source_count, double viewport_width, double viewport_height, double out[TBOX_STYLE_ANIM_COUNT][TBOX_STYLE_ANIM_VALUE_SIZE]) {
    if (!ctx->has_starting_style || !tbox_transition_any(entry->style))
        return false;
    const tbox_style *parent = entry->node->parent != NULL ? tbox_style_table_find(&ctx->styles, entry->node->parent) : NULL;
    tbox_css_computed_style computed = tbox_css_cascade_resolve_starting_style_in_viewport(sources, source_count, entry->node, viewport_width, viewport_height, ctx->prefers_reduced_motion, &(tbox_css_query_context){ parent != NULL ? parent->query_container : NULL, parent != NULL ? parent->custom_properties : NULL });
    tbox_style starting = tbox_style_resolve_in_viewport(&ctx->frame_arena, entry->node, parent, &computed, viewport_width, viewport_height);
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

/* The states by node for one frame, in the frame arena: open addressing
 * over at least twice as many slots as states, so the per-element lookup
 * does not walk the list. `slots` is NULL when it could not be allocated,
 * and the lookup walks the list instead. */
typedef struct tbox_transition_index {
    tbox_transition_state **slots;
    size_t mask;
} tbox_transition_index;

static size_t tbox_transition_hash(const tbox_html_node *node) {
    uint64_t key = (uint64_t)(uintptr_t)node;
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdull;
    key ^= key >> 33;
    return (size_t)key;
}

/* The slot holding `node`'s state, or the empty one where it would go. */
static tbox_transition_state **tbox_transition_slot(const tbox_transition_index *index, const tbox_html_node *node) {
    size_t slot = tbox_transition_hash(node) & index->mask;
    while (index->slots[slot] != NULL && index->slots[slot]->node != node)
        slot = (slot + 1) & index->mask;
    return &index->slots[slot];
}

/* Room for the states there are plus one per element that may gain one. */
static tbox_transition_index tbox_transition_index_build(tbox_context *ctx) {
    tbox_transition_index index = { NULL, 0 };
    size_t count                = ctx->styles.count;
    for (const tbox_transition_state *state = ctx->transitions; state != NULL; state = state->next)
        count++;
    if (count > SIZE_MAX / 4 / sizeof(*index.slots))
        return index;
    size_t capacity = 16;
    while (capacity < count * 2)
        capacity *= 2;
    index.slots = tbox_arena_alloc_zero(&ctx->frame_arena, capacity * sizeof(*index.slots));
    index.mask  = capacity - 1;
    for (tbox_transition_state *state = ctx->transitions; index.slots != NULL && state != NULL; state = state->next) {
        tbox_transition_state **slot = tbox_transition_slot(&index, state->node);
        if (*slot == NULL)
            *slot = state;
    }
    return index;
}

void tbox_context_apply_transitions(tbox_context *ctx, const tbox_css_cascade_source *sources, size_t source_count, double viewport_width, double viewport_height) {
    ctx->next_frame_time        = INFINITY;
    tbox_transition_index index = tbox_transition_index_build(ctx);
    double now = ctx->animation_time;
    for (size_t i = 0; i < ctx->styles.count; i++) {
        tbox_style_entry *entry = &ctx->styles.items[i];
        if (entry->style->generated != 0)
            continue; /* ::before/::after nodes are rebuilt every frame: no transitions */
        if (!tbox_transition_any(entry->style))
            continue; /* no state: losing `transition` cancels the running ones */
        tbox_transition_state **slot = index.slots != NULL ? tbox_transition_slot(&index, entry->node) : NULL;
        tbox_transition_state *state = slot != NULL ? *slot : ctx->transitions;
        while (slot == NULL && state != NULL && state->node != entry->node) state = state->next;
        bool hidden = entry->style->display == TBOX_STYLE_DISPLAY_NONE;
        bool appearing = state == NULL ? !hidden : state->hidden && !hidden;
        if (state == NULL) {
            state = calloc(1, sizeof(*state));
            if (state == NULL) continue;
            state->node = entry->node;
            state->next = ctx->transitions;
            ctx->transitions = state;
            if (slot != NULL)
                *slot = state;
            /* First seen hidden: nothing to transition from or to yet. */
            if (!appearing)
                for (int p = 0; p < TBOX_STYLE_ANIM_COUNT; p++)
                    tbox_style_animatable_read(entry->style, (tbox_style_animatable)p, state->values[p]);
        }
        state->seen = true;
        state->hidden = hidden;
        /* Appearing: the "previous" values are the @starting-style ones,
         * so the change below starts a transition from them; without
         * such rules the element just takes its values. */
        if (appearing) {
            if (state->tracks != NULL)
                for (int p = 0; p < TBOX_STYLE_ANIM_COUNT; p++) state->tracks[p].active = false;
            if (!tbox_transition_starting_values(ctx, entry, sources, source_count, viewport_width, viewport_height, state->values)) {
                for (int p = 0; p < TBOX_STYLE_ANIM_COUNT; p++)
                    tbox_style_animatable_read(entry->style, (tbox_style_animatable)p, state->values[p]);
                continue;
            }
        }
        for (int p = 0; p < TBOX_STYLE_ANIM_COUNT; p++) {
            tbox_style_animatable property = (tbox_style_animatable)p;
            double target[TBOX_STYLE_ANIM_VALUE_SIZE];
            tbox_style_animatable_read(entry->style, property, target);
            if (memcmp(target, state->values[p], sizeof(target)) != 0) {
                double current[TBOX_STYLE_ANIM_VALUE_SIZE];
                memcpy(current, state->values[p], sizeof(current));
                if (state->tracks != NULL && state->tracks[p].active)
                    tbox_transition_current(property, &state->tracks[p], state->values[p], now, current);
                const tbox_style_transition *transition = tbox_style_transition_find(entry->style, property);
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
                double current[TBOX_STYLE_ANIM_VALUE_SIZE];
                tbox_transition_current(property, track, target, now, current);
                tbox_style *own = tbox_style_table_mutable(&ctx->styles, i, &ctx->frame_arena);
                if (own != NULL)
                    tbox_style_animatable_write(own, property, current, &ctx->frame_arena);
                /* in its delay the value holds still until start_time */
                tbox_context_wake_at(ctx, now < track->start_time ? track->start_time : -INFINITY);
            } else track->active = false;
        }
    }
    /* The elements not found above left the document or their
     * transitions: their states (and running transitions) go. */
    tbox_transition_state **link = &ctx->transitions;
    while (*link != NULL) {
        if ((*link)->seen) {
            (*link)->seen = false;
            link = &(*link)->next;
        } else {
            tbox_transition_state *removed = *link;
            *link = removed->next;
            tbox_transition_free(removed);
        }
    }
}
