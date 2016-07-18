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

#include "sol-common-buildopts.h"
#include "sol-platform.h"
#include "sol-platform-linux.h"
#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Soletta platform Linux micro interaction.
 * @ingroup Platform
 */

/**
 * @brief struct that describes the Linux micro module
 * @see SOL_PLATFORM_LINUX_MICRO_MODULE()
 */
typedef struct sol_platform_linux_micro_module {
#ifndef SOL_NO_API_VERSION
#define SOL_PLATFORM_LINUX_MICRO_MODULE_API_VERSION (1) /**< Compile time API version to be checked during runtime */
    uint16_t api_version; /**< The API version */
#endif
    const char *name; /**< The module name */
    /**
     * @brief Callback used to initialize the module.
     *
     * @param module The module handle
     * @param service The service name
     * @return @c 0 on success @c -errno on error.
     * @note It is called once.
     */
    int (*init)(const struct sol_platform_linux_micro_module *module, const char *service);

    /**
     * @brief Callback used to shutdown the module.
     *
     * @param module The module handle
     * @param service The service name
     * @note It is called once.
     */
    void (*shutdown)(const struct sol_platform_linux_micro_module *module, const char *service);

    /**
     * @brief Callback used to start the service
     * @param module The module handle
     * @param service The service name
     * @return @c 0 on success @c -errno on error.
     */
    int (*start)(const struct sol_platform_linux_micro_module *module, const char *service);

    /**
     * @brief Callback used to stop the service
     * @param module The module handle
     * @param service The service name
     * @return @c 0 on success @c -errno on error.
     */
    int (*stop)(const struct sol_platform_linux_micro_module *module, const char *service, bool force_immediate);

    /**
     * @brief Callback used to restart the service
     * @param module The module handle
     * @param service The service name
     * @return @c 0 on success @c -errno on error.
     */
    int (*restart)(const struct sol_platform_linux_micro_module *module, const char *service);

    /**
     * @brief Callback used to start a service monitor
     * @param module The module handle
     * @param service The service name
     * @return @c 0 on success @c -errno on error.
     */
    int (*start_monitor)(const struct sol_platform_linux_micro_module *module, const char *service);

    /**
     * @brief Callback used to stop a service monitor
     * @param module The module handle
     * @param service The service name
     * @return @c 0 on success @c -errno on error.
     */
    int (*stop_monitor)(const struct sol_platform_linux_micro_module *module, const char *service);
} sol_platform_linux_micro_module;

/**
 * @brief Inform the service observers the current state of the @c service
 *
 * @param service The service name
 * @param state The current service state
 * @see sol_platform_add_service_monitor()
 */
void sol_platform_linux_micro_inform_service_state(const char *service, enum sol_platform_service_state state);

#ifdef SOL_PLATFORM_LINUX_MICRO_MODULE_EXTERNAL
/**
 * @brief Exports the Linux micro module.
 *
 * This macro should be used to declare a Linux micro module, making it visible to Soletta.
 * @param _NAME The meta type name.
 * @param decl The meta declarations.
 *
 * Example:
 * @code
 * SOL_PLATFORM_LINUX_MICRO_MODULE(MY_MODULE,
 *   .name = "My module"
 *   .init = init_module,
 *   .shutdown = shutdown_module,
 * @endcode
 */
#define SOL_PLATFORM_LINUX_MICRO_MODULE(_NAME, decl ...) \
    SOL_API const struct sol_platform_linux_micro_module *SOL_PLATFORM_LINUX_MICRO_MODULE = &((const struct sol_platform_linux_micro_module) { \
            SOL_SET_API_VERSION(.api_version = SOL_PLATFORM_LINUX_MICRO_MODULE_API_VERSION, ) \
            decl \
        })
#else
/**
 * @brief Exports the Linux micro module.
 *
 * This macro should be used to declare a Linux micro module, making it visible to Soletta.
 * @param _NAME The meta type name.
 * @param decl The meta declarations.
 *
 * Example:
 * @code
 * SOL_PLATFORM_LINUX_MICRO_MODULE(MY_MODULE,
 *   .name = "My module"
 *   .init = init_module,
 *   .shutdown = shutdown_module,
 * @endcode
 */
#define SOL_PLATFORM_LINUX_MICRO_MODULE(_NAME, decl ...) \
    const struct sol_platform_linux_micro_module SOL_PLATFORM_LINUX_MICRO_MODULE_ ## _NAME = { \
        SOL_SET_API_VERSION(.api_version = SOL_PLATFORM_LINUX_MICRO_MODULE_API_VERSION, ) \
        decl \
    }
#endif

#ifdef __cplusplus
}
#endif
