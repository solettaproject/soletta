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
        sol_coap_responsecode_t (*handle)(const struct sol_network_link_addr *cliaddr,
            const void *data, const struct sol_oic_map_reader *input, struct sol_oic_map_writer *output);
    }
    /**
     * @brief Callback handle to GET requests
     *
     * @param cliaddr The client address.
     * @param data The user's data pointer.
     * @param input Always NULL because GET requests shouldn't cointain payload
     *        data. Parameter kept for compatibility reasons.
     * @param output Handler to write data to response packet using, for
     *        example, @ref sol_oic_map_append().
     *
     * @return The coap response code of this request.
     */
    get,
    /**
     * @brief Callback handle to PUT requests
     *
     * @param cliaddr The client address.
     * @param data The user's data pointer.
     * @param input Handler to read request packet data using, for example,
     *        @ref SOL_OIC_MAP_LOOP() macro.
     * @param output Handler to write data to response packet using, for
     *        example, @ref sol_oic_map_append().
     *
     * @return The coap response code of this request.
     */
        put,
    /**
     * @brief Callback handle to POST requests
     *
     * @param cliaddr The client address.
     * @param data The user's data pointer.
     * @param input Handler to read request packet data using, for example,
     *        @ref SOL_OIC_MAP_LOOP() macro.
     * @param output Handler to write data to response packet using, for
     *        example, @ref sol_oic_map_append().
     *
     * @return The coap response code of this request.
     */
        post,
    /**
     * @brief Callback handle to DELETE requests
     *
     * @param cliaddr The client address.
     * @param data The user's data pointer.
     * @param input Always NULL because DELETE requests shouldn't cointain
     *        payload data. Parameter kept for compatibility reasons.
     * @param output Handler to write data to response packet using, for
     *        example, @ref sol_oic_map_append().
     *
     * @return The coap response code of this request.
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
 * @}
 */

#ifdef __cplusplus
}
#endif
