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
#include <sol-coap.h>
#include <sol-network.h>
#include <sol-str-slice.h>
#include <sol-vector.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "sol-oic.h"

/**
 * @file
 * @brief Routines to create clients talking the OIC protocol.
 */

/**
 * @defgroup oic_client OIC Client
 * @ingroup OIC
 *
 * @brief Routines to create clients talking the OIC protocol.
 *
 * @{
 */

/**
 * @typedef sol_oic_client
 * @brief Opaque handler for an OIC client instance.
 *
 * It's created with sol_oic_client_new() and should be later
 * deleted with sol_oic_client_del()
 */
struct sol_oic_client;
typedef struct sol_oic_client sol_oic_client;

/**
 * @typedef sol_oic_pending
 * @brief Represents a pending OIC client call
 *
 * This can be used to cancel the pending call. Note that the context
 * might die by other means, e.g. the user returning false on a
 * request's response callback. The user is responsible for discarding
 * invalid request contexts.
 *
 */
struct sol_oic_pending;
typedef struct sol_oic_pending sol_oic_pending;

/**
 * @brief Structure defining an OIC resource. It's open to the API
 * user to bypass the need for getters for everything, but all fields
 * are marked as const to emphasize that the user must not alter any
 * of them.
 */
typedef struct sol_oic_resource {
#ifndef SOL_NO_API_VERSION
#define SOL_OIC_RESOURCE_API_VERSION (1)
    uint16_t api_version; /**< @brief API version */
#endif
    /**
     * @brief The resource address.
     */
    const struct sol_network_link_addr addr;
    /**
     * @brief The path pointing at this resource.
     */
    const struct sol_str_slice path;
    /**
     * @brief The Device ID as a UUID 16-byte array.
     */
    const struct sol_str_slice device_id;
    /**
     * @brief List of resource types (#sol_str_slice entries)
     * from this resource.
     */
    const struct sol_vector types;
    /**
     * @brief List of interfaces implemented by this resource.
     */
    const struct sol_vector interfaces;
    /**
     * @brief True if server supports observe mode for this resource
     */
    const bool observable;
    /**
     * @brief True if the connection established with this resource's
     * server is secure.
     */
    const bool secure;
} sol_oic_resource;

/**
 * @brief Creates a new OIC client intance.
 *
 * @return A new OIC client instance, or NULL in case of failure.
 *
 * @see sol_oic_client_del()
 */
struct sol_oic_client *sol_oic_client_new(void);

/**
 * @brief Delete @a client
 *
 * All memory used by @a client and all resources associated with it will be
 * freed.
 *
 * @param client The client to delete.
 *
 * @see sol_oic_client_new()
 */
void sol_oic_client_del(struct sol_oic_client *client);

/**
 * @brief Cancel a pending OIC call
 *
 * @param pending the pending OIC call handle
 */
void sol_oic_pending_cancel(struct sol_oic_pending *pending);

/**
 * @brief Send a discovevery packet to find resources.
 *
 * Sends a discovery packet to the destination address especified by
 * @a addr, which may be a multicast address for discovery purposes.
 *
 * When a response is received, the function @a resource_found_cb will
 * be called. Note that multiple responses can be received for this
 * request. As long as this function returns @c true, @a client will
 * continue waiting for more responses. When the function returns @c
 * false, the internal response handler will be freed and any new
 * replies that may arrive for this request will be ignored. After an
 * internal timeout is reached and the request is still alive, @a
 * resource_found_cb will be called with a @c NULL @a oic_res. For
 * this timeout invocation of @a resource_found_cb, if
 * resource_found_cb returns @c true, @a client will continue waiting
 * for responses until next a new timeout window closes, otherwise @a
 * client will terminate response waiting.
 *
 * @param client An OIC client instance.
 * @param addr The address of the server that contains the desired resource.
 *        May be a multicast address if it is desired to look for resources in
 *        multiple servers.
 * @param resource_type A string representation of the type of the desired
 *        resource. If empty or @c NULL, resources from all types will be
 *        discovered.
 * @param resource_interface A string representation of the interface of the desired
 *        resource. If empty or @c NULL, resources with all interfaces will be
 *        discovered.
 * @param resource_found_cb Callback to be called when a resource is
 *        found or when an internal timeout is reached. Parameter @a
 *        cli is the #sol_oic_client used to perform the request, @a
 *        res is the resource that was discovered and data is a
 *        pointer to the user's data parameter.
 * @param data A pointer to user's data.
 *
 * @return @c A pending call handle on success, @c NULL otherwise
 *         (when @c errno will be set to one of @c EINVAL, @c ENOMEM, @c
 *         EIO, @c EOVERFLOW, @c ERANGE). It will be valid until @a
 *         resource_found_cb returns @c false.
 */
