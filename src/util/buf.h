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


typedef struct {
    void *data;
    size_t data_length;
} proactor_buf_t;



#include <stdint.h>
#include <stddef.h>

// Struct definitions
struct eip_header {
    uint16_t encap_command;
    uint16_t encap_length;
    uint32_t encap_session_handle;
    uint32_t encap_status;
    uint64_t encap_sender_context;
    uint32_t encap_options;
};

struct buf_t {
    uint8_t *data;
    size_t len;
};

// Helper functions to pack and unpack data (no memcpy, no built-in functions)
static inline void encode_uint16_le(uint8_t *buf, uint16_t value) {
    buf[0] = (uint8_t)(value & 0xFF);
    buf[1] = (uint8_t)((value >> 8) & 0xFF);
}

static inline uint16_t decode_uint16_le(const uint8_t *buf) {
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static inline void encode_uint32_le(uint8_t *buf, uint32_t value) {
    buf[0] = (uint8_t)(value & 0xFF);
    buf[1] = (uint8_t)((value >> 8) & 0xFF);
    buf[2] = (uint8_t)((value >> 16) & 0xFF);
    buf[3] = (uint8_t)((value >> 24) & 0xFF);
}

static inline uint32_t decode_uint32_le(const uint8_t *buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static inline void encode_uint64_le(uint8_t *buf, uint64_t value) {
    buf[0] = (uint8_t)(value & 0xFF);
    buf[1] = (uint8_t)((value >> 8) & 0xFF);
    buf[2] = (uint8_t)((value >> 16) & 0xFF);
    buf[3] = (uint8_t)((value >> 24) & 0xFF);
    buf[4] = (uint8_t)((value >> 32) & 0xFF);
    buf[5] = (uint8_t)((value >> 40) & 0xFF);
    buf[6] = (uint8_t)((value >> 48) & 0xFF);
    buf[7] = (uint8_t)((value >> 56) & 0xFF);
}

static inline uint64_t decode_uint64_le(const uint8_t *buf) {
    return (uint64_t)buf[0] | ((uint64_t)buf[1] << 8) | ((uint64_t)buf[2] << 16) | ((uint64_t)buf[3] << 24) |
           ((uint64_t)buf[4] << 32) | ((uint64_t)buf[5] << 40) | ((uint64_t)buf[6] << 48) | ((uint64_t)buf[7] << 56);
}

// // Encode eip_header into buffer (manual byte manipulation)
// int encode_eip_header(struct buf_t *buf, const struct eip_header *header) {
//     if (buf->len < sizeof(struct eip_header)) {
//         return -1;  // Not enough space in the buffer
//     }

//     uint8_t *ptr = buf->data;

//     // Encode each field in little-endian
//     encode_uint16_le(ptr, header->encap_command);
//     ptr += sizeof(uint16_t);

//     encode_uint16_le(ptr, header->encap_length);
//     ptr += sizeof(uint16_t);

//     encode_uint32_le(ptr, header->encap_session_handle);
//     ptr += sizeof(uint32_t);

//     encode_uint32_le(ptr, header->encap_status);
//     ptr += sizeof(uint32_t);

//     encode_uint64_le(ptr, header->encap_sender_context);
//     ptr += sizeof(uint64_t);

//     encode_uint32_le(ptr, header->encap_options);

//     return 0;
// }

// // Decode eip_header from buffer (manual byte manipulation)
// int decode_eip_header(struct eip_header *header, const struct buf_t *buf) {
//     if (buf->len < sizeof(struct eip_header)) {
//         return -1;  // Not enough data in the buffer
//     }

//     const uint8_t *ptr = buf->data;

//     // Decode each field from little-endian to host format
//     header->encap_command = decode_uint16_le(ptr);
//     ptr += sizeof(uint16_t);

//     header->encap_length = decode_uint16_le(ptr);
//     ptr += sizeof(uint16_t);

//     header->encap_session_handle = decode_uint32_le(ptr);
//     ptr += sizeof(uint32_t);

//     header->encap_status = decode_uint32_le(ptr);
//     ptr += sizeof(uint32_t);

//     header->encap_sender_context = decode_uint64_le(ptr);
//     ptr += sizeof(uint64_t);

//     header->encap_options = decode_uint32_le(ptr);

//     return 0;
// }
