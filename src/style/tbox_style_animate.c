/* Animatable properties: the names `transition-property` and @keyframes
 * accept, and reading/writing each one as 4 doubles so the Orchestration
 * layer can interpolate them without knowing tbox_style's layout. */

#include <math.h>
#include <string.h>

#include <tbox/style.h>

#include "base/tbox_string.h"

#define BIT(p) (1ull << (p))
#define SIDES(first) (BIT(first) | BIT((first) + 1) | BIT((first) + 2) | BIT((first) + 3))
#define ALL_BITS ((1ull << TBOX_STYLE_ANIM_COUNT) - 1ull)

typedef struct tbox_style_animatable_name {
    const char *name;
    unsigned long long mask;
} tbox_style_animatable_name;

static const tbox_style_animatable_name tbox_style_animatable_names[] = {
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
    { "transform",                  BIT(TBOX_STYLE_ANIM_TRANSLATE_X) | BIT(TBOX_STYLE_ANIM_TRANSLATE_Y)                 },
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
    { "word-spacing",               BIT(TBOX_STYLE_ANIM_WORD_SPACING)                                                   },
};

unsigned long long tbox_style_animatable_mask(tbox_string_view name) {
    for (size_t i = 0; i < sizeof(tbox_style_animatable_names) / sizeof(tbox_style_animatable_names[0]); i++)
        if (tbox_string_view_equal_ascii_ci(name, tbox_string_view_from_cstr(tbox_style_animatable_names[i].name)))
            return tbox_style_animatable_names[i].mask;
    return 0;
}

typedef enum {
    KIND_COLOR,
    KIND_AUTO_COLOR,
    KIND_LENGTH,
    KIND_NUMBER,
    KIND_BORDER_COLOR,
    KIND_BORDER_WIDTH,
    KIND_RADIUS
} tbox_style_animatable_kind;

static tbox_style_animatable_kind tbox_style_animatable_kind_of(tbox_style_animatable p) {
    switch (p) {
    case TBOX_STYLE_ANIM_COLOR:
    case TBOX_STYLE_ANIM_BACKGROUND_COLOR:
    case TBOX_STYLE_ANIM_OUTLINE_COLOR:
    case TBOX_STYLE_ANIM_TEXT_DECORATION_COLOR:
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
    case TBOX_STYLE_ANIM_OPACITY:
    case TBOX_STYLE_ANIM_OUTLINE_WIDTH:
    case TBOX_STYLE_ANIM_OUTLINE_OFFSET:
    case TBOX_STYLE_ANIM_LETTER_SPACING:
    case TBOX_STYLE_ANIM_WORD_SPACING:
        return KIND_NUMBER;
    default:
        return KIND_LENGTH;
    }
}

static tbox_css_rgba *tbox_style_animatable_color(tbox_style *style, tbox_style_animatable p) {
    switch (p) {
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
    default:
        return &style->caret_color;
    }
}

static double *tbox_style_animatable_number(tbox_style *style, tbox_style_animatable p) {
    switch (p) {
    case TBOX_STYLE_ANIM_OPACITY:
        return &style->opacity;
    case TBOX_STYLE_ANIM_OUTLINE_WIDTH:
        return &style->outline_width;
    case TBOX_STYLE_ANIM_OUTLINE_OFFSET:
        return &style->outline_offset;
    case TBOX_STYLE_ANIM_LETTER_SPACING:
        return &style->letter_spacing;
    default:
        return &style->word_spacing;
    }
}

static tbox_style_length *tbox_style_animatable_length(tbox_style *style, tbox_style_animatable p) {
    if (p == TBOX_STYLE_ANIM_TRANSLATE_X)
        return &style->translate_x;
    if (p == TBOX_STYLE_ANIM_TRANSLATE_Y)
        return &style->translate_y;
    if (p == TBOX_STYLE_ANIM_WIDTH)
        return &style->width;
    if (p == TBOX_STYLE_ANIM_HEIGHT)
        return &style->height;
    if (p >= TBOX_STYLE_ANIM_PADDING_TOP && p <= TBOX_STYLE_ANIM_PADDING_LEFT)
        return &style->padding[p - TBOX_STYLE_ANIM_PADDING_TOP];
    if (p >= TBOX_STYLE_ANIM_MARGIN_TOP && p <= TBOX_STYLE_ANIM_MARGIN_LEFT)
        return &style->margin[p - TBOX_STYLE_ANIM_MARGIN_TOP];
    return &style->offset[p - TBOX_STYLE_ANIM_TOP];
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

void tbox_style_animatable_read(const tbox_style *style, tbox_style_animatable property, double out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0.0;
    if (style == NULL || property >= TBOX_STYLE_ANIM_COUNT)
        return;
    tbox_style *s = (tbox_style *)style; /* the accessors below only read through it */
    switch (tbox_style_animatable_kind_of(property)) {
    case KIND_COLOR:
    case KIND_AUTO_COLOR:
        tbox_style_animatable_color_read(*tbox_style_animatable_color(s, property), out);
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
    case KIND_LENGTH: {
        const tbox_style_length *length = tbox_style_animatable_length(s, property);
        out[0] = length->value, out[1] = length->px_offset, out[2] = (double)length->kind;
        break;
    }
    }
}

void tbox_style_animatable_write(tbox_style *style, tbox_style_animatable property, const double value[4]) {
    if (style == NULL || property >= TBOX_STYLE_ANIM_COUNT)
        return;
    switch (tbox_style_animatable_kind_of(property)) {
    case KIND_COLOR:
    case KIND_AUTO_COLOR:
        *tbox_style_animatable_color(style, property) = tbox_style_animatable_color_value(value);
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
        if (property == TBOX_STYLE_ANIM_OUTLINE_WIDTH && v < 0.0)
            v = 0.0;
        *tbox_style_animatable_number(style, property) = v;
        break;
    }
    case KIND_LENGTH: {
        tbox_style_length *length = tbox_style_animatable_length(style, property);
        bool non_negative         = property == TBOX_STYLE_ANIM_WIDTH || property == TBOX_STYLE_ANIM_HEIGHT || (property >= TBOX_STYLE_ANIM_PADDING_TOP && property <= TBOX_STYLE_ANIM_PADDING_LEFT);
        length->kind              = (tbox_style_length_kind)lround(value[2]);
        length->value             = non_negative && value[0] < 0.0 ? 0.0 : value[0];
        length->px_offset         = value[1];
        break;
    }
    }
}

bool tbox_style_animatable_interpolable(tbox_style_animatable property, const double from[4], const double to[4]) {
    switch (tbox_style_animatable_kind_of(property)) {
    case KIND_AUTO_COLOR:
        return from[3] > 0.0 && to[3] > 0.0;
    case KIND_LENGTH:
        return from[2] == to[2] && lround(from[2]) != TBOX_STYLE_LENGTH_AUTO;
    default:
        return true;
    }
}

void tbox_style_animatable_mix(const double from[4], const double to[4], double t, double out[4]) {
    for (int i = 0; i < 4; i++)
        out[i] = from[i] + (to[i] - from[i]) * t;
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
    for (size_t i = style->transition_count; i > 0; i--) {
        const tbox_style_transition *entry = &style->transitions[i - 1];
        if (entry->properties & BIT(property))
            return entry->duration > 0.0 ? entry : NULL;
    }
    return NULL;
}
