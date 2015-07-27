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

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>

#define SOL_LOG_DOMAIN &_sol_flow_log_domain
#include "sol-log-internal.h"
extern struct sol_log_domain _sol_flow_log_domain;

#include "sol-flow-packet.h"
#include "sol-util.h"

#define SOL_FLOW_PACKET_CHECK(packet, _type, ...)        \
    do {                                                \
        if (unlikely(!(packet))) {                      \
            SOL_WRN("" # packet "== NULL");              \
            return __VA_ARGS__;                         \
        }                                               \
        if (unlikely((packet)->type != _type)) {        \
            SOL_WRN("" # packet "->type != " # _type);   \
            return __VA_ARGS__;                         \
        }                                               \
    } while (0)

struct sol_flow_packet {
    const struct sol_flow_packet_type *type;
    void *data;
};

static inline void *
sol_flow_packet_get_memory(const struct sol_flow_packet *packet)
{
    if (!packet || !packet->type || !packet->type->data_size)
        return NULL;
    if (packet->type->data_size <= sizeof(packet->data))
        return (void *)&packet->data;
    return (void *)packet->data;
}

static int
packet_default_get(const struct sol_flow_packet_type *type, const void *mem, void *output)
{
    memcpy(output, mem, type->data_size);
    return 0;
}

static int
packet_default_init(const struct sol_flow_packet_type *type, void *mem, const void *input)
{
    SOL_NULL_CHECK(mem, -EINVAL);
    memcpy(mem, input, type->data_size);
    return 0;
}

static struct sol_flow_packet *
allocate_packet(const struct sol_flow_packet_type *type)
{
    struct sol_flow_packet *packet;
    uint16_t extra_mem = 0;

    if (type->data_size > sizeof(void *))
        extra_mem = type->data_size;

    packet = calloc(1, sizeof(*packet) + extra_mem);
    SOL_NULL_CHECK(packet, NULL);
    packet->type = type;
    if (extra_mem)
        packet->data = (uint8_t *)packet + sizeof(*packet);

    return packet;
}

static int
init_packet(struct sol_flow_packet *packet, const void *value)
{
    const struct sol_flow_packet_type *type;
    void *mem;

    type = packet->type;
    mem = sol_flow_packet_get_memory(packet);

    if (type->init)
        return type->init(type, mem, value);
    else if (type->data_size > 0)
        return packet_default_init(type, mem, value);

    return 0;
}

SOL_API struct sol_flow_packet *
sol_flow_packet_new(const struct sol_flow_packet_type *type, const void *value)
{
    struct sol_flow_packet *packet;
    int r;

    if (unlikely(type == SOL_FLOW_PACKET_TYPE_ANY)) {
        SOL_WRN("Couldn't create packet with type ANY. This type is used only on ports.");
        return NULL;
    }

    if (unlikely(!type)) {
        SOL_WRN("Couldn't create packet with NULL type");
        return NULL;
    }

    if (unlikely(type->api_version != SOL_FLOW_PACKET_TYPE_API_VERSION)) {
        SOL_WRN("Couldn't create packet with type '%s' that has unsupported version '%u', expected version is '%u'",
            type->name ? : "", type->api_version, SOL_FLOW_PACKET_TYPE_API_VERSION);
        return NULL;
    }

    if (type->get_constant)
        return type->get_constant(type, value);

    packet = allocate_packet(type);
    SOL_NULL_CHECK(packet, NULL);

    r = init_packet(packet, value);
    if (r < 0) {
        sol_flow_packet_del(packet);
        errno = -r;
        return NULL;
    }

    return packet;
}

SOL_API void
sol_flow_packet_del(struct sol_flow_packet *packet)
{
    SOL_NULL_CHECK(packet);

    if (packet->type->get_constant)
        return;

    if (packet->type->dispose)
        packet->type->dispose(packet->type, sol_flow_packet_get_memory(packet));
    free(packet);
}

SOL_API const struct sol_flow_packet_type *
sol_flow_packet_get_type(const struct sol_flow_packet *packet)
{
    SOL_NULL_CHECK(packet, NULL);

    return packet->type;
}

SOL_API int
sol_flow_packet_get(const struct sol_flow_packet *packet, void *output)
{
    const struct sol_flow_packet_type *type;
    const void *mem;

    SOL_NULL_CHECK(packet, -EINVAL);
    SOL_NULL_CHECK(packet->type, -EINVAL);
    SOL_NULL_CHECK(output, -EINVAL);

    type = packet->type;
    mem = sol_flow_packet_get_memory(packet);

    if (type->get)
        return type->get(type, mem, output);

    return packet_default_get(type, mem, output);
}


static int
packet_empty_get(const struct sol_flow_packet_type *type, const void *mem, void *output)
{
    return 0;
}

static const struct sol_flow_packet_type _SOL_FLOW_PACKET_TYPE_EMPTY;

static struct sol_flow_packet *
packet_empty_get_constant(const struct sol_flow_packet_type *packet_type, const void *input)
{
    static struct sol_flow_packet empty_packet = {
        .type = &_SOL_FLOW_PACKET_TYPE_EMPTY,
        .data = NULL,
    };

    return &empty_packet;
}

static const struct sol_flow_packet_type _SOL_FLOW_PACKET_TYPE_EMPTY = {
    .api_version = SOL_FLOW_PACKET_TYPE_API_VERSION,
    .name = "Empty",
    .get = packet_empty_get,
    .get_constant = packet_empty_get_constant,
};
SOL_API const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_EMPTY = &_SOL_FLOW_PACKET_TYPE_EMPTY;

SOL_API struct sol_flow_packet *
sol_flow_packet_new_empty(void)
{
    return sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_EMPTY, NULL);
}

static const struct sol_flow_packet_type _SOL_FLOW_PACKET_TYPE_BOOLEAN;

static struct sol_flow_packet *
packet_boolean_get_constant(const struct sol_flow_packet_type *packet_type, const void *value)
{
    static struct sol_flow_packet true_packet = {
        .type = &_SOL_FLOW_PACKET_TYPE_BOOLEAN,
        .data = (void *)true,
    };
    static struct sol_flow_packet false_packet = {
        .type = &_SOL_FLOW_PACKET_TYPE_BOOLEAN,
        .data = (void *)false,
    };

    bool *boolean = (bool *)value;

    if (!boolean) {
        errno = EINVAL;
        return NULL;
    }

    return *boolean ? &true_packet : &false_packet;
}

static const struct sol_flow_packet_type _SOL_FLOW_PACKET_TYPE_BOOLEAN = {
    .api_version = SOL_FLOW_PACKET_TYPE_API_VERSION,
    .name = "Boolean",
    .data_size = sizeof(bool),
    .get_constant = packet_boolean_get_constant,
};
SOL_API const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_BOOLEAN = &_SOL_FLOW_PACKET_TYPE_BOOLEAN;

SOL_API struct sol_flow_packet *
sol_flow_packet_new_boolean(bool boolean)
{
    return sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_BOOLEAN, &boolean);
}

