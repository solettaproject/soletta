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

#include <stdlib.h>
#include <errno.h>

#define SOL_LOG_DOMAIN &_sol_blob_log_domain
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-util-internal.h"
#include "sol-types.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_sol_blob_log_domain, "blob");

#ifndef SOL_NO_API_VERSION
#define SOL_BLOB_CHECK_API_VERSION(blob, ...) \
    if ((blob)->type->api_version != SOL_BLOB_TYPE_API_VERSION) { \
        SOL_WRN("" # blob \
            "(%p)->type->api_version(%hu) != " \
            "SOL_BLOB_TYPE_API_VERSION(%hu)", \
            (blob), (blob)->type->api_version, \
            SOL_BLOB_TYPE_API_VERSION); \
        return __VA_ARGS__; \
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

static void
blob_no_free(struct sol_blob *blob)
{
}

SOL_API const struct sol_blob_type SOL_BLOB_TYPE_DEFAULT = {
    SOL_SET_API_VERSION(.api_version = SOL_BLOB_TYPE_API_VERSION, )
    SOL_SET_API_VERSION(.sub_api = 0, )
    .free = blob_free,
};

SOL_API const struct sol_blob_type SOL_BLOB_TYPE_NO_FREE_DATA = {
    SOL_SET_API_VERSION(.api_version = SOL_BLOB_TYPE_API_VERSION, )
    SOL_SET_API_VERSION(.sub_api = 0, )
    .free = NULL,
};

SOL_API const struct sol_blob_type SOL_BLOB_TYPE_NO_FREE = {
    SOL_SET_API_VERSION(.api_version = SOL_BLOB_TYPE_API_VERSION, )
    SOL_SET_API_VERSION(.sub_api = 0, )
    .free = blob_no_free,
};
