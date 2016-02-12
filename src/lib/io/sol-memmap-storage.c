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

#include "sol-memmap-storage.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sol-buffer.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-str-slice.h"
#include "sol-str-table.h"
#include "sol-util-file.h"
#include "sol-vector.h"
#include "sol-util-internal.h"

#ifdef USE_I2C
#include <sol-i2c.h>
#endif

#define REL_PATH_IDX 2
#define DEV_NUMBER_IDX 3
#define DEV_NAME_IDX 4

struct pending_write_data {
    char *name;
    struct sol_blob *blob;
    const struct sol_memmap_entry *entry;
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status);
    const void *data;
    uint64_t mask;
};

struct map_internal {
    const struct sol_memmap_map *map;
    struct sol_timeout *timeout;
    char *resolved_path;
    struct sol_vector pending_writes;
    bool checked;
};

static struct sol_vector memory_maps = SOL_VECTOR_INIT(struct map_internal);

static bool
get_entry_metadata_on_map(const char *name, const struct sol_memmap_map *map, const struct sol_memmap_entry **entry, uint64_t *mask)
{
    uint32_t bit_size;

    if (sol_str_table_ptr_lookup(map->entries, sol_str_slice_from_str(name), entry)) {
        bit_size = (*entry)->bit_size;
        /* No mask if bit_size equal or greater than 64. Such data should not be read as an int */
        if (bit_size && (bit_size != (*entry)->size * 8) && bit_size < 64)
            *mask = (((uint64_t)1 << bit_size) - 1) << (*entry)->bit_offset;
        else
            *mask = 0;

        return true;
    }

    return false;
}

static bool
get_entry_metadata(const char *name, struct map_internal **map_internal, const struct sol_memmap_entry **entry, uint64_t *mask)
{
    int i;

    SOL_VECTOR_FOREACH_IDX (&memory_maps, *map_internal, i) {
        if (get_entry_metadata_on_map(name, (*map_internal)->map, entry, mask))
            return true;
    }

    *entry = NULL;
    *map_internal = NULL;

    return false;
}

static int
sol_memmap_read_raw_do(const char *path, const struct sol_memmap_entry *entry, uint64_t mask, struct sol_buffer *buffer)
{
    int fd, ret = 0;
    uint64_t value = 0;
    uint32_t i, j;

    fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        SOL_WRN("Could not open memory file [%s]: %s", path,
            sol_util_strerrora(errno));
        return -errno;
    }

    if (lseek(fd, entry->offset, SEEK_SET) < 0)
        goto error;

    if ((ret = sol_util_fill_buffer(fd, buffer, entry->size)) < 0)
        goto error;

    if (mask) {
        for (i = 0, j = 0; i < entry->size; i++, j += 8)
            value |= (uint64_t)((uint8_t *)buffer->data)[i] << j;

        value &= mask;
        value >>= entry->bit_offset;

        memset(buffer->data, 0, buffer->capacity);
        for (i = 0; i < entry->size; i++, value >>= 8)
            ((uint8_t *)buffer->data)[i] = value & 0xff;
    }

    if (close(fd) < 0)
        return -errno;

    return 0;

error:
    if (!ret)
        ret = -errno;
    close(fd);

    return ret;
}

