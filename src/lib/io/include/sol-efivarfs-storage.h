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

#include <stddef.h>
#include <stdint.h>

#include "sol-buffer.h"
#include "sol-log.h"
#include "sol-types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to save values to EFIVARS persistent storage
 */

/**
 * @defgroup EFIVARS EFI variables
 * @ingroup IO
 *
 * @brief EFI variables API for Soletta.
 *
 * @{
 */

/**
 * @brief Writes buffer contents to storage.
 *
 * Note that as writing operations are asynchronous, to check if it completely
 * succeeded, one needs to register a callback that will inform writing result.
 *
 * @param name name of property. It will create a new EFI variable with
 * this name.
 * @param blob blob that will be written
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @c cb
 *
 * return 0 on success, a negative number on failure
 */
int sol_efivars_write_raw(const char *name, struct sol_blob *blob,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data);

/**
 * @brief Read stored contents and set to buffer.
 *
 * This function will read the contents of the EFI variable @a name
 * into the buffer @a buffer. It will try to resize the buffer if
 * needed.
 *
 * @param name name of property. It will look for an EFI variable with
 * this name.
 * @param buffer buffer that will be set with read contents.
 *
 * return 0 on success, a negative number on failure
 */
int sol_efivars_read_raw(const char *name, struct sol_buffer *buffer);

/**
 * @brief Macro to create a struct @ref sol_buffer with value passed as argument
 * and flags SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED and SOL_BUFFER_FLAGS_NO_NUL_BYTE.
 */

