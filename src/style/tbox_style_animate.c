/* Animatable properties: the names `transition-property` and @keyframes
 * accept, and reading/writing each one as 4 doubles so the Orchestration
 * layer can interpolate them without knowing tbox_style's layout. */

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include <tbox/style.h>

#include "base/tbox_string.h"
#include "style/tbox_style_internal.h"

#define BIT(p) (1ull << (p))
#define SIDES(first) (BIT(first) | BIT((first) + 1) | BIT((first) + 2) | BIT((first) + 3))
#define ALL_BITS (~0ull >> (64 - TBOX_STYLE_ANIM_COUNT))
#define TRANSFORM_BITS (((1ull << TBOX_STYLE_MAX_TRANSFORMS) - 1ull) << TBOX_STYLE_ANIM_TRANSFORM_0)

static_assert(TBOX_STYLE_ANIM_COUNT <= 64, "animatable masks are 64-bit");
static_assert(TBOX_STYLE_ANIM_TRANSFORM_LAST - TBOX_STYLE_ANIM_TRANSFORM_0 + 1 == TBOX_STYLE_MAX_TRANSFORMS, "one slot per transform function");

typedef struct tbox_style_animatable_name {
    const char *name;
    unsigned long long mask;
} tbox_style_animatable_name;

static const tbox_style_animatable_name tbox_style_animatable_names[] = {
    { "-webkit-text-stroke", BIT(TBOX_STYLE_ANIM_TEXT_STROKE_WIDTH) | BIT(TBOX_STYLE_ANIM_TEXT_STROKE_COLOR) },
    { "-webkit-text-stroke-width", BIT(TBOX_STYLE_ANIM_TEXT_STROKE_WIDTH) },
    { "-webkit-text-stroke-color", BIT(TBOX_STYLE_ANIM_TEXT_STROKE_COLOR) },
    { "all",                        ALL_BITS                                                                            },
    { "color",                      BIT(TBOX_STYLE_ANIM_COLOR)                                                          },
    { "background-color",           BIT(TBOX_STYLE_ANIM_BACKGROUND_COLOR)                                               },
    { "background",                 BIT(TBOX_STYLE_ANIM_BACKGROUND_COLOR)                                               },
    { "opacity",                    BIT(TBOX_STYLE_ANIM_OPACITY)                                                        },
    { "border-color",               SIDES(TBOX_STYLE_ANIM_BORDER_TOP_COLOR)                                             },
    { "border-top-color",           BIT(TBOX_STYLE_ANIM_BORDER_TOP_COLOR)                                               },
    { "border-right-color",         BIT(TBOX_STYLE_ANIM_BORDER_RIGHT_COLOR)                                             },
    { "border-bottom-color",        BIT(TBOX_STYLE_ANIM_BORDER_BOTTOM_COLOR)                                            },
    { "border-left-color",          BIT(TBOX_STYLE_ANIM_BORDER_LEFT_COLOR)                                              },
    { "border-width",               SIDES(TBOX_STYLE_ANIM_BORDER_TOP_WIDTH)                                             },
    { "border-top-width",           BIT(TBOX_STYLE_ANIM_BORDER_TOP_WIDTH)                                               },
    { "border-right-width",         BIT(TBOX_STYLE_ANIM_BORDER_RIGHT_WIDTH)                                             },
    { "border-bottom-width",        BIT(TBOX_STYLE_ANIM_BORDER_BOTTOM_WIDTH)                                            },
    { "border-left-width",          BIT(TBOX_STYLE_ANIM_BORDER_LEFT_WIDTH)                                              },
    { "border",                     SIDES(TBOX_STYLE_ANIM_BORDER_TOP_COLOR) | SIDES(TBOX_STYLE_ANIM_BORDER_TOP_WIDTH)   },
    { "border-top",                 BIT(TBOX_STYLE_ANIM_BORDER_TOP_COLOR) | BIT(TBOX_STYLE_ANIM_BORDER_TOP_WIDTH)       },
    { "border-right",               BIT(TBOX_STYLE_ANIM_BORDER_RIGHT_COLOR) | BIT(TBOX_STYLE_ANIM_BORDER_RIGHT_WIDTH)   },
    { "border-bottom",              BIT(TBOX_STYLE_ANIM_BORDER_BOTTOM_COLOR) | BIT(TBOX_STYLE_ANIM_BORDER_BOTTOM_WIDTH) },
    { "border-left",                BIT(TBOX_STYLE_ANIM_BORDER_LEFT_COLOR) | BIT(TBOX_STYLE_ANIM_BORDER_LEFT_WIDTH)     },
    { "border-radius",              SIDES(TBOX_STYLE_ANIM_BORDER_TOP_LEFT_RADIUS)                                       },
    { "border-top-left-radius",     BIT(TBOX_STYLE_ANIM_BORDER_TOP_LEFT_RADIUS)                                         },
    { "border-top-right-radius",    BIT(TBOX_STYLE_ANIM_BORDER_TOP_RIGHT_RADIUS)                                        },
    { "border-bottom-right-radius", BIT(TBOX_STYLE_ANIM_BORDER_BOTTOM_RIGHT_RADIUS)                                     },
    { "border-bottom-left-radius",  BIT(TBOX_STYLE_ANIM_BORDER_BOTTOM_LEFT_RADIUS)                                      },
    { "outline",                    BIT(TBOX_STYLE_ANIM_OUTLINE_COLOR) | BIT(TBOX_STYLE_ANIM_OUTLINE_WIDTH)             },
    { "outline-color",              BIT(TBOX_STYLE_ANIM_OUTLINE_COLOR)                                                  },
    { "outline-width",              BIT(TBOX_STYLE_ANIM_OUTLINE_WIDTH)                                                  },
    { "outline-offset",             BIT(TBOX_STYLE_ANIM_OUTLINE_OFFSET)                                                 },
    { "text-decoration",            BIT(TBOX_STYLE_ANIM_TEXT_DECORATION_COLOR)                                          },
    { "text-decoration-color",      BIT(TBOX_STYLE_ANIM_TEXT_DECORATION_COLOR)                                          },
    { "accent-color",               BIT(TBOX_STYLE_ANIM_ACCENT_COLOR)                                                   },
    { "caret-color",                BIT(TBOX_STYLE_ANIM_CARET_COLOR)                                                    },
    { "translate",                  BIT(TBOX_STYLE_ANIM_TRANSLATE_X) | BIT(TBOX_STYLE_ANIM_TRANSLATE_Y)                 },
    { "transform",                  TRANSFORM_BITS                                                                      },
    { "offset-distance", BIT(TBOX_STYLE_ANIM_OFFSET_DISTANCE) },
    { "offset-rotate", BIT(TBOX_STYLE_ANIM_OFFSET_ROTATE) },
    { "offset-anchor", (BIT(TBOX_STYLE_ANIM_OFFSET_ANCHOR_X) | BIT(TBOX_STYLE_ANIM_OFFSET_ANCHOR_Y)) },
    { "offset-position", (BIT(TBOX_STYLE_ANIM_OFFSET_POSITION_X) | BIT(TBOX_STYLE_ANIM_OFFSET_POSITION_Y)) },
    { "offset", BIT(TBOX_STYLE_ANIM_OFFSET_DISTANCE) | BIT(TBOX_STYLE_ANIM_OFFSET_ROTATE) | (BIT(TBOX_STYLE_ANIM_OFFSET_ANCHOR_X) | BIT(TBOX_STYLE_ANIM_OFFSET_ANCHOR_Y)) | (BIT(TBOX_STYLE_ANIM_OFFSET_POSITION_X) | BIT(TBOX_STYLE_ANIM_OFFSET_POSITION_Y)) },
    { "rotate",                     BIT(TBOX_STYLE_ANIM_ROTATE)                                                         },
    { "scale",                      BIT(TBOX_STYLE_ANIM_SCALE)                                                          },
    { "transform-origin",           BIT(TBOX_STYLE_ANIM_TRANSFORM_ORIGIN_X) | BIT(TBOX_STYLE_ANIM_TRANSFORM_ORIGIN_Y)   },
    { "width",                      BIT(TBOX_STYLE_ANIM_WIDTH)                                                          },
    { "inline-size",                BIT(TBOX_STYLE_ANIM_WIDTH)                                                          },
    { "height",                     BIT(TBOX_STYLE_ANIM_HEIGHT)                                                         },
    { "block-size",                 BIT(TBOX_STYLE_ANIM_HEIGHT)                                                         },
    { "padding",                    SIDES(TBOX_STYLE_ANIM_PADDING_TOP)                                                  },
    { "padding-top",                BIT(TBOX_STYLE_ANIM_PADDING_TOP)                                                    },
    { "padding-right",              BIT(TBOX_STYLE_ANIM_PADDING_RIGHT)                                                  },
    { "padding-bottom",             BIT(TBOX_STYLE_ANIM_PADDING_BOTTOM)                                                 },
    { "padding-left",               BIT(TBOX_STYLE_ANIM_PADDING_LEFT)                                                   },
    { "margin",                     SIDES(TBOX_STYLE_ANIM_MARGIN_TOP)                                                   },
    { "margin-top",                 BIT(TBOX_STYLE_ANIM_MARGIN_TOP)                                                     },
    { "margin-right",               BIT(TBOX_STYLE_ANIM_MARGIN_RIGHT)                                                   },
    { "margin-bottom",              BIT(TBOX_STYLE_ANIM_MARGIN_BOTTOM)                                                  },
    { "margin-left",                BIT(TBOX_STYLE_ANIM_MARGIN_LEFT)                                                    },
    { "inset",                      SIDES(TBOX_STYLE_ANIM_TOP)                                                          },
    { "top",                        BIT(TBOX_STYLE_ANIM_TOP)                                                            },
    { "right",                      BIT(TBOX_STYLE_ANIM_RIGHT)                                                          },
    { "bottom",                     BIT(TBOX_STYLE_ANIM_BOTTOM)                                                         },
    { "left",                       BIT(TBOX_STYLE_ANIM_LEFT)                                                           },
    { "letter-spacing",             BIT(TBOX_STYLE_ANIM_LETTER_SPACING)                                                 },
    { "column-count", BIT(TBOX_STYLE_ANIM_COLUMN_COUNT) },
    { "column-width", BIT(TBOX_STYLE_ANIM_COLUMN_WIDTH) },
    { "columns", BIT(TBOX_STYLE_ANIM_COLUMN_COUNT) | BIT(TBOX_STYLE_ANIM_COLUMN_WIDTH) },
    { "column-gap", BIT(TBOX_STYLE_ANIM_COLUMN_GAP) },
    { "column-rule-width", BIT(TBOX_STYLE_ANIM_COLUMN_RULE_WIDTH) },
    { "column-rule-color", BIT(TBOX_STYLE_ANIM_COLUMN_RULE_COLOR) },
    { "column-rule", BIT(TBOX_STYLE_ANIM_COLUMN_RULE_WIDTH) | BIT(TBOX_STYLE_ANIM_COLUMN_RULE_COLOR) },
    { "word-spacing",               BIT(TBOX_STYLE_ANIM_WORD_SPACING)                                                   },
};

