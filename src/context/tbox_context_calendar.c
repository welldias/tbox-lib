#include "tbox_context_internal.h"

typedef struct tbox_date_popup_geometry {
    tbox_rect rect, previous, next;
    tbox_rect time_bars[2], confirm;
} tbox_date_popup_geometry;

static bool tbox_context_date_popup_geometry(tbox_context *ctx, tbox_date_popup_geometry *out);

static bool tbox_context_date_attribute(const tbox_html_node *node, const char *name, tbox_date *out);
static bool tbox_context_month_attribute(const tbox_html_node *node, const char *name, tbox_date *out);
static bool tbox_context_time_attribute(const tbox_html_node *node, const char *name,
                                        int *hour, int *minute);
static bool tbox_context_time_allowed(const tbox_html_node *node, int hour, int minute);
static bool tbox_context_datetime_attribute(const tbox_html_node *node, const char *name,
                                             tbox_date *date, int *hour, int *minute);
static int tbox_context_datetime_compare(tbox_date a, int hour_a, int minute_a,
                                         tbox_date b, int hour_b, int minute_b);
static bool tbox_context_calendar_bound(const tbox_html_node *node, const char *name, tbox_date *date);
static bool tbox_context_date_allowed(const tbox_html_node *node, tbox_date date);
static bool tbox_context_datetime_allowed(const tbox_html_node *node, tbox_date date, int hour, int minute);
static tbox_date tbox_context_date_today(int *hour, int *minute);
static int tbox_context_date_weekday(int year, int month, int day);
static void tbox_context_week_from_date(tbox_date date, int *year, int *week);
static bool tbox_context_datetime_choose_time(tbox_context *ctx, double x, double y, bool *hit);

int tbox_context_date_days(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) return 29;
    return days[month - 1];
}

bool tbox_context_date_parse(tbox_string_view value, tbox_date *out) {
    if (value.size != 10 || value.data == NULL || value.data[4] != '-' || value.data[7] != '-') return false;
    static const unsigned positions[] = {0, 1, 2, 3, 5, 6, 8, 9};
    for (size_t i = 0; i < sizeof(positions) / sizeof(positions[0]); i++)
        if (value.data[positions[i]] < '0' || value.data[positions[i]] > '9') return false;
    int year = (value.data[0] - '0') * 1000 + (value.data[1] - '0') * 100 +
        (value.data[2] - '0') * 10 + value.data[3] - '0';
    int month = (value.data[5] - '0') * 10 + value.data[6] - '0';
    int day = (value.data[8] - '0') * 10 + value.data[9] - '0';
    if (year < 1 || day < 1 || day > tbox_context_date_days(year, month)) return false;
    *out = (tbox_date){year, month, day};
    return true;
}

static bool tbox_context_date_attribute(const tbox_html_node *node, const char *name, tbox_date *out) {
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr(name));
    return attribute != NULL && tbox_context_date_parse(attribute->value, out);
}

bool tbox_context_month_parse(tbox_string_view value, tbox_date *out) {
    if (value.size != 7 || value.data == NULL || value.data[4] != '-') return false;
    static const unsigned positions[] = {0, 1, 2, 3, 5, 6};
    for (size_t i = 0; i < sizeof(positions) / sizeof(positions[0]); i++)
        if (value.data[positions[i]] < '0' || value.data[positions[i]] > '9') return false;
    int year = (value.data[0] - '0') * 1000 + (value.data[1] - '0') * 100 +
        (value.data[2] - '0') * 10 + value.data[3] - '0';
    int month = (value.data[5] - '0') * 10 + value.data[6] - '0';
    if (year < 1 || month < 1 || month > 12) return false;
    *out = (tbox_date){year, month, 1};
    return true;
}

static bool tbox_context_month_attribute(const tbox_html_node *node, const char *name, tbox_date *out) {
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr(name));
    return attribute != NULL && tbox_context_month_parse(attribute->value, out);
}

bool tbox_context_datetime_parse(tbox_string_view value, tbox_date *date, int *hour, int *minute) {
    if (value.data == NULL || value.size != 16 || value.data[10] != 'T' || value.data[13] != ':' ||
        !tbox_context_date_parse(tbox_string_view_make(value.data, 10), date)) return false;
    static const unsigned positions[] = {11, 12, 14, 15};
    for (size_t i = 0; i < sizeof(positions) / sizeof(positions[0]); i++)
        if (value.data[positions[i]] < '0' || value.data[positions[i]] > '9') return false;
    int parsed_hour = (value.data[11] - '0') * 10 + value.data[12] - '0';
    int parsed_minute = (value.data[14] - '0') * 10 + value.data[15] - '0';
    if (parsed_hour > 23 || parsed_minute > 59) return false;
    *hour = parsed_hour;
    *minute = parsed_minute;
    return true;
}

