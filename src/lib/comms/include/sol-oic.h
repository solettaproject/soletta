/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <sol-common-buildopts.h>
#include <sol-vector.h>
#include <sol-str-slice.h>
#include "sol-coap.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup OIC Open Interconnect Consortium
 * @ingroup Comms
 *
 * @brief Implementation of the protocol defined by Open Interconnect
 * Consortium (OIC - http://openinterconnect.org/)
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

/**
 * @brief Structure containing all fields that are retrieved by @ref
 * sol_oic_client_get_platform_info() and @ref
 * sol_oic_client_get_platform_info_by_addr(). It's open to the API
 * user to bypass the need for getters for everything, but all
 * callbacks returning an instance do so with a @c const modifier. The
 * user must never change these fields, ever.
 */
typedef struct sol_oic_platform_info {
#ifndef SOL_NO_API_VERSION
#define SOL_OIC_PLATFORM_INFO_API_VERSION (1)
    uint16_t api_version; /**< @brief API version */
#endif

    /* All fields are required by the spec.  Some of the fields are
     * obtained in runtime (such as system time, OS version), and are
     * not user-specifiable.
     */

    /**
     * @brief Platform identifier.
     */
    struct sol_str_slice platform_id;
    /**
     * @brief Name of manufacturer.
     */
    struct sol_str_slice manufacturer_name;
    /**
     * @brief URL to manufacturer.
     */
    struct sol_str_slice manufacturer_url;
    /**
     * @brief Model number as designated by manufacturer.
     */
    struct sol_str_slice model_number;
    /**
     * @brief Manufacturing date.
     */
    struct sol_str_slice manufacture_date;
    /**
     * @brief Version of the platform.
     */
    struct sol_str_slice platform_version;
    /**
     * @brief Version of the hardware.
     */
    struct sol_str_slice hardware_version;
    /**
     * @brief Version of the firmware.
     */
    struct sol_str_slice firmware_version;
    /**
     * @brief URL to manufacturer's support website.
     */
    struct sol_str_slice support_url;

    /* Read-only fields. */

    /**
     * @brief Version of the operational system running on the device.
     */
    struct sol_str_slice os_version;
    /**
     * @brief Current system time in the device
     */
    struct sol_str_slice system_time;
} sol_oic_platform_info;

/**
 * @brief Flags to set when adding a new resource to a server.
 *
 * Multiple flags can be set, just connect them using the | operator.
 *
 * @see sol_oic_server_register_resource()
 */
enum sol_oic_resource_flag {
    /**
     * @brief No flag is set.
     *
     * The device is non-discoverable and non-observable
     */
    SOL_OIC_FLAG_NONE = 0,
    /**
     * @brief The resource is discoverable by clients.
     */
    SOL_OIC_FLAG_DISCOVERABLE = 1 << 0,
    /**
     * @brief The resource is observable.
     *
     * Clients can request observable resources to be notified when a the
     * resource status has changes.
     */
    SOL_OIC_FLAG_OBSERVABLE = 1 << 1,
    /**
     * @brief The resource is active.
     *
     * Devices are set as inactive when they are uninitialized,
     * marked for deletion or already deleted.
     */
    SOL_OIC_FLAG_ACTIVE = 1 << 2,
    /**
     * @brief The resource is slow.
     *
     * Delays in response from slow resource is expected when processing
     * requests.
     */
    SOL_OIC_FLAG_SLOW = 1 << 3,
    /**
     * @brief The resource is secure.
     *
     * Connection established with a secure devices is secure.
     */
    SOL_OIC_FLAG_SECURE = 1 << 4,
    /**
     * @brief The resource is discoverable by clients only if request
     * contains an explicity query
     */
    SOL_OIC_FLAG_DISCOVERABLE_EXPLICIT = 1 << 5,
};

/**
 * @brief Structure containing all fields that are retrieved by @ref
 * sol_oic_client_get_server_info() and @ref
 * sol_oic_client_get_server_info_by_addr(). It's open to the API user
 * to bypass the need for getters for everything, but all callbacks
 * returning an instance do so with a @c const modifier. The user must
 * never change these fields, ever.
 */
typedef struct sol_oic_device_info {
#ifndef SOL_NO_API_VERSION
#define SOL_OIC_DEVICE_INFO_API_VERSION (1)
    uint16_t api_version; /**< @brief API version */
#endif

    /**
     * @brief Device name
     */
    struct sol_str_slice device_name;
    /**
     * @brief Spec version of the core specification implemented by this device.
     */
    struct sol_str_slice spec_version;
    /**
     * @brief Unique device identifier.
     */
    struct sol_str_slice device_id;
    /**
     * @brief Spec version of data model.
     */
    struct sol_str_slice data_model_version;
} sol_oic_device_info;

/**
 * @brief field type of sol_oic_repr_field structure.
 */
enum sol_oic_repr_type {
    SOL_OIC_REPR_TYPE_UINT, /** Unsigned int type. */
    SOL_OIC_REPR_TYPE_INT, /** Signed int type. */
    SOL_OIC_REPR_TYPE_SIMPLE, /** Unsigned 8-bit integer type. */
    SOL_OIC_REPR_TYPE_TEXT_STRING, /** String with text type. */
    SOL_OIC_REPR_TYPE_BYTE_STRING, /** String with bytes type. */
    SOL_OIC_REPR_TYPE_HALF_FLOAT, /** Half-precision float number type. */
    SOL_OIC_REPR_TYPE_FLOAT, /** Single-precision float number type. */
    SOL_OIC_REPR_TYPE_DOUBLE, /** Double-precision float number type. */
    SOL_OIC_REPR_TYPE_BOOL, /** Boolean precision type. */
    SOL_OIC_REPR_TYPE_UNSUPPORTED /**< Unsupported type. */
};

/**
 * @brief Structure to keep a single oic-map's field.
 *
 * Use this structure to read fields using a #sol_oic_map_reader and
 * macro @ref SOL_OIC_MAP_LOOP() or to write fields using a
 * #sol_oic_map_writer and function sol_oic_map_append().
 *
 * @see sol_oic_map_append()
 * @see SOL_OIC_MAP_LOOP()
 */
typedef struct sol_oic_repr_field {
    /**
     * @brief type of the data of this field.
     */
    enum sol_oic_repr_type type;
    /**
     * @brief Field's key as a string.
     */
    const char *key;
    /**
     * @brief Union used to access field's data in correct format specified by
     * @a type
     */
    union {
        /**
         * @brief Field's data if type is SOL_OIC_REPR_TYPE_UINT.
         */
        uint64_t v_uint;
        /**
         * @brief Field's data if type is SOL_OIC_REPR_TYPE_INT.
         */
        int64_t v_int;
        /**
         * @brief Field's data if type is SOL_OIC_REPR_TYPE_SIMPLE.
         */
        uint8_t v_simple;
        /**
         * @brief Field's data if type is SOL_OIC_REPR_TYPE_TEXT_STRING or
         * SOL_OIC_REPR_TYPE_BYTE_STRING.
         */
        struct sol_str_slice v_slice;
        /**
         * @brief Field's data if type is SOL_OIC_REPR_TYPE_FLOAT.
         */
        float v_float;
        /**
         * @brief Field's data if type is SOL_OIC_REPR_TYPE_DOUBLE.
         */
        double v_double;
        /**
         * @brief Field's data if type is SOL_OIC_REPR_TYPE_HALF_FLOAT.
         */
        void *v_voidptr;
        /**
         * @brief Field's data if type is SOL_OIC_REPR_TYPE_BOOL.
         */
        bool v_boolean;
    };
} sol_oic_repr_field;

/**
 * @brief Helper macro to create a #sol_oic_repr_field.
 *
 * @param key_ Field's key.
 * @param type_ Field's type.
 * @param ... Extra structure initialization commands.
 */
#define SOL_OIC_REPR_FIELD(key_, type_, ...) \
    (struct sol_oic_repr_field){.type = (type_), .key = (key_), { __VA_ARGS__ } }

/**
 * @brief Helper macro to create an unsigned integer #sol_oic_repr_field.
 *
 * @param key_ Field's key.
 * @param value_ The unsigned int value of this field.
 */
#define SOL_OIC_REPR_UINT(key_, value_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_UINT, .v_uint = (value_))

/**
 * @brief Helper macro to create a signed integer #sol_oic_repr_field.
 *
 * @param key_ Field's key.
 * @param value_ The signed int value of this field.
 */
#define SOL_OIC_REPR_INT(key_, value_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_INT, .v_int = (value_))

/**
 * @brief Helper macro to create a boolean #sol_oic_repr_field.
 *
 * @param key_ Field's key.
 * @param value_ The boolean value of this field.
 */
#define SOL_OIC_REPR_BOOL(key_, value_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_BOOL, .v_boolean = !!(value_))

/**
 * @brief Helper macro to create a simple integer #sol_oic_repr_field.
 *
 * @param key_ Field's key.
 * @param value_ The 8-bit integer value of this field.
 */
#define SOL_OIC_REPR_SIMPLE(key_, value_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_SIMPLE, .v_simple = (value_))

/**
 * @brief Helper macro to create a text string #sol_oic_repr_field.
 *
 * @param key_ Field's key.
 * @param value_ A pointer to the string to be set as the string value of this
 *        field.
 * @param len_ The length of the string pointed by @a value.
 */
#define SOL_OIC_REPR_TEXT_STRING(key_, value_, len_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_TEXT_STRING, .v_slice = SOL_STR_SLICE_STR((value_), (len_)))

/**
 * @brief Helper macro to create a byte string #sol_oic_repr_field.
 *
 * @param key_ Field's key.
 * @param value_ A pointer to the string to be set as the string value of this
 *        field.
 * @param len_ The length of the string pointed by @a value.
 */
#define SOL_OIC_REPR_BYTE_STRING(key_, value_, len_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_BYTE_STRING, .v_slice = SOL_STR_SLICE_STR((value_), (len_)))

/**
 * @brief Helper macro to create a half-precision float number
 * #sol_oic_repr_field.
 *
 * @param key_ Field's key.
 * @param value_ The value of the float number.
 */
#define SOL_OIC_REPR_HALF_FLOAT(key_, value_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_HALF_FLOAT, .v_voidptr = (void *)(value_))

/**
 * @brief Helper macro to create a single-precision float number
 * #sol_oic_repr_field.
 *
 * @param key_ Field's key.
 * @param value_ The value of the float number.
 */
#define SOL_OIC_REPR_FLOAT(key_, value_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_FLOAT, .v_float = (value_))

/**
 * @brief Helper macro to create a double-precision float #sol_oic_repr_field.
 *
 * @param key_ Field's key.
 * @param value_ The value of the float number.
 */
#define SOL_OIC_REPR_DOUBLE(key_, value_) \
    SOL_OIC_REPR_FIELD(key_, SOL_OIC_REPR_TYPE_DOUBLE, .v_double = (value_))

/**
 * @typedef sol_oic_map_writer
 *
 * @brief Opaque handler for an OIC packet map writer.
 *
 * This structure is used in callback parameters so users can add fields to an
 * OIC packet using @ref sol_oic_map_append().
 *
 * @see sol_oic_server_notify()
 * @see sol_oic_client_resource_request()
 */
struct sol_oic_map_writer;
typedef struct sol_oic_map_writer sol_oic_map_writer;

/**
 * @brief Used in @ref sol_oic_map_writer to state if the map has a content or not
 *
 * @see sol_oic_map_set_type()
 * @see sol_oic_map_get_type()
 */
enum sol_oic_map_type {
    /**
     * @brief Map with no content
     *
     * When an OIC map is used to create a packet and its type is
     * #SOL_OIC_MAP_NO_CONTENT, no payload will be added to the
     * packet.
     **/
    SOL_OIC_MAP_NO_CONTENT,
    /**
     * @brief Map with content.
     *
     * When an OIC map is used to create a packet and its type is
     * #SOL_OIC_MAP_CONTENT, a payload will be created and elements
     * from the map will be added to payload. If the map contains no
     * elements, an empty map will be added to payload.
     */
    SOL_OIC_MAP_CONTENT,
};

/**
 * @brief Handler for an OIC packet map reader.
 *
 * This structure is used in callback parameters so users can read fields from
 * an OIC packet using @ref SOL_OIC_MAP_LOOP.
 *
 * @param parser Internal Pointer. Not to be used.
 * @param ptr Internal Pointer. Not to be used.
 * @param remaining Internal information. Not to be used.
 * @param extra Internal information. Not to be used.
 * @param type Internal information. Not to be used.
 * @param flags Internal information. Not to be used.
 *
 * @note Fields from this structure are not expected to be accessed by
 * clients. They are exposed only to make it possible for stack
 * variable declarations of it.
 *
 * @see SOL_OIC_MAP_LOOP.
 */
typedef struct sol_oic_map_reader {
    const void *parser;
    const void *ptr;
    const uint32_t remaining;
    const uint16_t extra;
    const uint8_t type;
    const uint8_t flags;
} sol_oic_map_reader;

/**
 * @typedef sol_oic_request
 *
 * @brief Information about a client request.
 *
 * @see sol_oic_server_send_response
 */
struct sol_oic_request;
typedef struct sol_oic_request sol_oic_request;

/**
 * @typedef sol_oic_response
 *
 * @brief Information about a server response.
 *
 * @see sol_oic_server_send_response
 */
struct sol_oic_response;
typedef struct sol_oic_response sol_oic_response;

/**
 * @brief Possible reasons a @ref SOL_OIC_MAP_LOOP was terminated.
 */
enum sol_oic_map_loop_reason {
    /**
     * @brief Success termination. Everything was OK.
     */
    SOL_OIC_MAP_LOOP_OK = 0,
    /**
     * @brief Loop was terminated because an error occurred. Not all elements
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
 * @return @ref SOL_OIC_MAP_LOOP_OK if initialization was a success or
 *         @ref SOL_OIC_MAP_LOOP_ERROR if initialization failed.
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
 * @param reason @c SOL_OIC_MAP_LOOP_ERROR if an error occurred.
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
 * @param oic_map_writer The sol_oic_map_writer in which the element will be
 *        added.
 * @param repr The element
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_oic_server_notify()
 * @see sol_oic_client_resource_request()
 * @note As this function adds elements to @a oic_map_writer, it will update
 * its type to SOL_OIC_MAP_CONTENT when needed.
 */
int sol_oic_map_append(struct sol_oic_map_writer *oic_map_writer, struct sol_oic_repr_field *repr);

/**
 * @brief set current @a oic_map_writer type.
 *
 * Use this function if you want to change @a oic_map_writer type to
 * SOL_OIC_MAP_CONTENT without adding elements to it. This will force OIC to
 * create a payload in packet with an empty list if map is empty.
 * Trying to change from SOL_OIC_MAP_CONTENT to SOL_OIC_MAP_NO_CONTENT will fail
 * if elements were already added to @a oic_map_writer.
 *
 * @param oic_map_writer The map to set the type.
 * @param type The new type of @a oic_map_writer.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_oic_map_set_type(struct sol_oic_map_writer *oic_map_writer, enum sol_oic_map_type type);

/**
 * @brief get current @a oic_map_writer type.
 *
 * @param oic_map_writer The map to get the type from.
 * @param type A pointer to an enum to be filled with @a oic_map_writer type.
 *
 * @return @c -EINVAL if any param is NULL. @c 0 otherwise.
 */
int sol_oic_map_get_type(struct sol_oic_map_writer *oic_map_writer, enum sol_oic_map_type *type);

/**
 * @brief Release memory from field.
 *
 * Release any memory that is hold by field, but not free @a field variable.
 *
 * @param field the field to be cleared.
 */
void sol_oic_repr_field_clear(struct sol_oic_repr_field *field);

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
 *     // Error handling
 * @endcode
 *
 * @note: If you add a break or a return statement in SOL_OIC_MAP_LOOP, it is
 * necessary to release the @a current_ memory using sol_oic_repr_field_clear().
 *
 * @see sol_oic_map_reader
 * @see sol_oic_map_loop_init
 * @see sol_oic_map_loop_next
 */
#define SOL_OIC_MAP_LOOP(map_, current_, iterator_, end_reason_) \
    for (end_reason_ = sol_oic_map_loop_init(map_, iterator_, current_); \
        end_reason_ == SOL_OIC_MAP_LOOP_OK && \
        sol_oic_map_loop_next(current_, iterator_, &end_reason_);)

/**
 * @brief Print the decoded cbor content of @a pkt.
 *
 * Checks if @a pkt is an OIC packet with cbor content in payload and prints it
 * in a human readable way.
 *
 * Used only for debug purposes.
 *
 * @param pkt The packet to be debuged.
 */
#ifdef SOL_LOG_ENABLED
void sol_oic_payload_debug(struct sol_coap_packet *pkt);
#else
static inline void
sol_oic_payload_debug(struct sol_coap_packet *pkt)
{
}
#endif

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
