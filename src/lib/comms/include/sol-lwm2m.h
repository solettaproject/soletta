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
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#include "sol-coap.h"
#include "sol-network.h"
#include "sol-str-slice.h"
#include "sol-vector.h"
#include "sol-buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines that handle LWM2M protocol.
 *
 */

/**
 * @defgroup LWM2M LWM2M
 * @ingroup Comms
 *
 * @brief Routines that handle LWM2M protocol.
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
 * @struct sol_lwm2m_client_info
 * @brief A handle that contains information about a registered LWM2M client.
 * @see sol_lwm2m_server_get_clients()
 */
struct sol_lwm2m_client_info;

/**
 * @struct sol_lwm2m_client_object_instance
 * @brief A handle that contains information about a client object instance.
 */
struct sol_lwm2m_client_object_instance;

/**
 * @struct sol_lwm2m_client_object
 * @brief A handle of a client's object.
 * @see sol_lwm2m_client_info_get_objects()
 */
struct sol_lwm2m_client_object;

/**
 * @brief LWM2M Client binding mode.
 *
 * A LWM2M server, may support multiple forms of binding.
 * The binding mode is requested by a client during its registration.
 *
 * In Queue binding mode a client flags to the server that it
 * may not be available for communication all the time, thus
 * the server must wait until it receives a heartbeat from the
 * client until it can send requests. The queue binding mode
 * is usefull, because the client may enter in deep sleep
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
 * @brief Enum that express a LWM2M client lifecycle changes.
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
     * Indicates that the server is discarting a client, since
     * the server did not hear from it after some time.
     */
    SOL_LWM2M_REGISTRATION_EVENT_TIMEOUT
};

/**
 * @brief Enum that represent a LWM2M response/request content type.
 */
enum sol_lwm2m_content_type {
    /**
     * The content type message is pure text.
     */
    SOL_LWM2M_CONTENT_TYPE_TEXT = 1541,
    /**
     * The content type of the message is indeterminated, in order
     * words, is an array of bytes.
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
 * @brief Enum that represent the TLV type.
 * @see #sol_lwm2m_tlv
 */
enum sol_lwm2m_tlv_type {
    /**
     * The TLV represents an object instance
     */
    SOL_LWM2M_TLV_TYPE_OBJECT_INSTANCE = 0,
    /**
     * The TLV repreents an resource instance.
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
 * @brief Enum that represents an LWM2M resource data type.
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
    SOL_LWM2M_RESOURCE_DATA_TYPE_BOOLEAN,
    /**
     * The resource value is opaque.
     */
    SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE,
    /**
       The resource value is a timestamp (Unix time).
     */
    SOL_LWM2M_RESOURCE_DATA_TYPE_TIME,
    /**
     * The resource value is a object link.
     */
    SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK,
    /**
     * The resource value is indeterminated.
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
 * @see sol_lwm2m_parse_tlv()
 */
struct sol_lwm2m_tlv {
#ifndef SOL_NO_API_VERSION
#define SOL_LWM2M_TLV_API_VERSION (1)
    /** @brief API version */
    uint16_t api_version;
    /** @brief Unused */
    uint16_t reserved;
#endif

    /** @brief The TLV type */
    enum sol_lwm2m_tlv_type type;
    /** @brief The id of the object/instance/resource */
    uint16_t id;
    /** @brief The TLV content */
    struct sol_buffer content;
};

/**
 * @brief Struct that represents an LWM2M resource.
 * @see sol_lwm2m_resource_init()
 */
struct sol_lwm2m_resource {
#ifndef SOL_NO_API_VERSION
#define SOL_LWM2M_RESOURCE_API_VERSION (1)
    /** @brief API version */
    uint16_t api_version;
    /** @brief Unused */
    uint16_t reserved;
#endif
    /** @brief The resource type */
    enum sol_lwm2m_resource_type type;

    /** @brief The resource data type */
    enum sol_lwm2m_resource_data_type data_type;

