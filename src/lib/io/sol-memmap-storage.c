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
#include "sol-util.h"
#include "sol-util-file.h"
#include "sol-vector.h"

#ifdef USE_I2C
#include <sol-i2c.h>
#endif

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define REL_PATH_IDX 2
#define DEV_NUMBER_IDX 3
#define DEV_NAME_IDX 4

struct map_resolved_path {
    const struct sol_memmap_map *map;
    char *resolved_path;
};

static struct sol_ptr_vector memory_maps = SOL_PTR_VECTOR_INIT;
static struct sol_ptr_vector checked_maps = SOL_PTR_VECTOR_INIT;
static struct sol_ptr_vector resolved_maps_to_free = SOL_PTR_VECTOR_INIT;

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
get_entry_metadata(const char *name, const struct sol_memmap_map **map, const struct sol_memmap_entry **entry, uint64_t *mask)
{
    int i;

    SOL_PTR_VECTOR_FOREACH_IDX (&memory_maps, *map, i) {
        if (get_entry_metadata_on_map(name, *map, entry, mask))
            return true;
    }

    entry = NULL;
    map = NULL;

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
        SOL_WRN("Could not open memory file [%s]", path);
        return -errno;
    }

    if (lseek(fd, entry->offset, SEEK_SET) < 0)
        goto error;

    if (sol_util_fill_buffer(fd, buffer, entry->size) < 0)
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
    ret = -errno;
    close(fd);

    return ret;
}

static int
sol_memmap_write_raw_do(const char *path, const struct sol_memmap_entry *entry, uint64_t mask, const struct sol_buffer *buffer)
{
    FILE *file;
    int ret = 0;

    file = fopen(path, "r+e");
    if (!file) {
        SOL_WRN("Could not open memory file [%s]", path);
        return -errno;
    }

    if (fseek(file, entry->offset, SEEK_SET) < 0)
        goto error;

    if (mask) {
        uint64_t value = 0, old_value;
        uint32_t i, j;

        for (i = 0, j = 0; i < entry->size; i++, j += 8)
            value |= (uint64_t)((uint8_t *)buffer->data)[i] << j;

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
        fwrite(buffer->data, MIN(entry->size, buffer->used), 1, file);
    }

    if (ferror(file)) {
        errno = EIO;
        goto error;
    }

    if (fclose(file) != 0)
        return -errno;

    return 0;

error:
    ret = -errno;
    fclose(file);

    return ret;
}

