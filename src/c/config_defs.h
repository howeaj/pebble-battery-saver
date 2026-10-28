// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// App-specific definitions of config options, for inclusion by config.h

#pragma once

#include <pebble.h>

#include "macros.h"


/** The persist data version for dealing with backwards-incompatible changes.
    This doesn't need to change as long as you only append new fields
    and don't change the messageKeys or meaning of existing data.
    Note lowest allowed value is 1.
*/
#define PERSIST_CONFIG_VERSION (1)

#define FACE_TEXT_SIZE (11)  // max size of the branding custom text (including NULL terminator)
#define TZ_TEXT_SIZE (4)  // max size of the "alternate timezone" complication text (including NULL terminator)

/** Macro table defining all config data.

    - Conftype is the last word in one of the RECEIVE_CONFIG_ options in config.c
    - CType is the C-side storage type
    - arrsuffix is the optional array indicator with length for the struct definition, e.g. [3]
    - Message key must match config.json and package.json, and be a valid C identifier.
    - Default value should match config.json.
*/
#define X_CONFIG_OPTIONS(MACRO) \
    /* conftype, C type, arrsuffix,      message key,         default */ \
    MACRO(BOOL,  bool,,                  enableSeconds,       true) \
    MACRO(COLOR, GColor,,                bgColor,             GColorWhite) \
    MACRO(COLOR, GColor,,                fgColor,             GColorBlack) \
    MACRO(COLOR, GColor,,                hourColor,           GColorBlack) \
    MACRO(COLOR, GColor,,                minuteColor,         GColorBlack) \
    MACRO(COLOR, GColor,,                secondColor,         GColorBlack) \
/* end of X_CONFIG_OPTIONS */