unsigned long long tbox_style_animatable_mask(tbox_string_view name) {
    for (size_t i = 0; i < sizeof(tbox_style_animatable_names) / sizeof(tbox_style_animatable_names[0]); i++)
        if (tbox_string_view_equal_ascii_ci(name, tbox_string_view_from_cstr(tbox_style_animatable_names[i].name)))
            return tbox_style_animatable_names[i].mask;
    return 0;
}

typedef enum {
    KIND_INTEGER,
    KIND_COLOR,
    KIND_AUTO_COLOR,
    KIND_LENGTH,
    KIND_NUMBER,
    KIND_BORDER_COLOR,
    KIND_BORDER_WIDTH,
    KIND_RADIUS,
    KIND_OFFSET_ROTATE,
    KIND_OFFSET_POSITION,
    KIND_ROTATE,
    KIND_SCALE,
    KIND_TRANSFORM,
} tbox_style_animatable_kind;

static tbox_style_animatable_kind tbox_style_animatable_kind_of(tbox_style_animatable p) {
    if (p >= TBOX_STYLE_ANIM_TRANSFORM_0 && p <= TBOX_STYLE_ANIM_TRANSFORM_LAST)
        return KIND_TRANSFORM;
    switch (p) {
    case TBOX_STYLE_ANIM_COLUMN_COUNT:
        return KIND_INTEGER;
    case TBOX_STYLE_ANIM_COLUMN_RULE_COLOR:
    case TBOX_STYLE_ANIM_COLOR:
    case TBOX_STYLE_ANIM_BACKGROUND_COLOR:
    case TBOX_STYLE_ANIM_OUTLINE_COLOR:
    case TBOX_STYLE_ANIM_TEXT_DECORATION_COLOR:
    case TBOX_STYLE_ANIM_TEXT_STROKE_COLOR:
        return KIND_COLOR;
    case TBOX_STYLE_ANIM_ACCENT_COLOR:
    case TBOX_STYLE_ANIM_CARET_COLOR:
        return KIND_AUTO_COLOR;
    case TBOX_STYLE_ANIM_BORDER_TOP_COLOR:
    case TBOX_STYLE_ANIM_BORDER_RIGHT_COLOR:
    case TBOX_STYLE_ANIM_BORDER_BOTTOM_COLOR:
    case TBOX_STYLE_ANIM_BORDER_LEFT_COLOR:
        return KIND_BORDER_COLOR;
    case TBOX_STYLE_ANIM_BORDER_TOP_WIDTH:
    case TBOX_STYLE_ANIM_BORDER_RIGHT_WIDTH:
    case TBOX_STYLE_ANIM_BORDER_BOTTOM_WIDTH:
    case TBOX_STYLE_ANIM_BORDER_LEFT_WIDTH:
        return KIND_BORDER_WIDTH;
    case TBOX_STYLE_ANIM_BORDER_TOP_LEFT_RADIUS:
    case TBOX_STYLE_ANIM_BORDER_TOP_RIGHT_RADIUS:
    case TBOX_STYLE_ANIM_BORDER_BOTTOM_RIGHT_RADIUS:
    case TBOX_STYLE_ANIM_BORDER_BOTTOM_LEFT_RADIUS:
        return KIND_RADIUS;
    case TBOX_STYLE_ANIM_COLUMN_RULE_WIDTH:
    case TBOX_STYLE_ANIM_OPACITY:
    case TBOX_STYLE_ANIM_OUTLINE_WIDTH:
    case TBOX_STYLE_ANIM_OUTLINE_OFFSET:
    case TBOX_STYLE_ANIM_LETTER_SPACING:
    case TBOX_STYLE_ANIM_WORD_SPACING:
    case TBOX_STYLE_ANIM_TEXT_STROKE_WIDTH:
        return KIND_NUMBER;
    case TBOX_STYLE_ANIM_OFFSET_ROTATE:
        return KIND_OFFSET_ROTATE;
    case TBOX_STYLE_ANIM_OFFSET_ANCHOR_X:
    case TBOX_STYLE_ANIM_OFFSET_ANCHOR_Y:
    case TBOX_STYLE_ANIM_OFFSET_POSITION_X:
    case TBOX_STYLE_ANIM_OFFSET_POSITION_Y:
        return KIND_OFFSET_POSITION;
    case TBOX_STYLE_ANIM_OFFSET_DISTANCE:
        return KIND_LENGTH;
    case TBOX_STYLE_ANIM_ROTATE:
        return KIND_ROTATE;
    case TBOX_STYLE_ANIM_SCALE:
        return KIND_SCALE;
    case TBOX_STYLE_ANIM_TRANSFORM_0:
    case TBOX_STYLE_ANIM_TRANSFORM_LAST:
        return KIND_TRANSFORM;
    case TBOX_STYLE_ANIM_TRANSFORM_ORIGIN_X:
    case TBOX_STYLE_ANIM_TRANSFORM_ORIGIN_Y:
    case TBOX_STYLE_ANIM_COLUMN_WIDTH:
    case TBOX_STYLE_ANIM_COLUMN_GAP:
    case TBOX_STYLE_ANIM_TRANSLATE_X:
    case TBOX_STYLE_ANIM_TRANSLATE_Y:
    case TBOX_STYLE_ANIM_WIDTH:
    case TBOX_STYLE_ANIM_HEIGHT:
    case TBOX_STYLE_ANIM_PADDING_TOP:
    case TBOX_STYLE_ANIM_PADDING_RIGHT:
    case TBOX_STYLE_ANIM_PADDING_BOTTOM:
    case TBOX_STYLE_ANIM_PADDING_LEFT:
    case TBOX_STYLE_ANIM_MARGIN_TOP:
    case TBOX_STYLE_ANIM_MARGIN_RIGHT:
    case TBOX_STYLE_ANIM_MARGIN_BOTTOM:
    case TBOX_STYLE_ANIM_MARGIN_LEFT:
    case TBOX_STYLE_ANIM_TOP:
    case TBOX_STYLE_ANIM_RIGHT:
    case TBOX_STYLE_ANIM_BOTTOM:
    case TBOX_STYLE_ANIM_LEFT:
    case TBOX_STYLE_ANIM_COUNT:
        break;
    }
    /* No default above: -Wswitch flags a new property missing its kind. */
    return KIND_LENGTH;
}

