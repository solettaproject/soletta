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
#include <sol-str-slice.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup OIC Open Interconnect Consortium
 * @ingroup Comms
 *
 * Implementation of protocol defined by Open Interconnect Consortium
 * (OIC - http://openinterconnect.org/)
 *
 * It's a common communication framework based on industry standard
 * technologies to wirelessly connect and intelligently manage
 * the flow of information among devices, regardless of form factor,
 * operating system or service provider.
 *
 * Both client and server sides are covered by this module.
 *
 * @{
 */

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

/**
 * @struct sol_oic_map_writer
 *
 * @brief Opaque handler for an oic packet map writer.
 *
 * This structure is used in callback parameters so users can add fields to an
 * oic packet using @ref sol_oic_map_append().
 *
 * @see sol_oic_notify_observers()
 * @see sol_oic_client_resource_request()
 */
struct sol_oic_map_writer;

/**
 * @brief Handler for an oic packet map reader.
 *
 * This structure is used in callback parameters so users can read fields from
 * an oic packet using @ref SOL_OIC_MAP_LOOP.
 *
 * @note Fields from this structure are not expected to be accessed by clients.
 * @see SOL_OIC_MAP_LOOP.
 */
struct sol_oic_map_reader {
    const void *parser;
    const void *ptr;
    const uint32_t remaining;
    const uint16_t extra;
    const uint8_t type;
    const uint8_t flags;
};

/**
 * @brief Possible reasons a @ref SOL_OIC_MAP_LOOP was terminated.
 */
enum sol_oic_map_loop_reason {
    /**
     * @brief Success termination. Everything was OK.
     */
    SOL_OIC_MAP_LOOP_OK = 0,
    /**
     * @brief Loop was terminated because an error occured. Not all elements
     * were visited.
     */
    SOL_OIC_MAP_LOOP_ERROR
};

/**
 * @brief Initialize an iterator to loop through elements of @a map.
 *
 * @param map The sol_oic_map_reader element to be used to initialize the
 *        @a iterator.
 * @param iterator Iterator to be initialized so it can be used by
 *        @ref sol_oic_map_loop_next() to visit @a map elements.
 * @param repr Initialize this element to be used by
 *        @ref sol_oic_map_loop_next() to hold @a map elements.
 *
 * @return @c SOL_OIC_MAP_LOOP_OK if initialization was a success or
 *         @c SOL_OIC_MAP_LOOP_ERROR if initialization failed.
 *
 * @note Prefer using @ref SOL_OIC_MAP_LOOP instead of calling this function directly.
 *
 * @see sol_oic_map_reader
 */
enum sol_oic_map_loop_reason sol_oic_map_loop_init(const struct sol_oic_map_reader *map, struct sol_oic_map_reader *iterator, struct sol_oic_repr_field *repr);

/**
 * @brief Get the next element from @a iterator.
 *
 * @param repr The value of next element from @a iterator
 * @param iterator The sol_oic_map_reader iterator initialized by
 *        @ref sol_oic_map_loop_init function.
 * @param reason @c SOL_OIC_MAP_LOOP_ERROR if an error occured.
 *        @c SOL_OIC_MAP_LOOP_OK otherwise.
 *
 * @return false if one error occurred or if there is no more elements to read
 *         from @a iterator. true otherwise.
 *
 * @note Prefer using @ref SOL_OIC_MAP_LOOP instead of calling this function directly.
 *
 * @see sol_oic_map_reader
 */
bool sol_oic_map_loop_next(struct sol_oic_repr_field *repr, struct sol_oic_map_reader *iterator, enum sol_oic_map_loop_reason *reason);

/**
 * @brief Append an element to @a oic_map_writer
 *
 * @param oic_map_writer The sol_oic_map_writer in wich the element will be
 *        added.
 * @param repr The element
 *
 * @return true if the element was added successfully. False if an error
 *         occured and the element was not added.
 *
 * @see sol_oic_notify_observers()
 * @see sol_oic_client_resource_request()
 */
bool sol_oic_map_append(struct sol_oic_map_writer *oic_map_writer, struct sol_oic_repr_field *repr);

/**
 * @def SOL_OIC_MAP_LOOP(map_, current_, iterator_, end_reason_)
 *
 * @brief Macro to be used to loop through all elements from a
 * sol_oic_map_reader
 *
 * @param map_ A pointer to the struct sol_oic_map_reader to be looped
 * @param current_ A pointer to a struct sol_oic_repr_field to be filled with
 *        the current element data.
 * @param iterator_ A pointer to a struct sol_oic_map_reader to be used as an
 *        iterator
 * @param end_reason_ A pointer to a enum sol_oic_map_loop_reason to be filled
 *        with the reason this loop has terminated.
 *
 * Example to read data from a struct sol_oic_map_reader using this macro:
 * @code
 *
 * struct sol_oic_repr_field field;
 * enum sol_oic_map_loop_reason end_reason;
 * struct sol_oic_map_reader iterator;
 *
 * SOL_OIC_MAP_LOOP(map_reader, &field, &iterator, end_reason) {
 * {
 *      // do something
 * }
 *
 * if (end_reason != SOL_OIC_MAP_LOOP_OK)
 *     // Erro handling
 * @endcode
 *
 * @see sol_oic_map_reader
 * @see sol_oic_map_loop_init
 * @see sol_oic_map_loop_next
 */
#define SOL_OIC_MAP_LOOP(map_, current_, iterator_, end_reason_) \
    for (end_reason_ = sol_oic_map_loop_init(map_, iterator_, current_);  \
        end_reason_ == SOL_OIC_MAP_LOOP_OK && \
        sol_oic_map_loop_next(current_, iterator_, &end_reason_);)

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
