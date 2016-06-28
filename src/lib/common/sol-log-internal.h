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

#include "sol-log.h"

#ifdef SOL_LOG_ENABLED
#define SOL_LOG_INTERNAL_DECLARE(_var, _name)    \
    SOL_ATTR_UNUSED struct sol_log_domain _var = { \
        .name = "sol-" _name,                    \
        .color = SOL_LOG_COLOR_MAGENTA,          \
        .level = SOL_LOG_LEVEL_WARNING           \
    }

#define SOL_LOG_INTERNAL_DECLARE_STATIC(_var, _name)    \
    static SOL_LOG_INTERNAL_DECLARE(_var, _name)

#define SOL_LOG_INTERNAL_INIT_ONCE                               \
    do {                                                        \
        static bool _log_internal_init_once_first = true;       \
        if (_log_internal_init_once_first) {                    \
            sol_log_domain_init_level(SOL_LOG_DOMAIN);            \
            _log_internal_init_once_first = false;              \
        }                                                       \
    } while (0)
#else
#define SOL_LOG_INTERNAL_DECLARE(_var, _name)
#define SOL_LOG_INTERNAL_DECLARE_STATIC(_var, _name)
#define SOL_LOG_INTERNAL_INIT_ONCE
#ifdef SOL_LOG_DOMAIN
#undef SOL_LOG_DOMAIN
#define SOL_LOG_DOMAIN NULL
#endif // #ifdef SOL_LOG_DOMAIN
#endif // #ifdef SOL_LOG_ENABLED
