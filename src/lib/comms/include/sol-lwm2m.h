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

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#include "sol-coap.h"
#include "sol-network.h"
#include "sol-str-slice.h"
#include "sol-vector.h"
#include "sol-buffer.h"
#include "sol-types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines that handle the LWM2M protocol.
 *
 */

/**
 * @defgroup LWM2M LWM2M
 * @ingroup Comms
 *
 * @brief Routines that handle the LWM2M protocol.
 *
 * Supported features:
 * - Bootstrap interface.
 * - Registration interface.
 * - Management interface.
 * - Observation interface.
 * - TLV format.
 * - Data Access Control.
 * - CoAP Data Encryption (Pre-Shared Key and Raw Public Key modes).
 *
 * Unsupported features for now:
 * - LWM2M JSON.
 * - Queue Mode operation (only 'U' is supported for now).
 *
 * @warning Experimental API. Changes are expected in future releases.
 *
 * @{
 */

/**
 * @brief Macro that defines the default port for a NoSec LWM2M server.
 */
#define SOL_LWM2M_DEFAULT_SERVER_PORT_COAP (5683)

/**
 * @brief Macro that defines the default port for a DTLS-secured LWM2M server.
 */
#define SOL_LWM2M_DEFAULT_SERVER_PORT_DTLS (5684)

/**
 * @typedef sol_lwm2m_client_object
 * @brief A handle of a client's object.
 * @see sol_lwm2m_client_info_get_objects()
 */
struct sol_lwm2m_client_object;
typedef struct sol_lwm2m_client_object sol_lwm2m_client_object;

/**
 * @brief LWM2M Client binding mode.
 *
 * A LWM2M server may support multiple forms of binding.
 * The binding mode is requested by a client during its registration.
 *
 * In Queue binding mode a client flags to the server that it
 * may not be available for communication all the time, thus
 * the server must wait until it receives a heartbeat from the
 * client until it can send requests. The queue binding mode
 * is useful, because the client may enter in deep sleep
 * and save battery and only wake up in certain times.
 *
 * @note The default binding mode is #SOL_LWM2M_BINDING_MODE_U and is the only one supported right know.
 * @see sol_lwm2m_client_info_get_binding_mode()
 */
enum sol_lwm2m_binding_mode {
    /**
     * Indicates that the client is reacheable all the time
     * and all the communication must be done using UDP.
     */
    SOL_LWM2M_BINDING_MODE_U,
    /**
     * Indicate that the client is using Queued UDP binding
     * and all the communication must be done using UDP.
     */
    SOL_LWM2M_BINDING_MODE_UQ,
    /**
     * Indicates that the client is reacheable all the time
     * and all the communication must be done using SMS.
     */
    SOL_LWM2M_BINDING_MODE_S,
    /**
     * Indicates that the client is using Queued SMS binding
     * and all the communication must be done using SMS
     */
    SOL_LWM2M_BINDING_MODE_SQ,
    /**
     * Indicates that the client is using UDP and SMS binding.
     * When the server sends an UDP request the client must
     * send the response using UDP.
     * When the server sends a SMS request the client must
     * send the response using SMS.
     */
    SOL_LWM2M_BINDING_MODE_US,
    /**
     * Indicates that the client using Queued SMS and UDP binding.
     * When the server sends an UDP request the client must
     * send the response using UDP.
     * When the server sends a SMS request the client must
     * send the response using SMS.
     */
    SOL_LWM2M_BINDING_MODE_UQS,
    /**
     * It was not possible to determine the client binding.
     */
    SOL_LWM2M_BINDING_MODE_UNKNOWN = -1,
};

/**
 * @brief Enum that represents the UDP Security Mode.
 *
 * @note Certificate mode is not supported yet.
 */