struct sol_oic_pending *sol_oic_client_find_resources(struct sol_oic_client *client,
    struct sol_network_link_addr *addr, const char *resource_type,
    const char *resource_interface,
    bool (*resource_found_cb)(void *data, struct sol_oic_client *cli,
    struct sol_oic_resource *res),
    const void *data);

/**
 * @brief Retrieve platform information.
 *
 * Sends a packet to @a resource's server asking for platform information
 * defined at @ref sol_oic_platform_info.
 *
 * When a response is received, the function @a info_received_cb will
 * be called, with @a info parameter filled with the information
 * received, or @c NULL on errors.
 *
 * After an internal timeout is reached, @a info_received_cb will be
 * called with @c NULL @a info.
 *
 * @param client An OIC client instance.
 * @param resource The resource that is going to receive the request.
 * @param info_received_cb Callback to be called when response is received or
 *        when timeout is reached. Parameter cli is the sol_oic_client used to
 *        perform the request, info is the @ref sol_oic_platform_info
 *        structure with server info data, data is a pointer to user's data
 *        parameter.
 * @param data A pointer to user's data.
 *
 * @note As @a info_received_cb is always called, it can be used to
 * perform clean up operations.
 *
 * @return @c A pending call handle on success, @c NULL otherwise
 *         (when @c errno will be set to one of @c EINVAL, @c ENOMEM, @c
 *         EIO, @c EOVERFLOW, @c ERANGE). It will be valid until @a
 *         info_received_cb is called.
 */
struct sol_oic_pending *sol_oic_client_get_platform_info(struct sol_oic_client *client,
    struct sol_oic_resource *resource,
    void (*info_received_cb)(void *data, struct sol_oic_client *cli,
    const struct sol_oic_platform_info *info),
    const void *data);

/**
 * @brief Retrieve platform information from @a addr.
 *
 * Sends a packet to server identified by @a addr asking for platform
 * information defined at @ref sol_oic_platform_info.
 *
 * When a response is received, the function @a info_received_cb will
 * be called, with @a info parameter filled with the information
 * received, or @c NULL on errors.
 *
 * After internal timeout is reached @a info_received_cb will be
 * called with @c NULL @a info.
 *
 * @param client An OIC client instance.
 * @param addr The address of the server that contains the desired
 *        information.
 * @param info_received_cb Callback to be called when response is received or
 *        when timeout is reached. Parameter cli is the sol_oic_client used to
 *        perform the request, info is the @ref sol_oic_platform_info
 *        structure with server info data, data is a pointer to user's data
 *        parameter.
 * @param data A pointer to user's data.
 *
 * @note As @a info_received_cb is always called, it can be used to
 * perform clean up operations.
 *
 * @return @c A pending call handle on success, @c NULL otherwise.
 *         (when @c errno will be set to one of @c EINVAL, @c ENOMEM, @c
 *         EIO, @c EOVERFLOW, @c ERANGE). It will be valid until @a
 *         info_received_cb is called.
 */
struct sol_oic_pending *sol_oic_client_get_platform_info_by_addr(struct sol_oic_client *client,
    struct sol_network_link_addr *addr,
    void (*info_received_cb)(void *data, struct sol_oic_client *cli,
    const struct sol_oic_platform_info *info),
    const void *data);

/**
 * @brief Retrieve server information.
 *
 * Sends a packet to @a resource's server asking for server information
 * defined at @ref sol_oic_device_info.
 *
 * When a response is received, the function @a info_received_cb will
 * be called, with @a info parameter filled with the information
 * received, or @c NULL on errors.
 *
 * After internal timeout is reached @a info_received_cb will be
 * called with @c NULL @a info.
 *
 * @param client An OIC client instance.
 * @param resource The resource that is going to receive the request.
 * @param info_received_cb Callback to be called when response is received or
 *        when timeout is reached. Parameter cli is the sol_oic_client used to
 *        perform the request, info is the @ref sol_oic_device_info
 *        structure with server info data, data is a pointer to user's data
 *        parameter.
 * @param data A pointer to user's data.
 *
 * @note As @a info_received_cb is always called, it can be used to
 * perform clean up operations.
 *
 * @return @c A pending call handle on success, @c NULL otherwise.
 *         (when @c errno will be set to one of @c EINVAL, @c ENOMEM, @c
 *         EIO, @c EOVERFLOW, @c ERANGE). It will be valid until @a
 *         info_received_cb is called
 */
