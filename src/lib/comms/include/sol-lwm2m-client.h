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

#include "sol-lwm2m.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to create Clients talking the LWM2M protocol.
 *
 */

/**
 * @defgroup lwm2m_client LWM2M Client
 * @ingroup LWM2M
 *
 * @brief Routines to create Clients talking the LWM2M protocol.
 *
 * @{
 */

/**
 * @typedef sol_lwm2m_client
 * @brief A handle to a LWM2M client.
 * @see sol_lwm2m_client_new().
 */
struct sol_lwm2m_client;
typedef struct sol_lwm2m_client sol_lwm2m_client;

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
typedef struct sol_lwm2m_object {
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
     * @param payload The object's initial content.
     * @return 0 on success, -errno on error.
     */
    int (*create)(void *user_data, struct sol_lwm2m_client *client,
        uint16_t instance_id, void **instance_data, struct sol_lwm2m_payload payload);
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
} sol_lwm2m_object;

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
 * @see sol_lwm2m_client_add_object_instance()
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
int sol_lwm2m_client_add_object_instance(struct sol_lwm2m_client *client,
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
 * @}
 */
#ifdef __cplusplus
}
#endif
