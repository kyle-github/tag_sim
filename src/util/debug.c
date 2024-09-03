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


#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "time_utils.h"


/*
 * Logging routines.
 */

static volatile debug_level_t debug_level = DEBUG_WARN;


void debug_set_level(debug_level_t level)
{
    /* clamp level to sane values */
    if(level < DEBUG_WARN) {
        debug_level = DEBUG_WARN;
    } else if(level > DEBUG_FLOOD) {
        debug_level = DEBUG_FLOOD;
    } else {
        debug_level = level;
    }
}

debug_level_t debug_get_level(void)
{
    return debug_level;
}

void debug_impl(const char *func, int line, debug_level_t level, const char *templ, ...)
{
    va_list va;
    const char *prefix = NULL;

    switch(level) {
        case DEBUG_WARN: prefix = "WARN"; break;
        case DEBUG_INFO: prefix = "INFO"; break;
        case DEBUG_DETAIL: prefix = "DETAIL"; break;
        case DEBUG_FLOOD: prefix = "FLOOD"; break;
        case DEBUG_ERROR: prefix = "ERROR"; break;
        default: prefix = "UNKNOWN"; break;
    }

    if(level <= debug_level || level == DEBUG_ERROR) {
        /* print it out. */
        fprintf(stderr, "%s %s:%d ", prefix, func, line);
        va_start(va, templ);
        vfprintf(stderr, templ, va);
        va_end(va);
        fprintf(stderr,"\n");
    }
}



#define ROW_BUF_SIZE (300)
#define COLUMNS (size_t)(16)


void debug_dump_ptr(debug_level_t level, uint8_t *start_buf, uint8_t *end_buf)
{
    intptr_t data_len = 0;
    size_t max_row = 0;
    size_t row = 0;
    size_t column = 0;
    uint8_t *cur_buf = start_buf;
    char row_buf[ROW_BUF_SIZE + 1] = {0};

    if(level <= debug_level) {
        return;
    }

    if(!start_buf || !end_buf) {
        warn("Called with null pointer(s) to data!");
        return;
    }

    data_len = (intptr_t)end_buf - (intptr_t)start_buf;

    if(data_len <= 0) {
        warn("Called with no data to print or start_buf buffer > end_buf buffer!");
        return;
    }

    /* determine the number of rows we will need to print. */
    max_row = ((size_t)data_len + (COLUMNS - 1))/COLUMNS;

    for(row = 0; row < max_row; row++) {
        size_t offset = (row * COLUMNS);
        size_t row_offset = 0;

        /* print the prefix and address */
        row_offset = (size_t)snprintf(&row_buf[0], sizeof(row_buf),"%04zu", offset);

        for(column = 0; column < COLUMNS && ptr_before(cur_buf, end_buf) && row_offset < (int)sizeof(row_buf); cur_buf++, offset++, column++) {
            row_offset += (size_t)snprintf(&row_buf[row_offset], ROW_BUF_SIZE - row_offset, " %02x", *cur_buf);
        }

        /* output it, finally */
        fprintf(stderr,"%s\n", row_buf);
    }
}
