// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/* TODO

*/

#include <pebble.h>

#include "config.h"
#include "debug.h"
#include "macros.h"
#include "misc.h"


void mark_root_window_dirty(bool full_redraw);


/******************************************************************************
 This watchface implementation; handlers
******************************************************************************/

static GFont s_face_font;
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
    uint32_t y;
    uint32_t max;
    uint32_t thickness;
    uint32_t tickwidth;

    uint32_t phase;  // 0 or 1
    uint32_t current;
} ProgressBar;

static ProgressBar s_bar_hour = {
    .y = 35,
    .thickness = 3,
    .tickwidth = 15,
    .max = 12,
    .phase = 0,
    .current = 0
};

static ProgressBar s_bar_min = {
    .y = 60,
    .thickness = 2,
    .tickwidth = 3,
    .max = 60,
    .phase = 0,
    .current = 0
};

static ProgressBar s_bar_sec = {
    .y = 76,
    .thickness = 1,
    .tickwidth = 3,
    .max = 60,
    .phase = 0,
    .current = 0
};

// Draw `bar` between its `start` and `end` value
static void _drawbar(GContext* ctx, const ProgressBar* bar, uint32_t start, uint32_t end) {
    if (start != end) {
        graphics_context_set_fill_color(ctx, bar->phase ? GColorRed : GColorGreen);
        const uint32_t zero_origin = ((PBL_DISPLAY_WIDTH - (bar->tickwidth * bar->max)) >> 1);
        graphics_fill_rect(ctx,
            GRect(zero_origin + (start * bar->tickwidth), bar->y,
                  (end - start) * bar->tickwidth, bar->thickness),
            0, GCornerNone);
    }
}

static void bar_update(GContext* ctx, ProgressBar* bar, uint32_t value) {
    ASSERT(value <= bar->max);
    if (value < bar->current) {
        // Fill the remaining bar to the top
        const uint32_t start = bar->current;
        const uint32_t end = bar->max;
        _drawbar(ctx, bar, start, end);
        bar->current = 0;
        bar->phase = ~bar->phase;
    }
    if (value > bar->current) {
        const uint32_t start = bar->current;
        const uint32_t end = value;
        _drawbar(ctx, bar, start, end);
    }

    bar->current = value;
}


static void draw_root_layer(Layer* layer, GContext* ctx) {
    PROFILE_START();
    const GRect bounds = layer_get_bounds(layer);
    const Config* config = config_get();

    graphics_context_set_antialiased(ctx, false);

    if (s_is_full_redraw)
    {

        // background
        graphics_context_set_fill_color(ctx, config->bgColor);
        graphics_fill_rect(ctx, bounds, 0, GCornerNone);

        graphics_context_set_text_color(ctx, config->fgColor);

        // hour numbers
        {
            const char* hours[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"};
            GRect box = GRect(10, 20, 15, 15);
            for (int32_t i = 0; i < 12 ; i++) {  // TODO 24h mode
                graphics_draw_text(ctx, hours[i], s_face_font, box, GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
                box.origin.x += 15;
            }
        }

        // minute numbers
        {
            const char* mins[12] = {"5", "10", "15", "20", "25", "30", "35", "40", "45", "50", "55", "60"};
            GRect box = GRect(10, 60, 15, 15);
            for (int32_t i = 0; i < 12 ; i++) {  // TODO 24h mode
                graphics_draw_text(ctx, mins[i], s_face_font, box, GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
                box.origin.x += 15;
            }
        }
    }

    bar_update(ctx, &s_bar_hour, s_current_time.tm_hour % 12);
    bar_update(ctx, &s_bar_min, s_current_time.tm_min);
    bar_update(ctx, &s_bar_sec, s_current_time.tm_sec);

    draw_disconnect_status(bounds, ctx);
    draw_quiet_time_status(bounds, ctx);

    s_is_full_redraw = false;

    PROFILE_END("draw_root_layer");
}

static void init_resources(Layer *root_layer) {
    s_face_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
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

    tick_timer_service_unsubscribe();
    tick_timer_service_subscribe(config_get()->enableSeconds ? SECOND_UNIT : MINUTE_UNIT, tick_handler);

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
