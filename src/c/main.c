// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/* TODO

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

static GFont s_bar_label_font;
static GFont s_bar_title_font;
static bool s_is_full_redraw = false;

struct tm s_current_time;


static void handle_tick(struct tm *tick_time, TimeUnits units_changed) {
    // TRACE("handle_tick; s_num_seconds_shown = %d", s_num_seconds_shown);
    s_current_time = *tick_time;
    mark_root_window_dirty(false);
}

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

typedef struct ProgressBar {
    uint32_t y;  // origin Y coordinate
    uint32_t max;  // maximum value
    uint32_t tickwidth;  // pixels per tick
    uint32_t thickness;  // line thickness

    uint32_t phase;  // 0 or 1; which colour is currently increasing
    uint32_t current; // current value
} ProgressBar;


#define PBL_DISPLAY_SMALL (PBL_DISPLAY_WIDTH == 144)
#if PBL_DISPLAY_WIDTH == 144
    #define PBL_DISPLAY_WIDTH_SWITCH(classic, emery, gabbro) (classic)
    #define PBL_DISPLAY_SMALL_IF_ELSE(small, wide) (small)
#elif PBL_DISPLAY_WIDTH == 200
    #define PBL_DISPLAY_WIDTH_SWITCH(classic, emery, gabbro) (emery)
    #define PBL_DISPLAY_SMALL_IF_ELSE(small, wide) (wide)
#elif PBL_DISPLAY_WIDTH == 260
    #define PBL_DISPLAY_WIDTH_SWITCH(classic, emery, gabbro) (gabbro)
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

// Vertical spacing
#define BAR_BORDER PBL_DISPLAY_SMALL_IF_ELSE(2, 0)
#define BAR_LABEL_MARGIN PBL_DISPLAY_SMALL_IF_ELSE(BAR_BORDER + 1, 2)
#define HOUR_THICKNESS (3)
#define MIN_THICKNESS (3)
#define SEC_THICKNESS (2)
#define PEEK_MARGIN (2)  // Space between the top of the timeline peek and the lowest element
#define HOURMIN_MARGIN (10)  // Space between the hour and minute groups

// Bar y-offsets
#define SEC_BAR_Y_ORIGIN (UNOBSTRUCTED_HEIGHT - PEEK_MARGIN - SEC_THICKNESS)
#define MIN_BAR_Y_ORIGIN (SEC_BAR_Y_ORIGIN - BAR_LABEL_MARGIN \
                          - LABEL_FONT_HEIGHT - BAR_LABEL_MARGIN - MIN_THICKNESS)
#define HOUR_BAR_Y_ORIGIN (MIN_BAR_Y_ORIGIN - BAR_LABEL_MARGIN - LABEL_FONT_HEIGHT \
                           - HOURMIN_MARGIN \
                           - LABEL_FONT_HEIGHT - BAR_LABEL_MARGIN - HOUR_THICKNESS)

static ProgressBar s_bar_hour = {
    .y = HOUR_BAR_Y_ORIGIN,
    .max = 12,
    .tickwidth = PBL_DISPLAY_WIDTH_SWITCH(10, 15, 18),
    .thickness = HOUR_THICKNESS,
    .phase = 0,
    .current = 0
};

static ProgressBar s_bar_min = {
    .y = MIN_BAR_Y_ORIGIN,
    .max = 60,
    .tickwidth = PBL_DISPLAY_WIDTH_SWITCH(2, 3, 4),
    .thickness = MIN_THICKNESS,
    .phase = 0,
    .current = 0
};

static ProgressBar s_bar_sec = {
    .y = SEC_BAR_Y_ORIGIN,
    .max = 60,
    .tickwidth = PBL_DISPLAY_WIDTH_SWITCH(2, 3, 4),
    .thickness = SEC_THICKNESS,
    .phase = 0,
    .current = 0
};

// Where the x-origin should be for the bar, including the label overhang, to be centred
static int16_t bar_x_origin(const ProgressBar* bar) {
    const int16_t bar_width = (bar->tickwidth * bar->max);
    return ((PBL_DISPLAY_WIDTH - bar_width - LABEL_FONT_WIDTH) >> 1);
}

