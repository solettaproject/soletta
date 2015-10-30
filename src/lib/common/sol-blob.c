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

#include <stdlib.h>
#include <errno.h>

#define SOL_LOG_DOMAIN &_sol_blob_log_domain
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-util.h"
#include "sol-types.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_sol_blob_log_domain, "blob");

#ifndef SOL_NO_API_VERSION
#define SOL_BLOB_CHECK_API_VERSION(blob, ...) \
    if (!(blob)->type->api_version) {               \
        SOL_WRN("" # blob                            \
            "(%p)->type->api_version(%hu) != "   \
            "SOL_BLOB_TYPE_API_VERSION(%hu)",     \
            (blob), (blob)->type->api_version,   \
            SOL_BLOB_TYPE_API_VERSION);           \
        return __VA_ARGS__;                         \
    }
#else
#define SOL_BLOB_CHECK_API_VERSION(blob, ...)
#endif

#define SOL_BLOB_CHECK(blob, ...)                        \
    do {                                                \
        if (!(blob)) {                                  \
            SOL_WRN("" # blob " == NULL");               \
            return __VA_ARGS__;                         \
        }                                               \
        if (!(blob)->type) {                            \
            SOL_WRN("" # blob "(%p)->type == NULL",      \
                (blob));                             \
            return __VA_ARGS__;                         \
        }                                               \
        SOL_BLOB_CHECK_API_VERSION((blob), __VA_ARGS__) \
        if ((blob)->refcnt == 0) {                      \
            SOL_WRN("" # blob "(%p)->refcnt == 0",       \
                (blob));                             \
            return __VA_ARGS__;                         \
        }                                               \
    } while (0)

int sol_blob_init(void);
void sol_blob_shutdown(void);

int
sol_blob_init(void)
{
    sol_log_domain_init_level(SOL_LOG_DOMAIN);
    return 0;
}

void
sol_blob_shutdown(void)
{
}

SOL_API struct sol_blob *
sol_blob_new(const struct sol_blob_type *type, struct sol_blob *parent, const void *mem, size_t size)
{
    struct sol_blob *blob;

    SOL_NULL_CHECK(type, NULL);
#ifndef SOL_NO_API_VERSION
    SOL_INT_CHECK(type->api_version, != SOL_BLOB_TYPE_API_VERSION, NULL);
#endif

    blob = calloc(1, sizeof(struct sol_blob));
    SOL_NULL_CHECK(blob, NULL);
    if (sol_blob_setup(blob, type, mem, size) < 0) {
        free(blob);
        return NULL;
    }

    if (parent)
        sol_blob_set_parent(blob, parent);

    return blob;
}

SOL_API int
sol_blob_setup(struct sol_blob *blob, const struct sol_blob_type *type, const void *mem, size_t size)
{
    SOL_NULL_CHECK(blob, -EINVAL);
    SOL_NULL_CHECK(type, -EINVAL);
#ifndef SOL_NO_API_VERSION
    SOL_INT_CHECK(type->api_version, != SOL_BLOB_TYPE_API_VERSION, -EINVAL);
#endif

    blob->type = type;
    blob->mem = (void *)mem;
    blob->size = size;
    blob->refcnt = 1;
    return 0;
}

SOL_API struct sol_blob *
sol_blob_ref(struct sol_blob *blob)
{
    SOL_BLOB_CHECK(blob, NULL);
    errno = ENOMEM;
    SOL_INT_CHECK(blob->refcnt, == UINT16_MAX, NULL);
    errno = 0;
    blob->refcnt++;
    return blob;
}

SOL_API void
sol_blob_unref(struct sol_blob *blob)
{
    SOL_BLOB_CHECK(blob);
    blob->refcnt--;
    if (blob->refcnt > 0)
        return;

    if (blob->parent)
        sol_blob_unref(blob->parent);

    if (blob->type->free)
        blob->type->free(blob);
    else
        free(blob);
}

SOL_API void
sol_blob_set_parent(struct sol_blob *blob, struct sol_blob *parent)
{
    SOL_BLOB_CHECK(blob);

    if (parent) {
        SOL_BLOB_CHECK(parent);
        sol_blob_ref(parent);
    }

    if (blob->parent)
        sol_blob_unref(blob->parent);

    blob->parent = parent;
}


static void
blob_free(struct sol_blob *blob)
{
    free(blob->mem);
    free(blob);
}

static const struct sol_blob_type _SOL_BLOB_TYPE_DEFAULT = {
    SOL_SET_API_VERSION(.api_version = SOL_BLOB_TYPE_API_VERSION, )
    SOL_SET_API_VERSION(.sub_api = 0, )
    .free = blob_free,
};

static const struct sol_blob_type _SOL_BLOB_TYPE_NOFREE = {
    SOL_SET_API_VERSION(.api_version = SOL_BLOB_TYPE_API_VERSION, )
    SOL_SET_API_VERSION(.sub_api = 0, )
    .free = NULL,
};

SOL_API const struct sol_blob_type *SOL_BLOB_TYPE_DEFAULT = &_SOL_BLOB_TYPE_DEFAULT;

SOL_API const struct sol_blob_type *SOL_BLOB_TYPE_NOFREE = &_SOL_BLOB_TYPE_NOFREE;
