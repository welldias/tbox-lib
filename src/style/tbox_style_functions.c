#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "base/tbox_string.h"
#include "style/tbox_style_internal.h"

typedef struct tbox_style_calc_parser {
    tbox_string_view text;
    size_t pos;
    const tbox_style_units *units;
    double em_base;
    int depth;
    bool number_percent; /* opacity: 100% is the number 1 */
} tbox_style_calc_parser;

static bool tbox_style_calc_sum(tbox_style_calc_parser *p, tbox_style_calc_value *out);

static void tbox_style_calc_skip_space(tbox_style_calc_parser *p) {
    while (p->pos < p->text.size && tbox_ascii_is_space(p->text.data[p->pos]))
        p->pos++;
}

/* Arguments after an opening parenthesis, including nested expressions.
 * A failed parse discards the local parser, so only success unwinds depth. */
static bool tbox_style_calc_arguments(tbox_style_calc_parser *p, tbox_style_calc_value *args, size_t max, size_t *count) {
    *count = 0;
    p->depth++;
    for (;;) {
        if (*count == max || !tbox_style_calc_sum(p, &args[*count]))
            return false;
        (*count)++;
        tbox_style_calc_skip_space(p);
        if (p->pos < p->text.size && p->text.data[p->pos] == ',') {
            p->pos++;
            continue;
        }
        if (p->pos >= p->text.size || p->text.data[p->pos] != ')')
            return false;
        p->pos++;
        p->depth--;
        return true;
    }
}

/* Finite scalars and lengths folded to px; percentages/bounds require
 * layout and are deliberately excluded from stepped operations. */
static bool tbox_style_calc_scalar(tbox_style_calc_value value) {
    return !value.angle && value.bounds == 0 && value.percent == 0.0 && isfinite(value.px);
}

