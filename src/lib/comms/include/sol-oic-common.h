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

#include <sol-common-buildopts.h>
#include <sol-vector.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sol_oic_server_information {
#ifndef SOL_NO_API_VERSION
#define SOL_OIC_SERVER_INFORMATION_API_VERSION (1)
    uint16_t api_version;
    int : 0; /* save possible hole for a future field */
#endif

    /* All fields are required by the spec.  Some of the fields are
     * obtained in runtime (such as system time, OS version), and are
     * not user-specifiable.  */
    struct sol_str_slice platform_id;
    struct sol_str_slice manufacturer_name;
    struct sol_str_slice manufacturer_url;
    struct sol_str_slice model_number;
    struct sol_str_slice manufacture_date;
    struct sol_str_slice platform_version;
    struct sol_str_slice hardware_version;
    struct sol_str_slice firmware_version;
    struct sol_str_slice support_url;

    /* Read-only fields. */
    struct sol_str_slice os_version;
    struct sol_str_slice system_time;
};

enum sol_oic_resource_flag {
    SOL_OIC_FLAG_DISCOVERABLE = 1 << 0,
        SOL_OIC_FLAG_OBSERVABLE = 1 << 1,
        SOL_OIC_FLAG_ACTIVE = 1 << 2,
        SOL_OIC_FLAG_SLOW = 1 << 3,
        SOL_OIC_FLAG_SECURE = 1 << 4
};

enum sol_oic_repr_type {
    SOL_OIC_REPR_TYPE_UINT,
    SOL_OIC_REPR_TYPE_INT,
    SOL_OIC_REPR_TYPE_SIMPLE,
    SOL_OIC_REPR_TYPE_TEXT_STRING,
    SOL_OIC_REPR_TYPE_BYTE_STRING,
    SOL_OIC_REPR_TYPE_HALF_FLOAT,
    SOL_OIC_REPR_TYPE_FLOAT,
    SOL_OIC_REPR_TYPE_DOUBLE,
    SOL_OIC_REPR_TYPE_BOOLEAN
};

struct sol_oic_repr_field {
    enum sol_oic_repr_type type;
    const char *key;
    union {
        uint64_t v_uint;
        int64_t v_int;
        uint8_t v_simple;
        struct sol_str_slice v_slice;
        float v_float;
        double v_double;
        void *v_voidptr;
        bool v_boolean;
    };
};

#define SOL_OIC_REPR_FIELD(key_, type_, ...) \
    (struct sol_oic_repr_field){.type = (type_), .key = (key_), __VA_ARGS__ }

#define SOL_OIC_REPR_UINT(key_, value_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_UINT, .v_uint = (value_))
#define SOL_OIC_REPR_INT(key_, value_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_INT, .v_int = (value_))
#define SOL_OIC_REPR_BOOLEAN(key_, value_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_BOOLEAN, .v_boolean = !!(value_))
#define SOL_OIC_REPR_SIMPLE(key_, value_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_SIMPLE, .v_simple = (value_))
#define SOL_OIC_REPR_TEXT_STRING(key_, value_, len_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_TEXT_STRING, .v_slice = SOL_STR_SLICE_STR((value_), (len_)))
#define SOL_OIC_REPR_BYTE_STRING(key_, value_, len_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_BYTE_STRING, .v_slice = SOL_STR_SLICE_STR((value_), (len_)))
#define SOL_OIC_REPR_HALF_FLOAT(key_, value_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_HALF_FLOAT, .v_voidptr = (value_))
#define SOL_OIC_REPR_FLOAT(key_, value_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_FLOAT, .v_float = (value_))
#define SOL_OIC_REPR_DOUBLE(key_, value_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_DOUBLE, .v_double = (value_))

#ifdef __cplusplus
}
#endif
