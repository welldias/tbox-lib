#include "tbox_number.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool tbox_number_is_digit(char c) {
    return c >= '0' && c <= '9';
}

/* 10^0 .. 10^22 are exact doubles. */
static const double tbox_number_powers_of_ten[] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};

size_t tbox_number_scan(tbox_string_view text, double *out) {
    const char *s = text.data;
    size_t n = text.size, at = 0;
    bool negative = false;
    if (at < n && (s[at] == '+' || s[at] == '-'))
        negative = s[at++] == '-';

    /* Up to 19 significant digits fit a uint64_t; further digits only
     * shift the decimal exponent. */
    uint64_t mantissa = 0;
    int significant   = 0;
    long exponent     = 0;
    size_t digits     = 0;
    for (; at < n && tbox_number_is_digit(s[at]); at++, digits++) {
        if (significant < 19) {
            mantissa = mantissa * 10u + (uint64_t)(s[at] - '0');
            if (mantissa != 0) significant++;
        } else {
            exponent++;
        }
    }
    if (at + 1 < n && s[at] == '.' && tbox_number_is_digit(s[at + 1])) {
        for (at++; at < n && tbox_number_is_digit(s[at]); at++, digits++) {
            if (significant < 19) {
                mantissa = mantissa * 10u + (uint64_t)(s[at] - '0');
                if (mantissa != 0) significant++;
                exponent--;
            }
        }
    }
    if (digits == 0)
        return 0;

    if (at < n && (s[at] == 'e' || s[at] == 'E')) {
        size_t e = at + 1;
        bool exponent_negative = false;
        if (e < n && (s[e] == '+' || s[e] == '-'))
            exponent_negative = s[e++] == '-';
        if (e < n && tbox_number_is_digit(s[e])) {
            long value = 0;
            for (; e < n && tbox_number_is_digit(s[e]); e++)
                if (value < 100000) value = value * 10 + (s[e] - '0');
            exponent += exponent_negative ? -value : value;
            at = e;
        }
    }

    double result;
    if (mantissa == 0) {
        result = 0.0;
    } else if (mantissa <= (UINT64_C(1) << 53) && exponent >= -22 && exponent <= 22) {
        /* Both operands exact, so one IEEE operation rounds correctly. */
        result = exponent < 0 ? (double)mantissa / tbox_number_powers_of_ten[-exponent]
                              : (double)mantissa * tbox_number_powers_of_ten[exponent];
    } else {
        result = (double)mantissa * pow(10.0, (double)exponent);
    }
    *out = negative ? -result : result;
    return at;
}

bool tbox_number_parse(tbox_string_view text, double *out) {
    double value;
    if (text.size == 0 || tbox_number_scan(text, &value) != text.size || !isfinite(value))
        return false;
    *out = value;
    return true;
}

int tbox_number_format(char *out, size_t capacity, double value, int precision) {
    char buffer[64];
    int length = snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
    if (length < 0 || (size_t)length >= sizeof(buffer))
        return -1;
    /* %g emits only digits, sign, 'e', "inf"/"nan" and the locale's
     * decimal separator (possibly several bytes): turn that run into '.'. */
    size_t written = 0;
    for (size_t i = 0; i < (size_t)length;) {
        char c = buffer[i];
        if (tbox_number_is_digit(c) || c == '+' || c == '-' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            buffer[written++] = c;
            i++;
            continue;
        }
        buffer[written++] = '.';
        while (i < (size_t)length && !tbox_number_is_digit(buffer[i]))
            i++;
    }
    if (capacity > 0) {
        size_t copy = written < capacity - 1 ? written : capacity - 1;
        memcpy(out, buffer, copy);
        out[copy] = '\0';
    }
    return (int)written;
}