static tbox_css_rgba *tbox_style_animatable_color(tbox_style *style, tbox_style_animatable p) {
    switch (p) {
    case TBOX_STYLE_ANIM_COLUMN_RULE_COLOR:
        return &style->column_rule_color;
    case TBOX_STYLE_ANIM_COLOR:
        return &style->color;
    case TBOX_STYLE_ANIM_BACKGROUND_COLOR:
        return &style->background_color;
    case TBOX_STYLE_ANIM_OUTLINE_COLOR:
        return &style->outline_color;
    case TBOX_STYLE_ANIM_TEXT_DECORATION_COLOR:
        return &style->text_decoration_color;
    case TBOX_STYLE_ANIM_ACCENT_COLOR:
        return &style->accent_color;
    case TBOX_STYLE_ANIM_TEXT_STROKE_COLOR:
        return &style->text_stroke_color;
    default:
        return &style->caret_color;
    }
}

static double *tbox_style_animatable_number(tbox_style *style, tbox_style_animatable p) {
    switch (p) {
    case TBOX_STYLE_ANIM_COLUMN_RULE_WIDTH:
        return &style->column_rule_width;
    case TBOX_STYLE_ANIM_OPACITY:
        return &style->opacity;
    case TBOX_STYLE_ANIM_OUTLINE_WIDTH:
        return &style->outline_width;
    case TBOX_STYLE_ANIM_OUTLINE_OFFSET:
        return &style->outline_offset;
    case TBOX_STYLE_ANIM_LETTER_SPACING:
        return &style->letter_spacing;
    case TBOX_STYLE_ANIM_TEXT_STROKE_WIDTH:
        return &style->text_stroke_width;
    default:
        return &style->word_spacing;
    }
}

