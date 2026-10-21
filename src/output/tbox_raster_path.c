#include "output/tbox_raster_path.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_STROKER_H
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Keep below the signed outline limits of older supported FreeType builds. */
#define TBOX_RASTER_PATH_MAX_POINTS 32760

static bool tbox_path_ft_point(double x, double y, FT_Vector *out) {
    if (!isfinite(x) || !isfinite(y) || fabs(x) > 1048576 || fabs(y) > 1048576)
        return false;
    *out = (FT_Vector){ (FT_Pos)llround(x * 64), (FT_Pos)llround(y * 64) };
    return true;
}
static bool tbox_path_fill_outline(FT_Library library, const tbox_paint_path_segment *s, size_t count, FT_Outline *outline) {
    if (count == 0 || count > TBOX_RASTER_PATH_MAX_POINTS)
        return false;
    size_t contours = 0;
    for (size_t i = 0; i < count; i++)
        if (s[i].move || i == 0)
            contours++;
    if (FT_Outline_New(library, (FT_UInt)count, (FT_Int)contours, outline) != 0)
        return false;
    size_t c = 0;
    for (size_t i = 0; i < count; i++) {
        if (!tbox_path_ft_point(s[i].x1, s[i].y1, &outline->points[i])) {
            FT_Outline_Done(library, outline);
            return false;
        }
        outline->tags[i] = FT_CURVE_TAG_ON;
        if (i + 1 == count || s[i + 1].move)
            outline->contours[c++] = (short)i;
    }
    return true;
}
/* Dashes are cut by arc length before stroking. A dash continuing across
 * vertices stays one subpath, so joins remain joins rather than end caps. */
