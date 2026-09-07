#include "tbox_html_tokenizer.h"

#include "base/tbox_string.h"

static bool tbox_html_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static bool tbox_html_is_ascii_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool tbox_html_is_tag_name_end(char c) {
    return tbox_html_is_space(c) || c == '/' || c == '>';
}

void tbox_html_tokenizer_init(tbox_html_tokenizer *tokenizer, const char *input, size_t length, tbox_arena *arena) {
    tokenizer->input                 = input;
    tokenizer->length                = length;
    tokenizer->position              = 0;
    tokenizer->in_raw_text           = false;
    tokenizer->raw_text_end_tag_name = tbox_string_view_make(NULL, 0);
    tbox_vector_init(&tokenizer->pending_attributes, arena, sizeof(tbox_html_token_attribute), 0);
}

void tbox_html_tokenizer_enter_raw_text(tbox_html_tokenizer *tokenizer, tbox_string_view tag_name) {
    tokenizer->in_raw_text           = true;
    tokenizer->raw_text_end_tag_name = tag_name;
}

static void tbox_html_token_init_simple(tbox_html_token *out, tbox_html_token_type type, tbox_string_view text) {
    out->type            = type;
    out->text            = text;
    out->attributes      = NULL;
    out->attribute_count = 0;
    out->self_closing    = false;
}

static bool tbox_html_tokenizer_scan_text(tbox_html_tokenizer *t, tbox_html_token *out) {
    size_t start = t->position;
    while (t->position < t->length && t->input[t->position] != '<') {
        t->position++;
    }
    tbox_html_token_init_simple(out, TBOX_HTML_TOKEN_TEXT, tbox_string_view_make(t->input + start, t->position - start));
    return true;
}

/* Returns true if a raw-text closing sequence "</raw_text_end_tag_name"
 * (case-insensitive, followed by '>', '/', whitespace or end-of-input)
 * starts at t->input[idx]. */
static bool tbox_html_raw_text_close_at(const tbox_html_tokenizer *t, size_t idx, size_t *out_open_angle) {
    if (idx + 1 >= t->length || t->input[idx] != '<' || t->input[idx + 1] != '/') {
        return false;
    }

    size_t name_start = idx + 2;
    size_t name_len   = t->raw_text_end_tag_name.size;
    if (name_start + name_len > t->length) {
        return false;
    }

    tbox_string_view candidate = tbox_string_view_make(t->input + name_start, name_len);
    if (!tbox_string_view_equal_ascii_ci(candidate, t->raw_text_end_tag_name)) {
        return false;
    }

    size_t after = name_start + name_len;
    if (after < t->length) {
        char delimiter = t->input[after];
        if (!(delimiter == '>' || delimiter == '/' || tbox_html_is_space(delimiter))) {
            return false;
        }
    }

    *out_open_angle = idx;
    return true;
}

static bool tbox_html_tokenizer_next_impl(tbox_html_tokenizer *t, tbox_html_token *out);

static bool tbox_html_tokenizer_scan_raw_text(tbox_html_tokenizer *t, tbox_html_token *out) {
    size_t content_start = t->position;
    size_t i             = t->position;

    while (i < t->length) {
        size_t close_angle;
        if (t->input[i] == '<' && tbox_html_raw_text_close_at(t, i, &close_angle)) {
            t->in_raw_text = false;
            if (close_angle > content_start) {
                tbox_html_token_init_simple(out, TBOX_HTML_TOKEN_TEXT, tbox_string_view_make(t->input + content_start, close_angle - content_start));
                t->position = close_angle;
                return true;
            }
            t->position = close_angle;
            return tbox_html_tokenizer_next_impl(t, out);
        }
        i++;
    }

    t->position    = t->length;
    t->in_raw_text = false;
    if (t->length > content_start) {
        tbox_html_token_init_simple(out, TBOX_HTML_TOKEN_TEXT, tbox_string_view_make(t->input + content_start, t->length - content_start));
        return true;
    }
    tbox_html_token_init_simple(out, TBOX_HTML_TOKEN_EOF, tbox_string_view_make(NULL, 0));
    return true;
}

