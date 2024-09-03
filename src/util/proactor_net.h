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
    PROACTOR_EVENT_WAKE,
    PROACTOR_EVENT_RUN,
    PROACTOR_EVENT_STOP,
    PROACTOR_EVENT_DISPOSE
} proactor_event_t;


typedef enum {
    PROACTOR_CB_STATUS_DONE,
    PROACTOR_CB_STATUS_CONTINUE,
} proactor_cb_status_t;

/* forward declaration.  Implementation is hidden. */
typedef struct proactor_t proactor_t;

/* callback for proactor events */
typedef status_t (*proactor_event_cb_t)(proactor_t *proactor, proactor_event_t event, void *app_data);

extern proactor_t *proactor_net_create(proactor_event_cb_t event_cb, void *app_data);
extern void proactor_net_dispose(proactor_t *proactor);

extern status_t proactor_net_get_status(proactor_t *proactor);

extern void proactor_net_run(proactor_t *proactor);
extern void proactor_net_stop(proactor_t *proactor);

extern void proactor_net_wake(proactor_t *proactor);



/*
 * The following are functions that deal with individual sockets.  The actual
 * socket data is hidden inside the implementation and handles are used to
 * identify which socket we want.
 */


typedef struct {
    void *data_buffer;
    size_t buffer_capacity;
    size_t data_length;
} proactor_buf_t;


typedef intptr_t proactor_sock_handle_t;



/**
 * @brief Open a socket of the requested type and provide a handle to it.
 *
 * The address and port are used differently depending on the type of socket:
 *
 * PROACTOR_SOCK_TCP_LISTENER - address and port are local for binding and listening.
 *
 * PROACTOR_SOCK_TCP_CLIENT - address and port are locate a remote service.
 *
 * PROACTOR_SOCK_UDP - address and port are optional and indicate a local address binding.
 *
 * @param proactor - pointer to the proactor instance.
 * @param sock_handle_ptr - pointer to where the socket handle should be set.
 * @param socket_type - type of the socket.
 * @param address - IP address as a string.
 * @param port - port as an unsigned 16-bit integer
 * @return status_t
 */
extern status_t proactor_net_open_socket(proactor_t *proactor, proactor_sock_handle_t *socket_handle_ptr, proactor_socket_type_t socket_type, const char *address, uint16_t port);

extern status_t proactor_net_socket_close(proactor_sock_handle_t sock_handle);

/* accept new client connections */
typedef proactor_cb_status_t (*on_accept_cb_func_t)(proactor_sock_handle_t listener_sock_handle, proactor_sock_handle_t client_sock_handle, void *app_data);
extern status_t proactor_net_start_accept(proactor_sock_handle_t listener_sock_handle, on_accept_cb_func_t accept_cb, void *app_data);


/* receive data */
typedef proactor_cb_status_t (*on_receive_cb_func_t)(proactor_sock_handle_t sock_handle, const char *address, uint16_t port, proactor_buf_t *buffer, status_t status, void *app_data);
extern status_t proactor_net_start_receive(proactor_sock_handle_t socket_handle, proactor_buf_t *buffer, on_receive_cb_func_t receive_cb, void *app_data);


/* send data */
typedef proactor_cb_status_t (*on_sent_cb_func_t)(proactor_sock_handle_t sock_handle, proactor_buf_t *buffer, status_t status, void *app_data);
extern status_t proactor_net_start_send(proactor_sock_handle_t socket_handle, proactor_buf_t *buffer, on_sent_cb_func_t receive_cb, void *app_data);