static bool tbox_style_calc_operand(tbox_style_calc_parser *p, tbox_style_calc_value *out) {
    tbox_style_calc_skip_space(p);
    if (p->pos >= p->text.size || p->depth > 16)
        return false;
    const char *rest                         = p->text.data + p->pos;
    size_t left                              = p->text.size - p->pos;
    static const char *const trigonometric[] = { "sin(", "cos(", "tan(" };
    for (size_t f = 0; f < sizeof(trigonometric) / sizeof(trigonometric[0]); f++) {
        size_t length = strlen(trigonometric[f]);
        if (left < length || !tbox_string_view_equal_ascii_ci(tbox_string_view_make(rest, length), tbox_string_view_make(trigonometric[f], length)))
            continue;
        p->pos += length;
        const double pi = 3.14159265358979323846;
        tbox_style_calc_value arg;
        size_t count;
        if (!tbox_style_calc_arguments(p, &arg, 1, &count) || (!arg.number && !arg.angle) || arg.bounds != 0 || arg.percent != 0.0 || !isfinite(arg.px))
            return false;
        bool angle     = arg.angle;
        double degrees = angle ? fmod(arg.px, 360.0) : 0.0;
        double radians = angle ? degrees * (pi / 180.0) : arg.px;
        double result;
        if (f == 2 && fabs(fmod(radians, pi)) == pi / 2.0)
            return false; /* non-finite poles */
        result = f == 0 ? sin(radians) : f == 1 ? cos(radians) : tan(radians);
        /* Exactly represented quarter turns should not create tiny nonzero
         * lengths due to conversion through radians. */
        if (angle && fmod(degrees, 90.0) == 0.0) {
            int quarter = (int)(degrees / 90.0);
            if (f == 0)
                result = quarter % 2 == 0 ? copysign(0.0, degrees) : quarter == 1 || quarter == -3 ? 1.0 : -1.0;
            else if (f == 1)
                result = quarter % 2 != 0 ? 0.0 : quarter == 0 ? 1.0 : -1.0;
            else
                result = copysign(0.0, degrees);
        }
        if (!isfinite(result))
            return false;
        *out = (tbox_style_calc_value){ result, 0.0, true, 0, 0.0, 0.0, false };
        return true;
    }
    /* Inverse trig returns an angle, never a unitless radian number.
     * atan2 accepts equal types whose values no longer depend on layout. */
    static const char *const inverse[] = { "asin(", "acos(", "atan(", "atan2(" };
    for (size_t f = 0; f < sizeof(inverse) / sizeof(inverse[0]); f++) {
        size_t length = strlen(inverse[f]);
        if (left < length || !tbox_string_view_equal_ascii_ci(tbox_string_view_make(rest, length), tbox_string_view_make(inverse[f], length)))
            continue;
        p->pos += length;
        tbox_style_calc_value args[2];
        size_t count;
        if (!tbox_style_calc_arguments(p, args, 2, &count) || count != (f == 3 ? 2u : 1u))
            return false;
        for (size_t i = 0; i < count; i++) {
            if (args[i].bounds != 0 || args[i].percent != 0.0 || !isfinite(args[i].px) || (f != 3 && !args[i].number) || args[i].number != args[0].number || args[i].angle != args[0].angle)
                return false;
        }
        double a = args[0].px;
        if (f < 2 && (a < -1.0 || a > 1.0))
            return false;
        double radians = f == 0 ? asin(a) : f == 1 ? acos(a) : f == 2 ? atan(a) : atan2(a, args[1].px);
        double degrees = radians * (180.0 / 3.14159265358979323846);
        if (!isfinite(degrees))
            return false;
        *out = (tbox_style_calc_value){ degrees, 0.0, false, 0, 0.0, 0.0, true };
        return true;
    }
    /* Exponentials require numbers; hypot/abs preserve a scalar's type,
     * while sign always returns a number. */
    static const char *const functions[] = { "pow(", "sqrt(", "exp(", "log(", "hypot(", "abs(", "sign(" };
    for (size_t f = 0; f < sizeof(functions) / sizeof(functions[0]); f++) {
        size_t length = strlen(functions[f]);
        if (left < length || !tbox_string_view_equal_ascii_ci(tbox_string_view_make(rest, length), tbox_string_view_make(functions[f], length)))
            continue;
        p->pos += length;
        tbox_style_calc_value values[8];
        double args[8];
        size_t count;
        if (!tbox_style_calc_arguments(p, values, 8, &count))
            return false;
        for (size_t i = 0; i < count; i++) {
            if (!tbox_style_calc_scalar(values[i]) || (f < 4 && !values[i].number) || values[i].number != values[0].number)
                return false;
            args[i] = values[i].px;
        }
        double result;
        if (f == 0) {
            if (count != 2)
                return false;
            result = pow(args[0], args[1]);
        } else if (f == 1) {
            if (count != 1 || args[0] < 0.0)
                return false;
            result = sqrt(args[0]);
        } else if (f == 2) {
            if (count != 1)
                return false;
            result = exp(args[0]);
        } else if (f == 3) {
            if (count > 2 || args[0] <= 0.0 || (count == 2 && (args[1] <= 0.0 || args[1] == 1.0)))
                return false;
            result = count == 2 ? log(args[0]) / log(args[1]) : log(args[0]);
        } else if (f == 4) {
            result = 0.0;
            for (size_t i = 0; i < count; i++)
                result = hypot(result, args[i]);
        } else {
            if (count != 1)
                return false;
            result = f == 5 ? fabs(args[0]) : args[0] == 0.0 ? args[0] : args[0] < 0.0 ? -1.0 : 1.0;
        }
        if (!isfinite(result))
            return false;
        *out = (tbox_style_calc_value){ result, 0.0, f == 6 || values[0].number, 0, 0.0, 0.0, false };
        return true;
    }
    static const char *const stepped[] = { "round(", "mod(", "rem(" };
    for (size_t f = 0; f < sizeof(stepped) / sizeof(stepped[0]); f++) {
        size_t length = strlen(stepped[f]);
        if (left < length || !tbox_string_view_equal_ascii_ci(tbox_string_view_make(rest, length), tbox_string_view_make(stepped[f], length)))
            continue;
        p->pos += length;
        enum {
            ROUND_NEAREST,
            ROUND_UP,
            ROUND_DOWN,
            ROUND_TO_ZERO
        } strategy = ROUND_NEAREST;
        if (f == 0) {
            tbox_style_calc_skip_space(p);
            size_t start = p->pos, end = start;
            while (end < p->text.size && ((p->text.data[end] >= 'a' && p->text.data[end] <= 'z') || (p->text.data[end] >= 'A' && p->text.data[end] <= 'Z') || p->text.data[end] == '-'))
                end++;
            tbox_string_view word = tbox_string_view_make(p->text.data + start, end - start);
            bool named            = true;
            if (tbox_style_is(word, "nearest"))
                strategy = ROUND_NEAREST;
            else if (tbox_style_is(word, "up"))
                strategy = ROUND_UP;
            else if (tbox_style_is(word, "down"))
                strategy = ROUND_DOWN;
            else if (tbox_style_is(word, "to-zero"))
                strategy = ROUND_TO_ZERO;
            else
                named = false;
            if (named) {
                p->pos = end;
                tbox_style_calc_skip_space(p);
                if (p->pos >= p->text.size || p->text.data[p->pos++] != ',')
                    return false;
            }
        }
        tbox_style_calc_value args[2];
        size_t count;
        if (!tbox_style_calc_arguments(p, args, 2, &count))
            return false;
        if (f == 0 && count == 1 && args[0].number) {
            args[1] = (tbox_style_calc_value){ 1.0, 0.0, true, 0, 0.0, 0.0, false };
            count   = 2;
        }
        if (count != 2 || !tbox_style_calc_scalar(args[0]) || !tbox_style_calc_scalar(args[1]) || args[0].number != args[1].number || args[1].px == 0.0)
            return false;
        double a = args[0].px, b = args[1].px, result;
        if (f == 0) {
            double step = fabs(b), remainder = fmod(a, step);
            result = a; /* Exact multiples preserve signed zero. */
            if (remainder != 0.0) {
                double base  = a - remainder;
                double lower = remainder < 0.0 ? base - step : base;
                double upper = remainder < 0.0 ? base : base + step;
                if (strategy == ROUND_UP)
                    result = upper;
                else if (strategy == ROUND_DOWN)
                    result = lower;
                else if (strategy == ROUND_TO_ZERO)
                    result = base;
                else
                    result = a - lower < upper - a ? lower : upper;
                if (result == 0.0)
                    result = copysign(0.0, a);
            }
        } else {
            result = fmod(a, b);
            if (f == 1) {
                if (result == 0.0)
                    result = copysign(0.0, b);
                else if (signbit(result) != signbit(b))
                    result += b;
            }
        }
        if (!isfinite(result))
            return false;
        *out = (tbox_style_calc_value){ result, 0.0, args[0].number, 0, 0.0, 0.0, false };
        return true;
    }
    /* Comparison functions fold compatible affine expressions or retain
     * fixed px bounds until the layout percentage base is known. */
    static const char *const comparisons[3] = { "min(", "max(", "clamp(" };
    for (int f = 0; f < 3; f++) {
        size_t length = strlen(comparisons[f]);
        if (left < length || !tbox_string_view_equal_ascii_ci(tbox_string_view_make(rest, length), tbox_string_view_make(comparisons[f], length)))
            continue;
        p->pos += length;
        tbox_style_calc_value args[8];
        size_t count;
        if (!tbox_style_calc_arguments(p, args, 8, &count))
            return false;
        if ((f == 2 && count != 3) || count == 0)
            return false;
        /* Identity also preserves an already bounded nested value. */
        if (count == 1) {
            *out = args[0];
            return true;
        }
        bool same_px = true, same_percent = true;
        size_t percent_index = count, percent_count = 0;
        for (size_t i = 0; i < count; i++) {
            if (args[i].bounds != 0 || args[i].number != args[0].number || args[i].angle != args[0].angle)
                return false;
            same_px &= args[i].px == args[0].px;
            same_percent &= args[i].percent == args[0].percent;
            if (args[i].percent != 0.0) {
                percent_index = i;
                percent_count++;
            }
        }
        /* Equal slopes compare their px offsets; equal offsets compare
         * percentages (the layout percentage base is nonnegative). */
        if (same_percent || same_px) {
            double values[8];
            for (size_t i = 0; i < count; i++)
                values[i] = same_percent ? args[i].px : args[i].percent;
            double result = values[0];
            if (f == 2) {
                result = fmax(values[0], fmin(values[1], values[2]));
            } else {
                for (size_t i = 1; i < count; i++)
                    result = f == 0 ? fmin(result, values[i]) : fmax(result, values[i]);
            }
            *out = args[0];
            if (same_percent)
                out->px = result;
            else
                out->percent = result;
            return true;
        }
        /* A single affine percentage expression can retain fixed px
         * bounds until its containing block is known. */
        if (percent_count != 1)
            return false;
        if (args[0].number || (f == 2 && percent_index != 1))
            return false;
        tbox_style_calc_value value = args[percent_index];
        bool have                   = false;
        double bound                = 0.0;
        for (size_t i = 0; i < count; i++) {
            if (i == percent_index || f == 2)
                continue;
            if (!have || (f == 0 ? args[i].px < bound : args[i].px > bound))
                bound = args[i].px;
            have = true;
        }
        if (f == 0)
            value.bounds = 2, value.hi = bound;
        else if (f == 1)
            value.bounds = 1, value.lo = bound;
        else
            value.bounds = 3, value.lo = args[0].px, value.hi = args[2].px;
        *out = value;
        return true;
    }
    bool nested_calc = left >= 5 && tbox_string_view_equal_ascii_ci(tbox_string_view_make(rest, 5), tbox_string_view_from_cstr("calc("));
    if (nested_calc || rest[0] == '(') {
        p->pos += nested_calc ? 5 : 1;
        p->depth++;
        bool ok = tbox_style_calc_sum(p, out);
        p->depth--;
        tbox_style_calc_skip_space(p);
        if (!ok || p->pos >= p->text.size || p->text.data[p->pos] != ')')
            return false;
        p->pos++;
        return true;
    }
    /* One token: an optional sign, digits/dot/exponent, then a unit or %. */
    size_t start = p->pos;
    if (p->pos < p->text.size && (p->text.data[p->pos] == '+' || p->text.data[p->pos] == '-'))
        p->pos++;
    while (p->pos < p->text.size) {
        char c             = p->text.data[p->pos];
        bool exponent_sign = (c == '+' || c == '-') && p->pos > start && (p->text.data[p->pos - 1] == 'e' || p->text.data[p->pos - 1] == 'E') && p->pos >= start + 2 && p->text.data[p->pos - 2] >= '0' && p->text.data[p->pos - 2] <= '9';
        if (!((c >= '0' && c <= '9') || c == '.' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '%' || exponent_sign))
            break;
        p->pos++;
    }
    tbox_string_view token = tbox_string_view_make(p->text.data + start, p->pos - start);
    if (token.size == 0)
        return false;
    *out = (tbox_style_calc_value){ 0.0, 0.0, false, 0, 0.0, 0.0, false };
    if (token.data[token.size - 1] == '%') {
        if (!tbox_style_parse_number(tbox_string_view_make(token.data, token.size - 1), &out->percent))
            return false;
        if (p->number_percent) {
            out->number  = true;
            out->px      = out->percent / 100.0;
            out->percent = 0.0;
        }
        return true;
    }
    if (tbox_style_is(token, "pi") || tbox_style_is(token, "e")) {
        out->number = true;
        out->px     = tbox_style_is(token, "pi") ? 3.14159265358979323846 : 2.71828182845904523536;
        return true;
    }
    if (tbox_style_parse_number(token, &out->px)) {
        out->number = true;
        return true;
    }
    if (tbox_style_parse_angle_literal(token, &out->px)) {
        out->angle = true;
        return true;
    }
    return tbox_style_parse_dimension(token, p->units, p->em_base, &out->px);
}

static bool tbox_style_calc_product(tbox_style_calc_parser *p, tbox_style_calc_value *out) {
    if (!tbox_style_calc_operand(p, out))
        return false;
    for (;;) {
        size_t before = p->pos;
        tbox_style_calc_skip_space(p);
        if (p->pos >= p->text.size || (p->text.data[p->pos] != '*' && p->text.data[p->pos] != '/')) {
            p->pos = before; /* the sum checks the space before + and - */
            return true;
        }
        char op = p->text.data[p->pos++];
        tbox_style_calc_value rhs;
        if (!tbox_style_calc_operand(p, &rhs) || rhs.bounds != 0 || out->bounds != 0)
            return false;
        if (op == '*') {
            if (!out->number && !rhs.number)
                return false;
            double factor                = out->number ? out->px : rhs.px;
            tbox_style_calc_value scaled = out->number ? rhs : *out;
            *out                         = (tbox_style_calc_value){ scaled.px * factor, scaled.percent * factor, out->number && rhs.number, 0, 0.0, 0.0, scaled.angle };
        } else {
            if (!rhs.number || rhs.px == 0.0)
                return false;
            out->px /= rhs.px;
            out->percent /= rhs.px;
        }
    }
}