enum sol_lwm2m_security_mode {
    /**
     * Pre-Shared Key security mode with Cipher Suite TLS_PSK_WITH_AES_128_CCM_8
     * In this case, the following Resource IDs have to be filled as well:
     * /3 "Public Key or Identity":    PSK Identity [16 bytes; UTF-8 String];
     * /5 "Secret Key":                PSK [128-bit AES Key; 16 Opaque bytes];
     */
    SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY = 0,
    /**
     * Raw Public Key security mode with Cipher Suite TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8
     * In this case, the following Resource IDs have to be filled as well:
     * /3 "Public Key or Identity":                    Client's Raw Public Key [2x256-bit ECC key (one for each ECC Coordinate); 64 Opaque bytes];
     * /4 "Server Public Key or Identity Resource":    [Expected] Server's Raw Public Key [2x256-bit ECC key (one for each ECC Coordinate); 64 Opaque bytes];
     * /5 "Secret Key":                                Client's Private Key [256-bit ECC key; 32 Opaque bytes];
     */
    SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY = 1,
    /**
     * Certificate security mode with Cipher Suite TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8
     * In this case, the following Resource IDs have to be filled as well:
     * /3 "Public Key or Identity":                    X.509v3 Certificate [Opaque];
     * /4 "Server Public Key or Identity Resource":    [Expected] Server's X.509v3 Certificate [Opaque];
     * /5 "Secret Key":                                Client's Private Key [256-bit ECC key; 32 Opaque bytes];
     */
    SOL_LWM2M_SECURITY_MODE_CERTIFICATE = 2,
    /**
     * No security ("NoSec") mode (CoAP without DTLS)
     */
    SOL_LWM2M_SECURITY_MODE_NO_SEC = 3
};

/**
 * @brief Enum that represents a LWM2M response/request content type.
 */
enum sol_lwm2m_content_type {
    /**
     * The content type message is pure text.
     */
    SOL_LWM2M_CONTENT_TYPE_TEXT = 1541,
    /**
     * The content type of the message is undeterminated, in other
     * words, it is an array of bytes.
     */
    SOL_LWM2M_CONTENT_TYPE_OPAQUE = 1544,
    /**
     * The content type of the message is in TLV format.
     */
    SOL_LWM2M_CONTENT_TYPE_TLV = 1542,
    /**
     * The content type of the message is in JSON.
     * JSON content types are not supported right now.
     */
    SOL_LWM2M_CONTENT_TYPE_JSON = 1543
};

/**
 * @brief Enum that represents the TLV type.
 * @see #sol_lwm2m_tlv
 */
enum sol_lwm2m_tlv_type {
    /**
     * The TLV represents an object instance
     */
    SOL_LWM2M_TLV_TYPE_OBJECT_INSTANCE = 0,
    /**
     * The TLV represents a resource instance.
     */
    SOL_LWM2M_TLV_TYPE_RESOURCE_INSTANCE = 64,
    /**
     * The TLV is composed of multiple resources.
     */
    SOL_LWM2M_TLV_TYPE_MULTIPLE_RESOURCES = 128,
    /**
     * The TLV is a resource.
     */
    SOL_LWM2M_TLV_TYPE_RESOURCE_WITH_VALUE = 192
};

/**
 * @brief Enum that represents a LWM2M resource data type.
 * @see #sol_lwm2m_resource
 */
enum sol_lwm2m_resource_data_type {
    /**
     * The resource value is a string.
     */
    SOL_LWM2M_RESOURCE_DATA_TYPE_STRING,
    /**
     * The resource value is an integer.
     */
    SOL_LWM2M_RESOURCE_DATA_TYPE_INT,
    /**
     * The resource value is a float.
     */
    SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT,
    /**
     * The resource value is a boolean.
     */
    SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL,
    /**
     * The resource value is opaque.
     */
    SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE,
    /**
       The resource value is a timestamp (Unix time).
     */
    SOL_LWM2M_RESOURCE_DATA_TYPE_TIME,
    /**
     * The resource value is an object link.
     */
    SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK,
    /**
     * The resource value is undeterminated.
     */
    SOL_LWM2M_RESOURCE_DATA_TYPE_NONE = -1
};