static bool tbox_path_dash(const tbox_paint_path *path, tbox_vector *out) {
    size_t count  = path->dash_count;
    double period = 0;
    for (size_t i = 0; i < count; i++) {
        if (!isfinite(path->dashes[i]) || path->dashes[i] < 0)
            return false;
        period += path->dashes[i];
    }
    if (count % 2)
        period *= 2;
    if (!(period > 0) || !isfinite(period))
        return false;
    double phase = fmod(path->dashoffset, period);
    if (phase < 0)
        phase += period;
    size_t pattern_count = count % 2 ? count * 2 : count, index = 0;
    double remaining = 0;
    bool pen         = false;
    for (size_t i = 0; i < path->count; i++) {
        const tbox_paint_path_segment *s = &path->segments[i];
        if (i == 0 || s->move) {
            index       = 0;
            remaining   = path->dashes[0];
            pen         = false;
            double skip = phase;
            for (size_t k = 0; k < pattern_count && (remaining <= 0 || skip >= remaining); k++) {
                skip -= remaining;
                index     = (index + 1) % pattern_count;
                remaining = path->dashes[index % count];
            }
            remaining -= skip;
            if (s->move)
                continue;
        }
        double length = hypot(s->x1 - s->x0, s->y1 - s->y0), at = 0;
        while (at < length) {
            if (out->length >= TBOX_RASTER_PATH_MAX_POINTS / 2)
                return false;
            for (size_t k = 0; remaining <= 0 && k < pattern_count; k++) {
                index     = (index + 1) % pattern_count;
                remaining = path->dashes[index % count];
                pen       = false;
            }
            if (!(remaining > 0))
                return false;
            double step  = fmin(remaining, length - at);
            bool painted = index % 2 == 0;
            if (painted) {
                tbox_path_point a = { s->x0 + (s->x1 - s->x0) * at / length, s->y0 + (s->y1 - s->y0) * at / length };
                tbox_path_point b = { s->x0 + (s->x1 - s->x0) * (at + step) / length, s->y0 + (s->y1 - s->y0) * (at + step) / length };
                if (!pen && !tbox_path_segment_add(out, a, a, (tbox_path_point){ 1, 0 }, (tbox_path_point){ 1, 0 }, true))
                    return false;
                if (!tbox_path_line(out, a, b))
                    return false;
                pen = true;
            } else
                pen = false;
            at += step;
            remaining -= step;
            if (remaining <= 1e-10) {
                index     = (index + 1) % pattern_count;
                remaining = path->dashes[index % count];
                pen       = false;
            }
        }
    }
    return true;
}
static bool tbox_path_stroke_outline(FT_Library library, const tbox_paint_path *path, FT_Outline *outline) {
    if (!(path->width > 0) || path->width > 1048576 || path->miterlimit > 1048576)
        return false;
    FT_Stroker stroker;
    if (FT_Stroker_New(library, &stroker) != 0)
        return false;
    FT_Stroker_LineCap cap   = path->linecap == 1 ? FT_STROKER_LINECAP_ROUND : path->linecap == 2 ? FT_STROKER_LINECAP_SQUARE : FT_STROKER_LINECAP_BUTT;
    FT_Stroker_LineJoin join = path->linejoin == 1 ? FT_STROKER_LINEJOIN_ROUND : path->linejoin == 2 ? FT_STROKER_LINEJOIN_BEVEL : FT_STROKER_LINEJOIN_MITER_FIXED;
    FT_Stroker_Set(stroker, (FT_Fixed)llround(path->width * 32), cap, join, (FT_Fixed)llround(fmax(1, path->miterlimit) * 65536));
    tbox_arena scratch = tbox_arena_create(0);
    tbox_vector dashed;
    tbox_vector_init(&dashed, &scratch, sizeof(tbox_path_segment), 0);
    tbox_path_segment *original = tbox_arena_alloc_zero(&scratch, path->count * sizeof(*original));
    if (original == NULL) {
        tbox_arena_destroy(&scratch);
        FT_Stroker_Done(stroker);
        return false;
    }
    for (size_t i = 0; i < path->count; i++) {
        original[i].x0    = path->segments[i].x0;
        original[i].y0    = path->segments[i].y0;
        original[i].x1    = path->segments[i].x1;
        original[i].y1    = path->segments[i].y1;
        original[i].move  = path->segments[i].move;
        original[i].close = path->segments[i].close;
    }
    const tbox_path_segment *s = original;
    size_t count               = path->count;
    double period              = 0;
    for (size_t i = 0; i < path->dash_count; i++)
        period += path->dashes[i];
    bool ok = true;
    if (path->dash_count > 0 && period > 0) {
        ok    = tbox_path_dash(path, &dashed);
        s     = dashed.data;
        count = dashed.length;
    }
    for (size_t i = 0; ok && i < count;) {
        size_t end = i + 1;
        while (end < count && !s[end].move)
            end++;
        bool closed = s[end - 1].close;
        FT_Vector p;
        ok = tbox_path_ft_point(s[i].move ? s[i].x1 : s[i].x0, s[i].move ? s[i].y1 : s[i].y0, &p) && FT_Stroker_BeginSubPath(stroker, &p, !closed) == 0;
        for (size_t j = i; ok && j < end; j++) {
            if (s[j].move)
                continue;
            ok = tbox_path_ft_point(s[j].x1, s[j].y1, &p) && FT_Stroker_LineTo(stroker, &p) == 0;
        }
        if (ok)
            ok = FT_Stroker_EndSubPath(stroker) == 0;
        i = end;
    }
    FT_UInt points = 0, contours = 0;
    if (ok)
        ok = FT_Stroker_GetCounts(stroker, &points, &contours) == 0 && points > 0 && points <= TBOX_RASTER_PATH_MAX_POINTS;
    if (ok)
        ok = FT_Outline_New(library, points, (FT_Int)contours, outline) == 0;
    if (ok) {
        outline->n_points   = 0;
        outline->n_contours = 0;
        FT_Stroker_Export(stroker, outline);
    }
    tbox_arena_destroy(&scratch);
    FT_Stroker_Done(stroker);
    return ok;
}
typedef struct tbox_path_span_target {
    tbox_raster_path_span_fn span;
    void *userdata;
} tbox_path_span_target;
static void tbox_path_spans(int y, int count, const FT_Span *spans, void *userdata) {
    const tbox_path_span_target *t = userdata;
    for (int i = 0; i < count; i++)
        t->span(t->userdata, spans[i].x, -y - 1, spans[i].len, spans[i].coverage);
}
void tbox_raster_path_paint(tbox_raster_path_state *state, const tbox_paint_path *path, tbox_rect clip, tbox_raster_path_span_fn span, void *userdata) {
    if (state == NULL || path == NULL || span == NULL || state->failed || path->segments == NULL || path->dash_count > TBOX_STYLE_MAX_SVG_DASHES || path->count == 0 || path->count > TBOX_RASTER_PATH_MAX_POINTS)
        return;
    if (state->library == NULL) {
        FT_Library library;
        if (FT_Init_FreeType(&library) != 0) {
            state->failed = true;
            return;
        }
        state->library = library;
    }
    FT_Library library = state->library;
    FT_Outline outline = { 0 };
    bool ok            = path->stroke ? tbox_path_stroke_outline(library, path, &outline) : tbox_path_fill_outline(library, path->segments, path->count, &outline);
    if (!ok)
        return;
    for (int i = 0; i < outline.n_points; i++) {
        double x = outline.points[i].x / 64.0, y = outline.points[i].y / 64.0;
        if (!tbox_path_ft_point(path->matrix[0] * x + path->matrix[2] * y + path->matrix[4], -(path->matrix[1] * x + path->matrix[3] * y + path->matrix[5]), &outline.points[i])) {
            ok = false;
            break;
        }
    }
    if (!path->stroke && path->evenodd)
        outline.flags |= FT_OUTLINE_EVEN_ODD_FILL;
    if (ok) {
        tbox_path_span_target target = { span, userdata };
        FT_Raster_Params params      = { 0 };
        params.flags                 = FT_RASTER_FLAG_AA | FT_RASTER_FLAG_DIRECT | FT_RASTER_FLAG_CLIP;
        params.gray_spans            = tbox_path_spans;
        params.user                  = &target;
        params.clip_box              = (FT_BBox){ (FT_Pos)floor(clip.x), (FT_Pos)floor(-clip.y - clip.height), (FT_Pos)ceil(clip.x + clip.width), (FT_Pos)ceil(-clip.y) };
        FT_Outline_Render(library, &outline, &params);
    }
    FT_Outline_Done(library, &outline);
}
void tbox_raster_path_destroy(tbox_raster_path_state *state) {
    if (state != NULL && state->library != NULL)
        FT_Done_FreeType(state->library);
    if (state != NULL)
        *state = (tbox_raster_path_state){ 0 };
}
