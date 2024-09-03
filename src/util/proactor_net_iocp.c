
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
#include <winsock2.h>
#include <windows.h>

#define BUFFER_SIZE 8192
#define MAX_PENDING_CONNECTIONS 10

typedef struct {
    OVERLAPPED overlapped;
    WSABUF wsabuf;
    char buffer[BUFFER_SIZE];
    void *user_data;
} IO_OPERATION;

static HANDLE iocp;
static SOCKET server_socket = INVALID_SOCKET;
static SOCKET client_socket = INVALID_SOCKET;
static HANDLE completion_port;
static volatile int stop = 0;
static on_receive_cb_func_t receive_callback = NULL;
static on_send_complete_cb_func_t send_complete_callback = NULL;
static on_accept_cb_func_t accept_callback = NULL;
static void *receive_callback_user_data = NULL;
static void *send_complete_callback_user_data = NULL;
static void *accept_callback_user_data = NULL;

// Error handling
void handle_error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// Initialize IOCP
int proactor_tcp_iocp_init() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        handle_error("WSAStartup");
    }

    iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (iocp == NULL) {
        handle_error("CreateIoCompletionPort");
    }

    return 0;
}

// Cleanup IOCP
void proactor_tcp_iocp_cleanup() {
    if (server_socket != INVALID_SOCKET) {
        closesocket(server_socket);
    }
    if (client_socket != INVALID_SOCKET) {
        closesocket(client_socket);
    }
    if (iocp != NULL) {
        CloseHandle(iocp);
    }
    WSACleanup();
}

// Start listening server
int proactor_tcp_iocp_start_server(uint16_t port) {
    struct sockaddr_in server_addr;
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        handle_error("socket");
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        handle_error("bind");
    }

    if (listen(server_socket, MAX_PENDING_CONNECTIONS) == SOCKET_ERROR) {
        handle_error("listen");
    }

    if (CreateIoCompletionPort((HANDLE)server_socket, iocp, (ULONG_PTR)server_socket, 0) == NULL) {
        handle_error("CreateIoCompletionPort");
    }

    return 0;
}

// Connect to a server
int proactor_tcp_iocp_connect(const char *address, uint16_t port) {
    struct sockaddr_in server_addr;
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        handle_error("socket");
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, address, &server_addr.sin_addr);

    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        handle_error("connect");
    }

    if (CreateIoCompletionPort((HANDLE)client_socket, iocp, (ULONG_PTR)client_socket, 0) == NULL) {
        handle_error("CreateIoCompletionPort");
    }

    return 0;
}

// Handle data received
static void handle_data_event(SOCKET socket) {
    IO_OPERATION *io_op = (IO_OPERATION*)malloc(sizeof(IO_OPERATION));
    if (!io_op) {
        handle_error("malloc");
    }

    io_op->wsabuf.buf = io_op->buffer;
    io_op->wsabuf.len = BUFFER_SIZE;

    DWORD bytes_received = 0;
    DWORD flags = 0;
    int result = WSARecv(socket, &io_op->wsabuf, 1, &bytes_received, &flags, &io_op->overlapped, NULL);
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        handle_error("WSARecv");
    }

    // Handle received data
    if (bytes_received > 0 && receive_callback) {
        receive_callback(socket, io_op->buffer, bytes_received, receive_callback_user_data);
    }
}

// Handle completed send
static void handle_send_complete(SOCKET socket, IO_OPERATION *io_op) {
    if (send_complete_callback) {
        send_complete_callback(socket, send_complete_callback_user_data);
    }
    free(io_op);
}

// Handle accept event
static void handle_accept_event(SOCKET server_socket) {
    SOCKET client_socket = accept(server_socket, NULL, NULL);
    if (client_socket == INVALID_SOCKET) {
        handle_error("accept");
    }

    if (CreateIoCompletionPort((HANDLE)client_socket, iocp, (ULONG_PTR)client_socket, 0) == NULL) {
        handle_error("CreateIoCompletionPort");
    }

    if (accept_callback) {
        accept_callback(server_socket, client_socket, accept_callback_user_data);
    }
}

// Run the IOCP event loop
void proactor_tcp_iocp_run() {
    while (!stop) {
        DWORD bytesTransferred;
        ULONG_PTR key;
        OVERLAPPED *overlapped;
        BOOL result = GetQueuedCompletionStatus(iocp, &bytesTransferred, &key, &overlapped, INFINITE);
        if (!result) {
            handle_error("GetQueuedCompletionStatus");
        }

        SOCKET socket = (SOCKET)key;
        if (socket == (SOCKET)server_socket) {
            handle_accept_event(server_socket);
        } else if (socket == (SOCKET)client_socket) {
            // Handle read/write based on the event
            handle_data_event(socket);
        }
    }
}

// Stop the IOCP event loop
void proactor_tcp_iocp_stop() {
    stop = 1;
}

// Set callbacks
void proactor_tcp_iocp_set_receive_callback(on_receive_cb_func_t callback, void *user_data) {
    receive_callback = callback;
    receive_callback_user_data = user_data;
}

void proactor_tcp_iocp_set_send_complete_callback(on_send_complete_cb_func_t callback, void *user_data) {
    send_complete_callback = callback;
    send_complete_callback_user_data = user_data;
}

void proactor_tcp_iocp_set_accept_callback(on_accept_cb_func_t callback, void *user_data) {
    accept_callback = callback;
    accept_callback_user_data = user_data;
}

// Send data
int proactor_tcp_iocp_send(SOCKET socket, const char *data, size_t length) {
    IO_OPERATION *io_op = (IO_OPERATION*)malloc(sizeof(IO_OPERATION));
    if (!io_op) {
        handle_error("malloc");
    }

    io_op->wsabuf.buf = (char*)data;
    io_op->wsabuf.len = length;

    DWORD bytes_sent = 0;
    int result = WSASend(socket, &io_op->wsabuf, 1, &bytes_sent, 0, &io_op->overlapped, NULL);
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        handle_error("WSASend");
    }

    return 0;
}

// Wake up the proactor
void proactor_tcp_iocp_wake() {
    // This implementation does not use a wake-up mechanism for IOCP
}
