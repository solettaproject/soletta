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

#include <stdint.h>

#include "sol-update.h"

#include "sol-macros.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Defines API that Soletta update modules need to implement.
 */

/**
 * @defgroup UpdateModules Update modules
 * @ingroup Update
 *
 * @{
 */

/**
 * @brief Structure containing function that need to be implemented by Soletta
 * update modules.
 */
typedef struct sol_update {
#ifndef SOL_NO_API_VERSION
#define SOL_UPDATE_API_VERSION (1)
    uint16_t api_version; /**< @brief API version */
#endif

    /**
     * @brief Checks for updates.
     *
     * @see sol_update_check
     */
    struct sol_update_handle * (*check)(
        void (*cb)(void *data, int status, const struct sol_update_info *response),
        const void *data);

    /**
     * @brief Fetch updates.
     *
     * @see sol_update_fetch
     */
    struct sol_update_handle * (*fetch)(
        void (*cb)(void *data, int status),
        const void *data, bool resume);

    /**
     * @brief Cancel ongoing update tasks.
     *
     * @see sol_update_cancel
     */
    bool (*cancel)(struct sol_update_handle *handle);

    /**
     * @brief Get update task progress
     *
     * @see sol_update_get_progress
     */
    int (*get_progress)(struct sol_update_handle *handle);

    /**
     * @brief Install updates.
     *
     * @see sol_update_install
     */
    struct sol_update_handle * (*install)(void (*cb)(void *data, int status),
        const void *data);

    /**
     * @brief Function called when module is loaded.
     *
     * An opportunity to do setup tasks, like checking if an update completed
     * successfully.
     *
     * Must return 0 on success, and a negative number on failure.
     */
    int (*init)(void);

    /**
     * @brief Function called when module is unloaded.
     *
     * Cleanup tasks can be performed when this function is called.
     */
    void (*shutdown)(void);
} sol_update;

/**
 * @def SOL_UPDATE_DECLARE(_NAME, decl ...)
 * @brief Helper macro to declare a Soletta update module, so it can
 * be found correctly
 */
#ifdef SOL_UPDATE_MODULE_EXTERNAL
#define SOL_UPDATE_DECLARE(_NAME, decl ...) \
    SOL_API const struct sol_update *SOL_UPDATE = \
        &((const struct sol_update) { \
            SOL_SET_API_VERSION(.api_version = SOL_UPDATE_API_VERSION, ) \
            decl \
        })
#else
#define SOL_UPDATE_DECLARE(_NAME, decl ...) \
    SOL_API const struct sol_update SOL_UPDATE_ ## _NAME = { \
        SOL_SET_API_VERSION(.api_version = SOL_UPDATE_API_VERSION, ) \
        decl \
    }
#endif

/**
 * @}
 */
#ifdef __cplusplus
}
#endif
