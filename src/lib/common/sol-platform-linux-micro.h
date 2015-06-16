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

#include "sol-platform.h"
#include <stdbool.h>
#include <unistd.h>


#ifdef __cplusplus
extern "C" {
#endif

struct sol_platform_linux_micro_module {
    unsigned long int api_version;
#define SOL_PLATFORM_LINUX_MICRO_MODULE_API_VERSION (1UL)
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

struct sol_platform_linux_micro_fork_run *sol_platform_linux_micro_fork_run(void (*on_fork)(void *data), void (*on_child_exit)(void *data, uint64_t pid, int status), const void *data);
void sol_platform_linux_micro_fork_run_stop(struct sol_platform_linux_micro_fork_run *handle);
pid_t sol_platform_linux_micro_fork_run_get_pid(const struct sol_platform_linux_micro_fork_run *handle);

#ifdef SOL_PLATFORM_LINUX_MICRO_MODULE_EXTERNAL
#define SOL_PLATFORM_LINUX_MICRO_MODULE(_NAME, decl ...)                  \
    SOL_API const struct sol_platform_linux_micro_module *SOL_PLATFORM_LINUX_MICRO_MODULE = &((const struct sol_platform_linux_micro_module){.api_version = SOL_PLATFORM_LINUX_MICRO_MODULE_API_VERSION, decl })
#else
#define SOL_PLATFORM_LINUX_MICRO_MODULE(_NAME, decl ...)                    \
    const struct sol_platform_linux_micro_module SOL_PLATFORM_LINUX_MICRO_MODULE_ ## _NAME = { .api_version = SOL_PLATFORM_LINUX_MICRO_MODULE_API_VERSION, decl }
#endif

#ifdef __cplusplus
}
#endif