bool tbox_context_time_parse(tbox_string_view value, int *hour, int *minute) {
    if (value.data == NULL || value.size != 5 || value.data[2] != ':') return false;
    for (size_t i = 0; i < value.size; i++)
        if (i != 2 && (value.data[i] < '0' || value.data[i] > '9')) return false;
    int h = (value.data[0] - '0') * 10 + value.data[1] - '0';
    int m = (value.data[3] - '0') * 10 + value.data[4] - '0';
    if (h > 23 || m > 59) return false;
    *hour = h;
    *minute = m;
    return true;
}

static bool tbox_context_time_attribute(const tbox_html_node *node, const char *name,
                                        int *hour, int *minute) {
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr(name));
    return attribute != NULL && tbox_context_time_parse(attribute->value, hour, minute);
}

static bool tbox_context_time_allowed(const tbox_html_node *node, int hour, int minute) {
    int bound_hour, bound_minute, value = hour * 60 + minute;
    if (tbox_context_time_attribute(node, "min", &bound_hour, &bound_minute) &&
        value < bound_hour * 60 + bound_minute) return false;
    if (tbox_context_time_attribute(node, "max", &bound_hour, &bound_minute) &&
        value > bound_hour * 60 + bound_minute) return false;
    return true;
}

static bool tbox_context_datetime_attribute(const tbox_html_node *node, const char *name,
                                             tbox_date *date, int *hour, int *minute) {
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr(name));
    return attribute != NULL && tbox_context_datetime_parse(attribute->value, date, hour, minute);
}

int tbox_context_date_compare(tbox_date a, tbox_date b) {
    if (a.year != b.year) return a.year < b.year ? -1 : 1;
    if (a.month != b.month) return a.month < b.month ? -1 : 1;
    if (a.day != b.day) return a.day < b.day ? -1 : 1;
    return 0;
}

static int tbox_context_datetime_compare(tbox_date a, int hour_a, int minute_a,
                                         tbox_date b, int hour_b, int minute_b) {
    int date_order = tbox_context_date_compare(a, b);
    if (date_order != 0) return date_order;
    if (hour_a != hour_b) return hour_a < hour_b ? -1 : 1;
    if (minute_a != minute_b) return minute_a < minute_b ? -1 : 1;
    return 0;
}

static bool tbox_context_calendar_bound(const tbox_html_node *node, const char *name, tbox_date *date) {
    if (tbox_context_is_datetime_input(node)) {
        int hour, minute;
        return tbox_context_datetime_attribute(node, name, date, &hour, &minute);
    }
    if (tbox_context_is_month_input(node)) return tbox_context_month_attribute(node, name, date);
    if (tbox_context_is_week_input(node)) {
        const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr(name));
        return attribute != NULL && tbox_context_week_parse(attribute->value, date);
    }
    return tbox_context_date_attribute(node, name, date);
}

static bool tbox_context_date_allowed(const tbox_html_node *node, tbox_date date) {
    tbox_date limit;
    return !(tbox_context_calendar_bound(node, "min", &limit) && tbox_context_date_compare(date, limit) < 0) &&
           !(tbox_context_calendar_bound(node, "max", &limit) && tbox_context_date_compare(date, limit) > 0);
}

static bool tbox_context_datetime_allowed(const tbox_html_node *node, tbox_date date, int hour, int minute) {
    tbox_date limit;
    int limit_hour, limit_minute;
    if (tbox_context_datetime_attribute(node, "min", &limit, &limit_hour, &limit_minute) &&
        tbox_context_datetime_compare(date, hour, minute, limit, limit_hour, limit_minute) < 0) return false;
    if (tbox_context_datetime_attribute(node, "max", &limit, &limit_hour, &limit_minute) &&
        tbox_context_datetime_compare(date, hour, minute, limit, limit_hour, limit_minute) > 0) return false;
    return true;
}