/* `+` and `-` need whitespace on both sides, as in CSS. */
static bool tbox_style_calc_sum(tbox_style_calc_parser *p, tbox_style_calc_value *out) {
    if (!tbox_style_calc_product(p, out))
        return false;
    for (;;) {
        size_t before = p->pos;
        tbox_style_calc_skip_space(p);
        if (p->pos >= p->text.size || (p->text.data[p->pos] != '+' && p->text.data[p->pos] != '-'))
            return true;
        if (p->pos == before || p->pos + 1 >= p->text.size || !tbox_ascii_is_space(p->text.data[p->pos + 1]))
            return false;
        char op = p->text.data[p->pos++];
        tbox_style_calc_value rhs;
        if (!tbox_style_calc_product(p, &rhs) || out->number != rhs.number || out->angle != rhs.angle || rhs.bounds != 0 || out->bounds != 0)
            return false;
        double sign = op == '+' ? 1.0 : -1.0;
        out->px += sign * rhs.px;
        out->percent += sign * rhs.percent;
    }
}

/* Length expressions, including comparison and stepped functions, with
 * + - * / and nested numeric functions/parentheses. Lengths of any
 * supported unit fold into px right here; percentages stay separate, for
 * the Layout Tree to resolve against its containing block, unless
 * `percent_base` is nonnegative (font-size: percentages of the parent's
 * size). A unitless result is invalid. */

bool tbox_style_parse_calc(tbox_string_view text, const tbox_style_units *units, double em_base, double percent_base, double *out_px, double *out_percent) {
    tbox_style_calc_value value;
    if (!tbox_style_parse_calc_bounded(text, units, em_base, percent_base, &value) || value.bounds != 0)
        return false;
    *out_px      = value.px;
    *out_percent = value.percent;
    return true;
}

/* Shared entry: calculations are accepted only in functional notation;
 * constants such as pi/e are not standalone property values. */
static bool tbox_style_calc_evaluate(tbox_string_view text, const tbox_style_units *units, double em_base, bool number_percent, tbox_style_calc_value *out) {
    text                             = tbox_string_view_trim_ascii_space(text);
    bool function                    = false;
    static const char *const names[] = {
        "calc(",
        "min(",
        "max(",
        "clamp(",
        "round(",
        "mod(",
        "rem(",
        "pow(",
        "sqrt(",
        "exp(",
        "log(",
        "hypot(",
        "abs(",
        "sign(",
        "sin(",
        "cos(",
        "tan(",
        "asin(",
        "acos(",
        "atan(",
        "atan2(",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        size_t length = strlen(names[i]);
        if (text.size > length && tbox_string_view_equal_ascii_ci(tbox_string_view_make(text.data, length), tbox_string_view_make(names[i], length)))
            function = true;
    }
    if (!function || text.data[text.size - 1] != ')')
        return false;
    tbox_style_calc_parser parser = { text, 0, units, em_base, 0, number_percent };
    tbox_style_calc_value value;
    if (!tbox_style_calc_operand(&parser, &value) || !isfinite(value.px) || !isfinite(value.percent))
        return false;
    tbox_style_calc_skip_space(&parser);
    if (parser.pos != text.size)
        return false;
    *out = value;
    return true;
}

/* Keep literal parsing separate: operands never recursively enter this
 * property-level wrapper, so nested calculations share one depth limit. */
bool tbox_style_parse_angle(tbox_string_view text, const tbox_style_units *units, double *out) {
    if (tbox_style_parse_angle_literal(text, out))
        return isfinite(*out);
    tbox_style_calc_value value;
    if (!tbox_style_calc_evaluate(text, units, units->font_size, false, &value) || !value.angle || value.bounds != 0 || value.percent != 0.0)
        return false;
    *out = value.px;
    return true;
}

/* Opacity's number-percentage context converts percentages before arithmetic,
 * so calc(50% + .25) is .75 and dimensions remain invalid final results. */
bool tbox_style_parse_calc_number(tbox_string_view text, const tbox_style_units *units, double *out) {
    tbox_style_calc_value value;
    if (!tbox_style_calc_evaluate(text, units, units->font_size, true, &value) || !value.number || value.bounds != 0)
        return false;
    *out = value.px;
    return true;
}

bool tbox_style_parse_calc_bounded(tbox_string_view text, const tbox_style_units *units, double em_base, double percent_base, tbox_style_calc_value *out) {
    tbox_style_calc_value value;
    if (!tbox_style_calc_evaluate(text, units, em_base, false, &value) || value.number || value.angle)
        return false;
    if (percent_base >= 0.0) {
        value.px += percent_base * value.percent / 100.0;
        value.percent = 0.0;
        if ((value.bounds & 2u) && value.px > value.hi)
            value.px = value.hi;
        if ((value.bounds & 1u) && value.px < value.lo)
            value.px = value.lo;
        value.bounds = 0;
    }
    *out = value;
    return true;
}

/* <easing-function>: the keywords, cubic-bezier() (x1, x2 in [0, 1]) and
 * steps(<n>[, <position>]). linear() with stops is not supported. */
bool tbox_style_parse_easing(tbox_string_view token, tbox_style_easing *out) {
    token                    = tbox_string_view_trim_ascii_space(token);
    tbox_style_easing easing = tbox_style_easing_default();
    tbox_string_view args;
    if (tbox_style_is(token, "ease")) {
    } else if (tbox_style_is(token, "linear")) {
        easing.x1 = easing.y1 = 0.0, easing.x2 = easing.y2 = 1.0;
    } else if (tbox_style_is(token, "ease-in")) {
        easing.x1 = 0.42, easing.y1 = 0.0, easing.x2 = 1.0, easing.y2 = 1.0;
    } else if (tbox_style_is(token, "ease-out")) {
        easing.x1 = 0.0, easing.y1 = 0.0, easing.x2 = 0.58, easing.y2 = 1.0;
    } else if (tbox_style_is(token, "ease-in-out")) {
        easing.x1 = 0.42, easing.y1 = 0.0, easing.x2 = 0.58, easing.y2 = 1.0;
    } else if (tbox_style_is(token, "step-start") || tbox_style_is(token, "step-end")) {
        easing.steps = true, easing.step_count = 1, easing.step_position = tbox_style_is(token, "step-start") ? 1 : 0;
    } else if (tbox_style_function_args(token, "cubic-bezier", &args)) {
        tbox_string_view parts[4];
        double v[4];
        if (tbox_style_split_top_level(args, ',', parts, 4) != 4)
            return false;
        for (int i = 0; i < 4; i++)
            if (!tbox_style_parse_number(tbox_string_view_trim_ascii_space(parts[i]), &v[i]))
                return false;
        if (v[0] < 0.0 || v[0] > 1.0 || v[2] < 0.0 || v[2] > 1.0)
            return false;
        easing.x1 = v[0], easing.y1 = v[1], easing.x2 = v[2], easing.y2 = v[3];
    } else if (tbox_style_function_args(token, "steps", &args)) {
        tbox_string_view parts[2];
        size_t count = tbox_style_split_top_level(args, ',', parts, 2);
        int n;
        if (count == 0 || !tbox_style_parse_integer(parts[0], &n) || n < 1)
            return false;
        easing.steps = true, easing.step_count = (unsigned)n, easing.step_position = 0;
        if (count == 2) {
            tbox_string_view position = tbox_string_view_trim_ascii_space(parts[1]);
            if (tbox_style_is(position, "jump-start") || tbox_style_is(position, "start"))
                easing.step_position = 1;
            else if (tbox_style_is(position, "jump-none"))
                easing.step_position = 2;
            else if (tbox_style_is(position, "jump-both"))
                easing.step_position = 3;
            else if (!tbox_style_is(position, "jump-end") && !tbox_style_is(position, "end"))
                return false;
        }
        if (easing.step_position == 2 && n < 2)
            return false;
    } else
        return false;
    *out = easing;
    return true;
}

/* `url(x)`, `url("x")` or `url('x')` into `out` (NUL-terminated,
 * truncated to `capacity`). */
bool tbox_style_parse_url(tbox_string_view token, char *out, size_t capacity) {
    tbox_string_view args;
    if (!tbox_style_function_args(token, "url", &args))
        return false;
    if (args.size >= 2 && (args.data[0] == '"' || args.data[0] == '\'') && args.data[args.size - 1] == args.data[0])
        args = tbox_string_view_make(args.data + 1, args.size - 2);
    return tbox_utf8_copy_truncated(out, capacity, args) > 0;
}