static tbox_style_length *tbox_style_animatable_length(tbox_style *style, tbox_style_animatable p) {
    if (p == TBOX_STYLE_ANIM_COLUMN_WIDTH) return &style->column_width;
    if (p == TBOX_STYLE_ANIM_COLUMN_GAP) return &style->column_gap;
    if (p == TBOX_STYLE_ANIM_TRANSLATE_X)
        return &style->translate_x;
    if (p == TBOX_STYLE_ANIM_OFFSET_DISTANCE)
        return &style->offset_distance;
    if (p == TBOX_STYLE_ANIM_TRANSLATE_Y)
        return &style->translate_y;
    if (p == TBOX_STYLE_ANIM_WIDTH)
        return &style->width;
    if (p == TBOX_STYLE_ANIM_HEIGHT)
        return &style->height;
    if (p == TBOX_STYLE_ANIM_TRANSFORM_ORIGIN_X || p == TBOX_STYLE_ANIM_TRANSFORM_ORIGIN_Y)
        return &style->transform_origin[p - TBOX_STYLE_ANIM_TRANSFORM_ORIGIN_X];
    if (p >= TBOX_STYLE_ANIM_PADDING_TOP && p <= TBOX_STYLE_ANIM_PADDING_LEFT)
        return &style->padding[p - TBOX_STYLE_ANIM_PADDING_TOP];
    if (p >= TBOX_STYLE_ANIM_MARGIN_TOP && p <= TBOX_STYLE_ANIM_MARGIN_LEFT)
        return &style->margin[p - TBOX_STYLE_ANIM_MARGIN_TOP];
    if (p >= TBOX_STYLE_ANIM_TOP && p <= TBOX_STYLE_ANIM_LEFT)
        return &style->offset[p - TBOX_STYLE_ANIM_TOP];
    return NULL;
}

