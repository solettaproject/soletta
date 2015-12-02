/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "sol-common-buildopts.h"
#include "sol-str-slice.h"
#include "sol-types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Soletta flow packets manipulation.
 */

/**
 * @ingroup Flow
 *
 * @{
 */

/**
 * @struct sol_flow_packet
 *
 * Packet is a generic container for different kinds of contents. A
 * packet type defines what's the content and how it's stored and
 * retrieved.
 */
struct sol_flow_packet;

struct sol_flow_packet_type {
#ifndef SOL_NO_API_VERSION
#define SOL_FLOW_PACKET_TYPE_API_VERSION (1)
    uint16_t api_version;
#endif
    uint16_t data_size;
    const char *name;

    int (*init)(const struct sol_flow_packet_type *packet_type, void *mem, const void *input);

    int (*get)(const struct sol_flow_packet_type *packet_type, const void *mem, void *output);

    void (*dispose)(const struct sol_flow_packet_type *packet_type, void *mem);

    /* Internal. Used for types that have a set of constant values, so
     * no allocation is needed. */
    struct sol_flow_packet *(*get_constant)(const struct sol_flow_packet_type *packet_type, const void *value);
};

struct sol_flow_packet *sol_flow_packet_new(const struct sol_flow_packet_type *type, const void *value);
void sol_flow_packet_del(struct sol_flow_packet *packet);

const struct sol_flow_packet_type *sol_flow_packet_get_type(const struct sol_flow_packet *packet);
int sol_flow_packet_get(const struct sol_flow_packet *packet, void *output);

extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_EMPTY;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_BOOLEAN;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_BYTE;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_IRANGE;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_STRING;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_BLOB;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_JSON_OBJECT;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_JSON_ARRAY;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_DRANGE;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_ANY;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_ERROR;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_RGB;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_LOCATION;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_TIMESTAMP;
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_HTTP_RESPONSE;

/* Convenience functions to use certain types of common packets. */
struct sol_flow_packet *sol_flow_packet_new_empty(void);

struct sol_flow_packet *sol_flow_packet_new_boolean(bool boolean);
int sol_flow_packet_get_boolean(const struct sol_flow_packet *packet, bool *boolean);

struct sol_flow_packet *sol_flow_packet_new_byte(unsigned char byte);
int sol_flow_packet_get_byte(const struct sol_flow_packet *packet, unsigned char *byte);

struct sol_flow_packet *sol_flow_packet_new_irange(const struct sol_irange *irange);
struct sol_flow_packet *sol_flow_packet_new_irange_value(int32_t value);
int sol_flow_packet_get_irange(const struct sol_flow_packet *packet, struct sol_irange *irange);
int sol_flow_packet_get_irange_value(const struct sol_flow_packet *packet, int32_t *value);

struct sol_flow_packet *sol_flow_packet_new_string(const char *value);
struct sol_flow_packet *sol_flow_packet_new_string_slice(struct sol_str_slice slice);
int sol_flow_packet_get_string(const struct sol_flow_packet *packet, const char **value);
struct sol_flow_packet *sol_flow_packet_new_string_take(char *value);

struct sol_flow_packet *sol_flow_packet_new_blob(const struct sol_blob *value);
int sol_flow_packet_get_blob(const struct sol_flow_packet *packet, struct sol_blob **value);

/* blob value mem should point to a string with a valid JSON object. May or may not be NULL terminated */
struct sol_flow_packet *sol_flow_packet_new_json_object(const struct sol_blob *value);
int sol_flow_packet_get_json_object(const struct sol_flow_packet *packet, struct sol_blob **value);

/* blob value mem should point to a string with a valid JSON array. May or may not be NULL terminated */
struct sol_flow_packet *sol_flow_packet_new_json_array(const struct sol_blob *value);
int sol_flow_packet_get_json_array(const struct sol_flow_packet *packet, struct sol_blob **value);

