/*
 * This file is part of the Soletta Project
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

#include <sol-str-slice.h>
#include <sol-coap.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "sol-oic-common.h"

/**
 * @file
 * @brief Routines to create servers talking OIC protocol.
 */

/**
 * @defgroup oic_server OIC Server
 * @ingroup OIC
 *
 * @brief Routines to create servers talking OIC protocol.
 *
 * @{
 */

#ifndef OIC_PLATFORM_ID
/**
 * @brief Default Platform ID
 *
 * To override this just define OIC_PLATFORM_ID before including this
 * header.
 *
 * @see sol_oic_platform_information
 */
#define OIC_PLATFORM_ID "Unknown"
#endif

#ifndef OIC_MANUFACTURER_NAME
/**
 * @brief Default manufacturer name
 *
 * To override this just define OIC_MANUFACTURER_NAME before including this
 * header.
 *
 * @see sol_oic_platform_information
 */
#define OIC_MANUFACTURER_NAME "Soletta"
#endif
#ifndef OIC_MANUFACTURER_URL
/**
 * @brief Default manufacturer url
 *
 * To override this just define OIC_MANUFACTURER_URL before including this
 * header.
 *
 * @see sol_oic_platform_information
 */
#define OIC_MANUFACTURER_URL "https://soletta-project.org"
#endif
#ifndef OIC_MODEL_NUMBER
/**
 * @brief Default device model number
 *
 * To override this just define OIC_MODEL_NUMBER before including this
 * header.
 *
 * @see sol_oic_platform_information
 */
#define OIC_MODEL_NUMBER "Unknown"
#endif
#ifndef OIC_MANUFACTURE_DATE
/**
 * @brief Default manufacture date.
 *
 * To override this just define OIC_MANUFACTURE_DATE before including this
 * header.
 *
 * @see sol_oic_platform_information
 */
#define OIC_MANUFACTURE_DATE "2015-01-01"
#endif
#ifndef OIC_PLATFORM_VERSION
/**
 * @brief Default platform version.
 *
 * To override this just define OIC_PLATFORM_VERSION before including this
 * header.
 *
 * @see sol_oic_platform_information
 */
#define OIC_PLATFORM_VERSION "Unknown"
#endif
#ifndef OIC_HARDWARE_VERSION
/**
 * @brief Default hardware version.
 *
 * To override this just define OIC_HARDWARE_VERSION before including this
 * header.
 *
 * @see sol_oic_platform_information
 */
#define OIC_HARDWARE_VERSION "Unknown"
#endif
#ifndef OIC_FIRMWARE_VERSION
/**
 * @brief Default firmware version.
 *
 * To override this just define OIC_FIRMWARE_VERSION before including this
 * header.
 *
 * @see sol_oic_platform_information
 */
#define OIC_FIRMWARE_VERSION "Unknown"
#endif
#ifndef OIC_SUPPORT_URL
/**
 * @brief Default URL for manufacturer support.
 *
 * To override this just define OIC_SUPPORT_URL before including this
 * header.
 *
 * @see sol_oic_platform_information
 */
#define OIC_SUPPORT_URL "Unknown"
#endif
#ifndef OIC_DEVICE_NAME
/**
 * @brief Default device name.
 *
 * To override this just define OIC_DEVICE_NAME before including this
 * header.
 *
 * @see sol_oic_server_information
 */
#define OIC_DEVICE_NAME "Unknown"
#endif
#ifndef OIC_SPEC_VERSION
/**
 * @brief Default spec version
 *
 * To override this just define OIC_SPEC_VERSION before including this
 * header.
 *
 * @see sol_oic_server_information
 */
#define OIC_SPEC_VERSION ""
#endif
#ifndef OIC_DATA_MODEL_VERSION
/**
 * @brief Default data model version.
 *
 * To override this just define OIC_DATA_MODEL_VERSION before including this
 * header.
 *
 * @see sol_oic_server_information
 */
#define OIC_DATA_MODEL_VERSION "Unknown"
#endif

/**
 * @struct sol_oic_server_resource
 *
 * @brief Opaque handler for a server resource
 */
struct sol_oic_server_resource;

/**
 * @struct sol_oic_server_request
 *
 * @brief Information about a server request.
 *
 * @see sol_oic_server_create_response
 * @see sol_oic_server_send_response
 * @see sol_oic_server_request_free
 */
struct sol_oic_server_request;

/**
 * @struct sol_oic_server_response
 *
 * @brief Information about a server response.
 *
 * @see sol_oic_server_create_response
 * @see sol_oic_server_send_response
 * @see sol_oic_server_request_free
 */
struct sol_oic_server_response;

/**
 * @struct sol_oic_resource_type
 *
 * @brief structure defining the type of a resource.
 *
 * @see sol_oic_server_add_resource
 */
struct sol_oic_resource_type {
#ifndef SOL_NO_API_VERSION
#define SOL_OIC_RESOURCE_TYPE_API_VERSION (1)
    uint16_t api_version; /**< @brief API version */
#endif

    /**
     * @brief String representation of the resource type.
     *
     * The string may have namespace segments, separated by a dot ('.').
     */
    struct sol_str_slice resource_type;

    /**
     * @brief String representation of the interface implemeneted by this resource.
     */
    struct sol_str_slice interface;