static void tbox_style_animatable_color_read(tbox_css_rgba color, double out[4]) {
    double a = color.a / 255.0;
    out[0] = color.r * a, out[1] = color.g * a, out[2] = color.b * a, out[3] = a;
}

static double tbox_style_animatable_channel(double value) {
    return value < 0.0 ? 0.0 : value > 255.0 ? 255.0 : value;
}

static tbox_css_rgba tbox_style_animatable_color_value(const double v[4]) {
    double a            = v[3] < 0.0 ? 0.0 : v[3] > 1.0 ? 1.0 : v[3];
    tbox_css_rgba color = { 0, 0, 0, 0 };
    if (a > 0.0) {
        color.r = (unsigned char)lround(tbox_style_animatable_channel(v[0] / a));
        color.g = (unsigned char)lround(tbox_style_animatable_channel(v[1] / a));
        color.b = (unsigned char)lround(tbox_style_animatable_channel(v[2] / a));
    }
    color.a = (unsigned char)lround(a * 255.0);
    return color;
}

/* Border sides and corners are written one at a time: make the per-side
 * arrays authoritative first, so the other sides keep their values. */
static void tbox_style_animatable_split_borders(tbox_style *style) {
    if (style->border_per_side)
        return;
    for (size_t s = 0; s < 4; s++) {
        style->border_widths[s] = style->border_width;
        style->border_styles[s] = style->border_style;
        style->border_colors[s] = style->border_color;
    }
    style->border_per_side = true;
}

/* The identity function of `kind`, as a transform slot's value. */
static void tbox_style_animatable_identity(double kind, double out[TBOX_STYLE_ANIM_VALUE_SIZE]) {
    for (int i = 0; i < TBOX_STYLE_ANIM_VALUE_SIZE; i++)
        out[i] = 0.0;
    out[0] = kind;
    tbox_style_transform_kind k = (tbox_style_transform_kind)lround(kind);
    if (k == TBOX_STYLE_TRANSFORM_SCALE)
        out[1] = out[2] = 1.0;
    else if (k == TBOX_STYLE_TRANSFORM_MATRIX)
        out[1] = out[4] = 1.0;
}

