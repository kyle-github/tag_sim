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



#define NUM_EVENTS (32)


typedef enum {
    REQUEST_TYPE_ACCEPT,
    REQUEST_TYPE_READ,
    REQUEST_TYPE_WRITE,
    REQUEST_TYPE_TIMER,
} proactor_request_type_t;


typedef enum {
    SOCKET_TYPE_TCP_ACCEPT,
    SOCKET_TYPE_TCP_CLIENT,
    SOCKET_TYPE_UDP,
} proactor_socket_type_t;

struct proactor_t {
    /* implementation-specific data */
    int kq;
    int wakeup_fds[2];

    /* implementation-independent data */
    struct timespec tick_time_spec;
    bool stop;
    status_t status;

    proactor_event_cb_t event_cb;
    void *app_data;

    struct proactor_socket_t *sockets;
};



struct proactor_socket_t {
    struct proactor_socket_t *next;

    SOCKET sock;
    proactor_socket_type_t socket_type;
    status_t status;
    struct sockaddr remote_addr;

    struct proactor_t *proactor;

    proactor_buf_t *buffer;

    on_accept_cb_func_t accept_cb;
    on_close_cb_func_t close_cb;
    on_receive_cb_func_t receive_cb;
    on_sent_cb_func_t sent_cb;
    on_tick_cb_func_t tick_cb;

    void *sock_data;
};







struct proactor_t *proactor_net_create(proactor_event_cb_t event_cb, void *sock_data, void *app_data, uint64_t tick_period_ms)
{
    status_t rc = STATUS_OK;
    struct proactor_t *proactor = NULL;

    info("Starting.");

