#include "css_cascade/tbox_css_cascade_internal.h"
#include "tbox_context_internal.h"

/* `animation` + `@keyframes` (CSS Animations 1), run after transitions on
 * the frame's resolved styles: each running animation overwrites the
 * animatable properties its keyframes name, before layout.
 *
 * A keyframe's values are resolved once, when its animation starts: the
 * element's cascade with the keyframe's declarations on top (above every
 * normal declaration, below !important ones), through the usual Style
 * resolution. The implicit 0%/100% keyframes use the element's current
 * style, so they follow it. */

static const tbox_css_keyframes_rule *tbox_animation_find_rule(const tbox_css_cascade_source *sources, size_t source_count, const char *name, double viewport_width, double viewport_height, bool reduced_motion) {
    const tbox_css_keyframes_rule *found = NULL;
    tbox_string_view wanted = tbox_string_view_from_cstr(name);
    for (size_t s = 0; s < source_count; s++) {
        size_t count = tbox_css_stylesheet_keyframes_count(sources[s].stylesheet);
        const tbox_css_keyframes_rule *rules = tbox_css_stylesheet_keyframes(sources[s].stylesheet);
        for (size_t i = 0; i < count; i++)
            if (tbox_string_view_equal(rules[i].name, wanted) && tbox_css_cascade_media_matches(rules[i].media, viewport_width, viewport_height, reduced_motion))
                found = &rules[i]; /* the last one wins */
    }
    return found;
}

static void tbox_animation_free(tbox_animation_state *state) {
    free(state->frames);
    free(state);
}

void tbox_context_free_animations(tbox_context *ctx) {
    for (tbox_animation_state *state = ctx->animations; state != NULL;) {
        tbox_animation_state *next = state->next;
        tbox_animation_free(state);
        state = next;
    }
    ctx->animations = NULL;
}

/* Resolves every keyframe of `rule` for `entry`'s element into `state`. */
static bool tbox_animation_resolve_frames(tbox_context *ctx, tbox_animation_state *state, const tbox_style_entry *entry, const tbox_css_keyframes_rule *rule, const tbox_css_cascade_source *sources, size_t source_count, double viewport_width, double viewport_height) {
    free(state->frames);
    state->frames      = calloc(rule->frame_count, sizeof(*state->frames));
    state->frame_count = 0;
    state->rule        = rule;
    if (state->frames == NULL)
        return false;
    const tbox_style *parent = entry->node->parent != NULL ? tbox_style_table_find(&ctx->styles, entry->node->parent) : NULL;
    tbox_css_computed_style base = tbox_css_cascade_resolve_in_viewport(sources, source_count, entry->node, viewport_width, viewport_height, ctx->prefers_reduced_motion, &(tbox_css_query_context){ parent != NULL ? parent->query_container : NULL, parent != NULL ? parent->custom_properties : NULL });
    for (size_t f = 0; f < rule->frame_count; f++) {
        const tbox_css_keyframe *keyframe = &rule->frames[f];
        tbox_animation_frame *frame = &state->frames[state->frame_count++];
        frame->offset = keyframe->offset;
        frame->easing = tbox_style_easing_default();
        size_t capacity = base.count + keyframe->declaration_count;
        tbox_css_resolved_declaration *items = calloc(capacity > 0 ? capacity : 1, sizeof(*items));
        if (items == NULL)
            continue;
        size_t count = 0;
        for (size_t i = 0; i < base.count; i++) {
            bool replaced = false;
            for (size_t d = 0; d < keyframe->declaration_count && !replaced; d++)
                replaced = !base.items[i].important && tbox_string_view_equal(base.items[i].property, keyframe->declarations[d].property);
            if (!replaced) items[count++] = base.items[i];
        }
        for (size_t d = 0; d < keyframe->declaration_count; d++) {
            const tbox_css_declaration *declaration = &keyframe->declarations[d];
            bool important = false;
            tbox_string_view value = tbox_css_cascade_strip_important(declaration->value, &important);
            if (important)
                continue; /* ignored inside @keyframes */
            if (tbox_string_view_equal(declaration->property, tbox_string_view_from_cstr("animation-timing-function"))) {
                tbox_style_easing easing;
                if (tbox_style_parse_easing_function(value, &easing)) frame->easing = easing, frame->has_easing = true;
                continue;
            }
            bool kept_important = false;
            for (size_t i = 0; i < count && !kept_important; i++)
                kept_important = items[i].important && tbox_string_view_equal(items[i].property, declaration->property);
            if (kept_important)
                continue;
            if (!tbox_string_view_equal(declaration->property, tbox_string_view_from_cstr("all")))
                frame->mask |= tbox_style_animatable_mask(declaration->property);
            tbox_css_resolved_declaration *item = &items[count++];
            memset(item, 0, sizeof(*item));
            item->origin       = TBOX_CSS_ORIGIN_AUTHOR_INLINE;
            item->property     = declaration->property;
            item->value        = value;
            item->specificity  = (tbox_css_specificity){ 1000, 0, 0 };
            item->source_order = (size_t)-1;
        }
        tbox_css_computed_style computed = { items, count, NULL, { 0 } };
        tbox_css_computed_style_index(&computed);
        tbox_style style = tbox_style_resolve_in_viewport(&ctx->frame_arena, entry->node, parent, &computed, viewport_width, viewport_height);
        for (int p = 0; p < TBOX_STYLE_ANIM_COUNT; p++)
            if (frame->mask & (1ull << p)) tbox_style_animatable_read(&style, (tbox_style_animatable)p, frame->values[p]);
        free(items);
    }
    tbox_css_computed_style_destroy(&base);
    return true;
}