bool tbox_context_calendar_selection_allowed(tbox_context *ctx) {
    if (tbox_context_is_time_input(ctx->open_date))
        return tbox_context_time_allowed(ctx->open_date, ctx->date_hour, ctx->date_minute);
    return tbox_context_is_datetime_input(ctx->open_date) ?
        tbox_context_datetime_allowed(ctx->open_date, ctx->date_cursor, ctx->date_hour, ctx->date_minute) :
        tbox_context_date_allowed(ctx->open_date, ctx->date_cursor);
}

static tbox_date tbox_context_date_today(int *hour, int *minute) {
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    if (local == NULL) {
        *hour = 0;
        *minute = 0;
        return (tbox_date){2000, 1, 1};
    }
    *hour = local->tm_hour;
    *minute = local->tm_min;
    return (tbox_date){local->tm_year + 1900, local->tm_mon + 1, local->tm_mday};
}

void tbox_context_date_open(tbox_context *ctx, const tbox_html_node *node) {
    ctx->open_date = node;
    bool datetime = tbox_context_is_datetime_input(node);
    bool month = tbox_context_is_month_input(node);
    if (tbox_context_is_time_input(node)) {
        ctx->date_cursor = tbox_context_date_today(&ctx->date_hour, &ctx->date_minute);
        if (!tbox_context_time_attribute(node, "value", &ctx->date_hour, &ctx->date_minute)) {
            int hour, minute;
            if (tbox_context_time_attribute(node, "min", &hour, &minute) &&
                ctx->date_hour * 60 + ctx->date_minute < hour * 60 + minute) {
                ctx->date_hour = hour;
                ctx->date_minute = minute;
            }
            if (tbox_context_time_attribute(node, "max", &hour, &minute) &&
                ctx->date_hour * 60 + ctx->date_minute > hour * 60 + minute) {
                ctx->date_hour = hour;
                ctx->date_minute = minute;
            }
        }
        return;
    }
    if (tbox_context_is_week_input(node)) {
        const tbox_html_attribute *value = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
        if (value == NULL || !tbox_context_week_parse(value->value, &ctx->date_cursor)) {
            ctx->date_cursor = tbox_context_date_today(&ctx->date_hour, &ctx->date_minute);
            tbox_context_week_monday(&ctx->date_cursor);
            tbox_date limit;
            if (tbox_context_calendar_bound(node, "min", &limit) &&
                tbox_context_date_compare(ctx->date_cursor, limit) < 0) ctx->date_cursor = limit;
            if (tbox_context_calendar_bound(node, "max", &limit) &&
                tbox_context_date_compare(ctx->date_cursor, limit) > 0) ctx->date_cursor = limit;
        }
        return;
    }
    bool valid = datetime ? tbox_context_datetime_attribute(node, "value", &ctx->date_cursor,
        &ctx->date_hour, &ctx->date_minute) : month ?
        tbox_context_month_attribute(node, "value", &ctx->date_cursor) :
        tbox_context_date_attribute(node, "value", &ctx->date_cursor);
    if (!valid) {
        ctx->date_cursor = tbox_context_date_today(&ctx->date_hour, &ctx->date_minute);
        if (month) ctx->date_cursor.day = 1;
        tbox_date limit;
        int hour, minute;
        if (datetime) {
            if (tbox_context_datetime_attribute(node, "min", &limit, &hour, &minute) &&
                tbox_context_datetime_compare(ctx->date_cursor, ctx->date_hour, ctx->date_minute,
                    limit, hour, minute) < 0) {
                ctx->date_cursor = limit;
                ctx->date_hour = hour;
                ctx->date_minute = minute;
            }
            if (tbox_context_datetime_attribute(node, "max", &limit, &hour, &minute) &&
                tbox_context_datetime_compare(ctx->date_cursor, ctx->date_hour, ctx->date_minute,
                    limit, hour, minute) > 0) {
                ctx->date_cursor = limit;
                ctx->date_hour = hour;
                ctx->date_minute = minute;
            }
        } else {
            if (tbox_context_calendar_bound(node, "min", &limit) &&
                tbox_context_date_compare(ctx->date_cursor, limit) < 0) ctx->date_cursor = limit;
            if (tbox_context_calendar_bound(node, "max", &limit) &&
                tbox_context_date_compare(ctx->date_cursor, limit) > 0) ctx->date_cursor = limit;
        }
    }
    if (month) ctx->date_cursor.day = 1;
}