/**
 * @brief Enum data represents if an #sol_lwm2m_resource is an array or not.
 */
enum sol_lwm2m_resource_type {
    /**
     * The resource type has single value.
     */
    SOL_LWM2M_RESOURCE_TYPE_SINGLE,
    /**
     * The resource type is an array.
     */
    SOL_LWM2M_RESOURCE_TYPE_MULTIPLE,
    /**
     * The resource type is unknown.
     */
    SOL_LWM2M_RESOURCE_TYPE_UNKNOWN = -1
};

/**
 * @brief Struct that represents a Pre-Shared Key (PSK).
 *
 * A @c sol_vector holding elements of this type is used by the LWM2M Server
 * and LWM2M Bootstrap Server to keep a list of known Clients' Pre-Shared Keys.
 *
 * @see sol_lwm2m_server_new()
 * @see sol_lwm2m_bootstrap_server_new()
 */
typedef struct sol_lwm2m_security_psk {
    /** @brief The PSK Identity, composed of a 16-bytes UTF-8 String */
    struct sol_blob *id;
    /** @brief The PSK Key, composed of an Opaque 16-bytes (128-bit) AES Key */
    struct sol_blob *key;
} sol_lwm2m_security_psk;

/**
 * @brief Struct that represents a Raw Public Key (RPK) pair.
 *
 * An element of this type is used by the LWM2M Server
 * and LWM2M Bootstrap Server to store its own Private and Public keys.
 *
 * @see sol_lwm2m_server_new()
 * @see sol_lwm2m_bootstrap_server_new()
 */
typedef struct sol_lwm2m_security_rpk {
    /** @brief The Private Key, composed of an Opaque 32-bytes (128-bit) ECC key */
    struct sol_blob *private_key;
    /** @brief The Public Key, composed of an Opaque 64-bytes (2x256-bit) ECC key.
     *
     * This represents the X and Y coordinates in a contiguous set of bytes.
     * A @c sol_ptr_vector of @c sol_blob following this structure is used
     * by the LWM2M Server and LWM2M Bootstrap Server to keep a list of known
     * Clients' Public Keys.
     */
    struct sol_blob *public_key;
} sol_lwm2m_security_rpk;

/**
 * @brief Struct that represents TLV data.
 *
 * The binary format TLV (Type-Length-Value) is used to represent an array of values or a singular value, using a compact binary representation. \n It is needed by "Read" and "Write" operations on Object Instance(s) or on a Resource which supports multiple instances (Resource Instances).
 *
 * The format is an array of the following byte sequence, where each array entry represents an Object Instance, Resource or Resource Instance:
 *
 * Field | Format and Length | Description | Implemented as
 * ----- | ----------------- | ----------- | --------------
 * Type | 8-bits masked field | Bits 7-6: Indicates the type of identifier. \n Bits 5-0: All have special meanings as well. | enum sol_lwm2m_tlv.type
 * Identifier | 8-bit or 16-bit unsigned integer \n as indicated by Bit 5 from Type | Object Instance, Resource or Resource Instance ID | uint16_t sol_lwm2m_tlv.id
 * Length | 0-24bits unsigned integer \n as indicated by Bits 4-3 from Type | Length of the following field in bytes | size_t sol_buffer.capacity
 * Value | Sequence of bytes of size=Length | Value of the tag. \n The actual format depends on the Resource's data type @see sol_lwm2m_resource_data_type | void * sol_lwm2m_tlv.content->data
 *
 * @see sol_lwm2m_parse_tlv()
 */
typedef struct sol_lwm2m_tlv {
#ifndef SOL_NO_API_VERSION
#define SOL_LWM2M_TLV_API_VERSION (1)
    /** @brief API version */
    uint16_t api_version;
#endif

    /** @brief The TLV type */
    enum sol_lwm2m_tlv_type type;
    /** @brief The id of the object/instance/resource */
    uint16_t id;
    /** @brief The TLV content */
    struct sol_buffer content;
} sol_lwm2m_tlv;