static int
sol_memmap_write_raw_do(const char *path, const char *name, const struct sol_memmap_entry *entry, uint64_t mask, struct sol_blob *blob,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data, FILE *reuse_file)
{
    FILE *file = NULL;
    int ret = 0;

    if (!sol_blob_ref(blob))
        return -ENOMEM;

    if (reuse_file) {
        file = reuse_file;
    } else {
        file = fopen(path, "r+e");
        if (!file) {
            SOL_WRN("Could not open memory file [%s]: %s", path,
                sol_util_strerrora(errno));
            goto error;
        }
    }

    if (fseek(file, entry->offset, SEEK_SET) < 0)
        goto error;

    if (mask) {
        uint64_t value = 0, old_value;
        uint32_t i, j;

        /* entry->size > 8 implies that no mask should be used */
        assert(entry->size <= 8);

        for (i = 0, j = 0; i < entry->size; i++, j += 8)
            value |= (uint64_t)((uint8_t *)blob->mem)[i] << j;

        ret = fread(&old_value, entry->size, 1, file);
        if (!ret || ferror(file) || feof(file)) {
            errno = EIO;
            goto error;
        }

        /* We just read from file, let's rewind */
        if (fseek(file, entry->offset, SEEK_SET) < 0)
            goto error;

        value <<= entry->bit_offset;
        value &= mask;
        value |= (old_value & ~mask);
        fwrite(&value, entry->size, 1, file);
    } else {
        fwrite(blob->mem, sol_min(entry->size, blob->size), 1, file);
    }

    if (ferror(file)) {
        errno = EIO;
        goto error;
    }

    errno = 0;
    if (!reuse_file)
        fclose(file);

    if (cb)
        cb((void *)data, name, blob, -errno);

    sol_blob_unref(blob);

    return -errno;

error:
    SOL_DBG("Error writing to file [%s]: %s", path, sol_util_strerrora(errno));
    ret = -errno;
    if (file && !reuse_file)
        fclose(file);

    if (cb)
        cb((void *)data, name, blob, ret);

    sol_blob_unref(blob);

    return ret;
}

static void
version_write_cb(void *data, const char *name, struct sol_blob *blob, int status)
{
    if (status < 0)
        SOL_WRN("Could not write version to file: %d", status);
    sol_blob_unref(blob);
}

