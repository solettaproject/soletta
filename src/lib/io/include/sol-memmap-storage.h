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
 * A map must be provided, either directly via sol_memmap_add_map() or by
 * informing a JSON file to Soletta runner or generator.
 * This map needs to contain a property @c _version (@c SOL_MEMMAP_VERSION_ENTRY),
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

/**
 * @brief Name of property which contains stored map version
 */
#define SOL_MEMMAP_VERSION_ENTRY "_version"

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

/**
 * @brief Memory map basic struct.
 *
 * This struct holds informations about a memory map.
 */
typedef struct sol_memmap_map {
    uint8_t version; /**< Version of map. Functions will refuse to read/write on storage if this version and the one storad differs */
    const char *path; /**< Where to find the storage. On Linux, it is
                       * the file mapping the storage, like @c
                       * /dev/nvram. Optionally, it can also be in the
                       * form
                       * <tt>create,\<bus_type\>,\<rel_path\>,\<devnumber\>,\<devname\></tt>,
                       * where: @arg @a bus_type is the bus type
                       * (supported values are: @c i2c), @arg @a
                       * rel_path is the relative path for the device
                       * on @c /sys/devices, like @c
                       * platform/80860F41:05, @arg @a devnumber is
                       * the device number on the bus, like @c 0x50,
                       * and @arg @a devname is the device name, the
                       * one recognized by its driver. On Zephyr, this
                       * field must adhere to the form
                       * <tt>\<driver_name\>,\<min_erase_size\>,\<max_rw_size\>,\<mem_offset\></tt>,
                       * where: @arg @a driver_name is the driver name
                       * string, @arg @a erase_size is the minimum
                       * erasable section size, @arg @a max_rw_size is
                       * the maximum read/write sizes allowed and @arg
                       * @a mem_offset is the flash memory's starting
                       * offset (all sizes in bytes).
                       */
    uint32_t timeout; /**< Timeout, in milliseconds, of writing operations. After a write is requested, a timer will run and group all
                       * writing operations until it expires, when real writing will be performed */
    const struct sol_str_table_ptr *entries; /**< Entries on map, containing name, offset and size */ /* Memory trick in place, must be last on struct*/
} sol_memmap_map;

/**
 * @brief A memory map entry.
 *
 * @see sol_memmap_map
 */
typedef struct sol_memmap_entry {
    size_t offset; /**< Offset of this entry on storage, in bytes. If zero, it will be calculated from previous entry on @c entries array */
    size_t size; /**< Total size of this entry on storage, in bytes. */
    uint32_t bit_size; /**< Total size of this entry on storage, in bits. Must be up to <tt>size * 8</tt>. If zero, it will be assumed as <tt>size * 8</tt>. Note that this will be ignored if @c size is greater than 8. */
    uint8_t bit_offset; /**< Bit offset on first byte. Note that this will be ignored if @c size is greater than 8. */
} sol_memmap_entry;

/**
 * @brief Writes buffer contents to storage.
 *
 * Note that as writing operations are asynchronous, to check if it completely
 * succeeded, one needs to register a callback that will inform writing result.
 * A negative status on callback means failure; -ECANCELED for instance, means
 * that another write to the same property took place before this one was
 * completed.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param blob blob that will be written, according to its entry on map.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 */
int sol_memmap_write_raw(const char *name, struct sol_blob *blob,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data);

/**
 * @brief Read storage contents to buffer.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param buffer buffer where result will be read into, according to its entry
 * on map.
 *
 * return @c 0 on success, a negative number on failure
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
 * @return @c 0 on success, a negative number on failure.
 */
int sol_memmap_add_map(const struct sol_memmap_map *map);

/**
 * @brief Removes a previously added map from internal list of available maps.
 *
 * @param map map to be removed.
 *
 * @return @c 0 on success, a negative number on failure.
 */
int sol_memmap_remove_map(const struct sol_memmap_map *map);

/**
 * @brief Defines map timeout to actually perform write.
 *
 * @param map map to have its timeout changed
 * @param timeout new timeout, in milliseconds.
 *
 * @return @c 0 on success, a negative number on failure.
 *
 * @note This change will take effect after current active timer expires.
 * Active ones will remain unchanged
 */
int sol_memmap_set_timeout(struct sol_memmap_map *map, uint32_t timeout);

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
    blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, v, _s); \
    if (!blob) { \
        free(v); \
        return -EINVAL; \
    }

