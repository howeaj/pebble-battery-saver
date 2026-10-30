// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#pragma once

#include "debug.h"
#include "macros.h"

#include <ctype.h>
#include <pebble.h>


#if PROFILE
// Return milliseconds since the epoch
static inline uint32_t timestamp_ms(void) {
    time_t seconds = 0;
    const uint32_t millis = (uint32_t)time_ms(&seconds, NULL);
    return ((uint32_t)seconds * MS_PER_S) + millis;
}
#endif // PROFILE

#if CONFIGURABLE
void string_to_upper(char* str, size_t n);
void string_to_lower(char* str, size_t n);
ssize_t strscpy(char * restrict dst, const char * restrict src, size_t size);
#endif // CONFIGURABLE