static bool
check_version(const struct sol_memmap_map *map)
{
    uint8_t version = 0;
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(&version, sizeof(uint8_t),
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    struct sol_memmap_map *iter;
    const struct sol_memmap_entry *entry;
    int ret, i;
    uint64_t mask;

    if (!map->version) {
        SOL_WRN("Invalid memory_map_version. Should not be zero");
        return false;
    }

    /* Check if already checked.
     * TODO Maybe have a hash on soletta?*/
    SOL_PTR_VECTOR_FOREACH_IDX (&checked_maps, iter, i)
        if (iter == map) return true;

    if (!get_entry_metadata_on_map(MEMMAP_VERSION_ENTRY, map, &entry, &mask)) {
        SOL_WRN("No entry on memory map to property [%s]", MEMMAP_VERSION_ENTRY);
        return false;
    }

    ret = sol_memmap_read_raw_do(map->path, entry, mask, &buf);
    if (ret >= 0 && version == 0) {
        /* No version on file, we should be initialising it */
        version = map->version;
        if (sol_memmap_write_raw_do(map->path, entry, mask, &buf) < 0) {
            SOL_WRN("Could not write current map version to file");
            return false;
        }
    } else if (ret < 0) {
        SOL_WRN("Could not read current map version");
        return false;
    }

    if (version != map->version) {
        SOL_WRN("Memory map version mismatch. Expected %d but found %d",
            map->version, version);
        return false;
    }

    return sol_ptr_vector_append(&checked_maps, (void *)map) == 0;
}

SOL_API int
sol_memmap_write_raw(const char *name, const struct sol_buffer *buffer)
{
    const struct sol_memmap_map *map;
    const struct sol_memmap_entry *entry;
    uint64_t mask;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(buffer, -EINVAL);

    if (!get_entry_metadata(name, &map, &entry, &mask)) {
        SOL_WRN("No entry on memory map to property [%s]", name);
        return -ENOENT;
    }

    if (!check_version(map))
        return -EINVAL;

    if (buffer->used > entry->size)
        SOL_INF("Mapped size for [%s] is %zd, smaller than buffer contents: %zd",
            name, entry->size, buffer->used);

    return sol_memmap_write_raw_do(map->path, entry, mask, buffer);
}

SOL_API int
sol_memmap_read_raw(const char *name, struct sol_buffer *buffer)
{
    uint64_t mask;
    const struct sol_memmap_map *map;
    const struct sol_memmap_entry *entry;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(buffer, -EINVAL);

    if (!get_entry_metadata(name, &map, &entry, &mask)) {
        SOL_WRN("No entry on memory map to property [%s]", name);
        return -ENOENT;
    }

    if (!check_version(map))
        return -EINVAL;

    return sol_memmap_read_raw_do(map->path, entry, mask, buffer);
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

    SOL_DBG("Using memory file at [%s]", map->path);

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
resolve_i2c_path(const char *path, struct sol_memmap_map *map)
{
    char *rel_path = NULL, *dev_number_s = NULL, *dev_name = NULL, *end_ptr;
    unsigned int dev_number;
    struct sol_vector instructions;
    struct sol_buffer result_path = SOL_BUFFER_INIT_EMPTY;
    struct sol_str_slice command = sol_str_slice_from_str(path);
    int ret = -EINVAL;

    instructions = sol_util_str_split(command, ",", 5);
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

        map->path = sol_buffer_steal(&result_path, NULL);

        ret = 0;
        start = sol_util_timespec_get_current();
        while (stat(map->path, &st)) {
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

static struct sol_memmap_map *
resolve_map(const struct sol_memmap_map *map)
{
#ifdef USE_I2C
    struct sol_memmap_map *resolved_map = NULL;
    int r;

    if (strstartswith(map->path, "create,i2c,")) {
        resolved_map = calloc(1, sizeof(struct sol_memmap_map));
        SOL_NULL_CHECK(resolved_map, NULL);

        resolved_map->version = map->version;
        resolved_map->entries = map->entries;

        if (resolve_i2c_path(map->path, resolved_map) < 0) {
            SOL_WRN("Could not create i2c EEPROM device using command [%s]", map->path);
            goto error;
        }

        r = sol_ptr_vector_append(&resolved_maps_to_free, resolved_map);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        return resolved_map;
    }

    return (struct sol_memmap_map *)map;

error:
    free(resolved_map);
    return NULL;
#else
    return (struct sol_memmap_map *)map;
#endif
}

SOL_API int
sol_memmap_add_map(const struct sol_memmap_map *map)
{
    struct sol_memmap_map *resolved_map;
    int r;

    SOL_NULL_CHECK(map, -EINVAL);

    resolved_map = resolve_map(map);
    SOL_NULL_CHECK(resolved_map, -EINVAL);

    if (!check_map(resolved_map)) {
        SOL_WRN("Invalid memory map. Map->path: [%s]", resolved_map->path);
        return -EINVAL;
    }

    r = sol_ptr_vector_append(&memory_maps, (void *)resolved_map);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

SOL_API int
sol_memmap_remove_map(const struct sol_memmap_map *map)
{
    struct sol_memmap_map *iter;
    int i;

    SOL_NULL_CHECK(map, -EINVAL);

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&resolved_maps_to_free, iter, i) {
        if (iter->entries == map->entries) {
            sol_ptr_vector_del(&resolved_maps_to_free, i);
            free(iter);
            break;
        }
    }

    return sol_ptr_vector_remove(&memory_maps, map);
}
