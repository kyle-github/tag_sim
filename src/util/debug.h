/***************************************************************************
 *   Copyright (C) 2024 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 * This software is available under the Mozilla Public License version 2.0 *
 *                                                                         *
 * MPL 2.0:                                                                *
 *                                                                         *
 *   This Source Code Form is subject to the terms of the Mozilla Public   *
 *   License, v. 2.0. If a copy of the MPL was not distributed with this   *
 *   file, You can obtain one at http://mozilla.org/MPL/2.0/.              *
 *                                                                         *
 ***************************************************************************/


#pragma once

#include <stdint.h>

typedef enum {
    DEBUG_NONE,
    DEBUG_WARN,
    DEBUG_INFO,
    DEBUG_DETAIL,
    DEBUG_FLOOD,

    DEBUG_ERROR = 1000,
} debug_level_t;

/* debug helpers */
extern void debug_set_level(debug_level_t level);
extern debug_level_t debug_get_level(void);

extern void debug_impl(const char *func, int line, debug_level_t level, const char *templ, ...);

#define assert_error(COND, ...) do { if(!(COND)) { debug_impl(__func__, __LINE__, DEBUG_ERROR, __VA_ARGS__); exit(1); } } while(0)
#define warn(...) debug_impl(__func__, __LINE__, DEBUG_WARN, __VA_ARGS__)
#define info(...) debug_impl(__func__, __LINE__, DEBUG_INFO, __VA_ARGS__)
#define detail(...) debug_impl(__func__, __LINE__, DEBUG_DETAIL, __VA_ARGS__)
#define flood(...) debug_impl(__func__, __LINE__, DEBUG_FLOOD, __VA_ARGS__)

#define warn_do if(debug_get_level() <= (DEBUG_WARN))
#define info_do if(debug_get_level() <= (DEBUG_INFO))
#define detail_do if(debug_get_level() <= (DEBUG_DETAIL))
#define flood_do if(debug_get_level() <= (DEBUG_FLOOD))

extern void debug_dump_ptr(debug_level_t level, uint8_t *start, uint8_t *end);
