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


#ifdef IS_WINDOWS
    #define _WINSOCKAPI_
    #include <windows.h>
    #include <tchar.h>
    #include <strsafe.h>
    #include <io.h>
    #include <Winsock2.h>
    #include <Ws2tcpip.h>
#else
    /* assume it is POSIX of some sort... */
    #include <sys/time.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <errno.h>
#endif

#include "time_utils.h"


/*
 * This contains the utilities used by the test harness.
 */


#ifdef IS_WINDOWS

int util_sleep_ms(int ms)
{
    Sleep(ms);
    return 1;
}

#else

int util_sleep_ms(int ms)
{
    struct timeval tv;

    tv.tv_sec = ms/1000;
    tv.tv_usec = (ms % 1000)*1000;

    return select(0,NULL,NULL,NULL, &tv);
}

#endif


/*
 * time_ms
 *
 * Return the current epoch time in milliseconds.
 */

#ifdef IS_WINDOWS
int64_t util_time_ms(void)
{
    FILETIME ft;
    int64_t res;

    GetSystemTimeAsFileTime(&ft);

    /* calculate time as 100ns increments since Jan 1, 1601. */
    res = (int64_t)(ft.dwLowDateTime) + ((int64_t)(ft.dwHighDateTime) << 32);

    /* get time in ms */

    res = res / 10000;

    return  res;
}

#else


int64_t util_time_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv,NULL);

    return  ((int64_t)tv.tv_sec*1000)+ ((int64_t)tv.tv_usec/1000);
}

#endif