bool tbox_context_date_commit(tbox_context *ctx, const tbox_html_node *node, tbox_date date) {
    if (tbox_context_is_time_input(node)) {
        if (!tbox_context_time_allowed(node, ctx->date_hour, ctx->date_minute)) return false;
        int old_hour, old_minute;
        if (tbox_context_time_attribute(node, "value", &old_hour, &old_minute) &&
            old_hour == ctx->date_hour && old_minute == ctx->date_minute) return false;
        char value[6];
        snprintf(value, sizeof(value), "%02d:%02d", ctx->date_hour, ctx->date_minute);
        tbox_html_node_set_attribute(ctx->document, (tbox_html_node *)node,
            tbox_string_view_make("value", 5), tbox_string_view_make(value, 5));
        if (ctx->input_handler != NULL) {
            const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
            if (attribute != NULL) ctx->input_handler(ctx, (tbox_html_node *)node, attribute->value, ctx->input_userdata);
        }
        return true;
    }
    if (tbox_context_is_week_input(node)) {
        if (!tbox_context_week_monday(&date) || !tbox_context_date_allowed(node, date)) return false;
        int year, week;
        tbox_context_week_from_date(date, &year, &week);
        char value[9];
        snprintf(value, sizeof(value), "%04d-W%02d", year, week);
        const tbox_html_attribute *old = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
        if (old != NULL && tbox_string_view_equal(old->value, tbox_string_view_make(value, 8))) return false;
        tbox_html_node_set_attribute(ctx->document, (tbox_html_node *)node,
            tbox_string_view_make("value", 5), tbox_string_view_make(value, 8));
        if (ctx->input_handler != NULL) {
            const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
            if (attribute != NULL) ctx->input_handler(ctx, (tbox_html_node *)node, attribute->value, ctx->input_userdata);
        }
        return true;
    }
    bool datetime = tbox_context_is_datetime_input(node);
    bool month = tbox_context_is_month_input(node);
    if (datetime ? !tbox_context_datetime_allowed(node, date, ctx->date_hour, ctx->date_minute) :
        !tbox_context_date_allowed(node, date)) return false;
    tbox_date previous;
    int previous_hour, previous_minute;
    if ((datetime ? tbox_context_datetime_attribute(node, "value", &previous,
                      &previous_hour, &previous_minute) : month ?
                      tbox_context_month_attribute(node, "value", &previous) :
                      tbox_context_date_attribute(node, "value", &previous)) &&
        tbox_context_date_compare(previous, date) == 0 &&
        (!datetime || (previous_hour == ctx->date_hour && previous_minute == ctx->date_minute))) return false;
    char value[17];
    if (datetime)
        snprintf(value, sizeof(value), "%04d-%02d-%02dT%02d:%02d", date.year, date.month, date.day,
            ctx->date_hour, ctx->date_minute);
    else if (month) snprintf(value, sizeof(value), "%04d-%02d", date.year, date.month);
    else snprintf(value, sizeof(value), "%04d-%02d-%02d", date.year, date.month, date.day);
    tbox_html_node_set_attribute(ctx->document, (tbox_html_node *)node,
        tbox_string_view_make("value", 5), tbox_string_view_make(value, datetime ? 16 : month ? 7 : 10));
    if (ctx->input_handler != NULL) {
        const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
        if (attribute != NULL) ctx->input_handler(ctx, (tbox_html_node *)node, attribute->value, ctx->input_userdata);
    }
    return true;
}

static int tbox_context_date_weekday(int year, int month, int day) {
    static const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) year--;
    return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
}

static bool tbox_context_date_popup_geometry(tbox_context *ctx, tbox_date_popup_geometry *out) {
    if (ctx->open_date == NULL) return false;
    const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->open_date);
    if (box == NULL) return false;
    const double width = 238.0;
    const double height = tbox_context_is_datetime_input(ctx->open_date) ? 306.0 :
        tbox_context_is_month_input(ctx->open_date) ? 166.0 :
        tbox_context_is_time_input(ctx->open_date) ? 126.0 : 212.0;
    double x = box->border_box.x;
    if (x + width > ctx->viewport_width) x = ctx->viewport_width - width;
    if (x < 0.0) x = 0.0;
    double below = ctx->viewport_height - (box->border_box.y + box->border_box.height);
    double above = box->border_box.y;
    double y = below < height && above > below ? box->border_box.y - height :
        box->border_box.y + box->border_box.height;
    out->rect = (tbox_rect){x, y, width, height};
    out->previous = (tbox_rect){x + 8.0, y + 7.0, 24.0, 24.0};
    out->next = (tbox_rect){x + width - 32.0, y + 7.0, 24.0, 24.0};
    double time_top = tbox_context_is_time_input(ctx->open_date) ? 40.0 : 222.0;
    out->time_bars[0] = (tbox_rect){x + 40.0, y + time_top, 140.0, 12.0};
    out->time_bars[1] = (tbox_rect){x + 40.0, y + time_top + 26.0, 140.0, 12.0};
    out->confirm = (tbox_rect){x + width - 65.0, y + time_top + 50.0, 55.0, 25.0};
    return true;
}

