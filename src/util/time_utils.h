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

#include <stdbool.h>
#include <stdint.h>

extern int util_sleep_ms(int ms);
extern int64_t util_time_ms(void);

static inline bool ptr_before(void *ptr, void *end) {
    if(ptr && end) {
        if((intptr_t)(ptr) < (intptr_t)(end)) {
            return true;
        }
    }

    return false;
}


static inline bool is_hex(char c) {
    bool rc = false;

    if(c >= '0' && c <= '9') {
        rc = true;
    } else if((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
        rc = true;
    }

    return rc;
}
