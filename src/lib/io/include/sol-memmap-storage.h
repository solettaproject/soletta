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
#include "sol-str-table.h"
#include "sol-types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to save values to memory mapped persistent storage
 */

/**
 * @defgroup Memmap Memmap
 * @ingroup IO
 *
 * @brief Memory mapped persistence storage (like NVRAM or EEPROM) API on Soletta.
 *
 * A map must be provided, either directly via @c sol_memmap_add_map or by
 * informing a JSON file to Soletta runner or generator.
 * This map needs to contain a property @c _version (@c MEMMAP_VERSION_ENTRY),
 * which will store version of map stored. This API will refuse to work if
 * stored map is different from map version. Note that @c _version field
 * is a @c uint8_t and that versions should start on 1, so Soletta will know
 * if dealing with a totally new storage. It also considers 255 (0xff) as a
 * non-value, so fit new EEPROMs.
 * Note that a map may define a timeout value to perform writes. This way,
 * writings can be grouped together, as all writing operations will be
 * performed at timeout end. That said, it's important to note that when
 * writing is performed, no order is guaranteed for different keys, i.e.,
 * writing to 'a' and 'b' can be performed, at timeout end, as 'b' and 'a'
 * - but for a given key, only the last write will be performed at timeout end.
 * Multiple writes for the same key before timeout end will result in previous
 * writes being replaced, and their callbacks will be informed with status
 * -ECANCELED.
 *
 * @{
 */

#define MEMMAP_VERSION_ENTRY "_version" /**< Name of property which contains stored map version */

/**
 * @brief Macro to declare a @ref sol_memmap_entry variable setting fields
 * with values passed by argument.
 */
#define SOL_MEMMAP_ENTRY_BIT_SIZE(_name, _offset, _size, _bit_offset, _bit_size) \
    static struct sol_memmap_entry _name = { .offset = (_offset), .size = (_size), .bit_size = (_bit_size), .bit_offset = (_bit_offset) }


/**
 * @brief Macro to declare a @ref sol_memmap_entry variable without bit_offset
 * and bit_size.
 */
#define SOL_MEMMAP_ENTRY(_name, _offset, _size) \
    SOL_MEMMAP_ENTRY_BIT_SIZE(_name, _offset, _size, 0, 0)

/**
 * @brief Macro to declare a @ref sol_memmap_entry variable for boolean.
 * So size and bit_size are set to @c 1.
 */
#define SOL_MEMMAP_BOOL_ENTRY(_name, _offset, _bit_offset) \
    SOL_MEMMAP_ENTRY_BIT_SIZE(_name, _offset, 1, _bit_offset, 1)

struct sol_memmap_map {
    uint16_t api_version; /**< Version of map. Functions will refuse to read/write on storage if this version and the one storad differs */
    uint32_t timeout; /**< Timeout, in milliseconds, of writing operations. After a write is requested, a timer will run and group all
                       * writing operations until it expires, when real writing will be performed */
    const char *path; /**< Where to find the storage. Under Linux, it is the file mapping the storage, like @c /dev/nvram.
                       * Optionally, it can also be of form <tt> create,\<bus_type\>,\<rel_path\>,\<devnumber\>,\<devname\> </tt>, where:
                       * @arg @a bus_type is the bus type, supported values are: i2c
                       * @arg @a rel_path is the relative path for device on '/sys/devices',
                       * like 'platform/80860F41:05'
                       * @arg @a devnumber is device number on bus, like 0x50
                       * @arg @a devname is device name, the one recognized by its driver
                       */
    const struct sol_str_table_ptr *entries; /**< Entries on map, containing name, offset and size */ /* Memory trick in place, must be last on struct*/
};

struct sol_memmap_entry {
    size_t offset; /**< Offset of this entry on storage, in bytes. If zero, it will be calculated from previous entry on @c entries array */
    size_t size; /**< Total size of this entry on storage, in bytes. */
    uint32_t bit_size; /**< Total size of this entry on storage, in bits. Must be up to <tt>size * 8</tt>. If zero, it will be assumed as <tt>size * 8</tt>. Note that this will be ignored if @c size is greater than 8. */
    uint8_t bit_offset; /**< Bit offset on first byte. Note that this will be ignored if @c size is greater than 8. */
};

/**
 * @brief Writes buffer contents to storage.
 *
 * Note that as writing operations are asynchronous, to check if it completely
 * succeded, one needs to register a callback that will inform writing result.
 * A negative status on callback means failure; -ECANCELED for instance, means
 * that another write to the same property took place before this one was
 * completed.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via @c sol_memmap_add_map (if present in more than one,
 * behaviour is undefined)
 * @param blob blob that will be written, according to its entry on map.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @c cb
 *
 * return 0 on success, a negative number on failure
 */
int sol_memmap_write_raw(const char *name, struct sol_blob *blob,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data);

