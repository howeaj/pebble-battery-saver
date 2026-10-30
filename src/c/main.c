// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/* TODO
    Estimate battery remaining
*/

#include <pebble.h>

#include "config.h"
#include "debug.h"
#include "macros.h"
#include "misc.h"

// Note no existing platform's display is column-major
#if DISPLAY_ORIENTATION_COLUMN_MAJOR_INVERTED
    #error "This platform's display is column-major, so progress bars should be vertical!"
#endif

void mark_root_window_dirty(bool full_redraw);


/******************************************************************************
 This watchface implementation; handlers
******************************************************************************/

#define BAT_ENABLED (0)

static GFont s_bar_label_font;
static GFont s_bar_title_font;
static GBitmap* s_twelve;
#if BAT_ENABLED
static GBitmap* s_bat;
#endif // BAT_ENABLED

static bool s_is_full_redraw = false;

struct tm s_current_time;


#if CONFIGURABLE
static void handle_connection(bool is_connected) {
    LOG("is_connected changed to %s", BOOL_TO_STR(is_connected));
    mark_root_window_dirty(false);  // TODO could avoid a full redraw (probably not worth it)
}

static void draw_disconnect_status(const GRect bounds, GContext* ctx) {
    UNUSED(bounds);
    if (!connection_service_peek_pebble_app_connection() || FORCE_BLUETOOTH_DISCONNECT) {
    }
}

static void draw_quiet_time_status(const GRect bounds, GContext* ctx) {
    UNUSED(bounds);
    if (quiet_time_is_active() || FORCE_QUIET_TIME) {
    }
}
#endif // CONFIGURABLE

typedef struct ProgressBar {
    uint32_t y;  // origin Y coordinate
    uint32_t x_nudge;  // nudge from centred
    uint32_t max;  // maximum value
    uint32_t w;  // maximum value
    uint32_t thickness;  // line thickness
    bool extend; // Extend the bar beyond the last value, to mirror the first value
    bool alternate_labels; // Labels alternate above & below the bar

    uint32_t phase;  // 0 or 1; which colour is currently increasing
    uint32_t current; // current value
} ProgressBar;


#define PBL_DISPLAY_SMALL (PBL_DISPLAY_WIDTH == 144)
#if PBL_DISPLAY_WIDTH == 144
    #define PBL_DISPLAY_WIDTH_SWITCH(classic, chalk, emery, gabbro) (classic)
    #define PBL_DISPLAY_SMALL_IF_ELSE(small, wide) (small)
#elif PBL_DISPLAY_WIDTH == 180
    #define PBL_DISPLAY_WIDTH_SWITCH(classic, chalk, emery, gabbro) (chalk)
    #define PBL_DISPLAY_SMALL_IF_ELSE(small, wide) (small)
#elif PBL_DISPLAY_WIDTH == 200
    #define PBL_DISPLAY_WIDTH_SWITCH(classic, chalk, emery, gabbro) (emery)
    #define PBL_DISPLAY_SMALL_IF_ELSE(small, wide) (wide)
#elif PBL_DISPLAY_WIDTH == 260
    #define PBL_DISPLAY_WIDTH_SWITCH(classic, chalk, emery, gabbro) (gabbro)
    #define PBL_DISPLAY_SMALL_IF_ELSE(small, wide) (wide)
#else
    #error "Unsupported PBL_DISPLAY_WIDTH"
#endif

// We want to avoid the timeline peek without ever redrawing
// So calculate where timeline peek *could* cover,
// copying definitions from peek.h
#if PBL_ROUND
    #define TIMELINE_PEEK_ORIGIN_Y_VISIBLE ((PBL_DISPLAY_HEIGHT * 112) / 180)
#else // !PBL_ROUND
    #define MAX_TIMELINE_PEEK_HEIGHT (59)
    #define TIMELINE_PEEK_ORIGIN_Y_VISIBLE (PBL_DISPLAY_HEIGHT - MAX_TIMELINE_PEEK_HEIGHT)