void tbox_style_init_gradient(tbox_style_gradient *gradient) {
    memset(gradient, 0, sizeof(*gradient));
    gradient->angle     = 180.0;
    gradient->center[0] = gradient->center[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
}

/* One `<color> [<length-percentage> [<length-percentage>]]` stop (two
 * positions become two stops). */
static bool tbox_style_parse_gradient_stop(tbox_string_view part, const tbox_style_units *units, tbox_css_rgba current_color, tbox_style_gradient *out) {
    tbox_string_view tokens[3];
    size_t count = tbox_style_tokens(part, tokens, 3);
    if (count == 0)
        return false;
    tbox_css_rgba color;
    size_t color_index = 0;
    if (!tbox_style_parse_edge_color(tokens[0], current_color, &color)) {
        /* The color may also come after the position. */
        if (count != 2 || !tbox_style_parse_edge_color(tokens[1], current_color, &color))
            return false;
        color_index = 1;
    }
    tbox_style_length positions[2];
    size_t position_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (i == color_index)
            continue;
        tbox_style_length *position = &positions[position_count];
        if (out->kind == TBOX_STYLE_GRADIENT_CONIC) {
            double angle;
            if (tbox_style_parse_angle(tokens[i], units, &angle)) {
                *position = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, angle / 3.6, 0.0, 0, 0.0, 0.0 };
            } else if (!tbox_style_parse_spacing_length(tokens[i], units, position) || position->kind != TBOX_STYLE_LENGTH_PERCENT || position->px_offset != 0.0 || position->bounds != 0) {
                return false;
            }
        } else if (!tbox_style_parse_spacing_length(tokens[i], units, position) || position->kind == TBOX_STYLE_LENGTH_AUTO) {
            return false;
        }
        if (!isfinite(position->value) || !isfinite(position->px_offset))
            return false;
        position_count++;
    }
    if (position_count == 0)
        positions[position_count++] = (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
    for (size_t i = 0; i < position_count; i++) {
        if (out->stop_count == TBOX_STYLE_MAX_GRADIENT_STOPS)
            return false;
        out->stops[out->stop_count++] = (tbox_style_gradient_stop){ color, positions[i] };
    }
    return true;
}

/* `[repeating-]linear-gradient(...)` / `[repeating-]radial-gradient(...)`. */
bool tbox_style_parse_gradient(tbox_string_view token, const tbox_style_units *units, tbox_css_rgba current_color, tbox_style_gradient *out) {
    tbox_style_gradient gradient;
    tbox_style_init_gradient(&gradient);
    tbox_string_view args;
    if (tbox_style_function_args(token, "linear-gradient", &args) || (gradient.repeating = tbox_style_function_args(token, "repeating-linear-gradient", &args)))
        gradient.kind = TBOX_STYLE_GRADIENT_LINEAR;
    else if (tbox_style_function_args(token, "radial-gradient", &args) || (gradient.repeating = tbox_style_function_args(token, "repeating-radial-gradient", &args)))
        gradient.kind = TBOX_STYLE_GRADIENT_RADIAL;
    else if (tbox_style_function_args(token, "conic-gradient", &args) || (gradient.repeating = tbox_style_function_args(token, "repeating-conic-gradient", &args))) {
        gradient.kind  = TBOX_STYLE_GRADIENT_CONIC;
        gradient.angle = 0.0;
    } else
        return false;

    tbox_string_view parts[TBOX_STYLE_MAX_GRADIENT_STOPS + 1];
    size_t part_count = tbox_style_split_top_level(args, ',', parts, TBOX_STYLE_MAX_GRADIENT_STOPS + 1);
    if (part_count == 0)
        return false;
    size_t first_stop = 0;

    /* The optional first part: direction or shape/extent/center. */
    tbox_string_view tokens[8];
    size_t count = tbox_style_tokens(parts[0], tokens, 8);
    if (gradient.kind == TBOX_STYLE_GRADIENT_LINEAR) {
        double angle;
        if (count == 1 && tbox_style_parse_angle(tokens[0], units, &angle)) {
            gradient.angle = angle;
            first_stop     = 1;
        } else if (count >= 2 && count <= 3 && tbox_style_is(tokens[0], "to")) {
            int dx = 0, dy = 0;
            for (size_t i = 1; i < count; i++) {
                if (tbox_style_is(tokens[i], "left") && dx == 0)
                    dx = -1;
                else if (tbox_style_is(tokens[i], "right") && dx == 0)
                    dx = 1;
                else if (tbox_style_is(tokens[i], "top") && dy == 0)
                    dy = -1;
                else if (tbox_style_is(tokens[i], "bottom") && dy == 0)
                    dy = 1;
                else
                    return false;
            }
            if (dx != 0 && dy != 0) {
                gradient.corner[0] = dx;
                gradient.corner[1] = dy;
            } else {
                gradient.angle = dx == 1 ? 90.0 : dx == -1 ? 270.0 : dy == -1 ? 0.0 : 180.0;
            }
            first_stop = 1;
        }
    } else if (gradient.kind == TBOX_STYLE_GRADIENT_CONIC) {
        size_t i = 0;
        if (count > 0 && tbox_style_is(tokens[0], "from")) {
            if (count < 2 || !tbox_style_parse_angle(tokens[1], units, &gradient.angle))
                return false;
            i = 2;
        }
        if (i < count && tbox_style_is(tokens[i], "at")) {
            if (!tbox_style_parse_position(tokens + i + 1, count - i - 1, units, gradient.center))
                return false;
            i = count;
        }
        if (i != 0) {
            if (i != count)
                return false;
            first_stop = 1;
        }
    } else {
        bool shape_part = false, shape_set = false, extent_set = false;
        tbox_style_length radii[2];
        size_t radius_count = 0, i = 0;
        while (i < count) {
            if (tbox_style_is(tokens[i], "circle") || tbox_style_is(tokens[i], "ellipse")) {
                if (shape_set)
                    return false;
                gradient.circle = tbox_style_is(tokens[i], "circle");
                shape_set       = true;
            } else if (tbox_style_is(tokens[i], "farthest-corner") || tbox_style_is(tokens[i], "farthest-side") || tbox_style_is(tokens[i], "closest-corner") || tbox_style_is(tokens[i], "closest-side")) {
                if (extent_set || radius_count != 0)
                    return false;
                extent_set      = true;
                gradient.extent = tbox_style_is(tokens[i], "farthest-corner") ? TBOX_STYLE_GRADIENT_FARTHEST_CORNER : tbox_style_is(tokens[i], "farthest-side") ? TBOX_STYLE_GRADIENT_FARTHEST_SIDE : tbox_style_is(tokens[i], "closest-corner") ? TBOX_STYLE_GRADIENT_CLOSEST_CORNER : TBOX_STYLE_GRADIENT_CLOSEST_SIDE;
            } else if (tbox_style_is(tokens[i], "at")) {
                if (!tbox_style_parse_position(tokens + i + 1, count - i - 1, units, gradient.center))
                    return false;
                i          = count;
                shape_part = true;
                break;
            } else {
                tbox_style_length radius;
                if (!tbox_style_parse_spacing_length(tokens[i], units, &radius) || radius.kind == TBOX_STYLE_LENGTH_AUTO)
                    break;
                if (extent_set || radius_count == 2 || radius.bounds != 0 || !isfinite(radius.value) || !isfinite(radius.px_offset))
                    return false;
                if (radius.value < 0.0 && memchr(tokens[i].data, '(', tokens[i].size) == NULL)
                    return false;
                radii[radius_count++] = radius;
            }
            shape_part = true;
            i++;
        }
        if (shape_part && i != count)
            return false;
        if (radius_count != 0) {
            if (!shape_set)
                gradient.circle = radius_count == 1;
            if (gradient.circle ? radius_count != 1 || radii[0].kind != TBOX_STYLE_LENGTH_PX : radius_count != 2)
                return false;
            gradient.radius[0] = radii[0];
            gradient.radius[1] = gradient.circle ? radii[0] : radii[1];
        }
        if (shape_part)
            first_stop = 1;
    }

    for (size_t p = first_stop; p < part_count; p++)
        if (!tbox_style_parse_gradient_stop(parts[p], units, current_color, &gradient))
            return false;
    if (gradient.stop_count < 2)
        return false;
    *out = gradient;
    return true;
}

/* ---- transform ---- */

/* A transform function's number (scale(), matrix()...): a literal or a
 * numeric calc(); with `percent`, `50%` is 0.5 as in scale(). */
static bool tbox_style_transform_number(tbox_string_view raw, const tbox_style_units *units, bool percent, double *out) {
    tbox_string_view text = tbox_string_view_trim_ascii_space(raw);
    if (percent && text.size > 1 && text.data[text.size - 1] == '%' && tbox_style_parse_number(tbox_string_view_make(text.data, text.size - 1), out)) {
        *out /= 100.0;
        return isfinite(*out);
    }
    if (tbox_style_parse_number(text, out))
        return isfinite(*out);
    return tbox_style_parse_calc_number(text, units, out) && isfinite(*out);
}

