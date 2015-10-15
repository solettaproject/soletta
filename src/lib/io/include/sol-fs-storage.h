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

#include <stddef.h>
#include <stdint.h>

#include "sol-buffer.h"
#include "sol-log.h"
#include "sol-types.h"
#include "sol-util.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Writes buffer contents to storage.
 *
 * Note that as writing operations are asynchronous, to check if it completely
 * succeded, one needs to register a callback that will inform writing result.
 * However, right now filesystem storage does not perform asynchronous writing.
 * Even though, to keep all persistence APIs uniform, only on callback one can
 * check if writing completed successfully.
 *
 * @param name name of property. It will create a file on filesyste with
 * this name.
 * @param blob blob that will be written
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @c cb
 *
 * return 0 on success, a negative number on failure
 */
int sol_fs_write_raw(const char *name, struct sol_blob *blob,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data);
int sol_fs_read_raw(const char *name, struct sol_buffer *buffer);

#define CREATE_BUFFER(_val) \
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(_val, \
    sizeof(*(_val)), SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);

#define CREATE_BLOB(_val) \
    struct sol_blob *blob; \
    size_t _s = sizeof(*_val); \
    void *v = sol_util_memdup(_val, _s); \
    SOL_NULL_CHECK(v, -EINVAL); \
    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, v, _s); \
    if (!blob) { \
        free(v); \
        return -EINVAL; \
    }

static inline int
sol_fs_read_uint8(const char *name, uint8_t *value)
{
    CREATE_BUFFER(value);

    return sol_fs_read_raw(name, &buf);
}

static inline int
sol_fs_write_uint8(const char *name, uint8_t value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(&value);

    r = sol_fs_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

static inline int
sol_fs_read_bool(const char *name, bool *value)
{
    CREATE_BUFFER(value);

    return sol_fs_read_raw(name, &buf);
}

static inline int
sol_fs_write_bool(const char *name, bool value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(&value);

    r = sol_fs_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

static inline int
sol_fs_read_int32(const char *name, int32_t *value)
{
    CREATE_BUFFER(value);

    return sol_fs_read_raw(name, &buf);
}

static inline int
sol_fs_write_int32(const char *name, int32_t value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(&value);

    r = sol_fs_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

static inline int
sol_fs_read_irange(const char *name, struct sol_irange *value)
{
    CREATE_BUFFER(value);

    return sol_fs_read_raw(name, &buf);
}

static inline int
sol_fs_write_irange(const char *name, struct sol_irange *value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(value);

    r = sol_fs_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

static inline int
sol_fs_read_drange(const char *name, struct sol_drange *value)
{
    CREATE_BUFFER(value);

    return sol_fs_read_raw(name, &buf);
}

static inline int
sol_fs_write_drange(const char *name, struct sol_drange *value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(value);

    r = sol_fs_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

static inline int
sol_fs_read_double(const char *name, double *value)
{
    CREATE_BUFFER(value);

    return sol_fs_read_raw(name, &buf);
}

static inline int
sol_fs_write_double(const char *name, double value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(&value);

    r = sol_fs_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

static inline int
sol_fs_read_string(const char *name, char **value)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    int r;

    r = sol_fs_read_raw(name, &buf);
    if (r < 0) {
        sol_buffer_fini(&buf);
        return r;
    }

    *value = sol_buffer_steal(&buf, NULL);

    return 0;
}

static inline int
sol_fs_write_string(const char *name, const char *value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;
    struct sol_blob *blob;
    char *string;

    string = strdup(value);
    SOL_NULL_CHECK(string, -ENOMEM);

    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, string, strlen(value));
    SOL_NULL_CHECK(blob, -ENOMEM);

    r = sol_fs_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

/**
 * @}
 */

#undef CREATE_BUFFER

#undef CREATE_BLOB

#ifdef __cplusplus
}
#endif