bool tbox_context_date_popup_visible(tbox_context *ctx) {
    tbox_date_popup_geometry popup;
    return tbox_context_date_popup_geometry(ctx, &popup);
}

bool tbox_context_date_popup_contains(tbox_context *ctx, double x, double y) {
    tbox_date_popup_geometry popup;
    return tbox_context_date_popup_geometry(ctx, &popup) &&
        tbox_context_point_in_rect(popup.rect, x, y);
}

bool tbox_context_date_shift_month(tbox_date *date, int direction) {
    int year = date->year, month = date->month + direction;
    if (month < 1) { year--; month = 12; }
    if (month > 12) { year++; month = 1; }
    if (year < 1 || year > 9999) return false;
    int day = date->day;
    int last = tbox_context_date_days(year, month);
    if (day > last) day = last;
    *date = (tbox_date){year, month, day};
    return true;
}

bool tbox_context_date_shift_year(tbox_date *date, int direction) {
    int year = date->year + direction;
    if (year < 1 || year > 9999) return false;
    date->year = year;
    if (date->day > tbox_context_date_days(year, date->month))
        date->day = tbox_context_date_days(year, date->month);
    return true;
}

bool tbox_context_date_shift_day(tbox_date *date, int delta) {
    tbox_date next = *date;
    while (delta > 0) {
        if (next.day == tbox_context_date_days(next.year, next.month)) {
            if (next.year == 9999 && next.month == 12) return false;
            next.day = 1;
            next.month++;
            if (next.month == 13) { next.month = 1; next.year++; }
        } else next.day++;
        delta--;
    }
    while (delta < 0) {
        if (next.day == 1) {
            if (next.year == 1 && next.month == 1) return false;
            next.month--;
            if (next.month == 0) { next.month = 12; next.year--; }
            next.day = tbox_context_date_days(next.year, next.month);
        } else next.day--;
        delta++;
    }
    *date = next;
    return true;
}

bool tbox_context_week_monday(tbox_date *date) {
    int weekday = tbox_context_date_weekday(date->year, date->month, date->day);
    return tbox_context_date_shift_day(date, -((weekday + 6) % 7));
}

static void tbox_context_week_from_date(tbox_date date, int *year, int *week) {
    int weekday = (tbox_context_date_weekday(date.year, date.month, date.day) + 6) % 7;
    tbox_context_date_shift_day(&date, 3 - weekday);
    int ordinal = date.day;
    for (int month = 1; month < date.month; month++) ordinal += tbox_context_date_days(date.year, month);
    *year = date.year;
    *week = (ordinal - 1) / 7 + 1;
}

bool tbox_context_week_parse(tbox_string_view value, tbox_date *out) {
    if (value.data == NULL || value.size != 8 || value.data[4] != '-' || value.data[5] != 'W') return false;
    static const unsigned positions[] = {0, 1, 2, 3, 6, 7};
    for (size_t i = 0; i < sizeof(positions) / sizeof(positions[0]); i++)
        if (value.data[positions[i]] < '0' || value.data[positions[i]] > '9') return false;
    int year = (value.data[0] - '0') * 1000 + (value.data[1] - '0') * 100 +
        (value.data[2] - '0') * 10 + value.data[3] - '0';
    int week = (value.data[6] - '0') * 10 + value.data[7] - '0';
    if (year < 1 || week < 1) return false;
    int final_year, final_week;
    tbox_context_week_from_date((tbox_date){year, 12, 28}, &final_year, &final_week);
    if (week > final_week) return false;
    tbox_date monday = {year, 1, 4};
    if (!tbox_context_week_monday(&monday) ||
        !tbox_context_date_shift_day(&monday, (week - 1) * 7)) return false;
    *out = monday;
    return true;
}