/* A translation length as (percent, px): plain lengths have 0 percent. */
static bool tbox_style_transform_length(tbox_string_view raw, const tbox_style_units *units, double *out_percent, double *out_px) {
    tbox_style_length length;
    if (!tbox_style_parse_spacing_length(raw, units, &length) || length.kind == TBOX_STYLE_LENGTH_AUTO)
        return false;
    *out_percent = length.kind == TBOX_STYLE_LENGTH_PERCENT ? length.value : 0.0;
    *out_px      = length.kind == TBOX_STYLE_LENGTH_PERCENT ? length.px_offset : length.value;
    return isfinite(*out_percent) && isfinite(*out_px);
}

/* 4x4 matrices for the 3D functions, m[row][col] acting on column
 * vectors; out = a * b (b applies first). */
static void tbox_style_matrix4_multiply(const double a[4][4], const double b[4][4], double out[4][4]) {
    double r[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            r[i][j] = 0.0;
            for (int k = 0; k < 4; k++)
                r[i][j] += a[i][k] * b[k][j];
        }
    memcpy(out, r, sizeof(r));
}

static void tbox_style_matrix4_identity(double m[4][4]) {
    memset(m, 0, 16 * sizeof(double));
    m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0;
}

/* rotate3d(x, y, z, a) as CSS Transforms 2 §13 defines it. False for a
 * zero axis (no rotation). */
static bool tbox_style_matrix4_rotate(double x, double y, double z, double degrees, double m[4][4]) {
    double length = sqrt(x * x + y * y + z * z);
    tbox_style_matrix4_identity(m);
    if (!(length > 0.0))
        return false;
    x /= length, y /= length, z /= length;
    double half = degrees * 3.14159265358979323846 / 360.0;
    double sc = sin(half) * cos(half), sq = sin(half) * sin(half);
    /* CSS lists the matrix column by column: m_ij is m[j][i] here */
    m[0][0] = 1.0 - 2.0 * (y * y + z * z) * sq;
    m[1][0] = 2.0 * (x * y * sq + z * sc);
    m[2][0] = 2.0 * (x * z * sq - y * sc);
    m[0][1] = 2.0 * (x * y * sq - z * sc);
    m[1][1] = 1.0 - 2.0 * (x * x + z * z) * sq;
    m[2][1] = 2.0 * (y * z * sq + x * sc);
    m[0][2] = 2.0 * (x * z * sq + y * sc);
    m[1][2] = 2.0 * (y * z * sq - x * sc);
    m[2][2] = 1.0 - 2.0 * (x * x + y * y) * sq;
    return true;
}

/* The 2D affine part (a b c d e f) of a 4x4 matrix: what an orthographic
 * projection of the 3D transform leaves on the screen plane. */
static void tbox_style_matrix4_flatten(const double m[4][4], double out[6]) {
    out[0] = m[0][0], out[1] = m[1][0], out[2] = m[0][1], out[3] = m[1][1], out[4] = m[0][3], out[5] = m[1][3];
}

/* `args` split by commas into exactly `min`..`max` parts. */
static size_t tbox_style_transform_args(tbox_string_view args, tbox_string_view *parts, size_t min, size_t max) {
    if (args.size == 0)
        return 0;
    tbox_string_view all[17];
    size_t count = tbox_style_split_top_level(args, ',', all, 17);
    if (count < min || count > max)
        return 0;
    for (size_t i = 0; i < count; i++)
        parts[i] = all[i];
    return count;
}

/* One 3D function (rotateX, matrix3d, perspective...) as a 4x4 matrix.
 * 0 when `token` isn't one, -1 when it is but invalid, 1 when parsed. */
static int tbox_style_parse_transform_3d(tbox_string_view token, const tbox_style_units *units, double m[4][4]) {
    tbox_string_view args, parts[16];
    double v[16];
    tbox_style_matrix4_identity(m);
    if (tbox_style_function_args(token, "rotateX", &args) || tbox_style_function_args(token, "rotateY", &args)) {
        bool y = token.data[6] == 'Y' || token.data[6] == 'y';
        if (!tbox_style_parse_angle(args, units, &v[0]))
            return -1;
        tbox_style_matrix4_rotate(y ? 0.0 : 1.0, y ? 1.0 : 0.0, 0.0, v[0], m);
        return 1;
    }
    if (tbox_style_function_args(token, "rotate3d", &args)) {
        if (tbox_style_transform_args(args, parts, 4, 4) != 4 || !tbox_style_parse_angle(parts[3], units, &v[3]))
            return -1;
        for (size_t i = 0; i < 3; i++)
            if (!tbox_style_transform_number(parts[i], units, false, &v[i]))
                return -1;
        tbox_style_matrix4_rotate(v[0], v[1], v[2], v[3], m);
        return 1;
    }
    if (tbox_style_function_args(token, "matrix3d", &args)) {
        if (tbox_style_transform_args(args, parts, 16, 16) != 16)
            return -1;
        for (size_t i = 0; i < 16; i++) {
            if (!tbox_style_transform_number(parts[i], units, false, &v[i]))
                return -1;
            m[i % 4][i / 4] = v[i];
        }
        return 1;
    }
    if (tbox_style_function_args(token, "translateZ", &args)) {
        if (!tbox_style_transform_length(args, units, &v[0], &v[1]) || v[0] != 0.0)
            return -1;
        m[2][3] = v[1];
        return 1;
    }
    if (tbox_style_function_args(token, "scaleZ", &args)) {
        if (!tbox_style_transform_number(args, units, true, &v[0]))
            return -1;
        m[2][2] = v[0];
        return 1;
    }
    if (tbox_style_function_args(token, "scale3d", &args)) {
        if (tbox_style_transform_args(args, parts, 3, 3) != 3)
            return -1;
        for (size_t i = 0; i < 3; i++) {
            if (!tbox_style_transform_number(parts[i], units, true, &v[i]))
                return -1;
            m[i][i] = v[i];
        }
        return 1;
    }
    if (tbox_style_function_args(token, "perspective", &args)) {
        /* Only reaches w, which an orthographic projection drops. */
        if (!tbox_style_is(args, "none") && (!tbox_style_transform_length(args, units, &v[0], &v[1]) || v[0] != 0.0 || v[1] < 0.0))
            return -1;
        return 1;
    }
    return 0;
}

/* A 2D function, as its primitive. False when `token` isn't a valid one. */
static bool tbox_style_parse_transform_2d(tbox_string_view token, const tbox_style_units *units, tbox_style_transform_function *out) {
    tbox_string_view args, parts[6];
    size_t count;
    *out = (tbox_style_transform_function){ TBOX_STYLE_TRANSFORM_NONE, { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 } };
    double *v = out->v;
    if (tbox_style_function_args(token, "translate", &args)) {
        count = tbox_style_transform_args(args, parts, 1, 2);
        if (count == 0 || !tbox_style_transform_length(parts[0], units, &v[0], &v[1]) || (count == 2 && !tbox_style_transform_length(parts[1], units, &v[2], &v[3])))
            return false;
        out->kind = TBOX_STYLE_TRANSFORM_TRANSLATE;
    } else if (tbox_style_function_args(token, "translateX", &args) || tbox_style_function_args(token, "translateY", &args)) {
        size_t at = token.data[9] == 'Y' || token.data[9] == 'y' ? 2 : 0;
        if (!tbox_style_transform_length(args, units, &v[at], &v[at + 1]))
            return false;
        out->kind = TBOX_STYLE_TRANSFORM_TRANSLATE;
    } else if (tbox_style_function_args(token, "rotate", &args) || tbox_style_function_args(token, "rotateZ", &args)) {
        if (!tbox_style_parse_angle(args, units, &v[0]))
            return false;
        out->kind = TBOX_STYLE_TRANSFORM_ROTATE;
    } else if (tbox_style_function_args(token, "scale", &args)) {
        count = tbox_style_transform_args(args, parts, 1, 2);
        if (count == 0 || !tbox_style_transform_number(parts[0], units, true, &v[0]) || (count == 2 && !tbox_style_transform_number(parts[1], units, true, &v[1])))
            return false;
        if (count == 1)
            v[1] = v[0];
        out->kind = TBOX_STYLE_TRANSFORM_SCALE;
    } else if (tbox_style_function_args(token, "scaleX", &args) || tbox_style_function_args(token, "scaleY", &args)) {
        size_t at = token.data[5] == 'Y' || token.data[5] == 'y' ? 1 : 0;
        v[0] = v[1] = 1.0;
        if (!tbox_style_transform_number(args, units, true, &v[at]))
            return false;
        out->kind = TBOX_STYLE_TRANSFORM_SCALE;
    } else if (tbox_style_function_args(token, "skew", &args)) {
        count = tbox_style_transform_args(args, parts, 1, 2);
        if (count == 0 || !tbox_style_parse_angle(parts[0], units, &v[0]) || (count == 2 && !tbox_style_parse_angle(parts[1], units, &v[1])))
            return false;
        out->kind = TBOX_STYLE_TRANSFORM_SKEW;
    } else if (tbox_style_function_args(token, "skewX", &args) || tbox_style_function_args(token, "skewY", &args)) {
        size_t at = token.data[4] == 'Y' || token.data[4] == 'y' ? 1 : 0;
        if (!tbox_style_parse_angle(args, units, &v[at]))
            return false;
        out->kind = TBOX_STYLE_TRANSFORM_SKEW;
    } else if (tbox_style_function_args(token, "matrix", &args)) {
        if (tbox_style_transform_args(args, parts, 6, 6) != 6)
            return false;
        for (size_t i = 0; i < 6; i++)
            if (!tbox_style_transform_number(parts[i], units, false, &v[i]))
                return false;
        out->kind = TBOX_STYLE_TRANSFORM_MATRIX;
    } else if (tbox_style_function_args(token, "translate3d", &args)) {
        /* the z part goes to the 3D run, see tbox_style_parse_transform */
        if (tbox_style_transform_args(args, parts, 3, 3) != 3 || !tbox_style_transform_length(parts[0], units, &v[0], &v[1]) || !tbox_style_transform_length(parts[1], units, &v[2], &v[3]))
            return false;
        out->kind = TBOX_STYLE_TRANSFORM_TRANSLATE;
    } else {
        return false;
    }
    return true;
}