/**
 * @brief Struct that represents a LWM2M resource.
 * @see sol_lwm2m_resource_init()
 */
typedef struct sol_lwm2m_resource {
#ifndef SOL_NO_API_VERSION
#define SOL_LWM2M_RESOURCE_API_VERSION (1)
    /** @brief API version */
    uint16_t api_version;
#endif
    /** @brief The resource type */
    enum sol_lwm2m_resource_type type;
    /** @brief The resource data type */
    enum sol_lwm2m_resource_data_type data_type;
    /** @brief The resource id */
    uint16_t id;
    /** @brief The resource data array len */
    uint16_t data_len;
    /** @brief The resource data array */
    struct sol_lwm2m_resource_data {
        uint16_t id;
        union {
            /** @brief The resource is opaque or an string */
            struct sol_blob *blob;
            /** @brief The resource is a integer value */
            int64_t integer;
            /** @brief The resource is a float value */
            double fp;
            /** @brief The resource is a bool value */
            bool b;
        } content;
    } *data;
} sol_lwm2m_resource;

/**
 * @brief Enum that represents the Access Control Rights.
 *
 * Setting each bit means the LWM2M Server has the access right for that operation.
 */
enum sol_lwm2m_acl_rights {
    /**
     * No bit is set (No access rights for any operation)
     */
    SOL_LWM2M_ACL_NONE = 0,
    /**
     * 1st lsb: R (Read, Observe, Discover, Write Attributes)
     */
    SOL_LWM2M_ACL_READ = 1,
    /**
     * 2nd lsb: W (Write)
     */
    SOL_LWM2M_ACL_WRITE = 2,
    /**
     * 3rd lsb: E (Execute)
     */
    SOL_LWM2M_ACL_EXECUTE = 4,
    /**
     * 4th lsb: D (Delete)
     */
    SOL_LWM2M_ACL_DELETE = 8,
    /**
     * 5th lsb: C (Create)
     */
    SOL_LWM2M_ACL_CREATE = 16,
    /**
     * All 5 lsbs: Full Access Rights
     */
    SOL_LWM2M_ACL_ALL = 31
};

/**
 * @brief Convinent macro to initialize a LWM2M resource.
 *
 * This macro will first set the LWM2M resource api version then
 * call sol_lwm2m_resource_init().
 *
 * @param ret_value_ The return value of sol_lwm2m_resource_init()
 * @param resource_ The resource to be initialized.
 * @param id_ The resource id.
 * @param type_ The resource type.
 * @param resource_len_ The resource data size.
 * @param data_type_ The resource data type.
 * @param ... The LWM2M resource data, respecting the table according to the resource type.
 * @see sol_lwm2m_resource_init()
 */
#define SOL_LWM2M_RESOURCE_INIT(ret_value_, resource_, id_, type_, resource_len_, data_type_, ...) \
    do { \
        SOL_SET_API_VERSION((resource_)->api_version = SOL_LWM2M_RESOURCE_API_VERSION; ) \
        (ret_value_) = sol_lwm2m_resource_init((resource_), (id_), (type_), (resource_len_), (data_type_), __VA_ARGS__); \
    } while (0)

/**
 * @brief A helper macro to init SINGLE resources.
 *
 *
 * @param ret_value_ The return value of sol_lwm2m_resource_init()
 * @param resource_ The resource to be initialized.
 * @param id_ The resource id.
 * @param data_type_ The resource data type.
 * @param value_ The LWM2M resource data, respecting the table according to the resource type.
 * @see sol_lwm2m_resource_init()
 */
#define SOL_LWM2M_RESOURCE_SINGLE_INIT(ret_value_, resource_, id_, data_type_, value_) \
    do { \
        SOL_SET_API_VERSION((resource_)->api_version = SOL_LWM2M_RESOURCE_API_VERSION; ) \
        (ret_value_) = sol_lwm2m_resource_init((resource_), (id_), SOL_LWM2M_RESOURCE_TYPE_SINGLE, 1, (data_type_), value_); \
    } while (0)

