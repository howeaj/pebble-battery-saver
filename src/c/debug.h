// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// Debug options. Everything should be false for release builds!

#pragma once

#include <pebble.h>

#define DEBUG 1
#define PROFILE 0
#define DEMO 0
#define FAST_TIME_MS 0  // millisecond per minute, or 0 to disable
#define FORCE_BACKLIGHT_ON (DEMO || DEBUG || 0)
#define FORCE_BLUETOOTH_DISCONNECT (DEBUG || 0)  // because emu-bt-connection takes forever
#define FORCE_QUIET_TIME (DEBUG || 0)
#define FORCE_ALARM_INDICATOR (DEBUG ? (60 * 150) : 0)  // value is seconds in the future

#if DEMO
    #define IF_DEMO_ELSE(is_demo, not_demo) (is_demo)
#else  // !DEMO
    #define IF_DEMO_ELSE(is_demo, not_demo) (not_demo)
#endif  // !DEMO

#if FORCE_BACKLIGHT_ON
    #define DEMO_BACKLIGHT_ENABLE(on) light_enable(on)
#else // !FORCE_BACKLIGHT_ON
    #define DEMO_BACKLIGHT_ENABLE(on)
#endif // !FORCE_BACKLIGHT_ON
