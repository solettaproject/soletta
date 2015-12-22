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

#ifndef OIC_MANUFACTURER_NAME
/**
 * @brief Default manufacturer name
 *
 * To override this just define OIC_MANUFACTURER_NAME before including this
 * header.
 *
 * @see sol_oic_server_information
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
 * @see sol_oic_server_information
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
 * @see sol_oic_server_information
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
 * @see sol_oic_server_information
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
 * @see sol_oic_server_information
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
 * @see sol_oic_server_information
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
 * @see sol_oic_server_information
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
 * @see sol_oic_server_information
 */
#define OIC_SUPPORT_URL "Unknown"
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
    int : 0; /**< @brief Unused. Save possible hole for a future field */
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

    struct {
        sol_coap_responsecode_t (*handle)(const struct sol_network_link_addr *cliaddr,
            const void *data, const struct sol_oic_map_reader *input, struct sol_oic_map_writer *output);
    }
    /**
     * @brief Callback handle to GET requests
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
     * @param input Handler to read request packet data using, for example,
     *        @ref SOL_OIC_MAP_LOOP() macro.
     * @param output Handler to write data to response packet using, for
     *        example, @ref sol_oic_map_append().
     *
     * @return The coap response code of this request.
     */
    delete;
};

/**
 * @brief Initialize the oic server.
 *
 * The first call of sol_oic_server_init will initialize the oic-server and any
 * other following calls will increment the oic-server reference counter.
 * After using the oic-server, @ref sol_oic_server_shutdown() must be called to
 * decrement the reference counter and to release the resource when possible.
 *
 * This function must be called before calling any other sol-oic-server
 * function.
 *
 * @return 0 on success. A negative integer on errors.
 */
int sol_oic_server_init(void);

/**
 * @brief Release the oic server.
 *
 * Decrement the reference counter of the oic-server. When last reference is
 * released all resources associated with the oic-server are freed.
 *
 * @see sol_oic_server_init()
 */
void sol_oic_server_shutdown(void);

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
    void *data);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