#endif // !PBL_ROUND
#define UNOBSTRUCTED_HEIGHT TIMELINE_PEEK_ORIGIN_Y_VISIBLE

// Font metrics
#define LABEL_FONT FONT_KEY_LECO_20_BOLD_NUMBERS
#define LABEL_FONT_Y_PAD (6)  // blank pixels above chars
#define LABEL_FONT_HEIGHT (14)  // pixel height of chars
#define LABEL_FONT_WIDTH (10)  // pixel width of one char

#define BAR_OUTLINES (true)
#if BAR_OUTLINES
    #define PBL_BAR_OUTLINE_IF_ELSE(with, without) (with)
#else
    #define PBL_BAR_OUTLINE_IF_ELSE(with, without) (without)
#endif

#define HOUR_ONLY (true || !CONFIGURABLE)
#define SHOW_MINS (false && CONFIGURABLE)
#define SHOW_SECS (false && CONFIGURABLE)
#define BAR_COLOR_0 (GColorRed)
#define BAR_COLOR_1 (GColorGreen)

#define TICK_LARGE (5)

// Vertical spacing
#define BAR_BORDER PBL_BAR_OUTLINE_IF_ELSE(2, 0)
#if HOUR_ONLY
    #define BAR_LABEL_MARGIN PBL_BAR_OUTLINE_IF_ELSE(BAR_BORDER + TICK_LARGE + 2, 8)
#else // !HOUR_ONLY
    #define BAR_LABEL_MARGIN PBL_BAR_OUTLINE_IF_ELSE(BAR_BORDER + 1, 2)
#endif // !HOUR_ONLY
#define HOUR_THICKNESS (3)
#define MIN_THICKNESS (3)
#define SEC_THICKNESS (2)
#define PEEK_MARGIN (2)  // Space between the top of the timeline peek and the lowest element
#define HOURMIN_MARGIN (10)  // Space between the hour and minute groups

// Bar y-offsets
#define SEC_BAR_Y_ORIGIN (UNOBSTRUCTED_HEIGHT - PEEK_MARGIN - SEC_THICKNESS + PBL_DISPLAY_WIDTH_SWITCH(0, 0, 0, -2))
#define MIN_BAR_Y_ORIGIN (SEC_BAR_Y_ORIGIN - BAR_LABEL_MARGIN \
                          - LABEL_FONT_HEIGHT - BAR_LABEL_MARGIN - MIN_THICKNESS)
#define HOUR_BAR_Y_ORIGIN (MIN_BAR_Y_ORIGIN - BAR_LABEL_MARGIN - LABEL_FONT_HEIGHT \
                           - HOURMIN_MARGIN \
                           - LABEL_FONT_HEIGHT - BAR_LABEL_MARGIN - HOUR_THICKNESS)

#define SECS_PER_BAR (12 * 60 * 60)
#define PIXELS_PER_BAR PBL_DISPLAY_WIDTH_SWITCH(12 * 12, 12 * 12, 12 * 16, 12 * 20)
#define SECONDS_PER_PIXEL (SECS_PER_BAR / PIXELS_PER_BAR)

#if BAT_ENABLED
    #define NUDGE_FOR_BAT (-10)
#else // !BAT_ENABLED
    #define NUDGE_FOR_BAT (0)
#endif // !BAT_ENABLED

static ProgressBar s_bar_hour_only = {
    .y = (PBL_DISPLAY_HEIGHT / 2) + PBL_DISPLAY_WIDTH_SWITCH(NUDGE_FOR_BAT, NUDGE_FOR_BAT, 0, 0),
    .x_nudge = PBL_DISPLAY_WIDTH_SWITCH(0, 0, 0, -LABEL_FONT_WIDTH/2 + 1),
    .max = 12 * 60 * 60,
    .w = PIXELS_PER_BAR,
    .thickness = 2,
    .extend = false,
    .alternate_labels = true,
    .phase = 0,
    .current = 0
};