/**
 * @brief Read an uint8_t contents.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param value where result will be read into, according to its entry
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_memmap_read_uint8(const char *name, uint8_t *value)
{
    CREATE_BUFFER(value);

    return sol_memmap_read_raw(name, &buf);
}

/**
 * @brief Writes an uint8_t contents to storage.
 *
 * This funcion uses the function sol_memmap_write_raw() internally,
 * so the same behaviour must be considered.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param value the variable containing the value to be written.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 *
 * @see sol_memmap_write_raw()
 */
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

/**
 * @brief Reads a boolean contents.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param value where result will be read into, according to its entry
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_memmap_read_bool(const char *name, bool *value)
{
    CREATE_BUFFER(value);

    return sol_memmap_read_raw(name, &buf);
}

/**
 * @brief Writes a bool contents to storage.
 *
 * This funcion uses the function sol_memmap_write_raw() internally,
 * so the same behaviour must be considered.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param value the variable containing the value to be written.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 *
 * @see sol_memmap_write_raw()
 */
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

/**
 * @brief Reads an int32_t contents.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param value where result will be read into, according to its entry
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_memmap_read_int32(const char *name, int32_t *value)
{
    CREATE_BUFFER(value);

    return sol_memmap_read_raw(name, &buf);
}

/**
 * @brief Writes an int32_t contents to storage.
 *
 * This funcion uses the function sol_memmap_write_raw() internally,
 * so the same behaviour must be considered.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param value the variable containing the value to be written.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 *
 * @see sol_memmap_write_raw()
 */
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

/**
 * @brief Reads an irange content.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param value where result will be read into, according to its entry
 *
 * return @c 0 on success, a negative number on failure
 *
 * @ref sol_irange
 */
static inline int
sol_memmap_read_irange(const char *name, struct sol_irange *value)
{
    CREATE_BUFFER(value);

    return sol_memmap_read_raw(name, &buf);
}

/**
 * @brief Writes an irange contents to storage.
 *
 * This funcion uses the function sol_memmap_write_raw() internally,
 * so the same behaviour must be considered.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param value the variable containing the value to be written.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 *
 * @see sol_memmap_write_raw()
 * @ref sol_irange
 */
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

/**
 * @brief Reads a drange contents.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param value where result will be read into, according to its entry
 *
 * return @c 0 on success, a negative number on failure
 *
 * @ref sol_drange
 */
static inline int
sol_memmap_read_drange(const char *name, struct sol_drange *value)
{
    CREATE_BUFFER(value);

    return sol_memmap_read_raw(name, &buf);
}

/**
 * @brief Writes an drange contents to storage.
 *
 * This funcion uses the function sol_memmap_write_raw() internally,
 * so the same behaviour must be considered.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param value the variable containing the value to be written.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 *
 * @see sol_memmap_write_raw()
 * @ref sol_drange
 */
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

/**
 * @brief Reads a double contents.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param value where result will be read into, according to its entry
 *
 * return @c 0 on success, a negative number on failure
 */
static inline int
sol_memmap_read_double(const char *name, double *value)
{
    CREATE_BUFFER(value);

    return sol_memmap_read_raw(name, &buf);
}

/**
 * @brief Writes a double contents to storage.
 *
 * This funcion uses the function sol_memmap_write_raw() internally,
 * so the same behaviour must be considered.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param value the variable containing the value to be written.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 *
 * @see sol_memmap_write_raw()
 */
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

/**
 * @brief Reads a string contents.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param value where result will be read into, according to its entry
 *
 * return @c 0 on success, a negative number on failure
 */
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

/**
 * @brief Writes a string contents to storage.
 *
 * This funcion uses the function sol_memmap_write_raw() internally,
 * so the same behaviour must be considered.
 *
 * @param name name of property. must be present in one of maps previoulsy
 * added via sol_memmap_add_map() (if present in more than one,
 * behaviour is undefined)
 * @param value the variable containing the value to be written.
 * @param cb callback to be called when writing finishes. It contains status
 * of writing: if failed, is lesser than zero.
 * @param data user data to be sent to callback @a cb
 *
 * return @c 0 on success, a negative number on failure
 *
 * @see sol_memmap_write_raw()
 */
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

    blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, string, strlen(value) + 1);
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