    /**
     * @brief String representation of the path of this resource.
     * If path.data == NULL or path.len == 0, a path will be generated
     * automatically for this resource.
     */
    struct sol_str_slice path;

    struct {
        int (*handle)(struct sol_oic_server_request *request, void *data);
    }
    /**
     * @brief Callback handle to GET requests
     *
     * @param request Information about this request. To be used to create and
     *        send a response. Memory must be freed using
     *        sol_oic_server_request_free if not sending a response.
     * @param data The user's data pointer.
     *
     * @return @c 0 on success or a negative number on errors.
     *
     * @see sol_oic_server_create_response
     * @see sol_oic_server_send_response
     * @see sol_oic_server_request_free
     */
    get,
    /**
     * @brief Callback handle to PUT requests
     *
     * @param request Information about this request. To be used to create and
     *        send a response. Memory must be freed using
     *        sol_oic_server_request_free if not sending a response.
     * @param data The user's data pointer.
     *
     * @return @c 0 on success or a negative number on errors.
     *
     * @see sol_oic_server_create_response
     * @see sol_oic_server_send_response
     * @see sol_oic_server_request_free
     */
        put,
    /**
     * @brief Callback handle to POST requests
     *
     * @param request Information about this request. To be used to create and
     *        send a response. Memory must be freed using
     *        sol_oic_server_request_free if not sending a response.
     * @param data The user's data pointer.
     *
     * @return @c 0 on success or a negative number on errors.
     *
     * @see sol_oic_server_create_response
     * @see sol_oic_server_send_response
     * @see sol_oic_server_request_free
     */
        post,
    /**
     * @brief Callback handle to DELETE requests
     *
     * @param request Information about this request. To be used to create and
     *        send a response. Memory must be freed using
     *        sol_oic_server_request_free if not sending a response.
     * @param data The user's data pointer.
     *
     * @return @c 0 on success or a negative number on errors.
     *
     * @see sol_oic_server_create_response
     * @see sol_oic_server_send_response
     * @see sol_oic_server_request_free
     */
        del;
};

/**
 * @brief Add resource to oic-server.
 *
 * Create a new sol_oic_server_resource and associate it to the oic-server.
 *
 * @param rt The @ref sol_oic_resource_type structure with information about the
 *        resource that is being added.
 * @param handler_data Pointer to user data that will be passed to callbacks
 *        defined in @a rt.
 * @param flags Resourse flags.
 *
 * @return On success, a pointer to the new resource created. On errors, NULL.
 *
 * @see sol_oic_server_del_resource
 */
struct sol_oic_server_resource *sol_oic_server_add_resource(
    const struct sol_oic_resource_type *rt, const void *handler_data,
    enum sol_oic_resource_flag flags);

/**
 * @brief Delete a resource from the oic-server.
 *
 * Remove a resource created using @ref sol_oic_server_add_resource from the
 * oic-server
 *
 * @param resource The resource to be removed from server.
 *
 * @see sol_oic_server_add_resource
 */
void sol_oic_server_del_resource(struct sol_oic_server_resource *resource);

/**
 * @brief Send notification to all clients in observe list.
 *
 * Send a notification packet with data filled by @a fill_repr_map callback
 * to all clients that have registered in the @a resource as an observer.
 *
 * @param resource The target resource
 * @param fill_repr_map Callback to fill notification data. Callback parameters
 *        are @a data, the user's data pointer, @a oic_map_writer, the write
 *        handler to be used with @ref sol_oic_map_append to fill the
 *        notification data
 * @param data Pointer to user's data to be passed to @a fill_repr_map callback
 *
 * @return True on success, false on failure.
 */
bool sol_oic_notify_observers(struct sol_oic_server_resource *resource,
    bool (*fill_repr_map)(void *data, struct sol_oic_map_writer *oic_map_writer),
    const void *data);

/**
 * @brief Release memory from a request.
 *
 * @param request A pointer to the request to be released.
 */
void sol_oic_server_request_free(struct sol_oic_server_request *request);

/**
 * @brief Release memory from a response.
 *
 * @param response A pointer to the response to be released.
 */
void sol_oic_server_response_free(struct sol_oic_server_response *response);

/**
 * @brief Create a response to a oic server request.
 *
 * @param request The request that the created response will be used to reply.
 *
 * @return On success a new reponse element. @c NULL on errors.
 */
struct sol_oic_server_response *sol_oic_server_create_response(struct sol_oic_server_request *request);

/**
 * @brief Send a reponse as a reply to a request.
 *
 * @param request The request that created this response.
 * @param response The response to be sent. If @c NULL, an empty response will
 *        be sent.
 * @param code The CoAP code to be used in response.
 *
 * @return @c 0 on success or a negative number on errors.
 */
int sol_oic_server_send_response(struct sol_oic_server_request *request, struct sol_oic_server_response *response, sol_coap_responsecode_t code);

/**
 * @brief Get the packet data from a response.
 *
 * @param response The response to retrieve the data.
 *
 * @return The packet data from this response.
 */
struct sol_oic_map_writer *sol_oic_server_response_get_data(struct sol_oic_server_response *response);

/**
 * @brief Get the packet data from a request.
 *
 * @param request The request to retrieve the data.
 *
 * @return The packet data from this request.
 */
struct sol_oic_map_reader *sol_oic_server_request_get_data(struct sol_oic_server_request *request);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
