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
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sol_blob_type;
struct sol_blob {
    const struct sol_blob_type *type;
    struct sol_blob *parent;
    void *mem;
    size_t size;
    uint16_t refcnt;
};

struct sol_blob_type {
#define SOL_BLOB_TYPE_API_VERSION (1)
    uint16_t api_version;
    uint16_t sub_api;
    void (*free)(struct sol_blob *blob);
};

/*
 * The default type uses free() to release the blob's memory
 */
extern const struct sol_blob_type *SOL_BLOB_TYPE_DEFAULT;

struct sol_blob *sol_blob_new(const struct sol_blob_type *type, struct sol_blob *parent, const void *mem, size_t size);
int sol_blob_setup(struct sol_blob *blob, const struct sol_blob_type *type, const void *mem, size_t size);
struct sol_blob *sol_blob_ref(struct sol_blob *blob);
void sol_blob_unref(struct sol_blob *blob);
void sol_blob_set_parent(struct sol_blob *blob, struct sol_blob *parent);

#ifdef __cplusplus
}
#endif