SOL_API int
sol_flow_packet_get_boolean(const struct sol_flow_packet *packet, bool *boolean)
{
    SOL_FLOW_PACKET_CHECK(packet, SOL_FLOW_PACKET_TYPE_BOOLEAN, -EINVAL);
    return sol_flow_packet_get(packet, boolean);
}

static const struct sol_flow_packet_type _SOL_FLOW_PACKET_TYPE_IRANGE = {
    .api_version = SOL_FLOW_PACKET_TYPE_API_VERSION,
    .name = "IRange",
    .data_size = sizeof(struct sol_irange),
};

SOL_API const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_IRANGE = &_SOL_FLOW_PACKET_TYPE_IRANGE;

SOL_API struct sol_flow_packet *
sol_flow_packet_new_irange(const struct sol_irange *irange)
{
    return sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_IRANGE, irange);
}

SOL_API struct sol_flow_packet *
sol_flow_packet_new_irange_value(int32_t value)
{
    struct sol_irange irange = {
        .min = INT32_MIN,
        .max = INT32_MAX,
        .step = 1,
        .val = value,
    };

    return sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_IRANGE, &irange);
}

SOL_API int
sol_flow_packet_get_irange(const struct sol_flow_packet *packet, struct sol_irange *irange)
{
    SOL_FLOW_PACKET_CHECK(packet, SOL_FLOW_PACKET_TYPE_IRANGE, -EINVAL);
    return sol_flow_packet_get(packet, irange);
}

