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

struct sol_platform_linux_micro_module {
#ifndef SOL_NO_API_VERSION
    uint16_t api_version;
#define SOL_PLATFORM_LINUX_MICRO_MODULE_API_VERSION (1)
#endif
    const char *name;
    int (*init)(const struct sol_platform_linux_micro_module *module, const char *service);
    void (*shutdown)(const struct sol_platform_linux_micro_module *module, const char *service);
    int (*start)(const struct sol_platform_linux_micro_module *module, const char *service);
    int (*stop)(const struct sol_platform_linux_micro_module *module, const char *service, bool force_immediate);
    int (*restart)(const struct sol_platform_linux_micro_module *module, const char *service);
    int (*start_monitor)(const struct sol_platform_linux_micro_module *module, const char *service);
    int (*stop_monitor)(const struct sol_platform_linux_micro_module *module, const char *service);
};

void sol_platform_linux_micro_inform_service_state(const char *service, enum sol_platform_service_state state);

#ifdef SOL_PLATFORM_LINUX_MICRO_MODULE_EXTERNAL
#define SOL_PLATFORM_LINUX_MICRO_MODULE(_NAME, decl ...) \
    SOL_API const struct sol_platform_linux_micro_module *SOL_PLATFORM_LINUX_MICRO_MODULE = &((const struct sol_platform_linux_micro_module) { \
            SOL_SET_API_VERSION(.api_version = SOL_PLATFORM_LINUX_MICRO_MODULE_API_VERSION, ) \
            decl \
        })
#else
#define SOL_PLATFORM_LINUX_MICRO_MODULE(_NAME, decl ...) \
    const struct sol_platform_linux_micro_module SOL_PLATFORM_LINUX_MICRO_MODULE_ ## _NAME = { \
        SOL_SET_API_VERSION(.api_version = SOL_PLATFORM_LINUX_MICRO_MODULE_API_VERSION, ) \
        decl \
    }
#endif

#ifdef __cplusplus
}
#endif