struct sol_flow_packet *sol_flow_packet_new_drange(const struct sol_drange *drange);
struct sol_flow_packet *sol_flow_packet_new_drange_value(double value);
int sol_flow_packet_get_drange(const struct sol_flow_packet *packet, struct sol_drange *drange);
int sol_flow_packet_get_drange_value(const struct sol_flow_packet *packet, double *value);

struct sol_flow_packet *sol_flow_packet_new_error(int code, const char *msg);
int sol_flow_packet_get_error(const struct sol_flow_packet *packet, int *code, const char **msg);

struct sol_flow_packet *sol_flow_packet_new_rgb(const struct sol_rgb *rgb);
struct sol_flow_packet *sol_flow_packet_new_rgb_components(uint32_t red, uint32_t green, uint32_t blue);
int sol_flow_packet_get_rgb(const struct sol_flow_packet *packet, struct sol_rgb *rgb);
int sol_flow_packet_get_rgb_components(const struct sol_flow_packet *packet, uint32_t *red, uint32_t *green, uint32_t *blue);

struct sol_flow_packet *sol_flow_packet_new_direction_vector(const struct sol_direction_vector *direction_vector);
struct sol_flow_packet *sol_flow_packet_new_direction_vector_components(double x, double y, double z);
int sol_flow_packet_get_direction_vector(const struct sol_flow_packet *packet, struct sol_direction_vector *direction_vector);
int sol_flow_packet_get_direction_vector_components(const struct sol_flow_packet *packet, double *x, double *y, double *z);

struct sol_flow_packet *sol_flow_packet_new_location(const struct sol_location *location);
int sol_flow_packet_get_location(const struct sol_flow_packet *packet, struct sol_location *location);

struct sol_flow_packet *sol_flow_packet_new_timestamp(const struct timespec *timestamp);
int sol_flow_packet_get_timestamp(const struct sol_flow_packet *packet, struct timespec *timestamp);

const struct sol_flow_packet_type *sol_flow_packet_type_composed_new(const struct sol_flow_packet_type **types);
bool sol_flow_packet_is_composed_type(const struct sol_flow_packet_type *type);
int sol_flow_packet_get_composed_members_packet_types(const struct sol_flow_packet_type *type, const struct sol_flow_packet_type ***children, uint16_t *len);
int sol_flow_packet_get_composed_members(const struct sol_flow_packet *packet, struct sol_flow_packet ***children, uint16_t *len);
struct sol_flow_packet *sol_flow_packet_dup(const struct sol_flow_packet *packet);

struct sol_flow_packet *sol_flow_packet_new_http_response(int response_code, const char *url, const char *content_type, const struct sol_blob *content, const struct sol_vector *cookies, const struct sol_vector *headers);

int sol_flow_packet_get_http_response(const struct sol_flow_packet *packet, int *response_code, const char **url, const char **content_type, const struct sol_blob **content, struct sol_vector *cookies, struct sol_vector *headers);

/**
 * @brief Returns the packet type variable as string.
 *
 * This function will return the sol_flow_packet_type variable for a given type as string.
 * A common use for this function is when one is generating code for meta types nodes
 * that will be used by sol-fbp-generator.
 *
 * Example:
 *
 * @code
 * // will return the string "SOL_FLOW_PACKET_TYPE_IRANGE"
 * const char *int_packet_name = sol_flow_packet_get_packet_type_as_string(sol_str_slice_from_str("int"));
 *
 * // will return the string "SOL_FLOW_PACKET_TYPE_LOCATION"
 * const char *location_packet_name = sol_flow_packet_get_packet_type_as_string(sol_str_slice_from_str("location"));
 * @endcode
 *
 * @param type The Soletta type name (int, blob, error, string, location and etc.)
 * @return The Soletta packet type variable as string or @c NULL if the type was not found.
 *
 */
const char *sol_flow_packet_get_packet_type_as_string(const struct sol_str_slice type);
/**
 * @}
 */

#ifdef __cplusplus
}
#endif