/* `transform`: `none` or a list of functions, stored one per slot as
 * their primitives. Consecutive 3D functions multiply as 4x4 matrices
 * into one MATRIX slot holding the 2D part of the product (a 2D function
 * leaves z alone, so flattening around it changes nothing). More than
 * TBOX_STYLE_MAX_TRANSFORMS slots, or any invalid function, makes the
 * whole value invalid. Translation percentages are of the border box. */
bool tbox_style_parse_transform(tbox_string_view raw, const tbox_style_units *units, tbox_style_transform_function out[TBOX_STYLE_MAX_TRANSFORMS]) {
    tbox_style_transform_function result[TBOX_STYLE_MAX_TRANSFORMS];
    memset(result, 0, sizeof(result));
    if (tbox_style_is(raw, "none")) {
        memcpy(out, result, sizeof(result));
        return true;
    }
    tbox_string_view functions[32];
    size_t count = tbox_style_tokens(raw, functions, 32), slots = 0;
    if (count == 0)
        return false;
    double run[4][4];
    bool in_run = false;
    for (size_t f = 0; f <= count; f++) {
        double m[4][4];
        int is_3d = f < count ? tbox_style_parse_transform_3d(functions[f], units, m) : 0;
        tbox_style_transform_function fn;
        bool is_2d = f < count && is_3d == 0 && tbox_style_parse_transform_2d(functions[f], units, &fn);
        if (is_3d < 0 || (f < count && is_3d == 0 && !is_2d))
            return false;
        if (is_3d > 0) {
            if (!in_run)
                tbox_style_matrix4_identity(run);
            tbox_style_matrix4_multiply(run, m, run);
            in_run = true;
            continue;
        }
        if (in_run) {
            if (slots == TBOX_STYLE_MAX_TRANSFORMS)
                return false;
            result[slots].kind = TBOX_STYLE_TRANSFORM_MATRIX;
            tbox_style_matrix4_flatten(run, result[slots++].v);
            in_run = false;
        }
        if (!is_2d)
            continue;
        if (slots == TBOX_STYLE_MAX_TRANSFORMS)
            return false;
        result[slots++] = fn;
        /* translate3d's z: translations commute, so it starts a 3D run */
        tbox_string_view args, parts[3];
        double percent, px;
        if (tbox_style_function_args(functions[f], "translate3d", &args) && tbox_style_transform_args(args, parts, 3, 3) == 3 && tbox_style_transform_length(parts[2], units, &percent, &px)) {
            if (percent != 0.0)
                return false;
            if (px != 0.0) {
                tbox_style_matrix4_identity(run);
                run[2][3] = px;
                in_run    = true;
            }
        }
    }
    memcpy(out, result, sizeof(result));
    return true;
}

/* The `rotate` property: `none`, an angle, or an axis (x, y, z or three
 * numbers) with an angle, in either order. */
bool tbox_style_parse_rotate(tbox_string_view raw, const tbox_style_units *units, double axis[3], double *degrees) {
    double a[3] = { 0.0, 0.0, 0.0 }, angle = 0.0;
    if (!tbox_style_is(raw, "none")) {
        tbox_string_view tokens[5];
        size_t count = tbox_style_tokens(raw, tokens, 5);
        bool has_angle = false, has_axis = false;
        for (size_t i = 0; i < count; i++) {
            if (!has_angle && tbox_style_parse_angle(tokens[i], units, &angle) && !(tokens[i].size == 1 && tokens[i].data[0] == '0' && count == 4 && i < 3)) {
                has_angle = true;
            } else if (!has_axis && (tbox_style_is(tokens[i], "x") || tbox_style_is(tokens[i], "y") || tbox_style_is(tokens[i], "z"))) {
                a[tokens[i].data[0] == 'x' || tokens[i].data[0] == 'X' ? 0 : tokens[i].data[0] == 'y' || tokens[i].data[0] == 'Y' ? 1 : 2] = 1.0;
                has_axis = true;
            } else if (!has_axis && i + 2 < count && tbox_style_transform_number(tokens[i], units, false, &a[0]) && tbox_style_transform_number(tokens[i + 1], units, false, &a[1]) && tbox_style_transform_number(tokens[i + 2], units, false, &a[2])) {
                has_axis = true;
                i += 2;
            } else {
                return false;
            }
        }
        if (!has_angle)
            return false;
    }
    memcpy(axis, a, sizeof(a));
    *degrees = angle;
    return true;
}

/* The `scale` property: `none` or one to three numbers/percentages (y
 * defaults to x; z is ignored). False when invalid. */
bool tbox_style_parse_scale(tbox_string_view raw, const tbox_style_units *units, bool *has_scale, double out[2]) {
    if (tbox_style_is(raw, "none")) {
        *has_scale = false;
        out[0] = out[1] = 1.0;
        return true;
    }
    tbox_string_view tokens[3];
    double v[3];
    size_t count = tbox_style_tokens(raw, tokens, 3);
    if (count == 0)
        return false;
    for (size_t i = 0; i < count; i++)
        if (!tbox_style_transform_number(tokens[i], units, true, &v[i]))
            return false;
    *has_scale = true;
    out[0] = v[0];
    out[1] = count > 1 ? v[1] : v[0];
    return true;
}

/* `transform-origin`: one or two position values (keywords, lengths,
 * percentages) and an optional z length, which is checked and ignored. */
bool tbox_style_parse_transform_origin(tbox_string_view raw, const tbox_style_units *units, tbox_style_length out[2]) {
    tbox_string_view tokens[3];
    size_t count = tbox_style_tokens(raw, tokens, 3);
    if (count == 0)
        return false;
    if (count == 3) {
        double percent, px;
        if (!tbox_style_transform_length(tokens[2], units, &percent, &px) || percent != 0.0)
            return false;
        count = 2;
    }
    tbox_style_length position[2];
    if (!tbox_style_parse_position(tokens, count, units, position))
        return false;
    out[0] = position[0];
    out[1] = position[1];
    return true;
}

/* ---- filter ---- */

void tbox_style_matrix_identity(double m[20]) {
    memset(m, 0, 20 * sizeof(double));
    m[0] = m[6] = m[12] = m[18] = 1.0;
}

/* out = a * b (apply b first, then a). */
static void tbox_style_matrix_multiply(const double a[20], const double b[20], double out[20]) {
    double r[20];
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 5; col++) {
            double sum = col == 4 ? a[row * 5 + 4] : 0.0;
            for (int k = 0; k < 4; k++)
                sum += a[row * 5 + k] * b[k * 5 + col];
            r[row * 5 + col] = sum;
        }
    }
    memcpy(out, r, sizeof(r));
}

/* An amount: a number or percentage, `default_amount` when omitted. */
static bool tbox_style_parse_amount(tbox_string_view args, double default_amount, double *out) {
    args = tbox_string_view_trim_ascii_space(args);
    if (args.size == 0) {
        *out = default_amount;
        return true;
    }
    bool percent = args.data[args.size - 1] == '%';
    double value;
    if (!tbox_style_parse_number(percent ? tbox_string_view_make(args.data, args.size - 1) : args, &value) || value < 0.0)
        return false;
    *out = percent ? value / 100.0 : value;
    return true;
}

/* filter: none | <function>+ -- the Filter Effects 1 matrices, composed
 * left to right. Unsupported functions (blur, drop-shadow, url) are
 * skipped; anything unknown invalidates the value. */