struct sol_oic_pending *sol_oic_client_get_server_info(struct sol_oic_client *client,
    struct sol_oic_resource *resource,
    void (*info_received_cb)(void *data, struct sol_oic_client *cli,
    const struct sol_oic_device_info *info),
    const void *data);

/**
 * @brief Retrieve server information from @a addr.
 *
 * Sends a packet to server identified by @a addr asking for server
 * information defined at @ref sol_oic_device_info.
 *
 * When a response is received, the function @a info_received_cb will
 * be called, with @a info parameter filled with the information
 * received, or @c NULL on errors.
 *
 * After internal timeout is reached @a info_received_cb will be
 * called with @c NULL @a info.
 *
 * @param client An OIC client instance.
 * @param addr The address of the server that contains the desired
 *        information.
 * @param info_received_cb Callback to be called when response is received or
 *        when timeout is reached. Parameter cli is the sol_oic_client used to
 *        perform the request, info is the @ref sol_oic_device_info
 *        structure with server info data, data is a pointer to user's data
 *        parameter.
 * @param data A pointer to user's data.
 *
 * @return @c A pending call handle on success, @c NULL otherwise
 *         (when @c errno will be set to one of @c EINVAL, @c ENOMEM, @c
 *         EIO, @c EOVERFLOW, @c ERANGE). It will be valid until @a
 *         info_received_cb is called.
 */
struct sol_oic_pending *sol_oic_client_get_server_info_by_addr(struct sol_oic_client *client,
    struct sol_network_link_addr *addr,
    void (*info_received_cb)(void *data, struct sol_oic_client *cli,
    const struct sol_oic_device_info *info),
    const void *data);

/**
 * @brief Send a @a request packet to server.
 *
 * Send a CoAP @a request packet to server and wait for a response. When the
 * response arrives, @a callback will be called.
 * The @a request memory will be freed by this function on success or failure.
 *
 * @param client An OIC client instance.
 * @param request A request created using @ref sol_oic_client_request_new()
 *        or sol_oic_client_non_confirmable_request_new().
 * @param callback Callback to be called when a response from this request
 *        arrives. Parameter @a response_code is the header response code of
 *        this request, @a cli is the @a client used to perform the
 *        request, @a addr is the address of the server and repr_vec is a
 *        handler to access data from response, using @ref SOL_OIC_MAP_LOOP()
 *        macro. @a data is the user's @a callback_data. When timeout is reached
 *        and no packet has arrived, callback is called with @c NULL @a addr and
 *        @c NULL repr_vec so any clean up can be performed.
 * @param callback_data User's data to be passed to @a callback.
 *
 * @return @c A pending call handle on success, @c NULL otherwise
 *         (when @c errno will be set to one of @c EINVAL, @c ENOMEM, @c
 *         EIO, @c EOVERFLOW, @c ERANGE). It will be valid until @a
 *         callback is called.
 */
struct sol_oic_pending *sol_oic_client_request(struct sol_oic_client *client,
    struct sol_oic_request *request,
    void (*callback)(void *data, enum sol_coap_response_code response_code,
    struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_vec), const void *callback_data);

/**
 * @brief Create an OIC client request for an specific @a resource, using a
 * confirmable CoAP packet.
 *
 * @param method The coap request method as documented in @ref
 *        sol_coap_method.
 * @param res The resource that is going to receive the request.
 *
 * @return A valid client request on success or @c NULL on errors.
 */
struct sol_oic_request *sol_oic_client_request_new(enum sol_coap_method method, struct sol_oic_resource *res);

/**
 * @brief Create an OIC client request for an specific @a resource, using a
 * non-confirmable CoAP packet.
 *
 * @param method The coap request method as documented in @ref
 *        sol_coap_method.
 * @param res The resource that is going to receive the request.
 *
 * @return A valid client request on success or @c NULL on errors.
 */
struct sol_oic_request *sol_oic_client_non_confirmable_request_new(enum sol_coap_method method, struct sol_oic_resource *res);