void tbox_style_animatable_read(const tbox_style *style, tbox_style_animatable property, double out[TBOX_STYLE_ANIM_VALUE_SIZE]) {
    for (int i = 0; i < TBOX_STYLE_ANIM_VALUE_SIZE; i++)
        out[i] = 0.0;
    if (style == NULL || property >= TBOX_STYLE_ANIM_COUNT)
        return;
    tbox_style *s = (tbox_style *)style; /* the accessors below only read through it */
    switch (tbox_style_animatable_kind_of(property)) {
    case KIND_INTEGER:
        out[0] = style->column_count;
        break;
    case KIND_COLOR:
    case KIND_AUTO_COLOR:
        tbox_style_animatable_color_read(property == TBOX_STYLE_ANIM_TEXT_STROKE_COLOR && style->text_stroke_current_color ? style->color : *tbox_style_animatable_color(s, property), out);
        break;
    case KIND_BORDER_COLOR:
        tbox_style_animatable_color_read(tbox_style_border_side_color(style, (size_t)(property - TBOX_STYLE_ANIM_BORDER_TOP_COLOR)), out);
        break;
    case KIND_BORDER_WIDTH: {
        size_t side = (size_t)(property - TBOX_STYLE_ANIM_BORDER_TOP_WIDTH);
        out[0]      = style->border_per_side ? style->border_widths[side] : style->border_width;
        break;
    }
    case KIND_RADIUS: {
        size_t corner = (size_t)(property - TBOX_STYLE_ANIM_BORDER_TOP_LEFT_RADIUS);
        out[0] = style->border_radius_corners[corner], out[1] = style->border_radius_vertical[corner];
        out[2] = style->border_radius_percent[corner], out[3] = style->border_radius_vertical_percent[corner];
        break;
    }
    case KIND_NUMBER:
        out[0] = *tbox_style_animatable_number(s, property);
        break;
    case KIND_OFFSET_ROTATE:
        out[0] = style->offset_rotate.angle;
        out[1] = style->offset_rotate.auto_rotate ? 1.0 : 0.0;
        break;
    case KIND_OFFSET_POSITION: {
        bool anchor = property <= TBOX_STYLE_ANIM_OFFSET_ANCHOR_Y;
        const tbox_style_offset_position *p = anchor ? &tbox_style_transforms_of(style)->offset_anchor : &tbox_style_transforms_of(style)->offset_position;
        size_t axis = (size_t)(property - (anchor ? TBOX_STYLE_ANIM_OFFSET_ANCHOR_X : TBOX_STYLE_ANIM_OFFSET_POSITION_X));
        const tbox_style_length *l = &p->position[axis];
        out[0] = p->mode;
        out[1] = l->kind == TBOX_STYLE_LENGTH_PERCENT ? l->value : 0.0;
        out[2] = l->kind == TBOX_STYLE_LENGTH_PX ? l->value : l->px_offset;
        out[3] = l->bounds, out[4] = l->clamp_min, out[5] = l->clamp_max;
        break;
    }
    case KIND_LENGTH: {
        if (property == TBOX_STYLE_ANIM_OFFSET_DISTANCE) {
            const tbox_style_length *l = &style->offset_distance;
            out[0] = l->kind == TBOX_STYLE_LENGTH_PERCENT ? l->value : 0.0;
            out[1] = l->kind == TBOX_STYLE_LENGTH_PX ? l->value : l->px_offset;
            out[2] = TBOX_STYLE_LENGTH_PERCENT;
            out[3] = l->bounds, out[4] = l->clamp_min, out[5] = l->clamp_max;
            break;
        }
        const tbox_style_length *length = tbox_style_animatable_length(s, property);
        if (length != NULL)
            out[0] = length->value, out[1] = length->px_offset, out[2] = (double)length->kind;
        break;
    }
    case KIND_ROTATE:
        out[0] = style->rotate_axis[0], out[1] = style->rotate_axis[1], out[2] = style->rotate_axis[2], out[3] = style->rotate_angle;
        if (out[0] == 0.0 && out[1] == 0.0 && out[2] == 0.0)
            out[2] = 1.0; /* none and plain angles turn around z */
        break;
    case KIND_SCALE:
        out[0] = style->has_scale ? style->scale[0] : 1.0;
        out[1] = style->has_scale ? style->scale[1] : 1.0;
        break;
    case KIND_TRANSFORM: {
        const tbox_style_transform_function *f = &tbox_style_transforms_of(style)->functions[property - TBOX_STYLE_ANIM_TRANSFORM_0];
        out[0] = (double)f->kind;
        for (int i = 0; i < 6; i++)
            out[1 + i] = f->v[i];
        break;
    }
    }
}