static void draw_labels_around_bar(GContext* ctx, GRect bounds, const ProgressBar* bar, const char* title, const char** labels, size_t len) {
    // indices
    const int16_t label_spacing = (bar->tickwidth * bar->max) / len;
    const int16_t zero_x_origin = bar_x_origin(bar);
    const int16_t box_w = LABEL_FONT_WIDTH * 3;
    GRect box = GRect(zero_x_origin + label_spacing - (box_w / 2), 0, box_w, LABEL_FONT_HEIGHT);
    const int16_t above_y = bar->y - (LABEL_FONT_HEIGHT + BAR_LABEL_MARGIN);
    const int16_t below_y = bar->y + bar->thickness + BAR_LABEL_MARGIN;
    for (size_t i = 0; i < len ; i++) {
        box.origin.y = ((i % 2) ? above_y : below_y) - LABEL_FONT_Y_PAD;
        graphics_draw_text(ctx, labels[i], s_bar_label_font, box, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
        box.origin.x += label_spacing;
    }

    // title
    const int16_t title_x_offset = PBL_DISPLAY_WIDTH_SWITCH(-4, -2, 0);
    graphics_draw_text(ctx, title, s_bar_title_font,
        GRect(zero_x_origin + title_x_offset, bar->y - 9 - BAR_LABEL_MARGIN, 30, 10),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

static inline GRect _bar_rect(const ProgressBar* bar, uint32_t start, uint32_t end) {
    return GRect(
        bar_x_origin(bar) + (start * bar->tickwidth),
        bar->y,
        (end - start) * bar->tickwidth,
        bar->thickness
    );
}

#if PBL_BW
static void _draw_bar_outline(GContext* ctx, const ProgressBar* bar) {
    graphics_context_set_fill_color(ctx, config_get()->fgColor);
    graphics_fill_rect(ctx, grect_crop(_bar_rect(bar, 0, bar->max), -2), 0, GCornerNone);
}
#endif // PBL_BW

// Draw `bar` between its `start` and `end` value
static void _drawbar(GContext* ctx, const ProgressBar* bar, uint32_t start, uint32_t end) {
    if (start != end) {
        graphics_context_set_fill_color(ctx, bar->phase ? GColorRed : GColorGreen);
        graphics_fill_rect(ctx, _bar_rect(bar, start, end), 0, GCornerNone);
    }
}

// Update a bar which increases monotonically, wrapping at its max
static void bar_update_wrap(GContext* ctx, ProgressBar* bar, uint32_t value) {
    ASSERT(value <= bar->max);

    if (value < bar->current) {
        // Fill the remaining bar to the top
        _drawbar(ctx, bar, bar->current, bar->max);
        bar->current = 0;
        bar->phase = ~bar->phase;
    }

    if (value > bar->current) {
        _drawbar(ctx, bar, bar->current, value);
    }

    bar->current = value;
}

// Update a bar which increases or decreases between 0 and max
void bar_update_nowrap(GContext* ctx, ProgressBar* bar, uint32_t value) {
    ASSERT(value <= bar->max);
    bar->phase = value < bar->current;
    _drawbar(ctx, bar, bar->current, value);
    bar->current = value;
}



// Draw the static background; this only gets called once, on init
static void draw_background(GContext* ctx, GRect bounds) {
    const Config* config = config_get();

    graphics_context_set_fill_color(ctx, config->bgColor);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    graphics_context_set_text_color(ctx, config->fgColor);

    #define TEN_HOUR_LABEL PBL_DISPLAY_SMALL_IF_ELSE("0", "10") // TODO Ideally an X character instead of 0
    static const char* hours[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", TEN_HOUR_LABEL, "11", "12"};
    draw_labels_around_bar(ctx, bounds, &s_bar_hour, PBL_DISPLAY_SMALL_IF_ELSE("HR", "HOUR"), hours, ARRAY_LEN(hours));

    #if PBL_DISPLAY_SMALL
        static const char* mins[6] = {"10", "20", "30", "40", "50", "60"};
    #else // !PBL_DISPLAY_SMALL
        static const char* mins[12] = {"5", "10", "15", "20", "25", "30", "35", "40", "45", "50", "55", "60"};
    #endif // !PBL_DISPLAY_SMALL
    draw_labels_around_bar(ctx, bounds, &s_bar_min, "MIN", mins, ARRAY_LEN(mins));

    // sec label
    const int16_t title_x_offset = PBL_DISPLAY_WIDTH_SWITCH(-3, -1, 2);
    graphics_draw_text(ctx, "SEC", s_bar_title_font,
        GRect(bar_x_origin(&s_bar_sec) + title_x_offset, s_bar_sec.y + s_bar_sec.thickness + BAR_LABEL_MARGIN - 3, 30, 10),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

#if PBL_BW
    _draw_bar_outline(ctx, &s_bar_hour);
    _draw_bar_outline(ctx, &s_bar_min);
    _draw_bar_outline(ctx, &s_bar_sec);
#endif // PBL_BW

    bar_update_wrap(ctx, &s_bar_hour, s_bar_hour.max);
    bar_update_wrap(ctx, &s_bar_min, s_bar_min.max);
    bar_update_wrap(ctx, &s_bar_sec, s_bar_sec.max);
}

static void draw_root_layer(Layer* layer, GContext* ctx) {
    PROFILE_START();
    const GRect bounds = layer_get_bounds(layer);
    // const Config* config = config_get();

    graphics_context_set_antialiased(ctx, false);

    if (s_is_full_redraw)
    {
        draw_background(ctx, bounds);
        s_is_full_redraw = false;
    }

    bar_update_wrap(ctx, &s_bar_hour, s_current_time.tm_hour % 12);
    bar_update_wrap(ctx, &s_bar_min, s_current_time.tm_min);
    bar_update_wrap(ctx, &s_bar_sec, s_current_time.tm_sec);

    draw_disconnect_status(bounds, ctx);
    draw_quiet_time_status(bounds, ctx);

    PROFILE_END("draw_root_layer");
}

static void init_resources(Layer *root_layer) {
    s_bar_label_font = fonts_get_system_font(LABEL_FONT);
    s_bar_title_font = fonts_get_system_font(FONT_KEY_GOTHIC_09);
}

static void destroy_resources(void) {
}

static void handle_config(const Config* config) {
}


/******************************************************************************
 Generic watchface boilerplate
******************************************************************************/

static Window* s_main_window;

void mark_root_window_dirty(bool full_redraw) {
    layer_mark_dirty(window_get_root_layer(s_main_window));
    if (full_redraw) {
        s_is_full_redraw = true;
    }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    handle_tick(tick_time, units_changed);
}

static void activation_handler(void* context) {
    UNUSED(context);
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
    activation_handler(NULL);
}

#if FAST_TIME_MS
time_t s_fake_time = 0;
static void fast_time_handler(void* context) {
    s_fake_time += SECONDS_PER_MINUTE;
    tick_handler(localtime(&s_fake_time), SECOND_UNIT | MINUTE_UNIT | (((s_fake_time % 60) == 0) ? HOUR_UNIT : 0));
    app_timer_register(FAST_TIME_MS, fast_time_handler, NULL);
}
#endif // FAST_TIME_MS

static void new_config_handler(const Config* config) {
    handle_config(config);

    const time_t now = time(NULL);
    tick_handler(localtime(&now),
        SECOND_UNIT | MINUTE_UNIT | HOUR_UNIT | DAY_UNIT | MONTH_UNIT | YEAR_UNIT);

#if !FAST_TIME_MS
    tick_timer_service_unsubscribe();
    tick_timer_service_subscribe(config_get()->enableSeconds ? SECOND_UNIT : MINUTE_UNIT, tick_handler);
#endif // !FAST_TIME_MS

    activation_handler(NULL);
    mark_root_window_dirty(true);
}

static void main_window_load(Window *window) {
    config_init(new_config_handler);

    Layer* const root_layer = window_get_root_layer(window);
    layer_set_update_proc(root_layer, draw_root_layer);
    init_resources(root_layer);

    connection_service_subscribe((ConnectionHandlers){handle_connection, NULL});
    accel_tap_service_subscribe(accel_tap_handler);

    new_config_handler(config_get());
}

static void main_window_unload(Window *window) {
    destroy_resources();
    accel_tap_service_unsubscribe();
    connection_service_unsubscribe();
    tick_timer_service_unsubscribe();
    config_deinit();
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
}

int main(void) {
    DEMO_BACKLIGHT_ENABLE(true);

    init();
    app_event_loop();
    deinit();

    DEMO_BACKLIGHT_ENABLE(false);
}
