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


#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <errno.h>

#include "debug.h"
#include "status.h"

#include "proactor_net.h"


#define BUFFER_SIZE 8192
#define EVENT_SIZE 64


// static int kq;
// static int server_socket = -1;
// static int client_socket = -1;
// static int wakeup_fds[2];
// static int stop = 0;
// static on_receive_cb_func_t receive_callback = NULL;
// static on_send_complete_cb_func_t send_complete_callback = NULL;
// static on_accept_cb_func_t accept_callback = NULL;
// static void *receive_callback_user_data = NULL;
// static void *send_complete_callback_user_data = NULL;
// static void *accept_callback_user_data = NULL;
// static send_request_t *current_send_request = NULL;


typedef enum {
    CONTEXT_ACCEPT,
    CONTEXT_CLIENT,
} context_type_t;



struct base_context_t {
    context_type_t context_type;
    struct base_context_t *next;
};



struct accept_context_t {
    struct base_context_t base;

    SOCKET server_socket;

    on_accept_cb_func_t accept_cb;
    void *app_data;
};





struct proactor_t {
    int kq;
    int wakeup_fds[2];
    bool stop;
    status_t status;

    struct base_context_t *contexts;
};




// Helper function for error handling
void handle_error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}


proactor_p proactor_net_create(void)
{
    status_t rc = STATUS_OK;
    proactor_p proactor = NULL;

    info("Starting.");

    do {
        struct kevent ev;

        detail("Allocating memory for new proactor.");

        if(!(proactor = calloc(1, sizeof(*proactor)))) {
            warn("Unable to allocate proactor data!");
            rc = STATUS_NO_RESOURCE;
            break;
        }

        proactor->wakeup_fds[0] = INVALID_SOCKET;
        proactor->wakeup_fds[1] = INVALID_SOCKET;
        proactor->stop = false;

        detail("Opening kernel event queue file descriptor.");

        if((proactor->kq = kqueue()) == -1) {
            /* FIXME - handle errno errors */
            warn("Unable to open kernel queue!");
            rc = STATUS_INTERNAL_FAILURE;
            break;
        }

        detail("Opening wake pipe.");

        if(pipe(proactor->wakeup_fds) == -1) {
            /* FIXME - handle errno errors */
            warn("Unable to open kernel queue!");
            rc = STATUS_INTERNAL_FAILURE;
            break;
        }

        detail("Setting up event watching for the wake pipe.");

        EV_SET(&ev, proactor->wakeup_fds[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
        if(kevent(proactor->kq, &ev, 1, NULL, 0, NULL) == -1) {
            /* FIXME - handle errno errors */
            warn("Unable to open kernel queue!");
            rc = STATUS_INTERNAL_FAILURE;
            break;
        }
    } while(0);

    if(proactor) {
        proactor->status = rc;
    }

    return proactor;
}



// Cleanup the kevent system
void proactor_net_cleanup(proactor_p proactor)
{
    info("Starting.");

    if(!proactor) {
        warn("Called with a NULL proactor pointer!");
        return;
    }

    /* cleanup all the contexts */
    for(struct base_context_t *cur = proactor->contexts; cur; ) {
        struct base_context_t *next = cur->next;

        if(cur->context_type == CONTEXT_ACCEPT) {
            struct accept_context_t *accept_context = (struct accept_context_t *)cur;

            if(accept_context->accept_cb) {
                accept_context->accept_cb(accept_context->server_socket, INVALID_SOCKET, STATUS_TERMINATE, accept_context->app_data);
            }

            if(accept_context->server_socket != INVALID_SOCKET) {
                close(accept_context->server_socket);
            }
        } else if(cur->context_type == CONTEXT_CLIENT) {
            struct client_context_t *client_context = (struct client_context_t *)cur;

            if(client_context->sent_cb) {
                client_context->sent_cb(client_context->client_sock, STATUS_TERMINATE, client_context->app_data);
            }

            if(client_context->client_sock != INVALID_SOCKET) {
                close(client_context->client_sock);
            }
        } else {
            warn("Unknown kind of context %d!", cur->context_type);
        }

        free(cur);

        cur = next;
    }

    if (proactor->wakeup_fds[0] != INVALID_SOCKET) {
        close(proactor->wakeup_fds[0]);
        close(proactor->wakeup_fds[1]);
    }

    close(proactor->kq);

    info("Done.");
}



/* start up a listening socket and wait for clients to connect. */
status_t proactor_net_start_server(proactor_p proactor, const char *address, uint16_t port, on_accept_cb_func_t accept_cb, void *app_data)
{
    status_t rc = STATUS_OK;
    struct accept_context_t *accept_context = NULL;

    info("Starting.");

    do {
        struct sockaddr_in server_addr;
        struct kevent ev;

        if(!proactor) {
            warn("Called with a null proactor pointer!");
            rc = STATUS_NULL_PTR;
            break;
        }

        /* allocate a new accept context object */
        if(!(accept_context = calloc(1, sizeof(*accept_context)))) {
            warn("Unable to allocate accept context!");
            rc = STATUS_NO_RESOURCE;
            break;
        }

        accept_context->server_socket = INVALID_SOCKET;
        accept_context->base.context_type = CONTEXT_ACCEPT;
        accept_context->accept_cb = accept_cb;
        accept_context->app_data = app_data;

        /* clear the server address struct */
        memset(&server_addr, 0, sizeof(server_addr));

        /* create a new listening socket. */
        if((accept_context->server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            /* FIXME - handle errno errors */
            warn("Unable to open server socket!");
            rc = STATUS_INTERNAL_FAILURE;
            break;
        }

        /* start configuring the address and port info for the server socket */
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);

        /* set up the address */
        if(!address || strlen(address) == 0) {
            /* nothing was passed for the local address, so chose them all */
            server_addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            /* FIXME - check results! what if address is a host name? */
            inet_pton(AF_INET, address, &server_addr.sin_addr);
        }

        if (bind(accept_context->server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
            /* FIXME - handle errno errors */
            warn("Unable to open server socket!");
            rc = STATUS_INTERNAL_FAILURE;
            break;
        }

        if (listen(accept_context->server_socket, SOMAXCONN) == -1) {
            /* FIXME - handle errno errors */
            warn("Unable to open server socket!");
            rc = STATUS_INTERNAL_FAILURE;
            break;
        }

        EV_SET(&ev, accept_context->server_socket, EVFILT_READ, EV_ADD, 0, 0, accept_context);
        if (kevent(proactor->kq, &ev, 1, NULL, 0, NULL) == -1) {
            /* FIXME - handle errno errors */
            warn("Unable to open server socket!");
            rc = STATUS_INTERNAL_FAILURE;
            break;
        }

        /*
         * link the context up in the proactor object.
         * Do this last so that we do not need to unlink it from the proactor
         * if something has gone wrong.
         */
        accept_context->base.next = proactor->contexts;
        proactor->contexts = accept_context;
    } while(0);

    /* clean up if we need to. */
    if(rc != STATUS_OK) {
        warn("Error %s while attempting to set up a new server socket.", status_to_str(rc));

        if(accept_context) {
            /* if there is a callback, call it with the failure. */
            if(accept_context->accept_cb) {
                /* FIXME - should we pass rc or STATUS_TERMINATE here? */
                accept_cb(accept_context->server_socket, INVALID_SOCKET, rc, app_data);
            }

            if(accept_context->server_socket != INVALID_SOCKET) {
                close(accept_context->server_socket);
            }

            free(accept_context);
        }
    }

    info("Done with status %s.", status_to_str);

    return rc;
}



struct client_context_t {
    struct base_context_t base;

    SOCKET client_sock;

    on_receive_cb_func_t receive_cb;
    on_send_complete_cb_func_t sent_cb;
    void *app_data;
};


/* client connection */
status_t proactor_net_connect(proactor_p proactor, const char *address, uint16_t port, on_receive_cb_func_t receive_cb, on_send_complete_cb_func_t sent_cb, void *app_data)
{
    status_t rc = STATUS_OK;
    struct client_context_t *client_context = NULL;

    info("Starting.");

    do {
        struct sockaddr_in server_addr;
        struct kevent ev[2];

        if(!proactor) {
            warn("Called with a null proactor pointer!");
            rc = STATUS_NULL_PTR;
            break;
        }

        /* allocate a new accept context object */
        if(!(client_context = calloc(1, sizeof(*client_context)))) {
            warn("Unable to allocate accept context!");
            rc = STATUS_NO_RESOURCE;
            break;
        }

        client_context->base.context_type = CONTEXT_CLIENT;
        client_context->receive_cb = receive_cb;
        client_context->sent_cb = sent_cb;
        client_context->app_data = app_data;

        /* clear the server address struct */
        memset(&server_addr, 0, sizeof(server_addr));

        /* create a new listening socket. */
        if((client_context->client_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            /* FIXME - handle errno errors */
            warn("Unable to open server socket!");
            rc = STATUS_INTERNAL_FAILURE;
            break;
        }

        /* start configuring the address and port info for the server socket */
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);

        /* set up the address */
        if(!address || strlen(address) == 0) {
            /* nothing was passed for the local address, so chose them all */
            server_addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            /* FIXME - check results! what if address is a host name? */
            inet_pton(AF_INET, address, &server_addr.sin_addr);
        }

        /* connect up to the remote system. */
        if(connect(client_context->client_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
            /* FIXME - handle errno errors */
            warn("Unable to open server socket!");
            rc = STATUS_INTERNAL_FAILURE;
            break;
        }

        EV_SET(&ev[0], client_context->client_sock, EVFILT_READ, EV_ADD, 0, 0, client_context);
        EV_SET(&ev[1], client_context->client_sock, EVFILT_WRITE, EV_ADD, 0, 0, client_context);
        if(kevent(proactor->kq, ev, 2, NULL, 0, NULL) == -1) {
            /* FIXME - handle errno errors */
            warn("Unable to open server socket!");
            rc = STATUS_INTERNAL_FAILURE;
            break;
        }

        /*
         * link the context up in the proactor object.
         * Do this last so that we do not need to unlink it from the proactor
         * if something has gone wrong.
         */
        client_context->base.next = proactor->contexts;
        proactor->contexts = client_context;
    } while(0);

    /* clean up if we need to. */
    if(rc != STATUS_OK) {
        warn("Error %s while attempting to set up a new server socket.", status_to_str(rc));

        if(client_context) {
            /* FIXME - which callback should we call?. */
            if(client_context->sent_cb) {
                /* FIXME - should we pass rc or STATUS_TERMINATE here? */
                sent_cb(client_context->client_sock, rc, app_data);
            }

            if(client_context->client_sock && client_context->client_sock != INVALID_SOCKET) {
                close(client_context->client_sock);
            }

            free(client_context);
        }
    }

    return 0;
}

// Handle data from the server
static void handle_data_event(int fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
    if (bytes_read <= 0) {
        if (bytes_read < 0) {
            perror("read");
        }
        close(fd);

        // Remove the client socket from the kevent monitoring
        struct kevent ev;
        EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        EV_SET(&ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        if (kevent(kq, &ev, 2, NULL, 0, NULL) == -1) {
            handle_error("kevent");
        }
        return;
    }

    if (receive_callback) {
        receive_callback(fd, buffer, bytes_read, receive_callback_user_data);
    }
}

// Handle write readiness
static void handle_write_event(int fd) {
    if (current_send_request) {
        ssize_t bytes_sent = write(fd, current_send_request->data + current_send_request->offset,
                                   current_send_request->length - current_send_request->offset);
        if (bytes_sent < 0) {
            perror("write");
            return;
        }

        current_send_request->offset += bytes_sent;
        if (current_send_request->offset >= current_send_request->length) {
            // Send completed
            if (send_complete_callback) {
                send_complete_callback(fd, send_complete_callback_user_data);
            }

            free(current_send_request->data);
            free(current_send_request);
            current_send_request = NULL;
        }
    }
}

// Handle accept event
static void handle_accept_event(int fd) {
    int client_socket = accept(fd, NULL, NULL);
    if (client_socket == -1) {
        perror("accept");
        return;
    }

    if (accept_callback) {
        accept_callback(fd, client_socket, accept_callback_user_data);
    }

    struct kevent ev;
    EV_SET(&ev, client_socket, EVFILT_READ, EV_ADD, 0, 0, NULL);
    EV_SET(&ev, client_socket, EVFILT_WRITE, EV_ADD, 0, 0, NULL); // Monitor for write readiness
    if (kevent(kq, &ev, 2, NULL, 0, NULL) == -1) {
        handle_error("kevent");
    }
}

// Wake up the proactor run loop
void proactor_net_wake() {
    char buf = 1;
    if (write(wakeup_fds[1], &buf, 1) == -1) {
        handle_error("write");
    }
}

// Run the kevent event loop
void proactor_net_run() {
    struct kevent events[EVENT_SIZE];

    while (!stop) {
        int n = kevent(kq, NULL, 0, events, EVENT_SIZE, NULL);
        if (n == -1) {
            if (errno == EINTR) {
                // Interrupted by signal, continue
                continue;
            }
            handle_error("kevent");
        }

        for (int i = 0; i < n; i++) {
            if (events[i].ident == wakeup_fds[0]) {
                // Handle wake-up event
                char buf;
                read(wakeup_fds[0], &buf, 1);
            } else if (events[i].ident == server_socket) {
                // Handle accept event
                handle_accept_event(events[i].ident);
            } else if (events[i].ident == client_socket) {
                if (events[i].filter == EVFILT_READ) {
                    // Handle data event
                    handle_data_event(events[i].ident);
                }
                if (events[i].filter == EVFILT_WRITE) {
                    // Handle write event
                    handle_write_event(events[i].ident);
                }
            }
        }
    }
}

// Stop the kevent event loop
void proactor_net_stop() {
    stop = 1;
    proactor_net_wake(); // Ensure the loop is woken up
}

// Set callbacks
void proactor_net_set_receive_callback(on_receive_cb_func_t callback, void *user_data) {
    receive_callback = callback;
    receive_callback_user_data = user_data;
}

void proactor_net_set_send_complete_callback(on_send_complete_cb_func_t callback, void *user_data) {
    send_complete_callback = callback;
    send_complete_callback_user_data = user_data;
}

void proactor_net_set_accept_callback(on_accept_cb_func_t callback, void *user_data) {
    accept_callback = callback;
    accept_callback_user_data = user_data;
}

// Send data
int proactor_net_send(int socket, const char *data, size_t length) {
    if (current_send_request) {
        // There is already a send request in progress
        return -1;
    }

    current_send_request = (send_request_t*)malloc(sizeof(send_request_t));
    if (!current_send_request) {
        handle_error("malloc");
    }

    current_send_request->data = (char*)malloc(length);
    if (!current_send_request->data) {
        handle_error("malloc");
    }
    memcpy(current_send_request->data, data, length);
    current_send_request->length = length;
    current_send_request->offset = 0;

    struct kevent ev;
    EV_SET(&ev, socket, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
    if (kevent(kq, &ev, 1, NULL, 0, NULL) == -1) {
        handle_error("kevent");
    }

    return 0;
}