#if CONFIGURABLE

static ProgressBar s_bar_hour = {
    .y = HOUR_BAR_Y_ORIGIN,
    .x_nudge = PBL_DISPLAY_WIDTH_SWITCH(0, 0, -3, 0),
    .max = 12,
    .tickwidth = PBL_DISPLAY_WIDTH_SWITCH(10, 12, 14, 18),
    .thickness = HOUR_THICKNESS,
    .extend = true,
    .alternate_labels = true,
    .phase = 0,
    .current = 0
};

static ProgressBar s_bar_min = {
    .y = MIN_BAR_Y_ORIGIN,
    .x_nudge = PBL_DISPLAY_WIDTH_SWITCH(0, 0, 1, 2),
    .max = 60,
    .tickwidth = PBL_DISPLAY_WIDTH_SWITCH(2, 2, 3, 4),
    .thickness = MIN_THICKNESS,
    .extend = false,
    .alternate_labels = false,
    .phase = 0,
    .current = 0
};

static ProgressBar s_bar_sec = {
    .y = SEC_BAR_Y_ORIGIN,
    .x_nudge = PBL_DISPLAY_WIDTH_SWITCH(0, 0, 1, 2),
    .max = 60,
    .tickwidth = PBL_DISPLAY_WIDTH_SWITCH(2, 2, 3, 4),
    .thickness = SEC_THICKNESS,
    .extend = false,
    .alternate_labels = false,
    .phase = 0,
    .current = 0
};

#endif // CONFIGURABLE

// Where the x-origin should be for the bar, including the label overhang, to be centred
static int16_t bar_x_origin(const ProgressBar* bar) {
    const int16_t bar_width = bar->w + (bar->extend ? (bar->w / bar->max) : (HOUR_ONLY ? 0 : 1) * LABEL_FONT_WIDTH);
    return ((PBL_DISPLAY_WIDTH - bar_width) >> 1) + bar->x_nudge;
}