void tbox_style_animatable_write(tbox_style *style, tbox_style_animatable property, const double value[TBOX_STYLE_ANIM_VALUE_SIZE], tbox_arena *arena) {
    if (style == NULL || property >= TBOX_STYLE_ANIM_COUNT)
        return;
    switch (tbox_style_animatable_kind_of(property)) {
    case KIND_INTEGER:
        style->column_count = (int)round(fmax(0.0, fmin(INT_MAX, value[0])));
        break;
    case KIND_COLOR:
    case KIND_AUTO_COLOR:
        *tbox_style_animatable_color(style, property) = tbox_style_animatable_color_value(value);
        if (property == TBOX_STYLE_ANIM_TEXT_STROKE_COLOR) style->text_stroke_current_color = false;
        break;
    case KIND_BORDER_COLOR: {
        size_t side = (size_t)(property - TBOX_STYLE_ANIM_BORDER_TOP_COLOR);
        tbox_style_animatable_split_borders(style);
        style->border_colors[side] = tbox_style_animatable_color_value(value);
        if (side == 0)
            style->border_color = style->border_colors[0];
        break;
    }
    case KIND_BORDER_WIDTH: {
        size_t side = (size_t)(property - TBOX_STYLE_ANIM_BORDER_TOP_WIDTH);
        tbox_style_animatable_split_borders(style);
        style->border_widths[side] = value[0] > 0.0 ? value[0] : 0.0;
        if (side == 0)
            style->border_width = style->border_widths[0];
        break;
    }
    case KIND_RADIUS: {
        size_t corner                                 = (size_t)(property - TBOX_STYLE_ANIM_BORDER_TOP_LEFT_RADIUS);
        style->border_radius_corners[corner]          = value[0] > 0.0 ? value[0] : 0.0;
        style->border_radius_vertical[corner]         = value[1] > 0.0 ? value[1] : 0.0;
        style->border_radius_percent[corner]          = value[2] > 0.0 ? value[2] : 0.0;
        style->border_radius_vertical_percent[corner] = value[3] > 0.0 ? value[3] : 0.0;
        if (corner == 0)
            style->border_radius = style->border_radius_corners[0];
        break;
    }
    case KIND_NUMBER: {
        double v = value[0];
        if (property == TBOX_STYLE_ANIM_OPACITY)
            v = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
        if ((property == TBOX_STYLE_ANIM_OUTLINE_WIDTH || property == TBOX_STYLE_ANIM_COLUMN_RULE_WIDTH || property == TBOX_STYLE_ANIM_TEXT_STROKE_WIDTH) && v < 0.0)
            v = 0.0;
        *tbox_style_animatable_number(style, property) = v;
        break;
    }
    case KIND_OFFSET_ROTATE:
        style->offset_rotate.angle = value[0];
        style->offset_rotate.auto_rotate = value[1] != 0.0;
        break;
    case KIND_OFFSET_POSITION: {
        bool anchor = property <= TBOX_STYLE_ANIM_OFFSET_ANCHOR_Y;
        size_t offset = anchor ? offsetof(tbox_style_transforms, offset_anchor) : offsetof(tbox_style_transforms, offset_position);
        tbox_style_offset_position p = anchor ? tbox_style_transforms_of(style)->offset_anchor : tbox_style_transforms_of(style)->offset_position;
        size_t axis = (size_t)(property - (anchor ? TBOX_STYLE_ANIM_OFFSET_ANCHOR_X : TBOX_STYLE_ANIM_OFFSET_POSITION_X));
        p.mode = (unsigned char)lround(value[0]);
        p.position[axis] = (tbox_style_length){ .kind = TBOX_STYLE_LENGTH_PERCENT, .value = value[1], .px_offset = value[2],
            .bounds = (unsigned char)lround(value[3]), .clamp_min = value[4], .clamp_max = value[5] };
        tbox_style_group_set(style, TBOX_STYLE_GROUP_TRANSFORMS, arena, offset, &p, sizeof(p));
        break;
    }
    case KIND_LENGTH: {
        tbox_style_length *length = tbox_style_animatable_length(style, property);
        if (length == NULL)
            break;
        bool non_negative         = property == TBOX_STYLE_ANIM_COLUMN_WIDTH || property == TBOX_STYLE_ANIM_COLUMN_GAP || property == TBOX_STYLE_ANIM_WIDTH || property == TBOX_STYLE_ANIM_HEIGHT || (property >= TBOX_STYLE_ANIM_PADDING_TOP && property <= TBOX_STYLE_ANIM_PADDING_LEFT);
        length->kind              = (tbox_style_length_kind)lround(value[2]);
        length->value             = non_negative && value[0] < 0.0 ? 0.0 : value[0];
        length->px_offset         = value[1];
        if (property == TBOX_STYLE_ANIM_OFFSET_DISTANCE) {
            length->bounds = (unsigned char)lround(value[3]);
            length->clamp_min = value[4], length->clamp_max = value[5];
        }
        break;
    }
    case KIND_ROTATE:
        style->rotate_axis[0] = value[0], style->rotate_axis[1] = value[1], style->rotate_axis[2] = value[2];
        style->rotate_angle = value[3];
        break;
    case KIND_SCALE:
        style->has_scale = true;
        style->scale[0] = value[0], style->scale[1] = value[1];
        break;
    case KIND_TRANSFORM: {
        size_t slot = (size_t)(property - TBOX_STYLE_ANIM_TRANSFORM_0);
        tbox_style_transform_function f = { 0 };
        long kind = lround(value[0]);
        f.kind = kind > TBOX_STYLE_TRANSFORM_NONE && kind <= TBOX_STYLE_TRANSFORM_MATRIX ? (tbox_style_transform_kind)kind : TBOX_STYLE_TRANSFORM_NONE;
        for (int i = 0; i < 6; i++)
            f.v[i] = value[1 + i];
        tbox_style_group_set(style, TBOX_STYLE_GROUP_TRANSFORMS, arena, offsetof(tbox_style_transforms, functions) + slot * sizeof(f), &f, sizeof(f));
        break;
    }
    }
}

bool tbox_style_animatable_interpolable(tbox_style_animatable property, const double from[TBOX_STYLE_ANIM_VALUE_SIZE], const double to[TBOX_STYLE_ANIM_VALUE_SIZE]) {
    /* Stroke width is discrete. Keyframes already
     * switch non-interpolable values at 50%; ordinary transitions skip it. */
    if (property == TBOX_STYLE_ANIM_TEXT_STROKE_WIDTH) return false;
    switch (tbox_style_animatable_kind_of(property)) {
    case KIND_INTEGER:
        return from[0] > 0.0 && to[0] > 0.0;
    case KIND_AUTO_COLOR:
        return from[3] > 0.0 && to[3] > 0.0;
    case KIND_OFFSET_ROTATE:
        return from[1] == to[1];
    case KIND_OFFSET_POSITION:
        return from[0] == 0.0 && to[0] == 0.0 && from[3] == 0.0 && to[3] == 0.0;
    case KIND_LENGTH:
        if (property == TBOX_STYLE_ANIM_OFFSET_DISTANCE && (from[3] != 0.0 || to[3] != 0.0)) return false;
        return from[2] == to[2] && lround(from[2]) != TBOX_STYLE_LENGTH_AUTO;
    case KIND_ROTATE: {
        /* the same axis (as a direction), or one side not turning */
        if (from[3] == 0.0 || to[3] == 0.0)
            return true;
        double a = sqrt(from[0] * from[0] + from[1] * from[1] + from[2] * from[2]);
        double b = sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        return a > 0.0 && b > 0.0 && fabs((from[0] * to[0] + from[1] * to[1] + from[2] * to[2]) / (a * b) - 1.0) < 1e-9;
    }
    case KIND_TRANSFORM:
        /* function by function: the same primitive, or an empty slot that
         * stands for the other's identity (CSS Transforms 1 §9) */
        return from[0] == to[0] || lround(from[0]) == TBOX_STYLE_TRANSFORM_NONE || lround(to[0]) == TBOX_STYLE_TRANSFORM_NONE;
    default:
        return true;
    }
}

