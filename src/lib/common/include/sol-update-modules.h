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

#include <stdint.h>

#include "sol-update.h"

#include "sol-macros.h"

#ifdef __cplusplus
extern "C" {
#endif

struct sol_update {
#ifndef SOL_NO_API_VERSION
#define SOL_UPDATE_API_VERSION (1)
    uint16_t api_version; /**< API version */
#endif

    struct sol_update_handle * (*check)(const char *url,
        void (*cb)(void *data, int status,
        const struct sol_update_info *response),
        const void *data);

    struct sol_update_handle * (*fetch)(const struct sol_update_info *response,
        void (*cb)(void *data, int status, const char *file_path),
        const void *data, bool resume);

    bool (*cancel)(struct sol_update_handle *handle);

    int (*progress)(struct sol_update_handle *handle);

    struct sol_update_handle * (*install)(const char *file_path,
        void (*cb)(void *data, int status),
        void *data);

    void (*init)(void);
};

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

#ifdef __cplusplus
}
#endif