/**
 * @brief Release memory from a request.
 *
 * @param request A pointer to the request to be released.
 */
void sol_oic_client_request_free(struct sol_oic_request *request);

/**
 * @brief Get the packet writer from a client request.
 *
 * @param request The request to retrieve the writer.
 *
 * @return The packet writer from this request or @c NULL if the informed
 *         request is not a client request.
 */
struct sol_oic_map_writer *sol_oic_client_request_get_writer(struct sol_oic_request *request);

/**
 * @brief Set this resource as observable for this client.
 *
 * If the server providing the @a resource supports observing clients,
 * this sends it a request to add @a client to its observing list.
 * Clients in observation receive notifications when the server status
 * for resources changes. When a notification is received by @a
 * client, @a callback will be called. If the @a resource is not
 * observable, @a client will emulate the observing behavior using a
 * polling strategy, so @a callback will be notified with server
 * changes from time to time.
 *
 * To stop observing the server, call
 * sol_oic_client_resource_set_observable() with @a observe as @c
 * false.
 *
 * @param client An OIC client instance.
 * @param res The resource that is going to be observed
 * @param callback A callback to be called when notification responses arrive.
 *        Parameter @a response_code is the header response code of this
 *        request, @a cli is the @a client used to perform the request, @a addr
 *        is the address of the server, @a repr_map is the data from the
 *        notification and @a data is a pointer to user's data. To extract data
 *        from @a repr_map use @ref SOL_OIC_MAP_LOOP() macro. Callback is called
 *        with @c NULL @a addr and @c NULL @a repr_map when client is
 *        unobserved, so any clean up can be performed.
 * @param data A pointer to user's data.
 * @param observe If server will be obeserved or unobserved.
 *
 * @return @c 0 on success or a negative number on errors.
 */
int sol_oic_client_resource_set_observable(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(void *data, enum sol_coap_response_code response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_map),
    const void *data, bool observe);

/**
 * @brief Set this resource as observable for this client, using non-confirmable
 * packets.
 *
 * If the server providing the @a resource supports observing clients,
 * this sends it a request to add @a client to its observing list.
 * Clients in observation receive notifications when the server status
 * for resources changes. When a notification is received by @a
 * client, @a callback will be called. If the @a resource is not
 * observable, @a client will emulate the observing behavior using a
 * polling strategy, so @a callback will be notified with server
 * changes from time to time.
 *
 * When user wants to stop observing the server, call @ref
 * sol_oic_client_resource_set_observable() with @a observe as @c false.
 *
 * The only difference from @ref sol_oic_client_resource_set_observable() to
 * this function is that it uses CoAP non-confirmable packets to make the
 * request.
 *
 * @param client An OIC client instance.
 * @param res The resource that is going to be observed
 * @param callback A callback to be called when notification responses arrive.
 *        Parameter @a response_code is the header response code of this
 *        request, @a cli is the @a client used to perform the request, @a addr
 *        is the address of the server, @a repr_map is the data from the
 *        notification and @a data is a pointer to user's data. To extract data
 *        from @a repr_map use @ref SOL_OIC_MAP_LOOP() macro. Callback is called
 *        with @c NULL @a addr and @c NULL @a repr_map when client is
 *        unobserved, so any clean up can be performed.
 * @param data A pointer to user's data.
 * @param observe If server will be obeserved or unobserved.
 *
 * @return @c 0 on success or a negative number on errors.
 */
int sol_oic_client_resource_set_observable_non_confirmable(struct sol_oic_client *client, struct sol_oic_resource *res,
    void (*callback)(void *data, enum sol_coap_response_code response_code, struct sol_oic_client *cli, const struct sol_network_link_addr *addr,
    const struct sol_oic_map_reader *repr_map),
    const void *data, bool observe);

/**
 * @brief Take a reference of the given server.
 *
 * Increment the reference count of the resource, if it's valid.
 *
 * @param r The resource to reference.
 *
 * @return The same resource, with refcount increased, or NULL if invalid.
 */
struct sol_oic_resource *sol_oic_resource_ref(struct sol_oic_resource *r);

/**
 * @brief Release a reference from the given resource.
 *
 * When the last reference is released, the resource with it will be freed.
 *
 * @param r The resource to release.
 */
void sol_oic_resource_unref(struct sol_oic_resource *r);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