void tbox_style_animatable_mix(tbox_style_animatable property, const double from[TBOX_STYLE_ANIM_VALUE_SIZE], const double to[TBOX_STYLE_ANIM_VALUE_SIZE], double t, double out[TBOX_STYLE_ANIM_VALUE_SIZE]) {
    if (property == TBOX_STYLE_ANIM_TEXT_STROKE_WIDTH) {
        memcpy(out, t < 0.5 ? from : to, sizeof(double) * TBOX_STYLE_ANIM_VALUE_SIZE);
        return;
    }
    double a[TBOX_STYLE_ANIM_VALUE_SIZE], b[TBOX_STYLE_ANIM_VALUE_SIZE];
    memcpy(a, from, sizeof(a));
    memcpy(b, to, sizeof(b));
    tbox_style_animatable_kind kind = tbox_style_animatable_kind_of(property);
    if (kind == KIND_TRANSFORM && from[0] != to[0]) {
        if (lround(from[0]) == TBOX_STYLE_TRANSFORM_NONE)
            tbox_style_animatable_identity(to[0], a);
        else if (lround(to[0]) == TBOX_STYLE_TRANSFORM_NONE)
            tbox_style_animatable_identity(from[0], b);
    } else if (kind == KIND_ROTATE && (from[3] == 0.0 || to[3] == 0.0)) {
        /* a zero angle takes the other side's axis */
        const double *axis = from[3] == 0.0 ? to : from;
        for (int i = 0; i < 3; i++)
            a[i] = b[i] = axis[i];
    }
    for (int i = 0; i < TBOX_STYLE_ANIM_VALUE_SIZE; i++)
        out[i] = a[i] + (b[i] - a[i]) * t;
}

tbox_style_easing tbox_style_easing_default(void) {
    tbox_style_easing easing = { false, 0.25, 0.1, 0.25, 1.0, 1, 0 };
    return easing;
}

static double tbox_style_bezier(double t, double a, double b) {
    double inv = 1.0 - t;
    return 3.0 * inv * inv * t * a + 3.0 * inv * t * t * b + t * t * t;
}

double tbox_style_easing_apply(const tbox_style_easing *easing, double progress) {
    if (easing == NULL)
        return progress;
    if (easing->steps) {
        /* CSS Easing 1 §2.3: steps counted over [0, 1], before clamping. */
        double n    = easing->step_count > 0 ? (double)easing->step_count : 1.0;
        double step = floor(progress * n);
        if (easing->step_position == 1 || easing->step_position == 3)
            step += 1.0;
        if (progress >= 0.0 && step < 0.0)
            step = 0.0;
        double jumps = easing->step_position == 2 ? n - 1.0 : easing->step_position == 3 ? n + 1.0 : n;
        if (jumps < 1.0)
            jumps = 1.0;
        if (progress <= 1.0 && step > jumps)
            step = jumps;
        return step / jumps;
    }
    if (progress <= 0.0 || progress >= 1.0)
        return progress;
    if (easing->x1 == easing->y1 && easing->x2 == easing->y2)
        return progress; /* linear */
    /* x(t) is monotonic for x1, x2 in [0, 1]: bisect for t, then y(t). */
    double low = 0.0, high = 1.0;
    for (int i = 0; i < 24; i++) {
        double mid = (low + high) * 0.5;
        if (tbox_style_bezier(mid, easing->x1, easing->x2) < progress)
            low = mid;
        else
            high = mid;
    }
    return tbox_style_bezier((low + high) * 0.5, easing->y1, easing->y2);
}

const tbox_style_transition *tbox_style_transition_find(const tbox_style *style, tbox_style_animatable property) {
    if (style == NULL || property >= TBOX_STYLE_ANIM_COUNT)
        return NULL;
    const tbox_style_animation_lists *lists = tbox_style_animation_lists_of(style);
    for (size_t i = lists->transition_count; i > 0; i--) {
        const tbox_style_transition *entry = &lists->transitions[i - 1];
        if (entry->properties & BIT(property))
            return entry->duration > 0.0 ? entry : NULL;
    }
    return NULL;
}
