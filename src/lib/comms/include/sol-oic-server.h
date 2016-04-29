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

#include "sol-oic.h"

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
 * @see sol_oic_platform_info
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
 * @see sol_oic_platform_info
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
 * @see sol_oic_platform_info
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
 * @see sol_oic_platform_info
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
 * @see sol_oic_platform_info
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
 * @see sol_oic_platform_info
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
 * @see sol_oic_platform_info
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
 * @see sol_oic_platform_info
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
 * @see sol_oic_platform_info
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
 * @see sol_oic_device_info
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
 * @see sol_oic_device_info
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
 * @see sol_oic_device_info
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
 * @struct sol_oic_resource_type
 *
 * @brief structure defining the type of a resource.
 *
 * @see sol_oic_server_register_resource
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
        int (*handle)(void *data, struct sol_oic_request *request);
    }
    /**
     * @brief Callback handle to GET requests
     *
     * @param data The user's data pointer.
     * @param request Information about this request. Use
     *        sol_oic_server_request_get_reader() to get the request data
     *        reader, sol_oic_server_response_new() to create a response and
     *        sol_oic_server_send_response() to reply this request with the
     *        response created.
     *
     * @return @c 0 on success or a negative number on errors.
     *
     * @see sol_oic_server_request_get_reader
     * @see sol_oic_server_response_new
     * @see sol_oic_server_send_response
     */
    get,
    /**
     * @brief Callback handle to PUT requests
     *
     * @param data The user's data pointer.
     * @param request Information about this request. Use
     *        sol_oic_server_request_get_reader() to get the request data
     *        reader, sol_oic_server_response_new() to create a response and
     *        sol_oic_server_send_response() to reply this request with the
     *        response created.
     *
     * @return @c 0 on success or a negative number on errors.
     *
     * @see sol_oic_server_request_get_reader
     * @see sol_oic_server_response_new
     * @see sol_oic_server_send_response
     */
        put,
    /**
     * @brief Callback handle to POST requests
     *
     * @param data The user's data pointer.
     * @param request Information about this request. Use
     *        sol_oic_server_request_get_reader() to get the request data
     *        reader, sol_oic_server_response_new() to create a response and
     *        sol_oic_server_send_response() to reply this request with the
     *        response created.
     *
     * @return @c 0 on success or a negative number on errors.
     *
     * @see sol_oic_server_request_get_reader
     * @see sol_oic_server_response_new
     * @see sol_oic_server_send_response
     */
        post,
    /**
     * @brief Callback handle to DELETE requests
     *
     * @param data The user's data pointer.
     * @param request Information about this request. Use
     *        sol_oic_server_request_get_reader() to get the request data
     *        reader, sol_oic_server_response_new() to create a response and
     *        sol_oic_server_send_response() to reply this request with the
     *        response created.
     *
     * @return @c 0 on success or a negative number on errors.
     *
     * @see sol_oic_server_request_get_reader
     * @see sol_oic_server_response_new
     * @see sol_oic_server_send_response
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
 * @see sol_oic_server_unregister_resource
 */
struct sol_oic_server_resource *sol_oic_server_register_resource(
    const struct sol_oic_resource_type *rt, const void *handler_data,
    enum sol_oic_resource_flag flags);

/**
 * @brief Delete a resource from the oic-server.
 *
 * Remove a resource created using @ref sol_oic_server_register_resource from the
 * oic-server
 *
 * @param resource The resource to be removed from server.
 *
 * @see sol_oic_server_register_resource
 */
void sol_oic_server_unregister_resource(struct sol_oic_server_resource *resource);

/**
 * @brief Send notification to all observing clients.
 *
 * Send a notification packet with data filled in @a notification to all
 * observing clients of the resouce used to create @a notification.
 * This function always clear and invalidate the @a notification memory.
 *
 * @param notification The notification response created using
 * sol_oic_server_notification_new() function.
 *
 * @return @c 0 on success or a negative number on errors.
 */
int sol_oic_server_send_notification_to_observers(struct sol_oic_response *notification);

/**
 * @brief Create a notification response to send to observing clients of
 * @a resource.
 *
 * @param resource The resource that will be used to create this notification.
 *
 * @return A notification response on success or NULL on errors.
 *
 * @see sol_oic_server_send_notification_to_observers
 */
struct sol_oic_response *sol_oic_server_notification_new(struct sol_oic_server_resource *resource);

/**
 * @brief Send a reponse as a reply to a request.
 *
 * After sending the response, response and request elements memory are released
 * even on errors.
 *
 * @param request The request that created this response.
 * @param response The response to be sent.
 * @param code The CoAP code to be used in response packet.
 *
 * @return @c 0 on success or a negative number on errors.
 */
int sol_oic_server_send_response(struct sol_oic_request *request, struct sol_oic_response *response, sol_coap_responsecode_t code);

/**
 * @brief Create a new response to send a reply to @a request.
 *
 * @param request The request to be used to reply with this response.
 *
 * @return A new response on success or NULL on errors.
 */
struct sol_oic_response *sol_oic_server_response_new(struct sol_oic_request *request);

/**
 * @brief Free the response and all memory hold by it.
 *
 * @param response The response to be freed.
 */
void sol_oic_server_response_free(struct sol_oic_response *response);

/**
 * @brief Get the packet writer from a response.
 *
 * @param response The response to retrieve the writer.
 *
 * @return The packet writer from this response.
 */
struct sol_oic_map_writer *sol_oic_server_response_get_writer(struct sol_oic_response *response);

/**
 * @brief Get the packet reader from a request.
 *
 * @param request The request to retrieve the reader.
 *
 * @return The packet reader from this request or @c NULL if the informed
 *         request is not a server request.
 */
struct sol_oic_map_reader *sol_oic_server_request_get_reader(struct sol_oic_request *request);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