#ifdef __cplusplus
#define CREATE_BUFFER(_val) \
    struct sol_buffer buf SOL_BUFFER_INIT_FLAGS(_val, \
    sizeof(*(_val)), SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
#else
#define CREATE_BUFFER(_val) \
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(_val, \
    sizeof(*(_val)), SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
#endif /* __cplusplus */

/**
 * @brief Macro to create a struct @ref sol_blob with value passed as argument.
 */
#define CREATE_BLOB(_val) \
    struct sol_blob *blob; \
    size_t _s = sizeof(*_val); \
    void *v = malloc(_s); \
    SOL_NULL_CHECK(v, -ENOMEM); \
    memcpy(v, _val, _s); \
    blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, v, _s); \
    if (!blob) { \
        free(v); \
        return -EINVAL; \
    }

/**
 * @brief Reads an uint8_t from the EFI given variable and set to value.
 *
 * This function will read the contents of the EFI variable @a name
 * into the variable @a value.
 *
 * @param name name of property. It will look for an EFI variable with
 * this name.
 * @param value The variable that will be set with read contents.
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_efivars_read_uint8(const char *name, uint8_t *value)
{
    CREATE_BUFFER(value);

    return sol_efivars_read_raw(name, &buf);
}

/**
 * @brief Writes an uint8_t into an EFI variable.
 *
 * This function uses sol_efivars_write_raw() internally, the same
 * behaviour should be considered.
 *
 * @param name name of property. It will create a new EFI variable with
 * this name.
 * @param value The value to be written.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_efivars_write_uint8(const char *name, uint8_t value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(&value);

    r = sol_efivars_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

/**
 * @brief Reads a boolean from the EFI given variable and set to value.
 *
 * This function will read the contents of the EFI variable @a name
 * into the variable @a value.
 *
 * @param name name of property. It will look for an EFI variable with
 * this name.
 * @param value The variable that will be set with read contents.
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_efivars_read_bool(const char *name, bool *value)
{
    CREATE_BUFFER(value);

    return sol_efivars_read_raw(name, &buf);
}

/**
 * @brief Writes a boolean into an EFI variable.
 *
 * This function uses sol_efivars_write_raw() internally, the same
 * behaviour should be considered.
 *
 * @param name name of property. It will create a new EFI variable with
 * this name.
 * @param value The value to be written.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_efivars_write_bool(const char *name, bool value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(&value);

    r = sol_efivars_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

/**
 * @brief Reads an int32_t from the EFI given variable and set to value.
 *
 * This function will read the contents of the EFI variable @a name
 * into the variable @a value.
 *
 * @param name name of property. It will look for an EFI variable with
 * this name.
 * @param value The variable that will be set with read contents.
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_efivars_read_int32(const char *name, int32_t *value)
{
    CREATE_BUFFER(value);

    return sol_efivars_read_raw(name, &buf);
}

/**
 * @brief Writes an int32_t into an EFI variable.
 *
 * This function uses sol_efivars_write_raw() internally, the same
 * behaviour should be considered.
 *
 * @param name name of property. It will create a new EFI variable with
 * this name.
 * @param value The value to be written.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_efivars_write_int32(const char *name, int32_t value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(&value);

    r = sol_efivars_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

/**
 * @brief Reads a sol_irange from the EFI given variable and set to value.
 *
 * This function will read the contents of the EFI variable @a name
 * into the variable @a value.
 *
 * @param name name of property. It will look for an EFI variable with
 * this name.
 * @param value The variable that will be set with read contents.
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_efivars_read_irange(const char *name, struct sol_irange *value)
{
    CREATE_BUFFER(value);

    return sol_efivars_read_raw(name, &buf);
}

/**
 * @brief Writes a sol_irange into an EFI variable.
 *
 * This function uses sol_efivars_write_raw() internally, the same
 * behaviour should be considered.
 *
 * @param name name of property. It will create a new EFI variable with
 * this name.
 * @param value The value to be written.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_efivars_write_irange(const char *name, struct sol_irange *value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(value);

    r = sol_efivars_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

/**
 * @brief Reads a sol_drange from the EFI given variable and set to value.
 *
 * This function will read the contents of the EFI variable @a name
 * into the variable @a value.
 *
 * @param name name of property. It will look for an EFI variable with
 * this name.
 * @param value The variable that will be set with read contents.
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_efivars_read_drange(const char *name, struct sol_drange *value)
{
    CREATE_BUFFER(value);

    return sol_efivars_read_raw(name, &buf);
}

/**
 * @brief Writes a sol_drange into an EFI variable.
 *
 * This function uses sol_efivars_write_raw() internally, the same
 * behaviour should be considered.
 *
 * @param name name of property. It will create a new EFI variable with
 * this name.
 * @param value The value to be written.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_efivars_write_drange(const char *name, struct sol_drange *value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(value);

    r = sol_efivars_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

/**
 * @brief Reads a double from the EFI given variable and set to value.
 *
 * This function will read the contents of the EFI variable @a name
 * into the variable @a value.
 *
 * @param name name of property. It will look for an EFI variable with
 * this name.
 * @param value The variable that will be set with read contents.
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_efivars_read_double(const char *name, double *value)
{
    CREATE_BUFFER(value);

    return sol_efivars_read_raw(name, &buf);
}

/**
 * @brief Writes a double into an EFI variable.
 *
 * This function uses sol_efivars_write_raw() internally, the same
 * behaviour should be considered.
 *
 * @param name name of property. It will create a new EFI variable with
 * this name.
 * @param value The value to be written.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_efivars_write_double(const char *name, double value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(&value);

    r = sol_efivars_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

/**
 * @brief Reads a string from the EFI given variable and set to value.
 *
 * This function will read the contents of the EFI variable @a name
 * into the variable @a value.
 *
 * @param name name of property. It will look for an EFI variable with
 * this name.
 * @param value The variable that will be set with read contents.
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_efivars_read_string(const char *name, char **value)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    int r;

    r = sol_efivars_read_raw(name, &buf);
    if (r < 0) {
        sol_buffer_fini(&buf);
        return r;
    }

    *value = (char *)sol_buffer_steal(&buf, NULL);

    return 0;
}

/**
 * @brief Writes a string into an EFI variable.
 *
 * This function uses sol_efivars_write_raw() internally, the same
 * behaviour should be considered.
 *
 * @param name name of property. It will create a new EFI variable with
 * this name.
 * @param value The value to be written.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_efivars_write_string(const char *name, const char *value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;
    struct sol_blob *blob;
    char *string;

    string = strdup(value);
    SOL_NULL_CHECK(string, -ENOMEM);

    blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, string, strlen(value));
    SOL_NULL_CHECK_GOTO(blob, error);

    r = sol_efivars_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;

error:
    free(string);

    return -ENOMEM;
}

/**
 * @}
 */

#undef CREATE_BUFFER

#undef CREATE_BLOB

#ifdef __cplusplus
}
#endif
