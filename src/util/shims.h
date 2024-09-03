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

#include "status.h"


#ifdef IS_WINDOWS /* Assume MSVC before full C11 support */

    #include <Windows.h>
    #include <intrin.h>

    /* Basic atomic functions */
    typedef volatile LONG atomic_int;
    #define ATOMIC_INIT(value) ((value))
    #define atomic_load(ptr) (*(ptr))
    #define atomic_store(ptr, value) (*(ptr) = (value))
    #define atomic_fetch_add(ptr, value) _InterlockedAdd(ptr, value)
    #define atomic_fetch_sub(ptr, value) _InterlockedAdd(ptr, -value)
    #define atomic_compare_exchange_strong(ptr, expected, desired) \
        (InterlockedCompareExchange(ptr, desired, *expected) == *expected ? 1 : 0)

    /* basic mutex functions */
    typedef CRITICAL_SECTION mutex_t;
    #define MUTEX_INIT(mutex) InitializeCriticalSection(&mutex)
    #define MUTEX_LOCK(mutex) EnterCriticalSection(&mutex)
    #define MUTEX_UNLOCK(mutex) LeaveCriticalSection(&mutex)
    #define MUTEX_DESTROY(mutex) DeleteCriticalSection(&mutex)

    /* basic thread functions */
    typedef HANDLE thread_t;
    #define THREAD_CREATE(thread, func, arg) \
        ((thread) = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(func), (arg), 0, NULL) != NULL)
    #define THREAD_JOIN(thread) WaitForSingleObject((thread), INFINITE)
    #define THREAD_EXIT() ExitThread(0)

#else /* assume a POSIX system and compiler that support C11 fully. */

    #include <stdatomic.h>
    #include <pthread.h>

    /* basic atomic functions */
    typedef atomic_int atomic_int_t;
    #define ATOMIC_INIT(value) ATOMIC_VAR_INIT(value)
    #define atomic_load(ptr) atomic_load_explicit(ptr, memory_order_seq_cst)
    #define atomic_store(ptr, value) atomic_store_explicit(ptr, value, memory_order_seq_cst)
    #define atomic_fetch_add(ptr, value) atomic_fetch_add_explicit(ptr, value, memory_order_seq_cst)
    #define atomic_fetch_sub(ptr, value) atomic_fetch_sub_explicit(ptr, value, memory_order_seq_cst)
    #define atomic_compare_exchange_strong(ptr, expected, desired) \
        atomic_compare_exchange_strong_explicit(ptr, expected, desired, memory_order_seq_cst, memory_order_seq_cst)

    /* basic mutex functions */
    typedef pthread_mutex_t mutex_t;
    #define MUTEX_INIT(mutex) pthread_mutex_init(&mutex, NULL)
    #define MUTEX_LOCK(mutex) pthread_mutex_lock(&mutex)
    #define MUTEX_UNLOCK(mutex) pthread_mutex_unlock(&mutex)
    #define MUTEX_DESTROY(mutex) pthread_mutex_destroy(&mutex)

    /* basic thread functions */
    typedef pthread_t thread_t;
    #define THREAD_CREATE(thread, func, arg) (pthread_create(&thread, NULL, func, arg) == 0)
    #define THREAD_JOIN(thread) pthread_join(thread, NULL)
    #define THREAD_EXIT() pthread_exit(NULL)

#endif /* if IS_WINDOWS else */