static size_t tbox_html_find_literal(const tbox_html_tokenizer *t, size_t start, const char *literal, size_t literal_len) {
    if (start + literal_len > t->length) {
        return t->length;
    }
    for (size_t i = start; i + literal_len <= t->length; i++) {
        bool matched = true;
        for (size_t j = 0; j < literal_len; j++) {
            if (t->input[i + j] != literal[j]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return i;
        }
    }
    return t->length;
}

static bool tbox_html_tokenizer_scan_declaration(tbox_html_tokenizer *t, tbox_html_token *out, size_t pos) {
    if (pos + 1 < t->length && t->input[pos] == '-' && t->input[pos + 1] == '-') {
        size_t content_start = pos + 2;
        size_t close         = tbox_html_find_literal(t, content_start, "-->", 3);
        tbox_html_token_init_simple(out, TBOX_HTML_TOKEN_COMMENT, tbox_string_view_make(t->input + content_start, close - content_start));
        t->position = close < t->length ? close + 3 : close;
        return true;
    }

    if (pos + 7 <= t->length && tbox_string_view_equal_ascii_ci(tbox_string_view_make(t->input + pos, 7), tbox_string_view_make("DOCTYPE", 7))) {
        pos += 7;
        while (pos < t->length && tbox_html_is_space(t->input[pos])) {
            pos++;
        }
        size_t name_start = pos;
        while (pos < t->length && !tbox_html_is_space(t->input[pos]) && t->input[pos] != '>') {
            pos++;
        }
        tbox_string_view name = tbox_string_view_make(t->input + name_start, pos - name_start);
        while (pos < t->length && t->input[pos] != '>') {
            pos++;
        }
        tbox_html_token_init_simple(out, TBOX_HTML_TOKEN_DOCTYPE, name);
        t->position = pos < t->length ? pos + 1 : pos;
        return true;
    }

    size_t content_start = pos;
    size_t close         = content_start;
    while (close < t->length && t->input[close] != '>') {
        close++;
    }
    tbox_html_token_init_simple(out, TBOX_HTML_TOKEN_COMMENT, tbox_string_view_make(t->input + content_start, close - content_start));
    t->position = close < t->length ? close + 1 : close;
    return true;
}

static bool tbox_html_tokenizer_scan_end_tag(tbox_html_tokenizer *t, tbox_html_token *out, size_t pos) {
    size_t name_start = pos;
    size_t name_end   = pos;
    while (name_end < t->length && !tbox_html_is_tag_name_end(t->input[name_end])) {
        name_end++;
    }
    tbox_string_view name = tbox_string_view_make(t->input + name_start, name_end - name_start);

    size_t close = name_end;
    while (close < t->length && t->input[close] != '>') {
        close++;
    }

    tbox_html_token_init_simple(out, TBOX_HTML_TOKEN_END_TAG, name);
    t->position = close < t->length ? close + 1 : close;
    return true;
}

static bool tbox_html_tokenizer_scan_start_tag(tbox_html_tokenizer *t, tbox_html_token *out, size_t pos) {
    size_t name_start = pos;
    while (pos < t->length && !tbox_html_is_tag_name_end(t->input[pos])) {
        pos++;
    }
    tbox_string_view tag_name = tbox_string_view_make(t->input + name_start, pos - name_start);

    t->pending_attributes.length = 0;
    bool self_closing            = false;

    for (;;) {
        while (pos < t->length && tbox_html_is_space(t->input[pos])) {
            pos++;
        }
        if (pos >= t->length) {
            break;
        }

        char c = t->input[pos];
        if (c == '>') {
            pos++;
            break;
        }
        if (c == '/') {
            pos++;
            if (pos < t->length && t->input[pos] == '>') {
                self_closing = true;
                pos++;
            }
            break;
        }

        size_t attr_name_start = pos;
        while (pos < t->length) {
            char ch = t->input[pos];
            if (tbox_html_is_space(ch) || ch == '=' || ch == '/' || ch == '>') {
                break;
            }
            pos++;
        }
        if (pos == attr_name_start) {
            pos++;
            continue;
        }
        tbox_string_view attr_name = tbox_string_view_make(t->input + attr_name_start, pos - attr_name_start);

        while (pos < t->length && tbox_html_is_space(t->input[pos])) {
            pos++;
        }

        tbox_string_view attr_value = tbox_string_view_make(NULL, 0);
        if (pos < t->length && t->input[pos] == '=') {
            pos++;
            while (pos < t->length && tbox_html_is_space(t->input[pos])) {
                pos++;
            }
            if (pos < t->length && (t->input[pos] == '"' || t->input[pos] == '\'')) {
                char quote = t->input[pos];
                pos++;
                size_t value_start = pos;
                while (pos < t->length && t->input[pos] != quote) {
                    pos++;
                }
                attr_value = tbox_string_view_make(t->input + value_start, pos - value_start);
                if (pos < t->length) {
                    pos++;
                }
            } else {
                size_t value_start = pos;
                while (pos < t->length && !tbox_html_is_space(t->input[pos]) && t->input[pos] != '>') {
                    pos++;
                }
                attr_value = tbox_string_view_make(t->input + value_start, pos - value_start);
            }
        }

        tbox_html_token_attribute *attribute = tbox_vector_push(&t->pending_attributes);
        attribute->name                      = attr_name;
        attribute->value                     = attr_value;
    }

    t->position          = pos;
    out->type            = TBOX_HTML_TOKEN_START_TAG;
    out->text            = tag_name;
    out->attributes      = (const tbox_html_token_attribute *)t->pending_attributes.data;
    out->attribute_count = t->pending_attributes.length;
    out->self_closing    = self_closing;
    return true;
}

static bool tbox_html_tokenizer_scan_tag_open(tbox_html_tokenizer *t, tbox_html_token *out) {
    size_t pos = t->position + 1;

    if (pos < t->length && t->input[pos] == '/') {
        return tbox_html_tokenizer_scan_end_tag(t, out, pos + 1);
    }
    if (pos < t->length && t->input[pos] == '!') {
        return tbox_html_tokenizer_scan_declaration(t, out, pos + 1);
    }
    if (pos < t->length && tbox_html_is_ascii_alpha(t->input[pos])) {
        return tbox_html_tokenizer_scan_start_tag(t, out, pos);
    }

    /* Bogus "<" (e.g. "< ", "<3", or a lone trailing "<"): treat as one byte
     * of literal text. */
    tbox_html_token_init_simple(out, TBOX_HTML_TOKEN_TEXT, tbox_string_view_make(t->input + t->position, 1));
    t->position += 1;
    return true;
}

static bool tbox_html_tokenizer_next_impl(tbox_html_tokenizer *t, tbox_html_token *out) {
    if (t->in_raw_text) {
        return tbox_html_tokenizer_scan_raw_text(t, out);
    }
    if (t->position >= t->length) {
        tbox_html_token_init_simple(out, TBOX_HTML_TOKEN_EOF, tbox_string_view_make(NULL, 0));
        return true;
    }
    if (t->input[t->position] == '<') {
        return tbox_html_tokenizer_scan_tag_open(t, out);
    }
    return tbox_html_tokenizer_scan_text(t, out);
}

bool tbox_html_tokenizer_next(tbox_html_tokenizer *tokenizer, tbox_html_token *out_token) {
    return tbox_html_tokenizer_next_impl(tokenizer, out_token);
}
