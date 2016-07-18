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
 * @brief Structure containing the flags for safely freeing a larger structure
 *
 * This structure is meant to be used inside larger structures which are
 * affected by calls to external callbacks that in turn end up calling library
 * APIs. The possibility of a double free is particularly likely in such cases.
 */
typedef struct sol_reentrant {
    /**
     * @brief Structure is in use
     */
    bool in_use;
    /**
     * @brief Structure is stale and should be freed as soon as possible
     */
    bool delete_me;
} sol_reentrant;

/**
 * @brief Wraps a function call to an external callback
 *
 * @param handle The reentrant tracking the structure affected by the
 * function call
 *
 * Wraps the statement or statement block following it by marking the handle
 * passed in as the parameter as being in use. Thus, if you call an external
 * callback from within the block which attempts to free the structure, the
 * structure will be protected from deletion by the flags set in the preamble.
 * For example:
 *
 * @code
 * SO_REENTRANT_CALL(context->reentrant) {
 *     context->cb(context->data);
 *     context->cb_was_called = true;
 * }
 * @endcode
 *
 * will ensure that the context will not be freed as part of the call to
 * <code>context->cb()</code> and will be available after
 * <code>context->cb()</code> returns as long as all calls to free the context
 * are made via SOL_REENTRANT_FREE().
 */
#define SOL_REENTRANT_CALL(handle) \
    for (bool reentrant_run = true; reentrant_run; reentrant_run = false) \
        for (bool reentrant_was_used = (handle).in_use; reentrant_run; \
            (handle).in_use = reentrant_was_used, reentrant_run = false) \
            for ((handle).in_use = true; reentrant_run; reentrant_run = false)

/**
 * @brief Conditionally free a reentrant structure
 *
 * @param reentrant The reentrant to free
 *
 * Provides a condition statement for calling the function that frees the
 * structure associated with the reentrant passed in as the parameter. As a
 * side effect it also marks the structure as needing deletion.
 */
#define SOL_REENTRANT_FREE(reentrant) \
    if (({ (reentrant).delete_me = true; !(reentrant).in_use; }))

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