/**
 * @brief Read storage contents to buffer.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via @c sol_memmap_add_map (if present in more than one,
 * behaviour is undefined)
 * @param buffer buffer where result will be read into, according to its entry
 * on map.
 *
 * return 0 on success, a negative number on failure
 */
int sol_memmap_read_raw(const char *name, struct sol_buffer *buffer);

/**
 * @brief Add a map to internal list of available maps.
 *
 * As Soletta will keep a reference to this map, it should be kept alive
 * during memmap usage.
 *
 * @param map map to be add.
 *
 * @return 0 on success, a negative number on failure.
 */
int sol_memmap_add_map(const struct sol_memmap_map *map);

/**
 * @brief Removes a previously added map from internal list of available maps.
 *
 * @param map map to be removed.
 *
 * @return 0 on success, a negative number on failure.
 */
int sol_memmap_remove_map(const struct sol_memmap_map *map);

/**
 * @brief Defines map timeout to actually perform write.
 *
 * @param map map to have its timeout changed
 * @param timeout new timeout, in milliseconds.
 *
 * @return true if successfuly set map timeout.
 *
 * @note This change will take effect after current active timer expires.
 * Active ones will remain unchanged
 */
bool sol_memmap_set_timeout(struct sol_memmap_map *map, uint32_t timeout);

/**
 * @brief Get map timeout
 *
 * @see sol_memmap_set_timeout() for more details.
 *
 * @param map map to get its timeout
 *
 * @return timeout in milliseconds.
 */
uint32_t sol_memmap_get_timeout(const struct sol_memmap_map *map);

/**
 * @brief Macro to create a struct @ref sol_buffer with value passed as argument
 * and flags SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED and SOL_BUFFER_FLAGS_NO_NUL_BYTE.
 */
#define CREATE_BUFFER(_val) \
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(_val, \
    sizeof(*(_val)), SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);

/**
 * @brief Macro to create a struct @ref sol_blob with value passed as argument.
 */
#define CREATE_BLOB(_val) \
    struct sol_blob *blob; \
    size_t _s = sizeof(*_val); \
    void *v = malloc(_s); \
    SOL_NULL_CHECK(v, -ENOMEM); \
    memcpy(v, _val, _s); \
    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, v, _s); \
    if (!blob) { \
        free(v); \
        return -EINVAL; \
    }

static inline int
sol_memmap_read_uint8(const char *name, uint8_t *value)
{
    CREATE_BUFFER(value);

    return sol_memmap_read_raw(name, &buf);
}

static inline int
sol_memmap_write_uint8(const char *name, uint8_t value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(&value);

    r = sol_memmap_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

static inline int
sol_memmap_read_bool(const char *name, bool *value)
{
    CREATE_BUFFER(value);

    return sol_memmap_read_raw(name, &buf);
}

static inline int
sol_memmap_write_bool(const char *name, bool value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(&value);

    r = sol_memmap_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

static inline int
sol_memmap_read_int32(const char *name, int32_t *value)
{
    CREATE_BUFFER(value);

    return sol_memmap_read_raw(name, &buf);
}

static inline int
sol_memmap_write_int32(const char *name, int32_t value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(&value);

    r = sol_memmap_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

static inline int
sol_memmap_read_irange(const char *name, struct sol_irange *value)
{
    CREATE_BUFFER(value);

    return sol_memmap_read_raw(name, &buf);
}

static inline int
sol_memmap_write_irange(const char *name, struct sol_irange *value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(value);

    r = sol_memmap_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

static inline int
sol_memmap_read_drange(const char *name, struct sol_drange *value)
{
    CREATE_BUFFER(value);

    return sol_memmap_read_raw(name, &buf);
}

static inline int
sol_memmap_write_drange(const char *name, struct sol_drange *value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(value);

    r = sol_memmap_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

static inline int
sol_memmap_read_double(const char *name, double *value)
{
    CREATE_BUFFER(value);

    return sol_memmap_read_raw(name, &buf);
}

static inline int
sol_memmap_write_double(const char *name, double value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;

    CREATE_BLOB(&value);

    r = sol_memmap_write_raw(name, blob, cb, data);
    sol_blob_unref(blob);
    return r;
}

static inline int
sol_memmap_read_string(const char *name, char **value)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    int r;

    r = sol_memmap_read_raw(name, &buf);
    if (r < 0) {
        sol_buffer_fini(&buf);
        return r;
    }

    *value = (char *)sol_buffer_steal(&buf, NULL);

    return 0;
}

static inline int
sol_memmap_write_string(const char *name, const char *value,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r;
    struct sol_blob *blob;
    char *string;

    string = strdup(value);
    SOL_NULL_CHECK(string, -ENOMEM);

    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, string, strlen(value) + 1);
    SOL_NULL_CHECK_GOTO(blob, error);

    r = sol_memmap_write_raw(name, blob, cb, data);
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