static bool tbox_context_datetime_choose_time(tbox_context *ctx, double x, double y, bool *hit) {
    *hit = false;
    if (ctx == NULL || (!tbox_context_is_datetime_input(ctx->open_date) &&
        !tbox_context_is_time_input(ctx->open_date))) return false;
    tbox_date_popup_geometry popup;
    if (!tbox_context_date_popup_geometry(ctx, &popup)) return false;
    for (int part = 0; part < 2; part++) {
        tbox_rect bar = popup.time_bars[part];
        if (y < bar.y - 6.0 || y >= bar.y + bar.height + 6.0) continue;
        *hit = true;
        double position = x < bar.x ? bar.x : x > bar.x + bar.width ? bar.x + bar.width : x;
        int maximum = part == 0 ? 23 : 59;
        int value = (int)((position - bar.x) * maximum / bar.width + 0.5);
        int *current = part == 0 ? &ctx->date_hour : &ctx->date_minute;
        bool changed = *current != value;
        *current = value;
        return changed;
    }
    return false;
}

bool tbox_context_date_popup_click(tbox_context *ctx, double x, double y) {
    tbox_date_popup_geometry popup;
    if (!tbox_context_date_popup_geometry(ctx, &popup) ||
        !tbox_context_point_in_rect(popup.rect, x, y)) return false;
    if (tbox_context_is_time_input(ctx->open_date)) {
        if (tbox_context_point_in_rect(popup.confirm, x, y) &&
            tbox_context_calendar_selection_allowed(ctx)) {
            const tbox_html_node *node = ctx->open_date;
            ctx->open_date = NULL;
            tbox_context_date_commit(ctx, node, ctx->date_cursor);
            return true;
        }
        bool hit = false;
        tbox_context_datetime_choose_time(ctx, x, y, &hit);
        return true;
    }
    if (tbox_context_point_in_rect(popup.previous, x, y)) {
        if (tbox_context_is_month_input(ctx->open_date))
            tbox_context_date_shift_year(&ctx->date_cursor, -1);
        else {
            tbox_context_date_shift_month(&ctx->date_cursor, -1);
            if (tbox_context_is_week_input(ctx->open_date)) tbox_context_week_monday(&ctx->date_cursor);
        }
        return true;
    }
    if (tbox_context_point_in_rect(popup.next, x, y)) {
        if (tbox_context_is_month_input(ctx->open_date))
            tbox_context_date_shift_year(&ctx->date_cursor, 1);
        else {
            tbox_context_date_shift_month(&ctx->date_cursor, 1);
            if (tbox_context_is_week_input(ctx->open_date)) tbox_context_week_monday(&ctx->date_cursor);
        }
        return true;
    }
    if (tbox_context_is_month_input(ctx->open_date)) {
        double relative_x = x - popup.rect.x - 12.0;
        double relative_y = y - popup.rect.y - 40.0;
        if (relative_x >= 0.0 && relative_x < 216.0 && relative_y >= 0.0 && relative_y < 114.0) {
            int column = (int)(relative_x / 54.0);
            int row = (int)(relative_y / 38.0);
            tbox_date selected = {ctx->date_cursor.year, row * 4 + column + 1, 1};
            if (tbox_context_date_allowed(ctx->open_date, selected)) {
                const tbox_html_node *node = ctx->open_date;
                ctx->date_cursor = selected;
                ctx->open_date = NULL;
                tbox_context_date_commit(ctx, node, selected);
            }
        }
        return true;
    }
    bool datetime = tbox_context_is_datetime_input(ctx->open_date);
    if (datetime) {
        if (tbox_context_point_in_rect(popup.confirm, x, y)) {
            if (tbox_context_calendar_selection_allowed(ctx)) {
                const tbox_html_node *node = ctx->open_date;
                ctx->open_date = NULL;
                tbox_context_date_commit(ctx, node, ctx->date_cursor);
            }
            return true;
        }
        bool hit = false;
        tbox_context_datetime_choose_time(ctx, x, y, &hit);
        if (hit) return true;
    }
    double relative_x = x - popup.rect.x - 14.0;
    double relative_y = y - popup.rect.y - 58.0;
    if (relative_x >= 0.0 && relative_x < 210.0 && relative_y >= 0.0 && relative_y < 144.0) {
        int column = (int)(relative_x / 30.0);
        int row = (int)(relative_y / 24.0);
        int first = tbox_context_date_weekday(ctx->date_cursor.year, ctx->date_cursor.month, 1);
        int day = row * 7 + column - first + 1;
        if (day >= 1 && day <= tbox_context_date_days(ctx->date_cursor.year, ctx->date_cursor.month)) {
            tbox_date selected = {ctx->date_cursor.year, ctx->date_cursor.month, day};
            if (tbox_context_is_week_input(ctx->open_date)) tbox_context_week_monday(&selected);
            if (tbox_context_date_allowed(ctx->open_date, selected)) {
                ctx->date_cursor = selected;
                if (!datetime) {
                    const tbox_html_node *node = ctx->open_date;
                    ctx->open_date = NULL;
                    tbox_context_date_commit(ctx, node, selected);
                }
            }
        }
    }
    return true;
}

