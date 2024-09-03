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


#include "proactor_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>

#define BUFFER_SIZE 8192
#define MAX_EVENTS 64

typedef struct {
    char *data;
    size_t length;
    size_t offset;
    void *user_data;
} SendRequest;

static int epoll_fd;
static int server_socket = -1;
static int client_socket = -1;
static int wakeup_fds[2];
static int stop = 0;
static on_receive_cb_func_t receive_callback = NULL;
static on_send_complete_cb_func_t send_complete_callback = NULL;
static on_accept_cb_func_t accept_callback = NULL;
static void *receive_callback_user_data = NULL;
static void *send_complete_callback_user_data = NULL;
static void *accept_callback_user_data = NULL;
static SendRequest *current_send_request = NULL;

// Helper function for error handling
void handle_error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// Initialize the epoll system
int proactor_tcp_epoll_init() {
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        handle_error("epoll_create1");
    }

    if (pipe(wakeup_fds) == -1) {
        handle_error("pipe");
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = wakeup_fds[0];
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, wakeup_fds[0], &ev) == -1) {
        handle_error("epoll_ctl");
    }

    return 0;
}

// Cleanup the epoll system
void proactor_tcp_epoll_cleanup() {
    if (server_socket != -1) {
        close(server_socket);
    }
    if (client_socket != -1) {
        close(client_socket);
    }
    if (wakeup_fds[0] != -1) {
        close(wakeup_fds[0]);
        close(wakeup_fds[1]);
    }
    close(epoll_fd);

    // Free any remaining send request
    if (current_send_request) {
        free(current_send_request->data);
        free(current_send_request);
    }
}

// Start listening server
int proactor_tcp_epoll_start_server(uint16_t port) {
    struct sockaddr_in server_addr;
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        handle_error("socket");
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        handle_error("bind");
    }

    if (listen(server_socket, SOMAXCONN) == -1) {
        handle_error("listen");
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &ev) == -1) {
        handle_error("epoll_ctl");
    }

    return 0;
}

// Connect to the server
int proactor_tcp_epoll_connect(const char *address, uint16_t port) {
    struct sockaddr_in server_addr;
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        handle_error("socket");
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, address, &server_addr.sin_addr);

    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        handle_error("connect");
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.fd = client_socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &ev) == -1) {
        handle_error("epoll_ctl");
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

        // Remove the client socket from the epoll monitoring
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT;
        ev.data.fd = fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev) == -1) {
            handle_error("epoll_ctl");
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

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.fd = client_socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &ev) == -1) {
        handle_error("epoll_ctl");
    }
}

// Wake up the proactor run loop
void proactor_tcp_epoll_wake() {
    char buf = 1;
    if (write(wakeup_fds[1], &buf, 1) == -1) {
        handle_error("write");
    }
}

// Run the epoll event loop
void proactor_tcp_epoll_run() {
    struct epoll_event events[MAX_EVENTS];

    while (!stop) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n == -1) {
            if (errno == EINTR) {
                // Interrupted by signal, continue
                continue;
            }
            handle_error("epoll_wait");
        }

        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == wakeup_fds[0]) {
                // Handle wake-up event
                char buf;
                read(wakeup_fds[0], &buf, 1);
            } else if (events[i].data.fd == server_socket) {
                // Handle accept event
                handle_accept_event(events[i].data.fd);
            } else if (events[i].events & EPOLLIN) {
                // Handle data event
                handle_data_event(events[i].data.fd);
            } else if (events[i].events & EPOLLOUT) {
                // Handle write event
                handle_write_event(events[i].data.fd);
            }
        }
    }
}

// Stop the epoll event loop
void proactor_tcp_epoll_stop() {
    stop = 1;
    proactor_tcp_epoll_wake(); // Ensure the loop is woken up
}

// Set callbacks
void proactor_tcp_epoll_set_receive_callback(on_receive_cb_func_t callback, void *user_data) {
    receive_callback = callback;
    receive_callback_user_data = user_data;
}

void proactor_tcp_epoll_set_send_complete_callback(on_send_complete_cb_func_t callback, void *user_data) {
    send_complete_callback = callback;
    send_complete_callback_user_data = user_data;
}

void proactor_tcp_epoll_set_accept_callback(on_accept_cb_func_t callback, void *user_data) {
    accept_callback = callback;
    accept_callback_user_data = user_data;
}

// Send data
int proactor_tcp_epoll_send(int socket, const char *data, size_t length) {
    if (current_send_request) {
        // There is already a send request in progress
        return -1;
    }

    current_send_request = (SendRequest*)malloc(sizeof(SendRequest));
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

    struct epoll_event ev;
    ev.events = EPOLLOUT;
    ev.data.fd = socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, socket, &ev) == -1) {
        handle_error("epoll_ctl");
    }

    return 0;
}
