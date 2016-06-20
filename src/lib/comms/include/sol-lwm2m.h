/*
 * This file is part of the Soletta™ Project
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
 * - Registration interface.
 * - Management interface.
 * - Observation interface.
 * - TLV format.
 *
 * Unsupported features for now:
 * - Bootstrap.
 * - LWM2M JSON.
 * - Queue Mode operation (only 'U' is supported for now).
 * - Data encryption.
 * - Access rights.
 *
 * @{
 */

/**
 * @brief Macro that defines the default port for a LWM2M server.
 */
#define SOL_LWM2M_DEFAULT_SERVER_PORT (5683)

/**
 * @struct sol_lwm2m_client
 * @brief A handle to a LWM2M client.
 * @see sol_lwm2m_client_new().
 */
struct sol_lwm2m_client;

/**
 * @struct sol_lwm2m_server
 * @brief A handle to a LWM2M server.
 * @see sol_lwm2m_server_new()
 */
struct sol_lwm2m_server;

/**
 * @struct sol_lwm2m_bootstrap_server
 * @brief A handle to a LWM2M bootstrap server.
 * @see sol_lwm2m_bootstrap_server_new()
 */
struct sol_lwm2m_bootstrap_server;

/**
 * @struct sol_lwm2m_bootstrap_client_info
 * @brief A handle that contains information about a bootstrapping LWM2M client.
 */
struct sol_lwm2m_bootstrap_client_info;

/**
 * @struct sol_lwm2m_client_info
 * @brief A handle that contains information about a registered LWM2M client.
 * @see sol_lwm2m_server_get_clients()
 */
struct sol_lwm2m_client_info;

/**
 * @struct sol_lwm2m_client_object
 * @brief A handle of a client's object.
 * @see sol_lwm2m_client_info_get_objects()
 */
struct sol_lwm2m_client_object;

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
 * @brief Enum that expresses a LWM2M client lifecycle changes.
 *
 * @see sol_lwm2m_server_add_registration_monitor()
 */
enum sol_lwm2m_registration_event {
    /**
     * Indicates that a client was registered in the server.
     */
    SOL_LWM2M_REGISTRATION_EVENT_REGISTER,
    /**
     * Indicates that a client updated itself in the server.
     */
    SOL_LWM2M_REGISTRATION_EVENT_UPDATE,
    /**
     * Indicates that a client was unregistered.
     */
    SOL_LWM2M_REGISTRATION_EVENT_UNREGISTER,
    /**
     * Indicates that the server is discarding a client, since
     * the server did not hear from it after some time.
     */
    SOL_LWM2M_REGISTRATION_EVENT_TIMEOUT
};

/**
 * @brief Enum that express the bootstrapping lifecycle.
 *
 * @see sol_lwm2m_client_add_bootstrap_monitor()
 */