    /** @brief The resource id */
    uint16_t id;
    /** @brief The resource data array */
    union resource_data {
        /** @brief The resource is opaque or an string */
        struct sol_str_slice bytes;
        /** @brief The resource is a integer value */
        int64_t integer;
        /** @brief The resource is a float value */
        double fp;
        /** @brief The resource is a bool value */
        bool b;
    } *data;
    /** @brief The resource data array len */
    uint16_t data_len;
};

/**
 * @brief A LWM2M object implementation.
 *
 * Every LWM2M client must implement a set of LWM2M objects,
 * This struct is used by the sol-lwm2m to know which objects a
 * LWM2M implements.
 *
 * All the functions in this struct will be called by the sol-lwm2m infra,
 * when the LWM2M server request an operation.
 * For exampe, with the a LWM2M server requests the creation for a LWM2M
 * location object, the create function will be called.
 * When a LWM2M object does not support a certain operation,
 * one must not implement the corresponding method.
 * In order words, if an LWM2M object can't be deleted,
 * the @c del handle must be ponting to @c NULL
 *
 * @see sol_lwm2m_client_new()
 */
struct sol_lwm2m_object {
#ifndef SOL_NO_API_VERSION
#define SOL_LWM2M_OBJECT_API_VERSION (1)
    /** @brief API version */
    uint16_t api_version;
    /** @brief Unused */
    uint16_t reserved;
#endif

    /** @brief The object id */
    uint16_t id;
    /** @brief The object data */
    const void *data;

    /**
     * @brief Creates a new object instance.
     *
     *
     * @param obj_data The object data.
     * @param client The LWM2M client.
     * @param istance_id The instance ID that is being created.
     * @param instance_data The
     * @return 0 on success, -errno on error.
     */
    int (*create)(void *obj_data, struct sol_lwm2m_client *client,
        uint16_t instance_id, void **instance_data);
    /**
     * @brief Reads a resource.
     *
     * @param instance_data The instance data.
     * @param obj_data The object data.
     * @param client The LWM2M client.
     * @param instance_id The instance id.
     * @param res_id The resource that should be read.
     * @param res The resource content, it should be set using
     * sol_lwm2m_resource_init()
     * @return 0 on success, -ENOENT if the resource is empty, -EBADRQC
     * if the resource does not exist or -errno on error.
     * @see sol_lwm2m_resource_init()
     */
    int (*read)(void *instance_data, void *obj_data,
        struct sol_lwm2m_client *client, uint16_t instance_id,
        uint16_t res_id, struct sol_lwm2m_resource *res);
    /**
     * @brief Writes a resources.
     *
     * @param instance_data The instance data.
     * @param obj_data The object data.
     * @param client The LWM2M client.
     * @param instance_id The instance id.
     * @param res_id The resource id that is being written.
     * @param res The resource content.
     * @return 0 on success or -errno on error.
     */
    int (*write_resource)(void *instance_data, void *obj_data,
        struct sol_lwm2m_client *client, uint16_t instance_id, uint16_t res_id,
        const struct sol_lwm2m_resource *res);
    /**
     * @brief Writes a resource(s).
     *
     * This function is used when the LWM2M server sends data
     * to be written as TLV format. This means that one must
     * call the convenient functions #sol_lwm2m_tlv_to_int and friends
     * before writing the resource value.
     *
     * @param instance_data The instance data.
     * @param obj_data The object data.
     * @param client The LWM2M client.
     * @param instance_id The instance id.
     * @param tlvs An vector of #sol_lwm2m_tlv
     * @return 0 on success or -errno on error.
     */
    int (*write_tlv)(void *instance_data, void *obj_data,
        struct sol_lwm2m_client *client, uint16_t instance_id,
        struct sol_vector *tlvs);
    /**
     * @brief Executes a resource.
     *
     * @param instance_data The instance data.
     * @param obj_data The object data.
     * @param client The LWM2M client.
     * @param instance_id The instance id.
     * @param res_id The resource that should be executed.
     * @param args The arguments of the execute operation.
     * @return 0 on success or -errno on error.
     */
    int (*execute)(void *instance_data, void *obj_data,
        struct sol_lwm2m_client *client, uint16_t instance_id,
        uint16_t res_id, const struct sol_str_slice args);