bool tbox_style_parse_filter(tbox_string_view raw, const tbox_style_units *units, tbox_css_rgba current_color, tbox_style_filter_list *out_list) {
    tbox_style_filter_list list;
    memset(&list, 0, sizeof(list));
    if (tbox_style_is(raw, "none")) {
        *out_list = list;
        return true;
    }
    tbox_string_view functions[12];
    size_t count = tbox_style_tokens(raw, functions, 12);
    if (count == 0)
        return false;
    bool open_matrix = false; /* the last op is a matrix still taking functions */
    for (size_t f = 0; f < count; f++) {
        tbox_string_view args;
        double a, m[20];
        tbox_style_matrix_identity(m);
        if (tbox_style_function_args(functions[f], "grayscale", &args) || tbox_style_function_args(functions[f], "sepia", &args) || tbox_style_function_args(functions[f], "saturate", &args)) {
            bool saturate = tbox_string_view_equal_ascii_ci(tbox_string_view_make(functions[f].data, 3), tbox_string_view_from_cstr("sat"));
            bool sepia    = !saturate && tbox_string_view_equal_ascii_ci(tbox_string_view_make(functions[f].data, 3), tbox_string_view_from_cstr("sep"));
            if (!tbox_style_parse_amount(args, 1.0, &a))
                return false;
            if (saturate) {
                const double v[9] = { 0.213 + 0.787 * a, 0.715 - 0.715 * a, 0.072 - 0.072 * a, 0.213 - 0.213 * a, 0.715 + 0.285 * a, 0.072 - 0.072 * a, 0.213 - 0.213 * a, 0.715 - 0.715 * a, 0.072 + 0.928 * a };
                for (int r = 0; r < 3; r++)
                    for (int c = 0; c < 3; c++)
                        m[r * 5 + c] = v[r * 3 + c];
            } else {
                if (a > 1.0)
                    a = 1.0;
                const double g[9] = { 0.2126 + 0.7874 * (1 - a), 0.7152 - 0.7152 * (1 - a), 0.0722 - 0.0722 * (1 - a), 0.2126 - 0.2126 * (1 - a), 0.7152 + 0.2848 * (1 - a), 0.0722 - 0.0722 * (1 - a), 0.2126 - 0.2126 * (1 - a), 0.7152 - 0.7152 * (1 - a), 0.0722 + 0.9278 * (1 - a) };
                const double p[9] = { 0.393 + 0.607 * (1 - a), 0.769 - 0.769 * (1 - a), 0.189 - 0.189 * (1 - a), 0.349 - 0.349 * (1 - a), 0.686 + 0.314 * (1 - a), 0.168 - 0.168 * (1 - a), 0.272 - 0.272 * (1 - a), 0.534 - 0.534 * (1 - a), 0.131 + 0.869 * (1 - a) };
                const double *v   = sepia ? p : g;
                for (int r = 0; r < 3; r++)
                    for (int c = 0; c < 3; c++)
                        m[r * 5 + c] = v[r * 3 + c];
            }
        } else if (tbox_style_function_args(functions[f], "hue-rotate", &args)) {
            double degrees = 0.0;
            if (tbox_string_view_trim_ascii_space(args).size > 0 && !tbox_style_parse_angle(args, units, &degrees))
                return false;
            double c = cos(degrees * 3.141592653589793 / 180.0), sn = sin(degrees * 3.141592653589793 / 180.0);
            const double v[9] = { 0.213 + c * 0.787 - sn * 0.213, 0.715 - c * 0.715 - sn * 0.715, 0.072 - c * 0.072 + sn * 0.928, 0.213 - c * 0.213 + sn * 0.143, 0.715 + c * 0.285 + sn * 0.140, 0.072 - c * 0.072 - sn * 0.283, 0.213 - c * 0.213 - sn * 0.787, 0.715 - c * 0.715 + sn * 0.715, 0.072 + c * 0.928 + sn * 0.072 };
            for (int r = 0; r < 3; r++)
                for (int col = 0; col < 3; col++)
                    m[r * 5 + col] = v[r * 3 + col];
        } else if (tbox_style_function_args(functions[f], "invert", &args)) {
            if (!tbox_style_parse_amount(args, 1.0, &a))
                return false;
            if (a > 1.0)
                a = 1.0;
            for (int r = 0; r < 3; r++) {
                m[r * 5 + r] = 1.0 - 2.0 * a;
                m[r * 5 + 4] = a;
            }
        } else if (tbox_style_function_args(functions[f], "opacity", &args)) {
            if (!tbox_style_parse_amount(args, 1.0, &a))
                return false;
            m[18] = a > 1.0 ? 1.0 : a;
        } else if (tbox_style_function_args(functions[f], "brightness", &args)) {
            if (!tbox_style_parse_amount(args, 1.0, &a))
                return false;
            m[0] = m[6] = m[12] = a;
        } else if (tbox_style_function_args(functions[f], "contrast", &args)) {
            if (!tbox_style_parse_amount(args, 1.0, &a))
                return false;
            for (int r = 0; r < 3; r++) {
                m[r * 5 + r] = a;
                m[r * 5 + 4] = 0.5 - 0.5 * a;
            }
        } else if (tbox_style_function_args(functions[f], "drop-shadow", &args)) {
            /* <x> <y> [<blur>] [<color>]; the blur is a radius, like
             * text-shadow's: its standard deviation is half of it. */
            double lengths[4];
            tbox_css_rgba color;
            bool inset = false;
            if (!tbox_style_parse_shadow(args, units, current_color, 3, lengths, &color, &inset) || inset || lengths[2] < 0.0)
                return false;
            if (list.count == TBOX_STYLE_MAX_FILTERS)
                return false;
            tbox_style_filter_op *op = &list.ops[list.count++];
            op->kind                 = TBOX_STYLE_FILTER_DROP_SHADOW;
            op->shadow               = (tbox_style_shadow){ lengths[0], lengths[1], lengths[2], 0.0, color, false };
            list.spreads             = true;
            open_matrix              = false;
            continue;
        } else if (tbox_style_function_args(functions[f], "blur", &args)) {
            double radius = 0.0;
            tbox_style_length length;
            if (tbox_string_view_trim_ascii_space(args).size > 0 && !tbox_style_is(args, "0")) {
                if (!tbox_style_parse_length(args, units, &length) || length.kind != TBOX_STYLE_LENGTH_PX || length.value < 0.0)
                    return false;
                radius = length.value;
            }
            if (list.count == TBOX_STYLE_MAX_FILTERS)
                return false;
            tbox_style_filter_op *op = &list.ops[list.count++];
            op->kind                 = TBOX_STYLE_FILTER_BLUR;
            op->radius               = radius;
            list.spreads             = true;
            open_matrix              = false;
            continue;
        } else if (tbox_style_function_args(functions[f], "url", &args)) {
            continue; /* not supported: skipped */
        } else {
            return false;
        }
        if (!open_matrix) {
            if (list.count == TBOX_STYLE_MAX_FILTERS)
                return false;
            tbox_style_filter_op *op = &list.ops[list.count++];
            op->kind                 = TBOX_STYLE_FILTER_MATRIX;
            tbox_style_matrix_identity(op->matrix);
            open_matrix = true;
        }
        double *total = list.ops[list.count - 1].matrix;
        tbox_style_matrix_multiply(m, total, total);
    }
    *out_list = list;
    return true;
}

/* The color-only form of `list` for Render's op-by-op path: false when it
 * has no matrix, or spreads (then the layer runs the whole list). */
bool tbox_style_filter_matrix_of(const tbox_style_filter_list *list, double out[20]) {
    tbox_style_matrix_identity(out);
    if (list->spreads)
        return false;
    bool any = false;
    for (size_t i = 0; i < list->count; i++)
        if (list->ops[i].kind == TBOX_STYLE_FILTER_MATRIX) {
            tbox_style_matrix_multiply(list->ops[i].matrix, out, out);
            any = true;
        }
    return any;
}

/* The legacy `clip: rect(top, right, bottom, left)` (commas or spaces),
 * offsets from the border box's top-left corner, `auto` meaning that
 * edge of the box, becomes the equivalent clip-path inset(): the right
 * and bottom insets are `100% - offset`. `clip: auto` leaves it unset. */