/* The animation's directed progress at local time `t` (seconds since its
 * delay ended), CSS Animations 1 §4: false when it has no effect then. */
static bool tbox_animation_progress(const tbox_style_animation *animation, double t, double *out, bool *finished) {
    double duration = animation->duration, iterations = animation->iterations;
    double active = duration * iterations;
    *finished = false;
    double overall;
    if (t < 0.0) {
        if (!animation->fill_backwards) return false;
        overall = 0.0;
    } else if (isfinite(active) && t >= active) {
        *finished = true;
        if (!animation->fill_forwards) return false;
        overall = iterations;
    } else {
        overall = duration > 0.0 ? t / duration : iterations;
    }
    double iteration = floor(overall), fraction = overall - iteration;
    if (fraction == 0.0 && overall > 0.0 && (*finished || duration <= 0.0)) {
        iteration -= 1.0;
        fraction = 1.0;
    }
    bool odd = fmod(iteration, 2.0) == 1.0;
    bool reverse = animation->direction == TBOX_STYLE_ANIMATION_REVERSE ||
                   (animation->direction == TBOX_STYLE_ANIMATION_ALTERNATE && odd) ||
                   (animation->direction == TBOX_STYLE_ANIMATION_ALTERNATE_REVERSE && !odd);
    *out = reverse ? 1.0 - fraction : fraction;
    return true;
}

/* Writes `property` at directed progress `progress` between the keyframes
 * that name it (and the implicit 0%/100% ones, from `base`). */
