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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Structure and macros for making structures reentrancy-proof
 */

/**
 * @defgroup Reentrant Reentrant
 *
 * @{
 */

/**
 * @struct sol_reentrant
 *
 * @brief Structure containing the flags for safely freeing a larger structure
 *
 * This structure is meant to be used as the base of larger structures which
 * are affected by calls to external callbacks that in turn end up calling
 * library APIs. The possibility of a double free is particularly likely in
 * such cases.
 */
struct sol_reentrant {
    /**
     * @brief Structure is in use
     */
     bool in_use;
     /**
      * @brief Structure is stale and should be freed as soon as possible
      */
      bool is_stale;
};

/**
 * @brief Perform a function call to an external callback
 *
 * @param reentrant The reentrant affected by the function call
 *
 * @param call_statement The function call statement
 *
 * Wraps a call to a function by first recording whether the reentrant
 * structure was in use and, after the function call, restoring the state of
 * the reentrant structure from the recorded value.
 */
#define SOL_REENTRANT_CALL(reentrant, call_statement) \
    do { \
        struct sol_reentrant *local = ((struct sol_reentrant *)(reentrant)); \
        bool was_in_use = local->in_use; \
        local->in_use = true; \
        call_statement; \
        local->in_use = was_in_use; \
    } while(0)

/**
 * @brief Conditionally free a reentrant structure
 *
 * @param reentrant The reentrant to free
 *
 * @param free_function The free function to call
 *
 * Calls the function specified by free_function if the reentrant is not marked
 * as being in use. Otherwise, it marks the reentrant as stale but does not
 * free it.
 */
#define SOL_REENTRANT_FREE(reentrant, free_function) \
    do { \
        struct sol_reentrant *local = ((struct sol_reentrant *)(reentrant)); \
        local->is_stale = true; \
        if (!(local->in_use)) { \
            free_function((reentrant)); \
        } \
    } while(0)

/**
 * @brief Determine whether a reentrant structure is stale
 *
 * @param reentrant
 *
 * Evaluates to true if the reentrant is stale, and false otherwise
 */
#define SOL_REENTRANT_IS_STALE(reentrant) \
    (((struct sol_reentrant *)(reentrant))->is_stale)

#ifdef __cplusplus
}
#endif