SOL_API int
sol_flow_packet_get_irange_value(const struct sol_flow_packet *packet, int32_t *value)
{
    struct sol_irange ret;
    int ret_val;

    SOL_FLOW_PACKET_CHECK(packet, SOL_FLOW_PACKET_TYPE_IRANGE, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    ret_val = sol_flow_packet_get(packet, &ret);
    if (ret_val == 0) {
        *value = ret.val;
    }

    return ret_val;
}

static int
string_packet_init(const struct sol_flow_packet_type *packet_type, void *mem, const void *input)
{
    char **pstring = mem;

    *pstring = (char *)input;
    return 0;
}

static void
string_packet_dispose(const struct sol_flow_packet_type *packet_type, void *mem)
{
    char **pstring = mem;

    free(*pstring);
}

static const struct sol_flow_packet_type _SOL_FLOW_PACKET_TYPE_STRING = {
    .api_version = SOL_FLOW_PACKET_TYPE_API_VERSION,
    .name = "String",
    .data_size = sizeof(char *),
    .init = string_packet_init,
    .dispose = string_packet_dispose,
};

SOL_API const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_STRING = &_SOL_FLOW_PACKET_TYPE_STRING;

SOL_API struct sol_flow_packet *
sol_flow_packet_new_string(const char *value)
{
    struct sol_flow_packet *packet;
    char *data = value ? strdup(value) : NULL;

    SOL_NULL_CHECK(data, NULL);
    packet = sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_STRING, data);
    if (likely(packet)) {
        return packet;
    }

    SOL_WRN("Could not copy string from slice to send packet");
    free(data);
    return NULL;
}

SOL_API int
sol_flow_packet_get_string(const struct sol_flow_packet *packet, const char **value)
{
    SOL_FLOW_PACKET_CHECK(packet, SOL_FLOW_PACKET_TYPE_STRING, -EINVAL);
    return sol_flow_packet_get(packet, value);
}

SOL_API struct sol_flow_packet *
sol_flow_packet_new_string_slice(struct sol_str_slice slice)
{
    struct sol_flow_packet *packet;
    char *data;

    SOL_NULL_CHECK(slice.data, NULL);

    data = strndup(slice.data, slice.len);
    SOL_NULL_CHECK(data, NULL);

    packet = sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_STRING, data);
    if (likely(packet)) {
        return packet;
    }

    SOL_WRN("Could not copy string from slice to send packet");
    free(data);
    return NULL;
}

SOL_API struct
sol_flow_packet *
sol_flow_packet_new_string_take(char *value)
{
    struct sol_flow_packet *packet;
    char **pstring;

    packet = allocate_packet(SOL_FLOW_PACKET_TYPE_STRING);
    if (!packet) {
        goto error;
    }

    pstring = sol_flow_packet_get_memory(packet);
    if (!pstring) {
        goto string_error;
    }

    *pstring = value;

    return packet;
string_error:
    sol_flow_packet_del(packet);
error:
    SOL_WRN("Could not create the packet");
    free(value);
    return NULL;
}

static int
blob_packet_init(const struct sol_flow_packet_type *packet_type, void *mem, const void *input)
{
    struct sol_blob **pblob = mem;
    struct sol_blob **pin_blob = (void *)input; /* need to ref */

    if (*pin_blob) {
        *pin_blob = sol_blob_ref(*pin_blob);
        SOL_NULL_CHECK(*pin_blob, -ENOMEM);
    }

    *pblob = *pin_blob;
    return 0;
}

static void
blob_packet_dispose(const struct sol_flow_packet_type *packet_type, void *mem)
{
    struct sol_blob **pblob = mem;

    if (*pblob)
        sol_blob_unref(*pblob);
}

static const struct sol_flow_packet_type _SOL_FLOW_PACKET_TYPE_BLOB = {
    .api_version = SOL_FLOW_PACKET_TYPE_API_VERSION,
    .name = "Blob",
    .data_size = sizeof(struct sol_blob *),
    .init = blob_packet_init,
    .dispose = blob_packet_dispose,
};

SOL_API const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_BLOB = &_SOL_FLOW_PACKET_TYPE_BLOB;

SOL_API struct sol_flow_packet *
sol_flow_packet_new_blob(const struct sol_blob *blob)
{
    SOL_NULL_CHECK(blob, NULL);
    return sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_BLOB, &blob);
}

SOL_API int
sol_flow_packet_get_blob(const struct sol_flow_packet *packet, struct sol_blob **value)
{
    SOL_FLOW_PACKET_CHECK(packet, SOL_FLOW_PACKET_TYPE_BLOB, -EINVAL);
    return sol_flow_packet_get(packet, value);
}