static void draw_bar_labels(GContext* ctx, const ProgressBar* bar, const char* title, const char** labels, size_t len) {
    TRACE("draw_bar_labels");

    // indices
    const int16_t label_spacing = bar->w / len;
    const int16_t zero_x_origin = bar_x_origin(bar);
    const int16_t box_w = LABEL_FONT_WIDTH * 3;
    const int16_t above_y = bar->y - (LABEL_FONT_HEIGHT + BAR_LABEL_MARGIN) - LABEL_FONT_Y_PAD;
    const int16_t below_y = bar->y + bar->thickness + BAR_LABEL_MARGIN - LABEL_FONT_Y_PAD;
    GRect box = GRect(zero_x_origin + label_spacing - (box_w / 2), below_y, box_w, LABEL_FONT_HEIGHT);
    for (size_t i = 0; i < len ; i++) {
        if (bar->alternate_labels) {
            box.origin.y = (i % 2) ? above_y : below_y;
        }
        graphics_draw_text(ctx, labels[i], s_bar_label_font, box, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
        box.origin.x += label_spacing;
    }

    // title
    const int16_t title_x_offset = PBL_DISPLAY_WIDTH_SWITCH(-2, -3, -4, 0);
    graphics_draw_text(ctx, title, s_bar_title_font,
        GRect(zero_x_origin + title_x_offset, bar->y - 9 - BAR_LABEL_MARGIN, 30, 10),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

static inline GRect _bar_rect(const ProgressBar* bar, uint32_t start, uint32_t end) {
    LOG("start = %u, end = %u", start, end);

    const uint32_t originx = bar_x_origin(bar);
    const uint32_t startx = originx + ((start * bar->w) / bar->max);
    const uint32_t endx = originx + ((end * bar->w) / bar->max);
    LOG("startx = %u, endx = %u", startx, endx);
    return GRect(startx, bar->y, endx - startx, bar->thickness);
}

// Draw `bar` between its `start` and `end` value
static void _drawbar(GContext* ctx, const ProgressBar* bar, uint32_t start, uint32_t end) {
    if (start != end) {
        LOG("Drawing bar");
        graphics_context_set_fill_color(ctx, bar->phase ? BAR_COLOR_1 : BAR_COLOR_0);
        graphics_fill_rect(ctx, _bar_rect(bar, start, end), 0, GCornerNone);
    }
}

// Update a bar which increases monotonically, wrapping at its max
static void bar_update_wrap(GContext* ctx, ProgressBar* bar, uint32_t value) {
    TRACE("bar_update_wrap");
    ASSERT(value <= bar->max);

    LOG("New value %d", value);

    if (bar->extend && (value == 0)) {  // TODO do this differently?
        value = bar->max;
    }

    if (value < bar->current) {
        // Fill the remaining bar to the top
        _drawbar(ctx, bar, bar->current, bar->max + (bar->extend ? 1 : 0));
        bar->current = 0;
        bar->phase = !bar->phase;
    }

    if (value > bar->current) {
        _drawbar(ctx, bar, bar->current, value);
    }

    bar->current = value;
}

#if CONFIGURABLE
// Update a bar which increases or decreases between 0 and max
static void bar_update_nowrap(GContext* ctx, ProgressBar* bar, uint32_t value) {
    ASSERT(value <= bar->max);
    bar->phase = value < bar->current;
    _drawbar(ctx, bar, bar->current, value);
    bar->current = value;
}
#endif // CONFIGURABLE

static void draw_bg_hour_only(GContext* ctx) {
    TRACE("draw_bg_hour_only");
    ProgressBar* const bar = &s_bar_hour_only;
    const int16_t zero_x_origin = bar_x_origin(&s_bar_hour_only);

    #define TWELVE_HOUR_LABEL PBL_DISPLAY_WIDTH_SWITCH("", "", "", "12")
    static const char* hours[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", TWELVE_HOUR_LABEL};
    draw_bar_labels(ctx, &s_bar_hour_only, "", hours, ARRAY_LEN(hours));

    const int16_t w_12 = 4;
    const int16_t h_12 = 14;
    graphics_draw_bitmap_in_rect(ctx, s_twelve, GRect(MAX(zero_x_origin - w_12/2, 0), s_bar_hour_only.y - (h_12 + BAR_LABEL_MARGIN), 4, h_12));
#if !PBL_PLATFORM_GABBRO
    const int16_t bar_end = zero_x_origin + bar->w;
    const int16_t x_org = ((bar_end + w_12/2) > PBL_DISPLAY_WIDTH) ? (PBL_DISPLAY_WIDTH - w_12) : (bar_end - w_12/2);
    graphics_draw_bitmap_in_rect(ctx, s_twelve, GRect(x_org, s_bar_hour_only.y - (h_12 + BAR_LABEL_MARGIN), 4, h_12));
#endif //!PBL_PLATFORM_GABBRO

    // tickmarks
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite));
    const int16_t large = bar->w / 12;
    const int16_t medium = large / 2;
    const int16_t small = medium / 2;
    for (uint32_t i = 0; i <= bar->w; i++) {
        int16_t height = 0;
        if ((i % large) == 0) {
            height = TICK_LARGE + BAR_BORDER;
        } else if ((i % medium) == 0) {
            height = 3 + BAR_BORDER;
        } else if ((i % small) == 0) {
            height = 1 + BAR_BORDER;
        }
        if (height) {
            graphics_fill_rect(ctx,
                GRect(
                    zero_x_origin + i - 1 + ((i == bar->w) ? 1 : 0),
                    bar->y - height,
                    1,
                    (height * 2) + bar->thickness
                ), 0, GCornerNone
            );
        }
    }

    graphics_context_set_fill_color(ctx, config_get()->bgColor);
    GRect bar_rect = _bar_rect(bar, 0, bar->max);
    graphics_fill_rect(ctx, GRect(bar_rect.origin.x, bar_rect.origin.y - BAR_BORDER, bar_rect.size.w, bar_rect.size.h + (BAR_BORDER * 2)), 0, GCornerNone);

    bar_update_wrap(ctx, &s_bar_hour_only, s_bar_hour_only.max);
}

#if CONFIGURABLE
static void draw_bar_tickmarks(GContext* ctx, const ProgressBar* bar, bool above) {
    graphics_context_set_fill_color(ctx, config_get()->fgColor);

    const int16_t zero_x_origin = bar_x_origin(bar);
    const int16_t above_y = bar->y - BAR_BORDER;
    const int16_t below_y = bar->y + bar->thickness + BAR_BORDER;

    for (uint32_t i = 0; i < bar->max; i++) {
        bool is_above = (i >= PBL_DISPLAY_WIDTH_SWITCH(7, 6, 5, 5));
        if (!above) {
            is_above = !is_above;
        }
        graphics_fill_rect(ctx,
            GRect(
                zero_x_origin + ((i + 1) * bar->tickwidth) - 1,
                is_above ? above_y : below_y,
                1,
                (((i + 1) % 5) ? 1 : 3) * (is_above ? -1 : 1)
            ), 0, GCornerNone
        );
    }
}

#if BAR_OUTLINES
static void _draw_bar_outline(GContext* ctx, const ProgressBar* bar) {
    graphics_context_set_fill_color(ctx, config_get()->fgColor);
    graphics_fill_rect(ctx, grect_crop(_bar_rect(bar, 0, bar->max + (bar->extend ? 1 : 0)), -2), 0, GCornerNone);
}
#endif // BAR_OUTLINES

static void draw_bg_hour(GContext* ctx) {
    #define TEN_HOUR_LABEL PBL_DISPLAY_SMALL_IF_ELSE("0", "10") // TODO Ideally an X character instead of 0
    static const char* hours[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", TEN_HOUR_LABEL, "11", "12"};
    draw_bar_labels(ctx, &s_bar_hour, PBL_DISPLAY_SMALL_IF_ELSE("HR", "HOUR"), hours, ARRAY_LEN(hours));

    #if BAR_OUTLINES
        _draw_bar_outline(ctx, &s_bar_hour);
    #endif // BAR_OUTLINES

    bar_update_wrap(ctx, &s_bar_hour, s_bar_hour.max + 1);
}

static void draw_bg_min(GContext* ctx) {
    #if PBL_DISPLAY_SMALL
        static const char* mins[6] = {"1", "2", "3", "4", "5", "6"};  // TODO append small 0s (just draw some rects)
    #else // !PBL_DISPLAY_SMALL
        static const char* mins[6] = {"10", "20", "30", "40", "50", "60"};
    #endif // !PBL_DISPLAY_SMALL
    draw_bar_labels(ctx, &s_bar_min, "MIN", mins, ARRAY_LEN(mins));

    #if BAR_OUTLINES
        _draw_bar_outline(ctx, &s_bar_min);
    #endif // BAR_OUTLINES

    draw_bar_tickmarks(ctx, &s_bar_min, true);

    bar_update_wrap(ctx, &s_bar_min, s_bar_min.max);
}

static void draw_bg_sec(GContext* ctx) {
    // sec label
    const int16_t title_x_offset = PBL_DISPLAY_WIDTH_SWITCH(-1, -2, -3, 2);
    graphics_draw_text(ctx, "SEC", s_bar_title_font,
        GRect(bar_x_origin(&s_bar_sec) + title_x_offset, s_bar_sec.y + s_bar_sec.thickness + BAR_LABEL_MARGIN - 3, 30, 10),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    #if BAR_OUTLINES
        _draw_bar_outline(ctx, &s_bar_sec);
    #endif // BAR_OUTLINES

    draw_bar_tickmarks(ctx, &s_bar_sec, false);

    bar_update_wrap(ctx, &s_bar_sec, s_bar_sec.max);
}

#endif // CONFIGURABLE

// Draw the static background; this only gets called once, on init
static void draw_background(GContext* ctx, const GRect bounds) {
    TRACE("draw_background");
    const Config* config = config_get();

    // bg fill
    graphics_context_set_fill_color(ctx, config->bgColor);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    // bat picture
#if BAT_ENABLED
    GRect bat_bounds = gbitmap_get_bounds(s_bat);
    const int16_t y_offset = PBL_DISPLAY_WIDTH_SWITCH(2, 7, 15, 20);
    bat_bounds.origin = GPoint((PBL_DISPLAY_WIDTH - bat_bounds.size.w) / 2, PBL_DISPLAY_HEIGHT - bat_bounds.size.h - y_offset);
    graphics_draw_bitmap_in_rect(ctx, s_bat, bat_bounds);
#endif // BAT_ENABLED

    graphics_context_set_text_color(ctx, config->fgColor);

    const bool is_am = s_current_time.tm_hour < 12;
#if CONFIGURABLE
    s_bar_hour.phase = is_am ? 0 : 1;
#endif // CONFIGURABLE
    s_bar_hour_only.phase = is_am ? 0 : 1;

    if (HOUR_ONLY) {
        draw_bg_hour_only(ctx);
    }
#if CONFIGURABLE
    else
    {
        draw_bg_hour(ctx);
    }
    if (SHOW_MINS) {
        draw_bg_min(ctx);
    }
    if (SHOW_SECS) {
        draw_bg_sec(ctx);
    }
#endif // CONFIGURABLE
}

static void draw_root_layer(Layer* layer, GContext* ctx) {
    TRACE("draw_root_layer");
    PROFILE_START();
    const GRect bounds = layer_get_bounds(layer);
    // const Config* config = config_get();

    graphics_context_set_antialiased(ctx, false);

    if (s_is_full_redraw)
    {
        draw_background(ctx, bounds);
        s_is_full_redraw = false;
    }

    if (HOUR_ONLY) {
        bar_update_wrap(ctx, &s_bar_hour_only, (
                ((s_current_time.tm_hour % 12) * 60 * 60)
                + (s_current_time.tm_min * 60)
                + s_current_time.tm_sec
            )
        );
    }
#if CONFIGURABLE
    else
    {
        bar_update_wrap(ctx, &s_bar_hour, s_current_time.tm_hour % 12);
    }
    if (SHOW_MINS) {
        bar_update_wrap(ctx, &s_bar_min, s_current_time.tm_min);
    }
    if (SHOW_SECS) {
        bar_update_wrap(ctx, &s_bar_sec, s_current_time.tm_sec);
    }

    draw_disconnect_status(bounds, ctx);
    draw_quiet_time_status(bounds, ctx);
#endif // CONFIGURABLE

    PROFILE_END("draw_root_layer");
}

static void init_resources(Layer *root_layer) {
    s_bar_label_font = fonts_get_system_font(LABEL_FONT);
    s_bar_title_font = fonts_get_system_font(FONT_KEY_GOTHIC_09);
    s_twelve = gbitmap_create_with_resource(RESOURCE_ID_TWELVE);
#if BAT_ENABLED
    s_bat = gbitmap_create_with_resource(RESOURCE_ID_BAT);
#endif // BAT_ENABLED
}

static void destroy_resources(void) {
    gbitmap_destroy(s_twelve);
#if BAT_ENABLED
    gbitmap_destroy(s_bat);
#endif // BAT_ENABLED
}

static void handle_config(const Config* config) {
}


/******************************************************************************
 Generic watchface boilerplate
******************************************************************************/

static Window* s_main_window;
static AppTimer* s_update_timer = NULL;

static struct tm s_next_wakeup_tm;
static void schedule_next_update(void);
static void apptimer_tick_handler(void* context) {
    s_current_time = *((struct tm *)context);
    LOG("apptimer_tick_handler at %d:%d:%d", s_current_time.tm_hour, s_current_time.tm_min, s_current_time.tm_sec);
    mark_root_window_dirty(false);

#if !FAST_TIME_MS
    schedule_next_update();
#endif // FAST_TIME_MS
}

static void schedule_next_update(void) {
    static time_t next_wakeup = 0;

    const time_t now = time(NULL);
    if (next_wakeup == 0) {
        next_wakeup = now - (now % SECONDS_PER_PIXEL);
    }
    next_wakeup += SECONDS_PER_PIXEL;

    s_next_wakeup_tm = *localtime(&next_wakeup);
    const int32_t delay_ms = (next_wakeup - now + 1) * MS_PER_S;
    s_update_timer = app_timer_register(delay_ms, apptimer_tick_handler, &s_next_wakeup_tm);

    LOG("schedule_next_update, now=%d, next=%d, duration = %dms", now, next_wakeup, delay_ms);
    LOG("next = %d:%d:%d", s_next_wakeup_tm.tm_hour, s_next_wakeup_tm.tm_min, s_next_wakeup_tm.tm_sec);
}

void mark_root_window_dirty(bool full_redraw) {
    TRACE("mark_root_window_dirty");
    layer_mark_dirty(window_get_root_layer(s_main_window));
    if (full_redraw) {
        s_is_full_redraw = true;
    }
}

// static void activation_handler(void* context) {
//     UNUSED(context);
// }

// static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
//     activation_handler(NULL);
// }

#if FAST_TIME_MS
time_t s_fake_time = 0;
static void fast_time_handler(void* context) {
    s_fake_time += SECS_PER_FAST_TIME_TICK;
    apptimer_tick_handler(localtime(&s_fake_time));
    app_timer_register(FAST_TIME_MS, fast_time_handler, NULL);
}
#endif // FAST_TIME_MS

static void init_time(void) {
    TRACE("init_time");
    const time_t now = time(NULL);
    apptimer_tick_handler(localtime(&now));
}

static void new_config_handler(const Config* config) {
    TRACE("new_config_handler");
    handle_config(config);
    init_time();
    // activation_handler(NULL);
    mark_root_window_dirty(true);
}

static void main_window_load(Window *window) {
    TRACE("main_window_load");
    config_init(new_config_handler);

    Layer* const root_layer = window_get_root_layer(window);
    layer_set_update_proc(root_layer, draw_root_layer);
    init_resources(root_layer);

    // connection_service_subscribe((ConnectionHandlers){handle_connection, NULL});
    // accel_tap_service_subscribe(accel_tap_handler);

    new_config_handler(config_get());
}

static void main_window_unload(Window *window) {
    destroy_resources();
    // accel_tap_service_unsubscribe();
    // connection_service_unsubscribe();
    tick_timer_service_unsubscribe();

#if CONFIGURABLE
    config_deinit();
#endif // CONFIGURABLE
}

static void init(void) {
    s_main_window = window_create();
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });
    window_stack_push(s_main_window, true);

#if FAST_TIME_MS
    s_fake_time = time(NULL);
    app_timer_register(FAST_TIME_MS, fast_time_handler, NULL);
#endif // FAST_TIME_MS
}

static void deinit(void) {
    window_destroy(s_main_window);

    if (s_update_timer) {
        app_timer_cancel(s_update_timer);
        s_update_timer = NULL;
    }
}

int main(void) {
    DEMO_BACKLIGHT_ENABLE(true);

    init();
    app_event_loop();
    deinit();

    DEMO_BACKLIGHT_ENABLE(false);
}