    do {
        struct timespec ts = {0};
        struct kevent ev;

        detail("Allocating memory for new proactor.");

        if(!(proactor = calloc(1, sizeof(*proactor)))) {
            warn("Unable to allocate proactor data!");
            rc = STATUS_NO_RESOURCE;
            break;
        }


        proactor->wakeup_fds[0] = INVALID_SOCKET;
        proactor->wakeup_fds[1] = INVALID_SOCKET;

        ts.tv_sec = tick_period_ms / 1000;
        ts.tv_nsec = (tick_period_ms % 1000) * 1000000;

        proactor->tick_time_spec = ts;
        proactor->stop = false;
        proactor->event_cb = event_cb;
        proactor->app_data = app_data;

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


/* Clean up all resources.  */
void proactor_net_dispose(struct proactor_t *proactor)
{
    info("Starting.");

    if(!proactor) {
        warn("Called with a NULL proactor pointer!");
        return;
    }

    proactor_net_stop(proactor);

    /* call the dispose callback to let the app know that we are closing down. */
    if(proactor->event_cb) {
        proactor->event_cb(proactor, PROACTOR_EVENT_DISPOSE, proactor->status, proactor->app_data);
    }

    /* clean up all the sockets */
    for(struct proactor_socket_t *cur = proactor->sockets; cur; ) {
        struct proactor_socket_t *next = cur->next;

        if(cur->close_cb) {
            cur->close_cb(socket_to_handle(cur->sock), cur->status, cur->sock_data);
        }

        /* close the socket */
        close(cur->sock);

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



status_t proactor_net_get_status(struct proactor_t *proactor)
{
    status_t rc = STATUS_OK;

    if(proactor) {
        rc = proactor->status;
    } else {
        rc = STATUS_NULL_PTR;
    }

    return rc;
}







void proactor_net_run(struct proactor_t *proactor)
{
    if(!proactor) {
        warn("Called with a NULL proactor pointer!");
        return;
    }

    struct kevent events[NUM_EVENTS];

    while (!proactor->stop) {
        /* Get the events */
        int num_triggered_events = kevent(proactor->kq, NULL, 0, events, NUM_EVENTS, &(proactor->tick_time_spec));
        if (num_triggered_events == -1) {
            if (errno == EINTR) {
                warn("kevent() call interrupted by signal!");

                /* Can this interruption actually happen? */
                continue;
            }
        }

        for (int i = 0; i < num_triggered_events; i++) {
            struct proactor_socket_t *socket = (struct proactor_socket_t *)(events[i].udata);

            if(events[i].ident == proactor->wakeup_fds[0]) {
                char buf;

                detail("Proactor woken up.");

                /* we need to clear the pipe so that we do not triggered READ again. */
                read(proactor->wakeup_fds[0], &buf, 1);

            } else {
                if (events[i].filter == EVFILT_READ) {
                    /* call the right callback depending on the type of the socket. */
                    if(socket) {
                        if(socket->socket_type == PROACTOR_SOCK_TCP_LISTENER) {
                            /* do accept */
                            /* FIXME - should check result! */
                            process_accept_ready(proactor, socket);
                        } else if(socket->socket_type == PROACTOR_SOCK_TCP_CLIENT || socket->socket_type == PROACTOR_SOCK_UDP) {
                            process_read_ready(proactor, socket);
                        } else {
                            warn("Unknown socket type %d!", socket->socket_type);
                        }
                    } /* else FIXME - what should we do here? */
                }

                if (events[i].filter == EVFILT_WRITE) {
                    /* the socket is writable */
                    process_write_ready(proactor, socket);
                }

                /* else?  FIXME - how to handle other filters? */
            }
        }

        /* call the tick CB on the proactor instance. */
        if(proactor->event_cb) {
            proactor->event_cb(proactor, PROACTOR_EVENT_TICK, STATUS_OK, proactor->app_data);
        }

        /* call the tick CB on all the sockets. */
        for(struct proactor_socket_t *sock; sock; sock = sock->next) {
            if(sock->tick_cb) {
                sock->tick_cb(sock, STATUS_OK, sock->sock_data, proactor->app_data);
            }
        }
    }
}




void proactor_net_stop(struct proactor_t *proactor)
{
    if(proactor) {
        proactor->stop = true;
        proactor_net_wake(proactor);
    }
}



void proactor_net_wake(struct proactor_t *proactor)
{
    if(proactor) {
        char buf = 1;
        if (write(proactor->wakeup_fds[1], &buf, 1) == -1) {
            warn("Unable to write to wake pipe!");
            /* FIXME - what should we do with errors? */
        }
    }
}






status_t process_accept_ready(struct proactor_t *proactor, struct proactor_socket_t *server_sock)
{
    status_t rc = STATUS_OK;

    info("Starting.");

    do {
        struct proactor_socket_t *client = NULL;
        struct sockaddr client_addr = {0};

        if(!server_sock->accept_cb) {
            warn("No callback on listener socket!");
            rc = STATUS_NULL_PTR;
            break;
        }

        /* allocate a new socket struct */
        client = (struct proactor_socket_t *)calloc(1, sizeof(*client));
        if(!client) {
            warn("Unable to allocate new socket struct instance!");
            rc = STATUS_NO_RESOURCE;
            break;
        }

        client->socket_type = SOCKET_TYPE_TCP_CLIENT;

        /* get the new connection file descriptor */
        client->sock = accept(server_sock->sock, &client_addr, sizeof(client_addr));

        /* accept returns a positive, non-zero number for success and a negative number for failure. */
        if(client->sock > 0) {
            client->remote_addr = client_addr;
        } else {
            warn("Error calling accept() on listening socket!");
            rc = STATUS_INTERNAL_FAILURE;
            /* we do _NOT_ break here as we want to fall through to the line below. */
        }

        /* call the callback */
        server_sock->accept_cb(server_sock, client, rc, server_sock->sock_data, proactor->app_data);
    } while(0);

    info("Done with status %s.", status_to_str(rc));

    return rc;
}



status_t process_read_ready(struct proactor_t *proactor, struct proactor_socket_t *sock)
{
    status_t rc = STATUS_OK;

    info("Starting.");

    do {
        socklen_t addr_len = sizeof(sock->remote_addr);
        ssize_t read_rc = 0;

        if(!sock->receive_cb) {
            warn("No read callback on socket!");
            rc = STATUS_NULL_PTR;
            break;
        }

        read_rc = recvfrom(sock->sock, sock->buffer->data, sock->buffer->data_length, 0, &(sock->remote_addr), &addr_len);

        if(read_rc > 0) {
            /* we got data! */
            sock->buffer->data_length = read_rc;

            if(sock->receive_cb) {
                sock->receive_cb(sock, &(sock->remote_addr), &(sock->buffer), rc, sock->sock_data, proactor->app_data);
            }
        } else if(read_rc == 0) {
            /* socket closed? */
            if(sock->close_cb) {
                sock->close_cb(sock, rc, sock->sock_data, proactor->app_data);
            }
        } else {
            /* read_rc < 0 */
        }
    } while(0);

    info("Done with status %s.", status_to_str(rc));

    return rc;
}




status_t process_write_ready(struct proactor_t *proactor, struct proactor_socket_t *sock)
{
    status_t rc = STATUS_OK;



}


// void proactor_net_run(proactor_t *proactor)
// {
//     if(!proactor) {
//         warn("Called with null proactor pointer!");
//         return;
//     }


//     while(!proactor->stop) {
//         struct kevent events[NUM_EVENTS];
//         int kevent_rc = 0;

//         kevent_rc = kevent(proactor->kq, NULL, 0, events, NUM_EVENTS, &(proactor->tick_time_spec));
//         if(kevent_rc < 0) {
//             warn("System error getting events!");
//             proactor->status = STATUS_INTERNAL_FAILURE;
//             break;
//         }

//         if(kevent_rc == 0) {
//             /* timeout */
//             if(proactor->event_cb) {
//                 proactor->event_cb(proactor, PROACTOR_EVENT_TIMEOUT, proactor->status, proactor->app_data);
//             }

//             continue;
//         }

//         /* kevent_rc is > 0 so we got some events */
//         for(int i=0; i < kevent_rc; i++) {
//             if (events[i].ident == proactor->wakeup_fds[0]) {


//                 /* read the wakeup socket to clear the data. */
//                 char tmp;
//                 read(proactor->wakeup_fds[0], &tmp, sizeof(tmp));

//                 if(proactor->event_cb) {
//                     proactor->event_cb(proactor, PROACTOR_EVENT_WAKE, proactor->app_data);
//                 }
//             } else {
//                 /* this is a read or write event */
//                 int event = events[i].filter;
//                 struct proactor_socket_t *sock = (struct proactor_socket_t *)(events[i].udata);

//                 if(event == EVFILT_READ) {
//                     if(sock->socket_type == PROACTOR_SOCK_TCP_LISTENER) {
//                         process_accept_ready(proactor, sock);
//                     } else {
//                         process_read_ready(proactor, sock);
//                     }
//                 } else if(event == EVFILT_WRITE) {
//                     process_write_ready(proactor, sock);
//                 }
//             }
//         }
//     }
// }


// void proactor_net_stop(proactor_t *proactor);

// void proactor_net_wake(proactor_t *proactor);


/* start up a listening socket and wait for clients to connect. */
// status_t proactor_net_start_server(proactor_t *proactor, const char *address, uint16_t port)
// {
//     status_t rc = STATUS_OK;
//     struct accept_context_t *accept_context = NULL;

//     info("Starting.");

//     do {
//         struct sockaddr_in server_addr;
//         struct kevent ev;

//         if(!proactor) {
//             warn("Called with a null proactor pointer!");
//             rc = STATUS_NULL_PTR;
//             break;
//         }

//         /* allocate a new accept context object */
//         if(!(accept_context = calloc(1, sizeof(*accept_context)))) {
//             warn("Unable to allocate accept context!");
//             rc = STATUS_NO_RESOURCE;
//             break;
//         }

//         accept_context->server_socket = INVALID_SOCKET;
//         accept_context->base.context_type = CONTEXT_ACCEPT;
//         accept_context->accept_cb = accept_cb;
//         accept_context->app_data = app_data;

//         /* clear the server address struct */
//         memset(&server_addr, 0, sizeof(server_addr));

//         /* create a new listening socket. */
//         if((accept_context->server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
//             /* FIXME - handle errno errors */
//             warn("Unable to open server socket!");
//             rc = STATUS_INTERNAL_FAILURE;
//             break;
//         }

//         /* start configuring the address and port info for the server socket */
//         server_addr.sin_family = AF_INET;
//         server_addr.sin_port = htons(port);

//         /* set up the address */
//         if(!address || strlen(address) == 0) {
//             /* nothing was passed for the local address, so chose them all */
//             server_addr.sin_addr.s_addr = INADDR_ANY;
//         } else {
//             /* FIXME - check results! what if address is a host name? */
//             inet_pton(AF_INET, address, &server_addr.sin_addr);
//         }

//         if (bind(accept_context->server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
//             /* FIXME - handle errno errors */
//             warn("Unable to open server socket!");
//             rc = STATUS_INTERNAL_FAILURE;
//             break;
//         }

//         if (listen(accept_context->server_socket, SOMAXCONN) == -1) {
//             /* FIXME - handle errno errors */
//             warn("Unable to open server socket!");
//             rc = STATUS_INTERNAL_FAILURE;
//             break;
//         }

//         EV_SET(&ev, accept_context->server_socket, EVFILT_READ, EV_ADD, 0, 0, accept_context);
//         if (kevent(proactor->kq, &ev, 1, NULL, 0, NULL) == -1) {
//             /* FIXME - handle errno errors */
//             warn("Unable to open server socket!");
//             rc = STATUS_INTERNAL_FAILURE;
//             break;
//         }

//         /*
//          * link the context up in the proactor object.
//          * Do this last so that we do not need to unlink it from the proactor
//          * if something has gone wrong.
//          */
//         accept_context->base.next = proactor->contexts;
//         proactor->contexts = accept_context;
//     } while(0);

//     /* clean up if we need to. */
//     if(rc != STATUS_OK) {
//         warn("Error %s while attempting to set up a new server socket.", status_to_str(rc));

//         if(accept_context) {
//             /* if there is a callback, call it with the failure. */
//             if(accept_context->accept_cb) {
//                 /* FIXME - should we pass rc or STATUS_TERMINATE here? */
//                 accept_cb(accept_context->server_socket, INVALID_SOCKET, rc, app_data);
//             }

//             if(accept_context->server_socket != INVALID_SOCKET) {
//                 close(accept_context->server_socket);
//             }

//             free(accept_context);
//         }
//     }

//     info("Done with status %s.", status_to_str);

//     return rc;
// }



// struct client_context_t {
//     struct base_context_t base;

//     SOCKET client_sock;

//     on_receive_cb_func_t receive_cb;
//     on_send_complete_cb_func_t sent_cb;
//     void *app_data;
// };


/* client connection */
// status_t proactor_net_connect(struct proactor_t *proactor, const char *address, uint16_t port, on_receive_cb_func_t receive_cb, on_send_complete_cb_func_t sent_cb, void *app_data)
// {
//     status_t rc = STATUS_OK;
//     struct client_context_t *client_context = NULL;

//     info("Starting.");

//     do {
//         struct sockaddr_in server_addr;
//         struct kevent ev[2];

//         if(!proactor) {
//             warn("Called with a null proactor pointer!");
//             rc = STATUS_NULL_PTR;
//             break;
//         }

//         /* allocate a new accept context object */
//         if(!(client_context = calloc(1, sizeof(*client_context)))) {
//             warn("Unable to allocate accept context!");
//             rc = STATUS_NO_RESOURCE;
//             break;
//         }

//         client_context->base.context_type = CONTEXT_CLIENT;
//         client_context->receive_cb = receive_cb;
//         client_context->sent_cb = sent_cb;
//         client_context->app_data = app_data;

//         /* clear the server address struct */
//         memset(&server_addr, 0, sizeof(server_addr));

//         /* create a new listening socket. */
//         if((client_context->client_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
//             /* FIXME - handle errno errors */
//             warn("Unable to open server socket!");
//             rc = STATUS_INTERNAL_FAILURE;
//             break;
//         }

//         /* start configuring the address and port info for the server socket */
//         server_addr.sin_family = AF_INET;
//         server_addr.sin_port = htons(port);

//         /* set up the address */
//         if(!address || strlen(address) == 0) {
//             /* nothing was passed for the local address, so chose them all */
//             server_addr.sin_addr.s_addr = INADDR_ANY;
//         } else {
//             /* FIXME - check results! what if address is a host name? */
//             inet_pton(AF_INET, address, &server_addr.sin_addr);
//         }

//         /* connect up to the remote system. */
//         if(connect(client_context->client_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
//             /* FIXME - handle errno errors */
//             warn("Unable to open server socket!");
//             rc = STATUS_INTERNAL_FAILURE;
//             break;
//         }

//         EV_SET(&ev[0], client_context->client_sock, EVFILT_READ, EV_ADD, 0, 0, client_context);
//         EV_SET(&ev[1], client_context->client_sock, EVFILT_WRITE, EV_ADD, 0, 0, client_context);
//         if(kevent(proactor->kq, ev, 2, NULL, 0, NULL) == -1) {
//             /* FIXME - handle errno errors */
//             warn("Unable to open server socket!");
//             rc = STATUS_INTERNAL_FAILURE;
//             break;
//         }

//         /*
//          * link the context up in the proactor object.
//          * Do this last so that we do not need to unlink it from the proactor
//          * if something has gone wrong.
//          */
//         client_context->base.next = proactor->contexts;
//         proactor->contexts = client_context;
//     } while(0);

//     /* clean up if we need to. */
//     if(rc != STATUS_OK) {
//         warn("Error %s while attempting to set up a new server socket.", status_to_str(rc));

//         if(client_context) {
//             /* FIXME - which callback should we call?. */
//             if(client_context->sent_cb) {
//                 /* FIXME - should we pass rc or STATUS_TERMINATE here? */
//                 sent_cb(client_context->client_sock, rc, app_data);
//             }

//             if(client_context->client_sock && client_context->client_sock != INVALID_SOCKET) {
//                 close(client_context->client_sock);
//             }

//             free(client_context);
//         }
//     }

//     return 0;
// }

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

// // Handle accept event
// static void handle_accept_event(int fd) {
//     int client_socket = accept(fd, NULL, NULL);
//     if (client_socket == -1) {
//         perror("accept");
//         return;
//     }

//     if (accept_callback) {
//         accept_callback(fd, client_socket, accept_callback_user_data);
//     }

//     struct kevent ev;
//     EV_SET(&ev, client_socket, EVFILT_READ, EV_ADD, 0, 0, NULL);
//     EV_SET(&ev, client_socket, EVFILT_WRITE, EV_ADD, 0, 0, NULL); // Monitor for write readiness
//     if (kevent(kq, &ev, 2, NULL, 0, NULL) == -1) {
//         handle_error("kevent");
//     }
// }

// // Wake up the proactor run loop
// void proactor_net_wake() {
//     char buf = 1;
//     if (write(proactor->wakeup_fds[1], &buf, 1) == -1) {
//         handle_error("write");
//     }
// }

// // Run the kevent event loop
// void proactor_net_run() {
//     struct kevent events[EVENT_SIZE];

//     while (!stop) {
//         int num_triggered_events = kevent(kq, NULL, 0, events, EVENT_SIZE, NULL);
//         if (num_triggered_events == -1) {
//             if (errno == EINTR) {
//                 // Interrupted by signal, continue
//                 continue;
//             }
//             handle_error("kevent");
//         }

//         for (int i = 0; i < num_triggered_events; i++) {
//             if (events[i].ident == proactor->wakeup_fds[0]) {
//                 // Handle wake-up event
//                 char buf;
//                 read(proactor->wakeup_fds[0], &buf, 1);
//             } else if (events[i].ident == server_socket) {
//                 // Handle accept event
//                 handle_accept_event(events[i].ident);
//             } else if (events[i].ident == client_socket) {
//                 if (events[i].filter == EVFILT_READ) {
//                     // Handle data event
//                     handle_data_event(events[i].ident);
//                 }
//                 if (events[i].filter == EVFILT_WRITE) {
//                     // Handle write event
//                     handle_write_event(events[i].ident);
//                 }
//             }
//         }
//     }
// }

// Stop the kevent event loop
// void proactor_net_stop() {
//     stop = 1;
//     proactor_net_wake(); // Ensure the loop is woken up
// }

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