    /**
     * @brief Deletes an object instance.
     *
     * @param instance_data The instance data to be freed.
     * @param obj_data The object data.
     * @param client The LWM2M client.
     * @param instance_id The instance ID that is being deleted.
     * @return 0 on success or -errno on error.
     */
    int (*del)(void *instance_data, void *obj_data,
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
 * @param path The Objectes path, may be @c NULL.
 * @param sms The SMS number, may be @c NULL.
 * @param objects The implemented objects, must not be @c NULL.
 * @return A LWM2M client handle or @c NULL on error
 * @see sol_lwm2m_client_del()
 * @see sol_lwm2m_add_object_instance()
 * @see sol_lwm2m_client_start()
 */
struct sol_lwm2m_client *sol_lwm2m_client_new(const char *name,
    const char *path, const char *sms, const struct sol_lwm2m_object **objects);

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
 * @param client The client to create the object instance..
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
int sol_lwm2m_send_update(struct sol_lwm2m_client *client);

/**
 * @brief Clears a #sol_lwm2m_resource.
 *
 * @param resource The resource to ne cleared.
 * @see sol_lwm2m_resource_init()
 */
void sol_lwm2m_resource_clear(struct sol_lwm2m_resource *resource);

/**
 * @brief Initializes an LWM2M resource.
 *
 * This function makes it easir to init a LWM2M resource, it
 * will set the proper fields and fill its data. Note that
 * the last argument type varies with the resource type and one
 * must follow the table below.
 *
 * Resource type | Last argument type
 * ------------- | ------------------
 * SOL_LWM2M_RESOURCE_DATA_TYPE_STRING | struct sol_str_slice
 * SOL_LWM2M_RESOURCE_DATA_TYPE_INT | int64_t
 * SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT | double
 * SOL_LWM2M_RESOURCE_DATA_TYPE_BOOLEAN | bool
 * SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE | struct sol_str_slice
 * SOL_LWM2M_RESOURCE_DATA_TYPE_TIME | int64_t
 * SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK | uint16_t, uint16_t
 *
 *
 * @param resource The resource to be initialized.
 * @param id The resource id.
 * @param data_type The resource type.
 * @param resource_len The resource data size.
 * @param ... The LWM2M resource data, respecting the table according to the resource type.
 * @return 0 on success, negative errno on error.
 * @see sol_lwm2m_resource_clear()
 */
static inline int
sol_lwm2m_resource_init(struct sol_lwm2m_resource *resource,
    uint16_t id, uint16_t resource_len,
    enum sol_lwm2m_resource_data_type data_type, ...)
{
    uint16_t i;
    va_list ap;
    int r = 0;

    if (!resource || data_type == SOL_LWM2M_RESOURCE_DATA_TYPE_NONE ||
        !resource_len)
        return -EINVAL;

    SOL_SET_API_VERSION(resource->api_version = SOL_LWM2M_RESOURCE_API_VERSION; )
    resource->id = id;
    if (resource_len > 1)
        resource->type = SOL_LWM2M_RESOURCE_TYPE_MULTIPLE;
    else
        resource->type = SOL_LWM2M_RESOURCE_TYPE_SINGLE;
    resource->data_type = data_type;
    resource->data = calloc(resource_len, sizeof(union resource_data));
    resource->data_len = resource_len;

    va_start(ap, data_type);

    for (i = 0; i < resource_len; i++) {
        switch (resource->data_type) {
        case SOL_LWM2M_RESOURCE_DATA_TYPE_OPAQUE:
        case SOL_LWM2M_RESOURCE_DATA_TYPE_STRING:
            resource->data[i].bytes = va_arg(ap, struct sol_str_slice);
            break;
        case SOL_LWM2M_RESOURCE_DATA_TYPE_FLOAT:
            resource->data[i].fp = va_arg(ap, double);
            break;
        case SOL_LWM2M_RESOURCE_DATA_TYPE_INT:
        case SOL_LWM2M_RESOURCE_DATA_TYPE_TIME:
            resource->data[i].integer = va_arg(ap, int64_t);
            break;
        case SOL_LWM2M_RESOURCE_DATA_TYPE_BOOLEAN:
            resource->data[i].integer = va_arg(ap, int);
            break;
        case SOL_LWM2M_RESOURCE_DATA_TYPE_OBJ_LINK:
            resource->data[i].integer = (uint16_t)va_arg(ap, int);
            resource->data[i].integer = (resource->data[i].integer << 16) |
                (uint16_t)va_arg(ap, int);
            break;
        default:
            r = -EINVAL;
        }
    }

    if (r < 0)
        free(resource->data);

    va_end(ap);
    return r;
}

/**
 * @brief Callback that is used to inform a LWM2M client registration event.
 *
 * @param server The LWM2M server.
 * @param cinfo The client that generated the registration event.
 * @param event The registration event itself.
 * @param data User data.
 * @see #sol_lwm2m_registration_event
 * @see sol_lwm2m_server_add_registration_monitor()
 */
typedef void (*sol_lwm2m_server_regisration_event_cb)(void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *cinfo,
    enum sol_lwm2m_registration_event event);

/**
 * @brief Callback used to inform a observable/read response.
 *
 * @param server The LWM2M server
 * @param client The LWM2M client
 * @param path The client's path
 * @param response_code The reponse code.
 * @param content_type The reponse content type.
 * @param content The reponse content.
 * @param data User data.
 * @see sol_lwm2m_server_add_observer()
 * @see sol_lwm2m_server_management_read()
 * @see sol_lwm2m_parse_tlv()
 */
typedef void (*sol_lwm2m_server_content_cb)
    (void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,
    sol_coap_responsecode_t response_code,
    enum sol_lwm2m_content_type content_type,
    struct sol_str_slice content);

/**
 * @brief Callback used to inform create/write/execute/delete response.
 *
 * @param server The LW2M server
 * @param client The LWM2M client
 * @param path The client's path
 * @param response_code The operation @c response_code
 * @param data User data.
 * @see sol_lwl2m_server_management_write()
 * @see sol_lwl2m_server_management_execute()
 * @see sol_lwl2m_server_management_create()
 * @see sol_lwl2m_server_management_delete()
 */
typedef void (*sol_lwm2m_server_management_status_response_cb)(
    void *data,
    struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    sol_coap_responsecode_t response_code);

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
void sol_lwm2m_tlv_array_clear(struct sol_vector *tlvs);

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
int sol_lwm2m_tlv_to_float(struct sol_lwm2m_tlv *tlv, double *value);

/**
 * @brief Converts an TLV value to boolean value.
 *
 * @param tlv The tlv data.
 * @param value The converted value.
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_tlv_to_bool(struct sol_lwm2m_tlv *tlv, bool *value);

/**
 * @brief Converts an TLV value to int value.
 *
 * @param tlv The tlv data.
 * @param value The converted value.
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_tlv_to_int(struct sol_lwm2m_tlv *tlv, int64_t *value);

/**
 *
 * @brief Get TLV content is plain bytes.
 * @param tlv The tlv data.
 * @param bytes The content.
 * @param len The length of @c bytes
 * @return 0 on succes, -errno on error.
 */
int sol_lwm2m_tlv_get_bytes(struct sol_lwm2m_tlv *tlv, uint8_t **bytes, uint16_t *len);

/**
 * @brief Converts an TLV value to object link.
 *
 * @param tlv The tlv data.
 * @param object_id The object id.
 * @param instance_id the instance id.
 * @return 0 on success, -errno on error.
 */
int sol_lwm2m_tlv_to_obj_link(struct sol_lwm2m_tlv *tlv, uint16_t *object_id, uint16_t *instance_id);

/**
 * @brief Adds a registration monitor.
 *
 * This function register a monitor, making it easir to observe a LWM2M client's life cycle.
 * This means that everytime a LWM2M client is registered, updated, deleted or timedout,
 * @c cb will be called.
 *
 * @param server The LWM2M server.
 * @param cb The previous registered callback.
 * @param data The user data to @c cb.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_server_add_registration_monitor()
 * @see #sol_lwm2m_server_regisration_event_cb
 */
int sol_lwm2m_server_add_registration_monitor(struct sol_lwm2m_server *server,
    sol_lwm2m_server_regisration_event_cb cb, const void *data);

/**
 * @brief Removes a registration monitor.
 *
 * @param server The LWM2M server.
 * @param cb The previous registered callback.
 * @param data The user data to @c cb.
 * @return 0 on success, -errno on error.
 * @see sol_lwm2m_server_add_registration_monitor()
 */
int sol_lwm2m_server_del_registration_monitor(struct sol_lwm2m_server *server,
    sol_lwm2m_server_regisration_event_cb cb, const void *data);

/**
 * @brief Gets all registerd clients.
 *
 * @param server The LWM2M Server.
 * @return An vector of #sol_lwm2m_client_info or @c NULL on error.
 * @note One must not add or remove elements from the returned vector.
 * @see sol_lwm2m_server_add_registration_monitor()
 */
const struct sol_ptr_vector *sol_lwm2m_server_get_clients(const struct sol_lwm2m_server *server);

/**
 * @brief Observers an client object, instance or resource.
 *
 * Every time the observed path changes, the client will notify the LWM2M server.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client to be observed.
 * @param path The path to be observed (Example: /3/0/0).
 * @param cb A callback to eb called when the observed path changes.
 * @param data User data to @c cb
 * @return 0 on success, -errno on error.
 * @see #sol_lwm2m_server_content_cb
 * @see sol_lwm2m_server_del_observer()
 */
int sol_lwm2m_server_add_observer(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path, sol_lwm2m_server_content_cb cb, const void *data);

/**
 * @brief Unobserve a client object, instance or resource.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client to be unobserved.
 * @param path The path to be unobserved (Example: /3/0/0).
 * @param cb The previous registered callback.
 * @param data User data to @c cb
 * @return 0 on success, -errno on error.
 *
 * @note In order do completly unobserve a path, all observers must be deleted.
 * @see #sol_lwm2m_server_content_cb
 * @see sol_lwm2m_server_add_observer()
 */
int sol_lwm2m_server_del_observer(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client,
    const char *path,  sol_lwm2m_server_content_cb cb, const void *data);

/**
 * @brief Writes an object instance or resource.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to write
 * @param path The object path to be written (Example /1/1).
 * @param resources An array of #sol_lwm2m_resource
 * @param len The length of @c resources
 * @param cb A callback to be called when the write operation is completed.
 * @param data User data to @c cb
 * @return 0 on success, -errno on error.
 *
 * @note All data is sent using TLV.
 *
 * @see #sol_lwm2m_server_management_status_response_cb
 */
int sol_lwm2m_server_management_write(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    struct sol_lwm2m_resource *resources, size_t len,
    sol_lwm2m_server_management_status_response_cb cb, const void *data);

/**
 * @brief Deletes an object instance on a client.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to delete an object
 * @param path The object path to be deleted (Example /1/1).
 * @param cb A callback to be called when the delete operation is completed.
 * @param data User data to @c cb
 * @return 0 on success, -errno on error.
 *
 * @see #sol_lwm2m_server_management_status_response_cb
 */
int sol_lwm2m_server_management_delete(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    sol_lwm2m_server_management_status_response_cb cb, const void *data);

/**
 * @brief Executes an resource on a client.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to execute the resource.
 * @param path The object path to be executed (Example /1/1/8).
 * @param args Arguments to the execute command.
 * @param cb A callback to be called when the execute operation is completed.
 * @param data User data to @c cb
 * @return 0 on success, -errno on error.
 *
 * @see #sol_lwm2m_server_management_status_response_cb
 */
int sol_lwm2m_server_management_execute(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path, const char *args,
    sol_lwm2m_server_management_status_response_cb cb, const void *data);

/**
 * @brief Creates an object instance on a client.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to create an object instance.
 * @param path The object path to create be created (Example /1).
 * @param resources An array of #sol_lwm2m_resource which contains the required resources to create an object.
 * @param len The length of @c resources.
 * @param cb A callback to be called when the create operation is completed.
 * @param data User data to @c cb
 * @return 0 on success, -errno on error.
 *
 * @note All data is sent using TLV.
 *
 * @see #sol_lwm2m_server_management_status_response_cb
 */
int sol_lwm2m_server_management_create(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    struct sol_lwm2m_resource *resources, size_t len,
    sol_lwm2m_server_management_status_response_cb cb, const void *data);

/**
 * @brief Reads an object, instance or object from a client.
 *
 * @param server The LWM2M server.
 * @param client The LWM2M client info to be read.
 * @param path The path to be read (Example /3/0/0).
 * @param cb A callback to be called when the read operation is completed.
 * @param data User data to @c cb
 * @return 0 on success, -errno on error.
 *
 * @see #sol_lwm2m_server_content_cb
 */
int sol_lwm2m_server_management_read(struct sol_lwm2m_server *server,
    struct sol_lwm2m_client_info *client, const char *path,
    sol_lwm2m_server_content_cb cb, const void *data);

/**
 * @brief Deletes a server instance.
 *
 * Use this function to stop the LWM2M server and release its resources.
 *
 * @param server The LWM2M server to be deleted.
 * @see sol_lwm2m_server_del()
 */
void sol_lwm2m_server_del(struct sol_lwm2m_server *server);

/**
 * @brief Gets the name of client.
 *
 * @param client The LWM2M cliento info.
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
const char *sol_lwm2m_client_info_get_sms(const struct sol_lwm2m_client_info *client);

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
 * @brief Gets the client address.
 *
 * @param client The LWM2M client info.
 * @return The @c client address or @c NULL on error.
 */
const struct sol_network_link_addr *sol_lwm2m_client_info_get_address(const struct sol_lwm2m_client_info *client);

/**
 * @brief Get client objects.
 *
 * @param client The LWM2M client info.
 * @return A array of #sol_lwm2m_client_object or @c NULL on error.
 * @note One must not add or remove elements from the returned vector.
 * @see #sol_lwm2m_client_object
 */
const struct sol_vector *sol_lwm2m_client_info_get_objects(const struct sol_lwm2m_client_info *client);

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
 * @param object The LWM2M object object to get the instances.
 * @return An array of uint16_t or @c NULL on error.
 * @note One must not add or remove elements from the returned vector.
 */
const struct sol_vector *sol_lwm2m_client_object_get_instances(const struct sol_lwm2m_client_object *object);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif
