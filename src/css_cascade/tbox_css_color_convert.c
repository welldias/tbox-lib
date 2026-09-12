#include <tbox/css_cascade.h>

#include <math.h>

static int tbox_css_hex_digit_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool tbox_css_hex_to_rgba(tbox_string_view hex, tbox_css_rgba *out_color) {
    if (hex.data == NULL || hex.size < 1 || hex.data[0] != '#') {
        return false;
    }

    size_t digit_count = hex.size - 1;
    if (digit_count != 3 && digit_count != 4 && digit_count != 6 && digit_count != 8) {
        return false;
    }

    const char *digits = hex.data + 1;
    int values[8];
    for (size_t i = 0; i < digit_count; i++) {
        int value = tbox_css_hex_digit_value(digits[i]);
        if (value < 0) {
            return false;
        }
        values[i] = value;
    }

    unsigned char r, g, b, a;
    if (digit_count == 3 || digit_count == 4) {
        /* Shorthand: each digit is duplicated to make a byte (0x3 -> 0x33). */
        r = (unsigned char)(values[0] * 17);
        g = (unsigned char)(values[1] * 17);
        b = (unsigned char)(values[2] * 17);
        a = (digit_count == 4) ? (unsigned char)(values[3] * 17) : 0xFF;
    } else {
        r = (unsigned char)((values[0] << 4) | values[1]);
        g = (unsigned char)((values[2] << 4) | values[3]);
        b = (unsigned char)((values[4] << 4) | values[5]);
        a = (digit_count == 8) ? (unsigned char)((values[6] << 4) | values[7]) : 0xFF;
    }

    if (out_color != NULL) {
        out_color->r = r;
        out_color->g = g;
        out_color->b = b;
        out_color->a = a;
    }
    return true;
}

static double tbox_css_clamp01(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

/* CSS Color Module Level 3 section 4.2's HueToRGB, applied at one of the
 * three hue offsets (+1/3, 0, -1/3) that together turn (h, s, l) into
 * (r, g, b). All three of `p`, `q`, `t` are already in [0, 1] (t may arrive
 * slightly outside that range before the wrap below, since it's `h` shifted
 * by +-1/3). */
static double tbox_css_hue_to_rgb(double p, double q, double t) {
    if (t < 0.0) {
        t += 1.0;
    }
    if (t > 1.0) {
        t -= 1.0;
    }
    if (t < 1.0 / 6.0) {
        return p + (q - p) * 6.0 * t;
    }
    if (t < 1.0 / 2.0) {
        return q;
    }
    if (t < 2.0 / 3.0) {
        return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    }
    return p;
}

tbox_css_rgba tbox_css_hsla_to_rgba(tbox_css_hsla hsla) {
    double h = fmod(hsla.h, 360.0);
    if (h < 0.0) {
        h += 360.0;
    }
    h /= 360.0;

    double s = tbox_css_clamp01(hsla.s);
    double l = tbox_css_clamp01(hsla.l);
    double a = tbox_css_clamp01(hsla.a);

    double r, g, b;
    if (s == 0.0) {
        r = g = b = l;
    } else {
        double q = (l < 0.5) ? (l * (1.0 + s)) : (l + s - l * s);
        double p = 2.0 * l - q;
        r        = tbox_css_hue_to_rgb(p, q, h + 1.0 / 3.0);
        g        = tbox_css_hue_to_rgb(p, q, h);
        b        = tbox_css_hue_to_rgb(p, q, h - 1.0 / 3.0);
    }

    tbox_css_rgba result;
    result.r = (unsigned char)(r * 255.0 + 0.5);
    result.g = (unsigned char)(g * 255.0 + 0.5);
    result.b = (unsigned char)(b * 255.0 + 0.5);
    result.a = (unsigned char)(a * 255.0 + 0.5);
    return result;
}