static void tbox_animation_apply_property(const tbox_animation_state *state, const tbox_style_animation *animation, tbox_style_animatable property, double progress, const double base[TBOX_STYLE_ANIM_VALUE_SIZE], tbox_style *style) {
    unsigned long long bit = 1ull << property;
    const double *from = base, *to = NULL;
    double from_offset = 0.0, to_offset = 1.0;
    const tbox_style_easing *easing = &animation->easing;
    for (size_t f = 0; f < state->frame_count; f++) {
        const tbox_animation_frame *frame = &state->frames[f];
        if (!(frame->mask & bit)) continue;
        if (frame->offset <= progress) {
            from = frame->values[property], from_offset = frame->offset;
            easing = frame->has_easing ? &frame->easing : &animation->easing;
        } else {
            to = frame->values[property], to_offset = frame->offset;
            break;
        }
    }
    if (to == NULL) {
        to = from_offset >= 1.0 ? from : base; /* the implicit 100% keyframe */
        to_offset = 1.0;
    }
    double span = to_offset - from_offset;
    double local = span > 0.0 ? (progress - from_offset) / span : 1.0;
    local = tbox_style_easing_apply(easing, local < 0.0 ? 0.0 : local > 1.0 ? 1.0 : local);
    double value[TBOX_STYLE_ANIM_VALUE_SIZE];
    if (tbox_style_animatable_interpolable(property, from, to)) tbox_style_animatable_mix(property, from, to, local, value);
    else memcpy(value, local < 0.5 ? from : to, sizeof(value));
    tbox_style_animatable_write(style, property, value);
}

void tbox_context_apply_animations(tbox_context *ctx, const tbox_css_cascade_source *sources, size_t source_count, double viewport_width, double viewport_height) {
    double now = ctx->animation_time;
    for (tbox_animation_state *state = ctx->animations; state != NULL; state = state->next) state->seen = false;
    for (size_t i = 0; i < ctx->styles.count; i++) {
        tbox_style_entry *entry = &ctx->styles.items[i];
        if (entry->style.generated != 0 || entry->style.animation_count == 0 || entry->style.display == TBOX_STYLE_DISPLAY_NONE)
            continue;
        double base[TBOX_STYLE_ANIM_COUNT][TBOX_STYLE_ANIM_VALUE_SIZE];
        for (int p = 0; p < TBOX_STYLE_ANIM_COUNT; p++)
            tbox_style_animatable_read(&entry->style, (tbox_style_animatable)p, base[p]);
        for (size_t a = 0; a < entry->style.animation_count; a++) {
            const tbox_style_animation *animation = &entry->style.animations[a];
            const tbox_css_keyframes_rule *rule = tbox_animation_find_rule(sources, source_count, animation->name, viewport_width, viewport_height, ctx->prefers_reduced_motion);
            if (rule == NULL)
                continue;
            tbox_animation_state *state = ctx->animations;
            while (state != NULL && !(state->node == entry->node && strcmp(state->name, animation->name) == 0)) state = state->next;
            if (state == NULL) {
                state = calloc(1, sizeof(*state));
                if (state == NULL) continue;
                state->node = entry->node;
                snprintf(state->name, sizeof(state->name), "%s", animation->name);
                state->start_time = now;
                state->next = ctx->animations;
                ctx->animations = state;
            }
            state->seen = true;
            if (state->rule != rule && !tbox_animation_resolve_frames(ctx, state, entry, rule, sources, source_count, viewport_width, viewport_height))
                continue;
            if (animation->paused && !state->paused) state->paused = true, state->paused_at = now;
            else if (!animation->paused && state->paused) state->paused = false, state->start_time += now - state->paused_at;
            double local = (state->paused ? state->paused_at : now) - state->start_time - animation->delay;
            double progress;
            bool finished;
            bool effect = tbox_animation_progress(animation, local, &progress, &finished);
            /* in its delay nothing moves until the delay ends */
            if (!finished && !state->paused) tbox_context_wake_at(ctx, local < 0.0 ? now - local : -INFINITY);
            if (!effect) continue;
            unsigned long long animated = 0;
            for (size_t f = 0; f < state->frame_count; f++) animated |= state->frames[f].mask;
            for (int p = 0; p < TBOX_STYLE_ANIM_COUNT; p++)
                if (animated & (1ull << p)) tbox_animation_apply_property(state, animation, (tbox_style_animatable)p, progress, base[p], &entry->style);
        }
    }
    tbox_animation_state **link = &ctx->animations;
    while (*link != NULL) {
        if ((*link)->seen) link = &(*link)->next;
        else {
            tbox_animation_state *removed = *link;
            *link = removed->next;
            tbox_animation_free(removed);
        }
    }
}
