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

#include <stddef.h>
#include <stdint.h>

#include "buf.h"
#include "status.h"


#ifndef IS_WINDOWS
    #define SOCKET int
#endif


#ifndef INVALID_SOCKET
    #define INVALID_SOCKET (-1)
#endif



typedef enum {
    PROACTOR_SOCK_TCP_LISTENER,
    PROACTOR_SOCK_TCP_CLIENT,
    PROACTOR_SOCK_UDP,
} proactor_socket_type_t;


typedef enum {
    PROACTOR_EVENT_TICK,
    PROACTOR_EVENT_TIMEOUT,
    PROACTOR_EVENT_WAKE,
    PROACTOR_EVENT_RUN,
    PROACTOR_EVENT_STOP,
    PROACTOR_EVENT_DISPOSE
} proactor_event_t;



/* forward declaration.  Implementation is hidden. */
struct proactor_t proactor_t;
struct proactor_socket_t;


/* callback for proactor events */
typedef status_t (*proactor_event_cb_t)(struct proactor_t *proactor, proactor_event_t event, status_t status, void *app_data);

extern struct proactor_t *proactor_net_create(proactor_event_cb_t event_cb, void *sock_data, void *app_data, uint64_t tick_period_ms);
extern void proactor_net_dispose(struct proactor_t *proactor);

extern status_t proactor_net_get_status(struct proactor_t *proactor);

extern void proactor_net_run(struct proactor_t *proactor);
extern void proactor_net_stop(struct proactor_t *proactor);

extern void proactor_net_wake(struct proactor_t *proactor);



/*
 * The following are functions that deal with individual sockets.  The actual
 * socket data is hidden inside the implementation and handles are used to
 * identify which socket we want.
 */


/* define a simple buffer struct for passing buffers back and forth across the API boundary. */


extern status_t proactor_net_socket_open(struct proactor_t *proactor, struct proactor_socket_t **socket, proactor_socket_type_t socket_type, const char *address, uint16_t port, void *sock_data, void *app_data);
extern status_t proactor_net_socket_close(struct proactor_socket_t *socket);


typedef status_t (*on_accept_cb_func_t)(struct proactor_socket_t *listener_socket, struct proactor_socket_t *client_socket, status_t status, void *sock_data, void *app_data);
typedef status_t (*on_close_cb_func_t)(struct proactor_socket_t *socket, status_t status, void *sock_data, void *app_data);
typedef status_t (*on_receive_cb_func_t)(struct proactor_socket_t *socket, struct sockaddr *remote_addr, proactor_buf_t *buffer, status_t status, void *sock_data, void *app_data);
typedef status_t (*on_sent_cb_func_t)(struct proactor_socket_t *socket, proactor_buf_t *buffer, status_t status, void *sock_data, void *app_data);
typedef status_t (*on_tick_cb_func_t)(struct proactor_socket_t *socket, status_t status, void *sock_data, void *app_data);


extern status_t proactor_net_socket_set_accept_callback(struct proactor_socket_t *listener_socket, on_accept_cb_func_t accept_cb);
extern status_t proactor_net_socket_set_close_callback(struct proactor_socket_t *socket, on_close_cb_func_t close_cb);
extern status_t proactor_net_socket_set_receive_callback(struct proactor_socket_t *socket, on_receive_cb_func_t receive_cb);
extern status_t proactor_net_socket_set_sent_callback(struct proactor_socket_t *socket, on_sent_cb_func_t sent_cb);
extern status_t proactor_net_socket_set_tick_callback(struct proactor_socket_t *socket, on_tick_cb_func_t tick_cb);

extern status_t proactor_net_start_accept(struct proactor_socket_t *listener_socket);
extern status_t proactor_net_start_receive(struct proactor_socket_t *socket, proactor_buf_t *buf);
extern status_t proactor_net_start_send(struct proactor_socket_t *socket, proactor_buf_t *buf);
extern status_t proactor_net_start_timer(struct proactor_socket_t *socket);