/**
 * @brief A helper macro to init int resources.
 *
 * This macro will automatically cast the int value to an @c int64_t, thus avoiding
 * some problems that may happen depending on the platform.
 * The most common case to use this macro is when one wants to set a resource using
 * a literal number.
 * Example:
 * @code
 * //Some code...
 * SOL_LWM2M_RESOUCE_INT_INIT(ret_value, &my_resource, resource_id, 10);
 * return ret_value;
 * //More code...
 * @endcode
 *
 * @param ret_value_ The return value of sol_lwm2m_resource_init()
 * @param resource_ The resource to be initialized.
 * @param id_ The resource id.
 * @param value_ The int value
 * @see SOL_LWM2M_RESOURCE_INIT()
 * @see sol_lwm2m_resource_init()
 * @see SOL_TYPE_CHECK()
 * @note This can be safely used for @ref SOL_LWM2M_RESOURCE_DATA_TYPE_TIME
 */
#define SOL_LWM2M_RESOURCE_SINGLE_INT_INIT(ret_value_, resource_, id_, value_) \
    do { \
        SOL_SET_API_VERSION((resource_)->api_version = SOL_LWM2M_RESOURCE_API_VERSION; ) \
        (ret_value_) = sol_lwm2m_resource_init((resource_), (id_), SOL_LWM2M_RESOURCE_TYPE_SINGLE, 1, SOL_LWM2M_RESOURCE_DATA_TYPE_INT, SOL_TYPE_CHECK(int64_t, (value_))); \
    } while (0)

/**
 * @brief A payload received from the network used to create a LWM2M object instance.
 *
 * @see sol_lwm2m_content_type
 * @see struct sol_lwm2m_object::create
 */
typedef struct sol_lwm2m_payload {
    /** @brief The payload type
     * @see sol_lwm2m_content_type
     */
    enum sol_lwm2m_content_type type;
    union sol_lwm2m_payload_data {
        /** @brief The payload content as TLV format -
            use only when @c sol_lwm2m_payload::type is #SOL_LWM2M_CONTENT_TYPE_TLV
         */
        struct sol_vector tlv_content;
        /** @brief The payload content in bytes. */
        struct sol_str_slice slice_content;
    } payload /** @brief The payload data */;
} sol_lwm2m_payload;

/**
 * @brief Clears a #sol_lwm2m_resource.
 *
 * @param resource The resource to be cleared.
 * @see sol_lwm2m_resource_init()
 */
void sol_lwm2m_resource_clear(struct sol_lwm2m_resource *resource);

/**
 * @brief Initializes a LWM2M resource.
 *
 * This function makes it easier to init a LWM2M resource, it
 * will set the proper fields and fill its data. Note that
 * the last argument type varies with the resource type and one
 * must follow the table below.
 *
 * Resource type | Last argument type
 * ------------- | ------------------
 * SOL_LWM2M_RESOURCE_DATA_TYPE_STRING | struct sol_blob *
 * SOL_LWM2M_RESOURCE_DATA_TYPE_INT | int64_t
 * SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT | double
 * SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL | bool
 * SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE | struct sol_blob *
 * SOL_LWM2M_RESOURCE_DATA_TYPE_TIME | int64_t
 * SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK | uint16_t, uint16_t
 *
 *
 * @param resource The resource to be initialized.
 * @param id The resource id.
 * @param type The resource type.
 * @param resource_len The resource data size.
 * @param data_type The resource data type.
 * @param ... The LWM2M resource data, respecting the table according to the resource data type.
 * @return 0 on success, negative errno on error.
 * @see sol_lwm2m_resource_clear()
 * @see SOL_LWM2M_RESOURCE_INIT()
 *
 * @note The LWM2M resource api_version must be set,
 * before calling this function.
 */