void tbox_context_date_text(tbox_vector *items, const tbox_font_face *face,
                                   tbox_rect rect, tbox_string_view value, tbox_css_rgba color) {
    if (face == NULL || value.size == 0) return;
    double width = tbox_font_measure_text(face, value);
    tbox_paint_op *op = tbox_vector_push(items);
    *op = (tbox_paint_op){.kind = TBOX_PAINT_TEXT_RUN,
        .rect = {rect.x + (rect.width - width) / 2.0,
                 rect.y + (rect.height - tbox_font_face_line_height(face)) / 2.0,
                 width, tbox_font_face_line_height(face)},
        .color = color, .text = value, .face = face};
}

void tbox_context_paint_date_popup(tbox_context *ctx, tbox_vector *items) {
    tbox_date_popup_geometry popup;
    if (!tbox_context_date_popup_geometry(ctx, &popup)) return;
    const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->open_date);
    const tbox_font_face *face = box != NULL && box->style != NULL ?
        tbox_font_face_cache_get(ctx->fonts, tbox_string_view_from_cstr(box->style->font_family),
            false, false, 13.0) : NULL;
    tbox_context_push_fill(items, popup.rect, (tbox_css_rgba){105, 112, 122, 255}, false, (tbox_rect){0});
    tbox_context_push_fill(items, (tbox_rect){popup.rect.x + 1.0, popup.rect.y + 1.0,
        popup.rect.width - 2.0, popup.rect.height - 2.0},
        (tbox_css_rgba){255, 255, 255, 255}, false, (tbox_rect){0});
    char *heading = tbox_arena_alloc(&ctx->frame_arena, 16);
    if (heading != NULL) {
        int length = tbox_context_is_time_input(ctx->open_date) ?
            snprintf(heading, 16, "%02d:%02d", ctx->date_hour, ctx->date_minute) :
            tbox_context_is_month_input(ctx->open_date) ?
            snprintf(heading, 16, "%04d", ctx->date_cursor.year) :
            snprintf(heading, 16, "%04d-%02d", ctx->date_cursor.year, ctx->date_cursor.month);
        if (length > 0 && length < 16)
            tbox_context_date_text(items, face,
                (tbox_rect){popup.rect.x + 34.0, popup.rect.y + 6.0, popup.rect.width - 68.0, 26.0},
                tbox_string_view_make(heading, (size_t)length), (tbox_css_rgba){25, 25, 25, 255});
    }
    if (!tbox_context_is_time_input(ctx->open_date)) {
        tbox_context_date_text(items, face, popup.previous, tbox_string_view_make("<", 1),
            (tbox_css_rgba){25, 25, 25, 255});
        tbox_context_date_text(items, face, popup.next, tbox_string_view_make(">", 1),
            (tbox_css_rgba){25, 25, 25, 255});
    }
    if (tbox_context_is_month_input(ctx->open_date)) {
        static const char *names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        for (int month = 1; month <= 12; month++) {
            tbox_rect cell = {popup.rect.x + 12.0 + 54.0 * ((month - 1) % 4),
                              popup.rect.y + 40.0 + 38.0 * ((month - 1) / 4), 54.0, 38.0};
            tbox_date date = {ctx->date_cursor.year, month, 1};
            bool allowed = tbox_context_date_allowed(ctx->open_date, date);
            bool highlighted = month == ctx->date_cursor.month && allowed;
            if (highlighted)
                tbox_context_push_fill(items,
                    (tbox_rect){cell.x + 2.0, cell.y + 2.0, cell.width - 4.0, cell.height - 4.0},
                    (tbox_css_rgba){65, 115, 195, 255}, false, (tbox_rect){0});
            tbox_context_date_text(items, face, cell, tbox_string_view_from_cstr(names[month - 1]),
                !allowed ? (tbox_css_rgba){170, 170, 170, 255} :
                highlighted ? (tbox_css_rgba){255, 255, 255, 255} :
                (tbox_css_rgba){25, 25, 25, 255});
        }
        return;
    }
    if (!tbox_context_is_time_input(ctx->open_date)) {
    static const char *weekdays[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    for (int column = 0; column < 7; column++)
        tbox_context_date_text(items, face,
            (tbox_rect){popup.rect.x + 14.0 + 30.0 * column, popup.rect.y + 34.0, 30.0, 22.0},
            tbox_string_view_from_cstr(weekdays[column]), (tbox_css_rgba){90, 95, 105, 255});
    int first = tbox_context_date_weekday(ctx->date_cursor.year, ctx->date_cursor.month, 1);
    int days = tbox_context_date_days(ctx->date_cursor.year, ctx->date_cursor.month);
    for (int day = 1; day <= days; day++) {
        int cell = first + day - 1;
        tbox_rect rect = {popup.rect.x + 14.0 + 30.0 * (cell % 7),
                          popup.rect.y + 58.0 + 24.0 * (cell / 7), 30.0, 24.0};
        tbox_date date = {ctx->date_cursor.year, ctx->date_cursor.month, day};
        if (tbox_context_is_week_input(ctx->open_date)) tbox_context_week_monday(&date);
        bool highlighted = tbox_context_date_compare(date, ctx->date_cursor) == 0;
        bool allowed = tbox_context_date_allowed(ctx->open_date, date);
        if (highlighted && allowed)
            tbox_context_push_fill(items, (tbox_rect){rect.x + 2.0, rect.y + 1.0, 26.0, 22.0},
                (tbox_css_rgba){65, 115, 195, 255}, false, (tbox_rect){0});
        char *label = tbox_arena_alloc(&ctx->frame_arena, 3);
        if (label != NULL) {
            int length = snprintf(label, 3, "%d", day);
            if (length > 0 && length < 3)
                tbox_context_date_text(items, face, rect, tbox_string_view_make(label, (size_t)length),
                    !allowed ? (tbox_css_rgba){170, 170, 170, 255} :
                    highlighted ? (tbox_css_rgba){255, 255, 255, 255} :
                    (tbox_css_rgba){25, 25, 25, 255});
        }
    }
    }
    if (tbox_context_is_datetime_input(ctx->open_date) || tbox_context_is_time_input(ctx->open_date)) {
        static const char *labels[] = {"H", "M"};
        for (int part = 0; part < 2; part++) {
            tbox_rect bar = popup.time_bars[part];
            int value = part == 0 ? ctx->date_hour : ctx->date_minute;
            int maximum = part == 0 ? 23 : 59;
            tbox_context_date_text(items, face,
                (tbox_rect){bar.x - 29.0, bar.y - 5.0, 20.0, 22.0},
                tbox_string_view_from_cstr(labels[part]), (tbox_css_rgba){25, 25, 25, 255});
            tbox_context_push_fill(items, bar, (tbox_css_rgba){220, 225, 232, 255}, false, (tbox_rect){0});
            double filled = bar.width * value / maximum;
            if (filled > 0.0)
                tbox_context_push_fill(items, (tbox_rect){bar.x, bar.y, filled, bar.height},
                    (tbox_css_rgba){65, 115, 195, 255}, false, (tbox_rect){0});
            char *digits = tbox_arena_alloc(&ctx->frame_arena, 3);
            if (digits != NULL) {
                int length = snprintf(digits, 3, "%02d", value);
                if (length == 2)
                    tbox_context_date_text(items, face,
                        (tbox_rect){bar.x + bar.width + 9.0, bar.y - 5.0, 30.0, 22.0},
                        tbox_string_view_make(digits, 2), (tbox_css_rgba){25, 25, 25, 255});
            }
        }
        bool enabled = tbox_context_calendar_selection_allowed(ctx);
        tbox_context_push_fill(items, popup.confirm,
            enabled ? (tbox_css_rgba){65, 115, 195, 255} : (tbox_css_rgba){180, 185, 193, 255},
            false, (tbox_rect){0});
        tbox_context_date_text(items, face, popup.confirm, tbox_string_view_make("OK", 2),
            (tbox_css_rgba){255, 255, 255, 255});
    }
}

bool tbox_context_datetime_drag(tbox_context *ctx, double x, double y) {
    bool hit;
    return tbox_context_datetime_choose_time(ctx, x, y, &hit);
}