static bool
check_version(struct map_internal *map_internal)
{
    uint8_t version = 0;
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(&version, sizeof(uint8_t),
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    const struct sol_memmap_entry *entry;
    uint64_t mask;
    struct sol_blob *blob;
    int ret;

    if (map_internal->checked)
        return true;

#ifndef SOL_NO_API_VERSION
    if (!map_internal->map->api_version || map_internal->map->api_version == UINT16_MAX) {
        SOL_WRN("Invalid memory_map_version. Should be between 1 and %" PRIu16 ". Found %" PRIu16,
            UINT16_MAX, map_internal->map->api_version);
        return false;
    }
#endif

    if (!get_entry_metadata_on_map(MEMMAP_VERSION_ENTRY, map_internal->map, &entry,
        &mask)) {

        SOL_WRN("No entry on memory map to property [%s]", MEMMAP_VERSION_ENTRY);
        return false;
    }

    ret = sol_memmap_read_raw_do(map_internal->resolved_path, entry, mask, &buf);
    if (ret >= 0 && (version == 0 || version == 255)) {
        blob = sol_blob_new(SOL_BLOB_TYPE_NOFREE, NULL, &map_internal->map->api_version, sizeof(uint16_t));
        SOL_NULL_CHECK(blob, false);

        /* No version on file, we should be initialising it */
        version = map_internal->map->api_version;
        if (sol_memmap_write_raw_do(map_internal->resolved_path, MEMMAP_VERSION_ENTRY, entry, mask, blob, version_write_cb, NULL, NULL) < 0) {
            SOL_WRN("Could not write current map version to file");
            sol_blob_unref(blob);
            return false;
        }
    } else if (ret < 0) {
        SOL_WRN("Could not read current map version");
        return false;
    }

    if (version != map_internal->map->api_version) {
        SOL_WRN("Memory map version mismatch. Expected %d but found %d",
            map_internal->map->api_version, version);
        return false;
    }

    map_internal->checked = true;
    return true;
}

static bool
perform_pending_writes(void *data)
{
    int i, r;
    struct pending_write_data *pending;
    FILE *file = NULL;
    struct map_internal *map_internal = data;
    struct sol_vector tmp_vector;

    map_internal->timeout = NULL;

    file = fopen(map_internal->resolved_path, "r+e");
    if (!file) {
        SOL_WRN("Error opening file [%s]: %s", map_internal->resolved_path,
            sol_util_strerrora(errno));
        return false;
    }

    tmp_vector = map_internal->pending_writes;
    sol_vector_init(&map_internal->pending_writes, sizeof(struct pending_write_data));

    SOL_VECTOR_FOREACH_IDX (&tmp_vector, pending, i) {
        sol_memmap_write_raw_do(map_internal->map->path, pending->name, pending->entry,
            pending->mask, pending->blob, pending->cb, pending->data, file);
        free(pending->name);
        sol_blob_unref(pending->blob);
    }
    sol_vector_clear(&tmp_vector);

    r = fclose(file);
    if (r)
        SOL_WRN("Error closing file [%s]: %s", map_internal->resolved_path,
            sol_util_strerrora(errno));

    SOL_DBG("Performed pending writes on [%s]", map_internal->resolved_path);

    return false;
}

static bool
replace_pending_write(struct sol_vector *pending_writes, const char *name,
    const struct sol_memmap_entry *entry, uint64_t mask, struct sol_blob *blob,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    struct pending_write_data *pending;
    int i;

    SOL_VECTOR_FOREACH_IDX (pending_writes, pending, i) {
        if (streq(pending->name, name)) {
            if (pending->cb)
                pending->cb((void *)pending->data, pending->name, pending->blob,
                    -ECANCELED);
            sol_blob_unref(pending->blob);
            pending->blob = sol_blob_ref(blob);
            if (!pending->blob) {
                /* If we couldn't ref, let's delete this entry and let
                 * the caller re-add this write */
                free(pending->name);
                sol_vector_del(pending_writes, i);
                return false;
            }
            pending->cb = cb;
            pending->data = data;

            return true;
        }
    }

    return false;
}

static int
fill_pending_write(struct pending_write_data *pending, const char *name,
    const struct sol_memmap_entry *entry, uint64_t mask, struct sol_blob *blob,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    pending->blob = sol_blob_ref(blob);
    SOL_NULL_CHECK(pending->blob, -ENOMEM);

    pending->name = strdup(name);
    SOL_NULL_CHECK(name, -ENOMEM);

    pending->entry = entry;
    pending->mask = mask;
    pending->cb = cb;
    pending->data = data;

    return 0;
}

static int
add_write(struct map_internal *map_internal, const char *name,
    const struct sol_memmap_entry *entry, uint64_t mask, struct sol_blob *blob,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    struct pending_write_data *pending;

    /* If there's a pending write for the very same entry, we replace it */
    if (replace_pending_write(&map_internal->pending_writes, name, entry, mask, blob, cb, data))
        return 0;

    pending = sol_vector_append(&map_internal->pending_writes);

    SOL_NULL_CHECK(pending, -ENOMEM);

    if (fill_pending_write(pending, name, entry, mask, blob, cb, data) < 0)
        return -ENOMEM;

    if (!map_internal->timeout)
        map_internal->timeout = sol_timeout_add(map_internal->map->timeout,
            perform_pending_writes, map_internal);

    SOL_NULL_CHECK(map_internal->timeout, -ENOMEM);

    return 0;
}

SOL_API int
sol_memmap_write_raw(const char *name, struct sol_blob *blob,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    struct map_internal *map_internal;
    const struct sol_memmap_entry *entry;
    uint64_t mask;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(blob, -EINVAL);

    if (!get_entry_metadata(name, &map_internal, &entry, &mask)) {
        SOL_WRN("No entry on memory map to property [%s]", name);
        return -ENOENT;
    }

    if (!check_version(map_internal))
        return -EINVAL;

    if (blob->size > entry->size)
        SOL_INF("Mapped size for [%s] is %zd, smaller than buffer contents: %zd",
            name, entry->size, blob->size);

    return add_write(map_internal, name, entry, mask, blob, cb, data);
}

static bool
read_from_pending(const char *name, struct sol_buffer *buffer)
{
    struct pending_write_data *pending;
    struct map_internal *map_internal;
    int i, j;

    SOL_VECTOR_FOREACH_IDX (&memory_maps, map_internal, i) {
        if (!map_internal->pending_writes.len)
            continue;

        SOL_VECTOR_FOREACH_IDX (&map_internal->pending_writes, pending, j) {
            if (streq(name, pending->name)) {
                // TODO maybe a sol_buffer_append_blob?
                if (sol_buffer_ensure(buffer, pending->blob->size) < 0) {
                    // TODO how bad is this? return old value? fail reading?
                    SOL_WRN("Could not ensure buffer size to fit pending blob");
                    return false;
                }
                memcpy(buffer->data, pending->blob->mem, pending->blob->size);
                return true;
            }
        }
    }

    return false;
}

SOL_API int
sol_memmap_read_raw(const char *name, struct sol_buffer *buffer)
{
    uint64_t mask;
    struct map_internal *map_internal;
    const struct sol_memmap_entry *entry;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(buffer, -EINVAL);

    if (!get_entry_metadata(name, &map_internal, &entry, &mask)) {
        SOL_WRN("No entry on memory map to property [%s]", name);
        return -ENOENT;
    }

    if (!check_version(map_internal))
        return -EINVAL;

    if (read_from_pending(name, buffer))
        return 0;

    return sol_memmap_read_raw_do(map_internal->resolved_path, entry, mask, buffer);
}

static bool
check_entry(const struct sol_memmap_map *map,
    const struct sol_memmap_entry *entry,
    const char **failed_entry)
{
    int32_t bit_start, bit_end, other_start, other_end;
    const struct sol_str_table_ptr *iter;
    const struct sol_memmap_entry *other;

    bit_start = (entry->offset * 8) + entry->bit_offset;
    bit_end = bit_start + (entry->bit_size ? : entry->size * 8) - 1;

    for (iter = map->entries; iter->key; iter++) {
        if (iter->val == entry) continue;
        other = iter->val;

        other_start = (other->offset * 8) + other->bit_offset;
        other_end = other_start + (other->bit_size ? : other->size * 8) - 1;

        if (!((bit_start > other_end) || (bit_end < other_start))) {
            *failed_entry = iter->key;
            return false;
        }
    }

    return true;
}

static bool
check_map(const struct sol_memmap_map *map)
{
    const struct sol_str_table_ptr *iter;
    const char *failed_entry;
    struct sol_memmap_entry *entry;
    uint32_t last_offset = 0;

    SOL_DBG("Checking memory map whose path is [%s]", map->path);

    /* First, calculate any offset that was not set */
    for (iter = map->entries; iter->key; iter++) {
        entry = (void *)iter->val;
        if (entry->bit_offset > 7) {
            SOL_WRN("Entry [%s] bit_offset greater than 7, found: %d",
                iter->key, entry->bit_offset);
            return false;
        }
        if (!entry->offset)
            entry->offset = last_offset;
        last_offset = entry->offset + entry->size;

        SOL_DBG("Entry [%s] starting on offset [%zu] with size [%zu]", iter->key,
            entry->offset, entry->size);
    }

    /* Now check for overlaps */
    for (iter = map->entries; iter->key; iter++) {
        if (!check_entry(map, iter->val, &failed_entry)) {
            SOL_WRN("Entry [%s] overlaps entry [%s] on map", iter->key,
                failed_entry);
            return false;
        }
    }

    return true;
}

#ifdef USE_I2C
static int
resolve_i2c_path(const char *path, char **resolved_path)
{
    char *rel_path = NULL, *dev_number_s = NULL, *dev_name = NULL, *end_ptr;
    unsigned int dev_number;
    struct sol_vector instructions;
    struct sol_buffer result_path = SOL_BUFFER_INIT_EMPTY;
    struct sol_str_slice command = sol_str_slice_from_str(path);
    int ret = -EINVAL;

    instructions = sol_str_slice_split(command, ",", 5);
    if (instructions.len < 5) {
        SOL_WRN("Invalid create device path. Expected 'create,i2c,<rel_path>,"
            "<devnumber>,<devname>'");
        goto end;
    }

    rel_path = sol_str_slice_to_string(
        *(const struct sol_str_slice *)sol_vector_get(&instructions, REL_PATH_IDX));
    SOL_NULL_CHECK_GOTO(rel_path, end);

    dev_number_s = sol_str_slice_to_string(
        *(const struct sol_str_slice *)sol_vector_get(&instructions, DEV_NUMBER_IDX));
    SOL_NULL_CHECK_GOTO(dev_number_s, end);

    errno = 0;
    dev_number = strtoul(dev_number_s, &end_ptr, 0);
    if (errno || *end_ptr != '\0')
        goto end;

    dev_name = sol_str_slice_to_string(
        *(const struct sol_str_slice *)sol_vector_get(&instructions, DEV_NAME_IDX));
    SOL_NULL_CHECK_GOTO(dev_name, end);

    ret = sol_i2c_create_device(rel_path, dev_name, dev_number,
        &result_path);

    if (ret >= 0 || ret == -EEXIST) {
        const struct sol_str_slice ending = SOL_STR_SLICE_LITERAL("/eeprom");
        struct timespec start;
        struct stat st;

        ret = sol_buffer_append_slice(&result_path, ending);
        if (ret < 0)
            goto end;

        *resolved_path = sol_buffer_steal(&result_path, NULL);

        ret = 0;
        start = sol_util_timespec_get_current();
        while (stat(*resolved_path, &st)) {
            struct timespec elapsed, now = sol_util_timespec_get_current();

            sol_util_timespec_sub(&now, &start, &elapsed);
            /* Let's wait up to one second */
            if (elapsed.tv_sec > 0) {
                ret = -ENODEV;
                goto end;
            }

            sched_yield();
        }
    }

end:
    free(rel_path);
    free(dev_number_s);
    free(dev_name);
    sol_vector_clear(&instructions);

    return ret;
}
#endif

static char *
resolve_map_path(const struct sol_memmap_map *map)
{
#ifdef USE_I2C
    char *resolved_path;

    if (strstartswith(map->path, "create,i2c,")) {
        if (resolve_i2c_path(map->path, &resolved_path) < 0) {
            SOL_WRN("Could not create i2c EEPROM device using command [%s]", map->path);
            goto error;
        }

        return resolved_path;
    }

    return strdup(map->path);

error:
    return NULL;
#else
    return strdup(map->path);
#endif
}

SOL_API int
sol_memmap_add_map(const struct sol_memmap_map *map)
{
    struct map_internal *map_internal;

    SOL_NULL_CHECK(map, -EINVAL);

    if (!check_map(map)) {
        SOL_WRN("Invalid memory map. Map->path: [%s]", map->path);
        return -EINVAL;
    }

    map_internal = sol_vector_append(&memory_maps);
    SOL_NULL_CHECK(map_internal, -ENOMEM);

    map_internal->map = map;
    map_internal->resolved_path = resolve_map_path(map);
    SOL_NULL_CHECK_GOTO(map_internal, error);

    sol_vector_init(&map_internal->pending_writes, sizeof(struct pending_write_data));

    return 0;

error:
    sol_vector_del_last(&memory_maps);
    return -ENOMEM;
}

SOL_API int
sol_memmap_remove_map(const struct sol_memmap_map *map)
{
    struct map_internal *map_internal;
    int i;

    SOL_NULL_CHECK(map, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&memory_maps, map_internal, i) {
        if (map_internal->map == map) {
            if (map_internal->timeout) {
                sol_timeout_del(map_internal->timeout);
                perform_pending_writes(map_internal);
            }
            return sol_vector_del(&memory_maps, i);
        }
    }

    return -ENOENT;
}

SOL_API bool
sol_memmap_set_timeout(struct sol_memmap_map *map, uint32_t timeout)
{
    struct map_internal *map_internal;
    int i;

    SOL_NULL_CHECK(map, false);

    /* Rememeber, as we may have a copy of map (due to device resolving),
     * we need to update our proper copy */
    SOL_VECTOR_FOREACH_IDX (&memory_maps, map_internal, i) {
        if (map_internal->map == map) {
            map->timeout = timeout;
            return true;
        }
    }

    SOL_WRN("Map %p was not previously added. Call 'sol_memmap_add_map' before.",
        map);
    return false;
}

SOL_API uint32_t
sol_memmap_get_timeout(const struct sol_memmap_map *map)
{
    struct map_internal *map_internal;
    int i;

    SOL_NULL_CHECK(map, false);

    /* Rememeber, as we may have a copy of map (due to device resolving),
     * we need to check our proper copy */
    SOL_VECTOR_FOREACH_IDX (&memory_maps, map_internal, i) {
        if (map_internal->map == map) {
            return map->timeout;
        }
    }

    SOL_WRN("Map %p was not previously added. Call 'sol_memmap_add_map' before.",
        map);
    return 0;
}