static const struct sol_flow_packet_type _SOL_FLOW_PACKET_TYPE_DRANGE = {
    .api_version = SOL_FLOW_PACKET_TYPE_API_VERSION,
    .name = "DRange",
    .data_size = sizeof(struct sol_drange),
};

SOL_API const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_DRANGE = &_SOL_FLOW_PACKET_TYPE_DRANGE;

SOL_API struct sol_flow_packet *
sol_flow_packet_new_drange(const struct sol_drange *drange)
{
    return sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_DRANGE, drange);
}

SOL_API struct sol_flow_packet *
sol_flow_packet_new_drange_value(double value)
{
    struct sol_drange drange = {
        .min = -DBL_MAX,
        .max = DBL_MAX,
        .step = DBL_MIN,
        .val = value,
    };

    return sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_DRANGE, &drange);
}

SOL_API int
sol_flow_packet_get_drange(const struct sol_flow_packet *packet, struct sol_drange *drange)
{
    SOL_FLOW_PACKET_CHECK(packet, SOL_FLOW_PACKET_TYPE_DRANGE, -EINVAL);
    return sol_flow_packet_get(packet, drange);
}

SOL_API int
sol_flow_packet_get_drange_value(const struct sol_flow_packet *packet, double *value)
{
    struct sol_drange ret;
    int ret_val;

    SOL_FLOW_PACKET_CHECK(packet, SOL_FLOW_PACKET_TYPE_DRANGE, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    ret_val = sol_flow_packet_get(packet, &ret);
    if (ret_val == 0) {
        *value = ret.val;
    }

    return ret_val;
}

static const struct sol_flow_packet_type _SOL_FLOW_PACKET_TYPE_BYTE = {
    .api_version = SOL_FLOW_PACKET_TYPE_API_VERSION,
    .name = "Byte",
    .data_size = sizeof(unsigned char),
};
SOL_API const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_BYTE = &_SOL_FLOW_PACKET_TYPE_BYTE;

SOL_API struct sol_flow_packet *
sol_flow_packet_new_byte(unsigned char byte)
{
    return sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_BYTE, &byte);
}

SOL_API int
sol_flow_packet_get_byte(const struct sol_flow_packet *packet, unsigned char *byte)
{
    SOL_FLOW_PACKET_CHECK(packet, SOL_FLOW_PACKET_TYPE_BYTE, -EINVAL);
    return sol_flow_packet_get(packet, byte);
}

static const struct sol_flow_packet_type _SOL_FLOW_PACKET_TYPE_RGB = {
    .api_version = SOL_FLOW_PACKET_TYPE_API_VERSION,
    .name = "RGB",
    .data_size = sizeof(struct sol_rgb),
};
SOL_API const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_RGB = &_SOL_FLOW_PACKET_TYPE_RGB;

SOL_API struct sol_flow_packet *
sol_flow_packet_new_rgb(const struct sol_rgb *rgb)
{
    return sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_RGB, rgb);
}

SOL_API struct sol_flow_packet *
sol_flow_packet_new_rgb_components(uint32_t red, uint32_t green, uint32_t blue)
{
    struct sol_rgb rgb = {
        .red_max = 255,
        .green_max = 255,
        .blue_max = 255,
        .red = red,
        .blue = blue,
        .green = green,
    };

    return sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_RGB, &rgb);
}

SOL_API int
sol_flow_packet_get_rgb(const struct sol_flow_packet *packet, struct sol_rgb *rgb)
{
    SOL_FLOW_PACKET_CHECK(packet, SOL_FLOW_PACKET_TYPE_RGB, -EINVAL);
    return sol_flow_packet_get(packet, rgb);
}

SOL_API int
sol_flow_packet_get_rgb_components(const struct sol_flow_packet *packet, uint32_t *red, uint32_t *green, uint32_t *blue)
{
    struct sol_rgb ret;
    int ret_val;

    SOL_FLOW_PACKET_CHECK(packet, SOL_FLOW_PACKET_TYPE_RGB, -EINVAL);
    ret_val = sol_flow_packet_get(packet, &ret);
    if (ret_val == 0) {
        if (red) *red = ret.red;
        if (blue) *blue = ret.blue;
        if (green) *green = ret.green;
    }

    return ret_val;
}