void tbox_style_parse_clip_rect(tbox_string_view raw, const tbox_style_units *units, tbox_style_clip_path *out) {
    tbox_string_view args, parts[4];
    if (!tbox_style_function_args(tbox_string_view_trim_ascii_space(raw), "rect", &args))
        return;
    size_t count = memchr(args.data, ',', args.size) != NULL ? tbox_style_split_top_level(args, ',', parts, 4) : tbox_style_tokens(args, parts, 4);
    if (count != 4)
        return;
    tbox_style_length inset[4];
    for (size_t side = 0; side < 4; side++) {
        tbox_style_length offset = { TBOX_STYLE_LENGTH_PX, 0.0, 0.0, 0, 0.0, 0.0 };
        bool is_auto             = tbox_style_is(parts[side], "auto");
        if (!is_auto && (!tbox_style_parse_spacing_length(parts[side], units, &offset) || offset.kind != TBOX_STYLE_LENGTH_PX))
            return;
        if (side == 1 || side == 2) /* right, bottom: measured from the far edge */
            inset[side] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, is_auto ? 0.0 : 100.0, is_auto ? 0.0 : -offset.value, 0, 0.0, 0.0 };
        else
            inset[side] = offset;
    }
    tbox_style_clip_path clip;
    memset(&clip, 0, sizeof(clip));
    clip.kind      = TBOX_STYLE_CLIP_PATH_INSET;
    clip.center[0] = clip.center[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
    memcpy(clip.inset, inset, sizeof(inset));
    *out = clip;
}

/* `closest-side`/`farthest-side` or a length-percentage for a shape
 * radius; closest-side is AUTO, farthest-side is stored as a negative
 * percentage (-1) for Render. */
static bool tbox_style_parse_shape_radius(tbox_string_view token, const tbox_style_units *units, tbox_style_length *out) {
    if (tbox_style_is(token, "closest-side")) {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
        return true;
    }
    if (tbox_style_is(token, "farthest-side")) {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, -1.0, 0.0, 0, 0.0, 0.0 };
        return true;
    }
    return tbox_style_parse_spacing_length(token, units, out) && out->kind != TBOX_STYLE_LENGTH_AUTO && out->value >= 0.0;
}

bool tbox_style_parse_clip_path(tbox_string_view raw, const tbox_style_units *units, tbox_style_clip_path *out) {
    tbox_style_clip_path clip;
    memset(&clip, 0, sizeof(clip));
    clip.center[0] = clip.center[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
    if (tbox_style_is(raw, "none")) {
        *out = clip;
        return true;
    }
    tbox_string_view tokens[2];
    if (tbox_style_tokens(raw, tokens, 2) != 1)
        return false; /* a reference box after the shape is not supported */
    tbox_string_view args, parts[14];
    size_t count;
    if (tbox_style_function_args(tokens[0], "polygon", &args)) {
        tbox_string_view vertices[TBOX_STYLE_MAX_POLYGON_POINTS + 1];
        size_t n = tbox_style_split_top_level(args, ',', vertices, TBOX_STYLE_MAX_POLYGON_POINTS + 1);
        if (n == 0)
            return false;
        size_t first = 0;
        if (tbox_style_is(vertices[0], "evenodd") || tbox_style_is(vertices[0], "nonzero")) {
            clip.evenodd = tbox_style_is(vertices[0], "evenodd");
            first        = 1;
        }
        if (n - first < 3 || n - first > TBOX_STYLE_MAX_POLYGON_POINTS)
            return false;
        for (size_t i = first; i < n; i++) {
            tbox_string_view pair[2];
            if (tbox_style_tokens(vertices[i], pair, 2) != 2)
                return false;
            for (size_t axis = 0; axis < 2; axis++) {
                tbox_style_length *value = &clip.points[i - first][axis];
                if (!tbox_style_parse_spacing_length(pair[axis], units, value) || value->kind == TBOX_STYLE_LENGTH_AUTO || !isfinite(value->value) || !isfinite(value->px_offset))
                    return false;
            }
        }
        clip.kind        = TBOX_STYLE_CLIP_PATH_POLYGON;
        clip.point_count = n - first;
        *out             = clip;
        return true;
    }
    bool xywh = tbox_style_function_args(tokens[0], "xywh", &args);
    bool rect = !xywh && tbox_style_function_args(tokens[0], "rect", &args);
    if (xywh || rect || tbox_style_function_args(tokens[0], "inset", &args)) {
        clip.kind    = TBOX_STYLE_CLIP_PATH_INSET;
        count        = tbox_style_tokens(args, parts, 14);
        size_t round = count;
        for (size_t i = 0; i < count; i++)
            if (tbox_style_is(parts[i], "round"))
                round = i;
        tbox_style_length lengths[4];
        if (round < 1 || round > 4 || ((xywh || rect) && round != 4))
            return false;
        for (size_t i = 0; i < round; i++) {
            if (rect && tbox_style_is(parts[i], "auto")) {
                lengths[i] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, i == 1 || i == 2 ? 100.0 : 0.0, 0.0, 0, 0.0, 0.0 };
            } else if (!tbox_style_parse_spacing_length(parts[i], units, &lengths[i]) || lengths[i].kind == TBOX_STYLE_LENGTH_AUTO) {
                return false;
            }
        }
        if (rect) {
            /* Modern rect() coordinates are measured from top/left. The
             * common inset renderer floors a crossed edge to zero size. */
            for (size_t i = 0; i < 4; i++) {
                if (lengths[i].bounds != 0)
                    return false;
                clip.inset[i] = lengths[i];
                if (i == 1 || i == 2) {
                    double percent = lengths[i].kind == TBOX_STYLE_LENGTH_PERCENT ? lengths[i].value : 0.0;
                    double px      = lengths[i].kind == TBOX_STYLE_LENGTH_PX ? lengths[i].value : lengths[i].px_offset;
                    clip.inset[i]  = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 100.0 - percent, -px, 0, 0.0, 0.0 };
                }
            }
        } else if (xywh) {
            /* Convert x/y/width/height into the existing inset shape.
             * Bounded expressions cannot participate in this affine sum. */
            for (size_t i = 0; i < 4; i++)
                if (lengths[i].bounds != 0)
                    return false;
            for (size_t i = 2; i < 4; i++) {
                if (lengths[i].value < 0.0) {
                    if (memchr(parts[i].data, '(', parts[i].size) == NULL)
                        return false; /* negative literal sizes are invalid */
                    if (lengths[i].kind == TBOX_STYLE_LENGTH_PX || lengths[i].px_offset == 0.0)
                        lengths[i] = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, 0.0, 0.0, 0, 0.0, 0.0 };
                }
            }
            clip.inset[0] = lengths[1];
            clip.inset[3] = lengths[0];
            for (size_t axis = 0; axis < 2; axis++) {
                tbox_style_length pos = lengths[axis], size = lengths[axis + 2];
                double percent       = (pos.kind == TBOX_STYLE_LENGTH_PERCENT ? pos.value : 0.0) + (size.kind == TBOX_STYLE_LENGTH_PERCENT ? size.value : 0.0);
                double px            = (pos.kind == TBOX_STYLE_LENGTH_PX ? pos.value : pos.px_offset) + (size.kind == TBOX_STYLE_LENGTH_PX ? size.value : size.px_offset);
                clip.inset[axis + 1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 100.0 - percent, -px, 0, 0.0, 0.0 };
            }
        } else {
            for (size_t side = 0; side < 4; side++)
                clip.inset[side] = lengths[tbox_style_four_value_index(round, side)];
        }
        if (round < count) {
            size_t slash = count;
            for (size_t i = round + 1; i < count; i++)
                if (parts[i].size == 1 && parts[i].data[0] == '/')
                    slash = i;
            if (!tbox_style_parse_radius_list(parts + round + 1, slash - round - 1, units, clip.round_h, clip.round_h_percent))
                return false;
            if (slash < count) {
                if (!tbox_style_parse_radius_list(parts + slash + 1, count - slash - 1, units, clip.round_v, clip.round_v_percent))
                    return false;
            } else {
                memcpy(clip.round_v, clip.round_h, sizeof(clip.round_v));
                memcpy(clip.round_v_percent, clip.round_h_percent, sizeof(clip.round_v_percent));
            }
        }
    } else if (tbox_style_function_args(tokens[0], "circle", &args) || tbox_style_function_args(tokens[0], "ellipse", &args)) {
        bool circle = tbox_ascii_to_lower(tokens[0].data[0]) == 'c';
        clip.kind   = circle ? TBOX_STYLE_CLIP_PATH_CIRCLE : TBOX_STYLE_CLIP_PATH_ELLIPSE;
        count       = tbox_style_tokens(args, parts, 14);
        size_t at   = count;
        for (size_t i = 0; i < count; i++)
            if (tbox_style_is(parts[i], "at"))
                at = i;
        size_t radii = circle ? 1 : 2;
        if (at != 0 && at != radii)
            return false;
        for (size_t i = 0; i < at; i++)
            if (!tbox_style_parse_shape_radius(parts[i], units, &clip.radius[i]))
                return false;
        if (at < count && !tbox_style_parse_position(parts + at + 1, count - at - 1, units, clip.center))
            return false;
    } else {
        return false;
    }
    *out = clip;
    return true;
}