enum sol_lwm2m_bootstrap_event {
    /**
     * Indicates that a server finished bootstrapping the client.
     */
    SOL_LWM2M_BOOTSTRAP_EVENT_FINISHED,
    /**
     * Indicates that an error occurred during the bootstrap process.
     */
    SOL_LWM2M_BOOTSTRAP_EVENT_ERROR
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
struct sol_lwm2m_tlv {
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
};

/**
 * @brief Struct that represents a LWM2M resource.
 * @see sol_lwm2m_resource_init()
 */
struct sol_lwm2m_resource {
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
    union sol_lwm2m_resource_data {
        /** @brief The resource is opaque or an string */
        struct sol_blob *blob;
        /** @brief The resource is a integer value */
        int64_t integer;
        /** @brief The resource is a float value */
        double fp;
        /** @brief The resource is a bool value */
        bool b;
    } *data;
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
 * @param data_type_ The resource data type.
 * @param resource_len_ The resource data size.
 * @param ... The LWM2M resource data, respecting the table according to the resource type.
 * @see sol_lwm2m_resource_init()
 */
#define SOL_LWM2M_RESOURCE_INIT(ret_value_, resource_, id_, resource_len_, data_type_, ...) \
    do { \
        SOL_SET_API_VERSION((resource_)->api_version = SOL_LWM2M_RESOURCE_API_VERSION; ) \
        (ret_value_) = sol_lwm2m_resource_init((resource_), (id_), (resource_len_), (data_type_), __VA_ARGS__); \
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
#define SOL_LWM2M_RESOURCE_INT_INIT(ret_value_, resource_, id_, value_) \
    do { \
        SOL_SET_API_VERSION((resource_)->api_version = SOL_LWM2M_RESOURCE_API_VERSION; ) \
        (ret_value_) = sol_lwm2m_resource_init((resource_), (id_), 1, SOL_LWM2M_RESOURCE_DATA_TYPE_INT, SOL_TYPE_CHECK(int64_t, (value_))); \
    } while (0)

/** @brief A LWM2M object implementation.
 *
 * Every LWM2M client must implement a set of LWM2M objects,
 * This struct is used by the sol-lwm2m to know which objects a
 * LWM2M Client implements.
 *
 * All the functions in this struct will be called by the sol-lwm2m infra,
 * when the LWM2M server request an operation.
 * For example, when the LWM2M server requests the creation for a LWM2M
 * location object, the create function will be called.
 * When a LWM2M object does not support a certain operation,
 * one must not implement the corresponding method.
 * In order words, if a LWM2M object can't be deleted,
 * the @c del handle must be ponting to @c NULL
 *
 * @see sol_lwm2m_client_new()
 */
struct sol_lwm2m_object {
#ifndef SOL_NO_API_VERSION
#define SOL_LWM2M_OBJECT_API_VERSION (1)
    /** @brief API version */
    uint16_t api_version;
#endif

    /** @brief The object id */
    uint16_t id;

    /* @brief The number of resources that the object has */
    uint16_t resources_count;

    /**
     * @brief Creates a new object instance.
     *
     *
     * @param user_data The data provided during sol_lwm2m_client_new().
     * @param client The LWM2M client.
     * @param istance_id The instance ID that is being created.
     * @param instance_data The pointer where the instance data should be stored.
     * @param content_type The content type of the @c content
     * @param content The object's initial content.
     * @return 0 on success, -errno on error.
     */
    int (*create)(void *user_data, struct sol_lwm2m_client *client,
        uint16_t instance_id, void **instance_data,
        enum sol_lwm2m_content_type content_type,
        const struct sol_str_slice content);
    /**
     * @brief Reads a resource.
     *
     * When the LWM2M server requests to read an object, object instance
     * or a single resource, this function will be triggered.
     * This function will read one resource at time, in case
     * the LWM2M server wants to read an object instance or all instances
     * of an object the LWM2M client infrastructure will call this function
     * several times requesting to read each resource.
     *
     * @param instance_data The instance data.
     * @param user_data The data provided during sol_lwm2m_client_new().
     * @param client The LWM2M client.
     * @param instance_id The instance id.
     * @param res_id The resource that should be read.
     * @param res The resource content, it should be set using
     * sol_lwm2m_resource_init()
     * @return 0 on success, -ENOENT if the resource is empty, -EINVAL
     * if the resource does not exist or -errno on error.
     * @see sol_lwm2m_resource_init()
     */
    int (*read)(void *instance_data, void *user_data,
        struct sol_lwm2m_client *client, uint16_t instance_id,
        uint16_t res_id, struct sol_lwm2m_resource *res);
    /**
     * @brief Writes a resource.
     *
     * When the LWM2M server requests to write a resource and flags
     * that the content type of the request is text, a scalar type or
     * an opaque type, this function will be called.
     *
     * @param instance_data The instance data.
     * @param user_data The data provided during sol_lwm2m_client_new().
     * @param client The LWM2M client.
     * @param instance_id The instance id.
     * @param res_id The resource id that is being written.
     * @param res The resource content.
     * @return 0 on success or -errno on error.
     * @note This function is only called when the LWM2M server explicitly
     * says that the content type of the write operation is a text or
     * an opaque type.
     */
    int (*write_resource)(void *instance_data, void *user_data,
        struct sol_lwm2m_client *client, uint16_t instance_id, uint16_t res_id,
        const struct sol_lwm2m_resource *res);
    /**
     * @brief Writes a resource(s).
     *
     * Every time the LWM2M server requests to write a resource or a
     * whole object instance in TLV type, this function will be called.
     * The @c tlvs arrays contains #sol_lwm2m_tlv which is the
     * data that the LWM2M server demands to be written.
     * Since TLV is a binary type, one must call
     * #sol_lwm2m_tlv_get_int and friends function to obtain the TLV value.
     *
     * @param instance_data The instance data.
     * @param user_data The data provided during sol_lwm2m_client_new().
     * @param client The LWM2M client.
     * @param instance_id The instance id.
     * @param tlvs A vector of #sol_lwm2m_tlv
     * @return 0 on success or -errno on error.
     * @note Since TLV does not contains a field to express the
     * data type, it's the user's responsibility to know which
     * function should be used to get the content value.
     */
    int (*write_tlv)(void *instance_data, void *user_data,
        struct sol_lwm2m_client *client, uint16_t instance_id,
        struct sol_vector *tlvs);
    /**
     * @brief Executes a resource.
     *
     * A LWM2M Object resource may be executable. An executable resource
     * means that the LWM2M object instance will initiate some action
     * that was requested by the LWM2M server.
     * As an example, if the LWM2M server wants the client
     * to send an update request, the LWM2M server will send
     * an execute command on the path "/1/AnServerInstanceId/8", this will
     * trigger the LWM2M client, which will send the update
     * request.
     *
     * @param instance_data The instance data.
     * @param user_data The data provided during sol_lwm2m_client_new().
     * @param client The LWM2M client.
     * @param instance_id The instance id.
     * @param res_id The resource that should be executed.
     * @param args The arguments of the execute operation.
     * @return 0 on success or -errno on error.
     */
    int (*execute)(void *instance_data, void *user_data,
        struct sol_lwm2m_client *client, uint16_t instance_id,
        uint16_t res_id, const struct sol_str_slice args);

    /**
     * @brief Deletes an object instance.
     *
     * @param instance_data The instance data to be freed.
     * @param used_data The data provided during sol_lwm2m_client_new().
     * @param client The LWM2M client.
     * @param instance_id The instance ID that is being deleted.
     * @return 0 on success or -errno on error.
     */
    int (*del)(void *instance_data, void *user_data,
        struct sol_lwm2m_client *client, uint16_t instance_id);
};

/**
 *
 * @brief Creates a new LWM2M client.
 *
 * This function will create a new LWM2M client with its objects.
 * In order to start the LWM2M client and connect with the LWM2M servers,
 * one must call #sol_lwm2m_client_start.
 *
 * @param name The LWM2M client name, must not be @c NULL.
 * @param path The Objects path, may be @c NULL.
 * @param sms The SMS number, may be @c NULL.
 * @param objects The implemented objects, must not be @c NULL and must be @c NULL terminated.
 * @param data The user's data that will be passed to the object callbacks. (create, execute, read, write and del).
 * @return A LWM2M client handle or @c NULL on error
 * @see sol_lwm2m_client_del()
 * @see sol_lwm2m_add_object_instance()
 * @see sol_lwm2m_client_start()
 */
struct sol_lwm2m_client *sol_lwm2m_client_new(const char *name,
    const char *path, const char *sms, const struct sol_lwm2m_object **objects,
    const void *data);

/**
 * @brief Deletes a LWM2M client.
 *
 * This will automatically stop the LWM2M client as well.
 *
 * @param client The client to be deleted.
 *
 * @see sol_lwm2m_client_start()
 */
void sol_lwm2m_client_del(struct sol_lwm2m_client *client);

/**
 * @brief Creates an object instance.
 *
 * @param client The client to create the object instance in.
 * @param obj The object that the instance should be created.
 * @param data The instance data.
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_add_object_instance(struct sol_lwm2m_client *client,
    const struct sol_lwm2m_object *obj, const void *data);

/**
 * @brief Starts the LWM2M client.
 *
 * The LWM2M client will attempt to connect with all the registered
 * LWM2M servers.
 * The LWM2M client will look for the Security and Server LWM2M objects in order
 * to connect with the LWM2M servers.
 *
 * @param client The client to be started.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_client_stop()
 */
int sol_lwm2m_client_start(struct sol_lwm2m_client *client);

/**
 * @brief Stops the LWM2M client.
 *
 * This will make the LWM2M client to stop receiving/sending messages from/to
 * the LWM2M servers. It's important to note that the objects and
 * object instances will not be deleted.
 *
 * In order to be able to respond to commands from a LWM2M server,
 * one must call #sol_lwm2m_client_start
 *
 * @param client The client to be stopped.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_client_start()
 */
int sol_lwm2m_client_stop(struct sol_lwm2m_client *client);

/**
 * @brief Sends an update message to the LWM2M servers.
 *
 * This will trigger the update method of the LWM2M registration interface.
 * The client will send an update to all the registered LWM2M servers.
 *
 * @param client The client to send the update request.
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_client_send_update(struct sol_lwm2m_client *client);

/**
 * @brief Notifies all the observing LWM2M servers that a resource has changed.
 *
 * Use this function to notify the LWM2M servers that an
 * Object instance resource value has changed.
 *
 * @param client The LWM2M client.
 * @param paths The resource paths that were changed, must be @c NULL terminated.
 * @return 0 on success, -errno on error.
 * @note If a LWM2M server creates an object instance, writes on an object instance or
 * writes in an object resource, the LWM2M client infrastructure will automatically
 * notify all observing servers.
 */
int sol_lwm2m_client_notify(struct sol_lwm2m_client *client, const char **paths);

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
 * SOL_LWM2M_RESOURCE_DATA_TYPE_STRING | struct sol_str_slice
 * SOL_LWM2M_RESOURCE_DATA_TYPE_INT | int64_t
 * SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT | double
 * SOL_LWM2M_RESOURCE_DATA_TYPE_BOOL | bool
 * SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE | struct sol_str_slice
 * SOL_LWM2M_RESOURCE_DATA_TYPE_TIME | int64_t
 * SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK | uint16_t, uint16_t
 *
 *
 * @param resource The resource to be initialized.
 * @param id The resource id.
 * @param data_type The resource data type.
 * @param resource_len The resource data size.
 * @param ... The LWM2M resource data, respecting the table according to the resource data type.
 * @return 0 on success, negative errno on error.
 * @see sol_lwm2m_resource_clear()
 * @see SOL_LWM2M_RESOURCE_INIT()
 *
 * @note The LWM2M resource api_version must be set,
 * before calling this function.
 */
int sol_lwm2m_resource_init(struct sol_lwm2m_resource *resource,
    uint16_t id, uint16_t resource_len,
    enum sol_lwm2m_resource_data_type data_type, ...);

/**
 * @brief Creates a new LWM2M bootstrap server.
 *
 * The server will be immediately operational and waiting for connections.
 *
 * @param port The UDP port to be used.
 * @param known_clients An array with the name of all clients this server has Bootstrap Information for.
 * @return The LWM2M bootstrap server or @c NULL on error.
 */
struct sol_lwm2m_bootstrap_server *sol_lwm2m_bootstrap_server_new(uint16_t port,
    const char **known_clients);

/**
 * @brief Creates a new LWM2M server.
 *
 * The server will be immediately operational and waiting for connections.
 *
 * @param port The UDP port to be used.
 * @return The LWM2M server or @c NULL on error.
 */
struct sol_lwm2m_server *sol_lwm2m_server_new(uint16_t port);

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
 * @brief Adds a bootstrap request monitor to the server.
 *
 * This function register a bootstrap request monitor.
 * This means that every time a LWM2M client performs a Bootstrap Request
 * @c sol_lwm2m_bootstrap_server_request_cb will be called.
 *
 * @param server The LWM2M bootstrap server.
 * @param sol_lwm2m_bootstrap_server_request_cb A callback that is used to inform a LWM2M client bootstrap request - @c data User data; @c server The LWM2M bootstrap server; @c bs_cinfo The client that initiated the bootstrap request.
 * @param data The user data to @c sol_lwm2m_bootstrap_server_request_cb.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_bootstrap_server_del_request_monitor()
 */
int sol_lwm2m_bootstrap_server_add_request_monitor(struct sol_lwm2m_bootstrap_server *server,
    void (*sol_lwm2m_bootstrap_server_request_cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo),
    const void *data);

/**
 * @brief Removes a bootstrap request monitor from the server.
 *
 * @param server The LWM2M bootstrap server.
 * @param sol_lwm2m_bootstrap_server_request_cb The previous registered callback. - @c data User data; @c server The LWM2M bootstrap server; @c bs_cinfo The client that initiated the bootstrap request.
 * @param data The user data to @c sol_lwm2m_bootstrap_server_request_cb.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_bootstrap_server_add_request_monitor()
 */
int sol_lwm2m_bootstrap_server_del_request_monitor(struct sol_lwm2m_bootstrap_server *server,
    void (*sol_lwm2m_bootstrap_server_request_cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *bs_cinfo),
    const void *data);

/**
 * @brief Adds a bootstrap monitor to the client.
 *
 * This function register a monitor.
 * This means that every time a LWM2M bootstrap server performs a Bootstrap Finish
 * @c sol_lwm2m_client_bootstrap_event_cb will be called.
 *
 * @param client The LWM2M client.
 * @param sol_lwm2m_client_bootstrap_event_cb A callback that is used to inform a LWM2M bootstrap server event - @c data User data; @c client The LWM2M client; @c event The bootstrap event itself.
 * @param data The user data to @c sol_lwm2m_client_bootstrap_event_cb.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_client_del_bootstrap_finish_monitor()
 */
int sol_lwm2m_client_add_bootstrap_finish_monitor(struct sol_lwm2m_client *client,
    void (*sol_lwm2m_client_bootstrap_event_cb)(void *data,
    struct sol_lwm2m_client *client,
    enum sol_lwm2m_bootstrap_event event),
    const void *data);

/**
 * @brief Removes a bootstrap monitor from the client.
 *
 * @param client The LWM2M client.
 * @param sol_lwm2m_client_bootstrap_event_cb The previous registered callback. - @c data User data; @c client The LWM2M client; @c event The bootstrap event itself.
 * @param data The user data to @c sol_lwm2m_client_bootstrap_event_cb.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_client_add_bootstrap_finish_monitor()
 */
int sol_lwm2m_client_del_bootstrap_finish_monitor(struct sol_lwm2m_client *client,
    void (*sol_lwm2m_client_bootstrap_event_cb)(void *data,
    struct sol_lwm2m_client *client,
    enum sol_lwm2m_bootstrap_event event),
    const void *data);

/**
 * @brief Writes a full object through the Bootstrap Interface.
 *
 * @param server The LWM2M bootstrap server.
 * @param client The LWM2M bootstrap client info to write.
 * @param path The object path to be written (Example /2).
 * @param instances An array of #sol_lwm2m_resource arrays
 * @param instances_len An array with the length of each element from @c instances
 * @param instances_ids An array with the desired instance_id of each element from @c instances
 * @param len The length of @c instances
 * @param sol_lwm2m_bootstrap_server_status_response_cb A callback to be called when the write operation is completed. - @c server The LW2M bootstrap server; @c client The LWM2M client; @c path The client's path; @c response_code The operation's @c response_code; @c data User data.
 * @param data User data to @c sol_lwm2m_bootstrap_server_status_response_cb
 * @return 0 on success, -errno on error.
 *
 * @note All data is sent using TLV.
 * @note Only in Bootstrap Interface, the Write operation can be made using a TLV consisting of various Object Instances.
 */
int sol_lwm2m_bootstrap_server_write_object(struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    struct sol_lwm2m_resource **instances, size_t *instances_len,
    uint16_t *instances_ids, size_t len,
    void (*sol_lwm2m_bootstrap_server_status_response_cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data);

/**
 * @brief Writes an object instance or resource through the Bootstrap Interface.
 *
 * @param server The LWM2M bootstrap server.
 * @param client The LWM2M bootstrap client info to write.
 * @param path The object path to be written (Example /2/1).
 * @param resources An array of #sol_lwm2m_resource
 * @param len The length of @c resources
 * @param sol_lwm2m_bootstrap_server_status_response_cb A callback to be called when the write operation is completed. - @c server The LW2M bootstrap server; @c client The LWM2M client; @c path The client's path; @c response_code The operation's @c response_code; @c data User data.
 * @param data User data to @c sol_lwm2m_bootstrap_server_status_response_cb
 * @return 0 on success, -errno on error.
 *
 * @note All data is sent using TLV.
 * @note Only in Bootstrap Interface, the Write operation can be made using a TLV consisting of various Object Instances.
 */
int sol_lwm2m_bootstrap_server_write(struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    struct sol_lwm2m_resource *resources, size_t len,
    void (*sol_lwm2m_bootstrap_server_status_response_cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data);

/**
 * @brief Deletes an object instance on a client through the Bootstrap Interface.
 *
 * @param server The LWM2M bootstrap server.
 * @param client The LWM2M bootstrap client info to delete an object
 * @param path The object path to be deleted (Example /1/1).
 * @param sol_lwm2m_bootstrap_server_status_response_cb A callback to be called when the delete operation is completed. - @c server The LW2M bootstrap server; @c client The LWM2M client; @c path The client's path; @c response_code The operation's @c response_code; @c data User data.
 * @param data User data to @c sol_lwm2m_bootstrap_server_status_response_cb
 * @return 0 on success, -errno on error.
 *
 * @note Only in Bootstrap Interface, Delete operation MAY target to '/' URI to delete all the existing Object Instances - except LWM2M Bootstrap Server Account - in the LWM2M Client, for initialization purpose before LWM2M Bootstrap Server sends Write operation(s) to the LWM2M Client.
 */
int sol_lwm2m_bootstrap_server_delete_object_instance(struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    void (*sol_lwm2m_bootstrap_server_status_response_cb)(void *data,
    struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data);

/**
 * @brief Adds a registration monitor.
 *
 * This function registers a monitor, making it easier to observe a
 * LWM2M client's life cycle. This means that every time a LWM2M
 * client is registered, updated, deleted or timed out, @a
 * sol_lwm2m_server_registration_event_cb will be called.
 *
 * @param server The LWM2M server.
 * @param sol_lwm2m_server_registration_event_cb A callback that is used to inform a LWM2M client registration event - @c data User data; @c server The LWM2M server; @c cinfo The client that generated the registration event; @c event The registration event itself.
 * @param data The user data to @c sol_lwm2m_server_registration_event_cb.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_server_del_registration_monitor()
 */
int sol_lwm2m_server_add_registration_monitor(struct sol_lwm2m_server *server,
    void (*sol_lwm2m_server_registration_event_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event),
    const void *data);

/**
 * @brief Removes a registration monitor.
 *
 * @param server The LWM2M server.
 * @param sol_lwm2m_server_registration_event_cb The previous registered callback. - @c data User data; @c server The LWM2M server; @c cinfo The client that generated the registration event; @c event The registration event itself.
 * @param data The user data to @c sol_lwm2m_server_registration_event_cb.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_server_add_registration_monitor()
 */
int sol_lwm2m_server_del_registration_monitor(struct sol_lwm2m_server *server,
    void (*sol_lwm2m_server_registration_event_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event),
    const void *data);

/**
 * @brief Gets all registerd clients.
 *
 * @param server The LWM2M Server.
 * @return a vector of #sol_lwm2m_client_info or @c NULL on error.
 * @note One must not add or remove elements from the returned vector.
 * @see sol_lwm2m_server_add_registration_monitor()
 */
const struct sol_ptr_vector *sol_lwm2m_server_get_clients(const struct sol_lwm2m_server *server);

/**
 * @brief Observes a client object, instance or resource.
 *
 * Every time the observed path changes, the client will notify the LWM2M server.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client to be observed.
 * @param path The path to be observed (Example: /3/0/0).
 * @param sol_lwm2m_server_content_cb A callback to be called when the observed path changes, used to inform a observable/read response. - @c server The LWM2M server; @c client The LWM2M client; @c path The client's path; @c response_code The response code; @c content_type The response content type; @c content The response content; @c data User data.
 * @param data User data to @c sol_lwm2m_server_content_cb
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_server_del_observer()
 */
int sol_lwm2m_server_add_observer(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    void (*sol_lwm2m_server_content_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content),
    const void *data);

/**
 * @brief Unobserve a client object, instance or resource.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client to be unobserved.
 * @param path The path to be unobserved (Example: /3/0/0).
 * @param sol_lwm2m_server_content_cb The previous registered callback. - @c server The LWM2M server; @c client The LWM2M client; @c path The client's path; @ response_code The response code; @c content_type The response content type; @c content The response content; @c data User data.
 * @param data User data to @c sol_lwm2m_server_content_cb
 * @return 0 on success, -errno on error.
 *
 * @note In order do completly unobserve a path, all observers must be deleted.
 * @see sol_lwm2m_server_add_observer()
 */
int sol_lwm2m_server_del_observer(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    void (*sol_lwm2m_server_content_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content),
    const void *data);

/**
 * @brief Writes an object instance or resource.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to write
 * @param path The object path to be written (Example /1/1).
 * @param resources An array of #sol_lwm2m_resource
 * @param len The length of @c resources
 * @param sol_lwm2m_server_management_status_response_cb A callback to be called when the write operation is completed. - @c server The LW2M server; @c client The LWM2M client; @c path The client's path; @c response_code The operation's @c response_code; @c data User data.
 * @param data User data to @c sol_lwm2m_server_management_status_response_cb
 * @return 0 on success, -errno on error.
 *
 * @note All data is sent using TLV.
 */
int sol_lwm2m_server_write(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    struct sol_lwm2m_resource *resources, size_t len,
    void (*sol_lwm2m_server_management_status_response_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data);

/**
 * @brief Deletes an object instance on a client.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to delete an object
 * @param path The object path to be deleted (Example /1/1).
 * @param sol_lwm2m_server_management_status_response_cb A callback to be called when the delete operation is completed. - @c server The LW2M server; @c client The LWM2M client; @c path The client's path; @c response_code The operation's @c response_code; @c data User data.
 * @param data User data to @c sol_lwm2m_server_management_status_response_cb
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_server_delete_object_instance(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    void (*sol_lwm2m_server_management_status_response_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data);

/**
 * @brief Executes an resource on a client.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to execute the resource.
 * @param path The object path to be executed (Example /1/1/8).
 * @param args Arguments to the execute command.
 * @param sol_lwm2m_server_management_status_response_cb A callback to be called when the execute operation is completed. - @c server The LW2M server; @c client The LWM2M client; @c path The client's path; @c response_code The operation's @c response_code; @c data User data.
 * @param data User data to @c sol_lwm2m_server_management_status_response_cb
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_server_execute_resource(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path, const char *args,
    void (*sol_lwm2m_server_management_status_response_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data);

/**
 * @brief Creates an object instance on a client.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to create an object instance.
 * @param path The object path to be created (Example /1).
 * @param resources An array of #sol_lwm2m_resource which contains the required resources to create an object.
 * @param len The length of @c resources.
 * @param sol_lwm2m_server_management_status_response_cb A callback to be called when the create operation is completed. - @c server The LW2M server; @c client The LWM2M client; @c path The client's path; @c response_code The operation's @c response_code; @c data User data.
 * @param data User data to @c sol_lwm2m_server_management_status_response_cb
 * @return 0 on success, -errno on error.
 *
 * @note All data is sent using TLV.
 */
int sol_lwm2m_server_create_object_instance(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    struct sol_lwm2m_resource *resources, size_t len,
    void (*sol_lwm2m_server_management_status_response_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    enum sol_coap_response_code response_code),
    const void *data);

/**
 * @brief Reads an object, instance or resource from a client.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to be read.
 * @param path The path to be read (Example /3/0/0).
 * @param sol_lwm2m_server_content_cb A callback to be called when the read operation is completed. - @c server The LWM2M server; @c client The LWM2M client; @c path The client's path; @c response_code The response code; @c content_type The response content type; @c content The response content; @c data User data.
 * @param data User data to @c sol_lwm2m_server_content_cb
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_server_read(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    void (*sol_lwm2m_server_content_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    enum sol_coap_response_code response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content),
    const void *data);

/**
 * @brief Deletes a bootstrap server instance.
 *
 * Use this function to stop the LWM2M bootstrap server and release its resources.
 *
 * @param server The LWM2M bootstrap server to be deleted.
 * @see sol_lwm2m_bootstrap_server_new()
 */
void sol_lwm2m_bootstrap_server_del(struct sol_lwm2m_bootstrap_server *server);

/**
 * @brief Deletes a server instance.
 *
 * Use this function to stop the LWM2M server and release its resources.
 *
 * @param server The LWM2M server to be deleted.
 * @see sol_lwm2m_server_new()
 */
void sol_lwm2m_server_del(struct sol_lwm2m_server *server);

/**
 * @brief Signals the end of the Bootstrap Process.
 *
 * Use this function to tell the LWM2M Client that this LWM2M Bootstrap
 * Server has finished sending the available Bootstrap Information.
 *
 * @param server The LWM2M Bootstrap Server.
 * @param client The LWM2M Bootstrap Client info object.
 * @note After this function returns, the @c sol_lwm2m_bootstrap_client_info handle will be invalid!
 * @see sol_lwm2m_bootstrap_server_new()
 */
int sol_lwm2m_bootstrap_server_send_finish(struct sol_lwm2m_bootstrap_server *server,
    struct sol_lwm2m_bootstrap_client_info *client);

/**
 * @brief Gets the name of bootstrap client.
 *
 * @param client The LWM2M bootstrap client info.
 * @return The @c client name or @c NULL on error.
 */
const char *sol_lwm2m_bootstrap_client_info_get_name(const struct sol_lwm2m_bootstrap_client_info *client);

/**
 * @brief Gets the name of client.
 *
 * @param client The LWM2M client info.
 * @return The @c client name or @c NULL on error.
 */
const char *sol_lwm2m_client_info_get_name(const struct sol_lwm2m_client_info *client);

/**
 * @brief Gets the client location path in the LWM2M server.
 *
 * This value is specified by the LWM2M server and it will be used by the client
 * to identify itself.
 *
 * @param client The LWM2M client info.
 * @return The @c client location path or @c NULL on error.
 */
const char *sol_lwm2m_client_info_get_location(const struct sol_lwm2m_client_info *client);

/**
 * @brief Gets the client SMS number.
 *
 * A client may specify an SMS number to be used for communication.
 *
 * @param client The LWM2M client info.
 * @return The SMS number or @c NULL.
 */
const char *sol_lwm2m_client_info_get_sms_number(const struct sol_lwm2m_client_info *client);

/**
 * @brief Gets the client objects path.
 *
 * A LWM2M client may specify an alternate objects path.
 *
 * @param client The LWM2M client info.
 * @return The objects path or @c NULL.
 * @note The objects path may be @c NULL.
 */
const char *sol_lwm2m_client_info_get_objects_path(const struct sol_lwm2m_client_info *client);

/**
 * @brief Gets the client lifetime in seconds.
 *
 * @param client The LWM2M client info.
 * @param lifetime The client lifetime to be set.
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_client_info_get_lifetime(const struct sol_lwm2m_client_info *client, uint32_t *lifetime);

/**
 * @brief Gets the client binding mode.
 *
 * @param client The LWM2M client info.
 * @return The client binding mode or #SOL_LWM2M_BINDING_MODE_UNKNOWN on error.
 */
enum sol_lwm2m_binding_mode sol_lwm2m_client_info_get_binding_mode(const struct sol_lwm2m_client_info *client);

/**
 * @brief Gets the bootstrap client address.
 *
 * @param client The LWM2M bootstrap client info.
 * @return The @c client address or @c NULL on error.
 */
const struct sol_network_link_addr *sol_lwm2m_bootstrap_client_info_get_address(const struct sol_lwm2m_bootstrap_client_info *client);

/**
 * @brief Gets the client address.
 *
 * @param client The LWM2M client info.
 * @return The @c client address or @c NULL on error.
 */
const struct sol_network_link_addr *sol_lwm2m_client_info_get_address(const struct sol_lwm2m_client_info *client);

/**
 * @brief Get client's objects.
 *
 * @param client The LWM2M client info.
 * @return A array of #sol_lwm2m_client_object or @c NULL on error.
 * @note One must not add or remove elements from the returned vector.
 * @note Be advised that it's not recommended to store object
 * pointers, because during the client's update method, all
 * the objects are renewed.
 * @see #sol_lwm2m_client_object
 */
const struct sol_ptr_vector *sol_lwm2m_client_info_get_objects(const struct sol_lwm2m_client_info *client);

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