static const struct sol_flow_packet_type _SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR = {
    .api_version = SOL_FLOW_PACKET_TYPE_API_VERSION,
    .name = "DIRECTION_VECTOR",
    .data_size = sizeof(struct sol_direction_vector),
};
SOL_API const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR = &_SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR;

SOL_API struct sol_flow_packet *
sol_flow_packet_new_direction_vector(const struct sol_direction_vector *direction_vector)
{
    return sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR, direction_vector);
}

SOL_API struct sol_flow_packet *
sol_flow_packet_new_direction_vector_components(double x, double y, double z)
{
    struct sol_direction_vector direction_vector = {
        .x = x,
        .z = z,
        .y = y,
        .min = -DBL_MAX,
        .max = DBL_MAX,
    };

    return sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR, &direction_vector);
}

SOL_API int
sol_flow_packet_get_direction_vector(const struct sol_flow_packet *packet, struct sol_direction_vector *direction_vector)
{
    SOL_FLOW_PACKET_CHECK(packet, SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR, -EINVAL);
    return sol_flow_packet_get(packet, direction_vector);
}

SOL_API int
sol_flow_packet_get_direction_vector_components(const struct sol_flow_packet *packet, double *x, double *y, double *z)
{
    struct sol_direction_vector ret;
    int ret_val;

    SOL_FLOW_PACKET_CHECK(packet, SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR, -EINVAL);
    ret_val = sol_flow_packet_get(packet, &ret);
    if (ret_val == 0) {
        if (x) *x = ret.x;
        if (z) *z = ret.z;
        if (y) *y = ret.y;
    }

    return ret_val;
}

static const struct sol_flow_packet_type _SOL_FLOW_PACKET_TYPE_LOCATION = {
    .api_version = SOL_FLOW_PACKET_TYPE_API_VERSION,
    .name = "LOCATION",
    .data_size = sizeof(struct sol_location),
};
SOL_API const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_LOCATION = &_SOL_FLOW_PACKET_TYPE_LOCATION;

SOL_API struct sol_flow_packet *
sol_flow_packet_new_location(const struct sol_location *location)
{
    return sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_LOCATION, location);
}

SOL_API int
sol_flow_packet_get_location(const struct sol_flow_packet *packet, struct sol_location *location)
{
    SOL_FLOW_PACKET_CHECK(packet, SOL_FLOW_PACKET_TYPE_LOCATION, -EINVAL);
    return sol_flow_packet_get(packet, location);
}

static const struct sol_flow_packet_type _SOL_FLOW_PACKET_TYPE_ANY = {
    .api_version = SOL_FLOW_PACKET_TYPE_API_VERSION,
    .name = "Any",
    .data_size = sizeof(void *)
};
SOL_API const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_ANY = &_SOL_FLOW_PACKET_TYPE_ANY;

struct error_data {
    int code;
    char *msg;
};

static void
error_packet_dispose(const struct sol_flow_packet_type *packet_type, void *mem)
{
    struct error_data *error = mem;

    free(error->msg);
}

static int
error_packet_init(const struct sol_flow_packet_type *packet_type, void *mem, const void *input)
{
    const struct error_data *in = input;
    struct error_data *error = mem;

    error->msg = (in->msg) ? strdup(in->msg) : NULL;
    error->code = in->code;

    return 0;
}

static const struct sol_flow_packet_type _SOL_FLOW_PACKET_TYPE_ERROR = {
    .api_version = SOL_FLOW_PACKET_TYPE_API_VERSION,
    .name = "Error",
    .data_size = sizeof(struct error_data),
    .init = error_packet_init,
    .dispose = error_packet_dispose,
};
SOL_API const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_ERROR = &_SOL_FLOW_PACKET_TYPE_ERROR;

SOL_API struct sol_flow_packet *
sol_flow_packet_new_error(int code, const char *msg)
{
    struct error_data error;

    error.code = code;
    error.msg = (char *)msg;
    return sol_flow_packet_new(SOL_FLOW_PACKET_TYPE_ERROR, &error);
}

SOL_API int
sol_flow_packet_get_error(const struct sol_flow_packet *packet, int *code, const char **msg)
{
    struct error_data error;
    int ret;

    SOL_FLOW_PACKET_CHECK(packet, SOL_FLOW_PACKET_TYPE_ERROR, -EINVAL);
    ret = sol_flow_packet_get(packet, &error);
    SOL_INT_CHECK(ret, != 0, ret);

    if (msg)
        *msg = error.msg;
    if (code)
        *code = error.code;

    return ret;
}