int sol_lwm2m_resource_init(struct sol_lwm2m_resource *resource,
    uint16_t id, enum sol_lwm2m_resource_type type, uint16_t resource_len,
    enum sol_lwm2m_resource_data_type data_type, ...);

/**
 * @brief Initializes a LWM2M resource of type multiple using a @c sol_vector.
 *
 * This function makes it easier to init a LWM2M resource of type multiple,
 * dynamically setting the amount of Resource Instances desired.
 * The last argument is a @c sol_vector holding elements of type
 * @c sol_lwm2m_resource_data, each element carrying the Resource Instance ID
 * and related Resource Instance value.
 *
 * @param resource The resource to be initialized.
 * @param id The resource id.
 * @param data_type The resource data type.
 * @param res_instances The list of Resource Instances. Each Resource Instance value must respect the table according to the resource data type.
 * @return 0 on success, negative errno on error.
 * @see sol_lwm2m_resource_clear()
 * @see sol_lwm2m_resource_init()
 *
 * @note The LWM2M resource api_version must be set,
 * before calling this function.
 */
int sol_lwm2m_resource_init_vector(struct sol_lwm2m_resource *resource,
    uint16_t id, enum sol_lwm2m_resource_data_type data_type,
    struct sol_vector *res_instances);

/**
 * @brief Parses a binary content into TLV.
 *
 * @param content A binary data that contains the TLV.
 * @param tlv_values An array of #sol_lwm2m_tlv that will be filled.
 *
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_parse_tlv(const struct sol_str_slice content, struct sol_vector *tlv_values);

/**
 * @brief Clears an TLV array.
 *
 * @param tlvs The TLVs array to be cleared.
 */
void sol_lwm2m_tlv_list_clear(struct sol_vector *tlvs);

/**
 * @brief Clear a TLV.
 *
 * @param tlv The TLV the be cleared.
 */
void sol_lwm2m_tlv_clear(struct sol_lwm2m_tlv *tlv);

/**
 * @brief Converts an TLV value to float value.
 *
 * @param tlv The tlv data.
 * @param value The converted value.
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_tlv_get_float(struct sol_lwm2m_tlv *tlv, double *value);

/**
 * @brief Converts an TLV value to boolean value.
 *
 * @param tlv The tlv data.
 * @param value The converted value.
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_tlv_get_bool(struct sol_lwm2m_tlv *tlv, bool *value);

/**
 * @brief Converts an TLV value to int value.
 *
 * @param tlv The tlv data.
 * @param value The converted value.
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_tlv_get_int(struct sol_lwm2m_tlv *tlv, int64_t *value);

/**
 *
 * @brief Get TLV content is plain bytes.
 * @param tlv The tlv data.
 * @param buf The buffer to store the content.
 * @return 0 on succes, -errno on error.
 */
int sol_lwm2m_tlv_get_bytes(struct sol_lwm2m_tlv *tlv, struct sol_buffer *buf);

/**
 * @brief Converts an TLV value to object link.
 *
 * @param tlv The tlv data.
 * @param object_id The object id.
 * @param instance_id the instance id.
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_tlv_get_obj_link(struct sol_lwm2m_tlv *tlv, uint16_t *object_id, uint16_t *instance_id);

/**
 * @brief Gets the object id.
 *
 * @param object The LWM2M object to get the id.
 * @param id The object id to be filled.
 * @return 0 success, negative errno on error.
 */
int sol_lwm2m_client_object_get_id(const struct sol_lwm2m_client_object *object, uint16_t *id);

/**
 * @brief Gets the instances of a given object.
 *
 * @param object The LWM2M object to get the instances.
 * @return An array of uint16_t or @c NULL on error.
 * @note Be advised that it's not recommended to store object
 * instances pointers, because they might be deleted by other LWM2M servers,
 * thus removed from the returned list.
 */
const struct sol_ptr_vector *sol_lwm2m_client_object_get_instances(const struct sol_lwm2m_client_object *object);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif
