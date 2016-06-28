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

#include <zephyr.h>
#include <flash.h>
#include <device.h>

#include "sol-memmap-storage-impl.h"

#include "sol-buffer.h"
#include "sol-mainloop.h"
#include "sol-str-slice.h"
#include "sol-str-table.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

struct map_internal_zephyr {
    struct map_internal base;
    struct device *flash_dev;
    unsigned int min_erase_sz, max_rw_sz, mem_offset; // in bytes
};

int
sol_memmap_impl_read_raw(struct map_internal *map,
    const struct sol_memmap_entry *entry,
    uint64_t mask,
    struct sol_buffer *buffer)
{
    struct map_internal_zephyr *map_internal =
        (struct map_internal_zephyr *)map;
    size_t offset, times = 0, extra = 0;
    uint64_t value = 0;
    int r;

    offset = map_internal->mem_offset + entry->offset;

    /* entry->size may be bigger than the overall useful data on bit
     * mask cases, but we have to read the whole area anyway */
    r = sol_buffer_ensure(buffer, entry->size);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    if (entry->size > map_internal->max_rw_sz) {
        times = entry->size / map_internal->max_rw_sz;
        extra = entry->size % map_internal->max_rw_sz;
    }

    if (!(times || extra)) {
        r = flash_read(map_internal->flash_dev, offset, buffer->data,
            entry->size);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    } else {
        size_t off = 0;

        while (times) {
            r = flash_read(map_internal->flash_dev, offset + off,
                buffer->data + off, map_internal->max_rw_sz);
            SOL_INT_CHECK_GOTO(r, < 0, err);
            off += map_internal->max_rw_sz;
            times--;
        }
        if (extra) {
            r = flash_read(map_internal->flash_dev, offset + off,
                buffer->data + off, extra);
            SOL_INT_CHECK_GOTO(r, < 0, err);
        }
    }

    if (mask)
        fill_buffer_using_mask(value, mask, entry, buffer);

    buffer->used = entry->size;

    return 0;

err:
    sol_buffer_fini(buffer);
    SOL_WRN("Flash read failed");
    return r;
}

/* W25QXXDV memories won't write to any memory region that is not
 * erased, and the minimum erasable block size is
 * map_internal->min_erase_sz (also aligned to that size). We have to
 * read back that much bytes before any write, and we chose the heap
 * to do so. */
int
sol_memmap_impl_write_raw(struct map_internal *map,
    const char *name,
    const struct sol_memmap_entry *entry,
    uint64_t mask,
    struct sol_blob *blob,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    int r = 0;
    uint8_t *back_mem;
    size_t offset, mod, back_sz, cnt;
    struct map_internal_zephyr *map_internal =
        (struct map_internal_zephyr *)map;

    if (!sol_blob_ref(blob))
        return -errno;

    offset = map_internal->mem_offset + entry->offset;
    mod = offset % map_internal->min_erase_sz;

    /* check if we're spamming > 1 erase blocks */
    back_sz = map_internal->min_erase_sz *
        (1 + ((mod + entry->offset) / map_internal->min_erase_sz));

    back_mem = malloc(back_sz);
    if (!back_mem) {
        r = -ENOMEM;
        goto error;
    }

    for (cnt = 0; cnt < back_sz; cnt += map_internal->max_rw_sz) {
        r = flash_read(map_internal->flash_dev, offset - mod + cnt,
            back_mem + cnt, map_internal->max_rw_sz);
        SOL_INT_CHECK_GOTO(r, < 0, error);
    }

    r = flash_write_protection_set(map_internal->flash_dev, false);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    r = flash_erase(map_internal->flash_dev, offset - mod, back_sz);
    if (r < 0) {
        SOL_WRN("Flash erase (before write) failed");
        goto error;
    }

    if (mask) {
        uint64_t value = 0, old_value;
        uint32_t i, j;

        /* entry->size > 8 implies that no mask should be used */
        assert(entry->size <= 8);

        for (i = 0, j = 0; i < entry->size; i++, j += 8)
            value |= (uint64_t)((uint8_t *)blob->mem)[i] << j;

        memcpy(&old_value, back_mem + mod, entry->size);

        value <<= entry->bit_offset;
        value &= mask;
        value |= (old_value & ~mask);

        memcpy(back_mem + mod, &value, entry->size);

        r = flash_write_protection_set(map_internal->flash_dev, false);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        for (cnt = 0; cnt < back_sz; cnt += map_internal->max_rw_sz) {
            r = flash_write(map_internal->flash_dev, offset - mod + cnt,
                back_mem + cnt, map_internal->max_rw_sz);
            if (r < 0) {
                SOL_WRN("Flash write failed");
                goto error;
            }
            r = flash_write_protection_set(map_internal->flash_dev, false);
            SOL_INT_CHECK_GOTO(r, < 0, error);
        }
    } else {
        size_t size;

        if (blob->size > entry->size) {
            SOL_WRN("Trying to store entry data of size %zu bytes to a entry "
                "with %zu bytes of reserved space", blob->size, entry->size);
        }
        size = sol_util_min(entry->size, blob->size);

        memcpy(back_mem + mod, blob->mem, size);

        r = flash_write_protection_set(map_internal->flash_dev, false);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        for (cnt = 0; cnt < back_sz; cnt += map_internal->max_rw_sz) {
            r = flash_write(map_internal->flash_dev, offset - mod + cnt,
                back_mem + cnt, map_internal->max_rw_sz);
            if (r < 0) {
                SOL_WRN("Flash write failed");
                goto error;
            }
            r = flash_write_protection_set(map_internal->flash_dev, false);
            SOL_INT_CHECK_GOTO(r, < 0, error);
        }
    }

    if (cb)
        cb((void *)data, name, blob, r);

    sol_blob_unref(blob);

    return r;

error:
    SOL_DBG("Error writing to memmap: %s", sol_util_strerrora(-r));

    if (cb)
        cb((void *)data, name, blob, r);

    sol_blob_unref(blob);

    return r;
}

bool
sol_memmap_impl_perform_pending_writes(void *data)
{
    struct map_internal_zephyr *map_internal = data;
    struct pending_write_data *pending;
    struct sol_vector tmp_vector;
    int i;

    //sol-memmap-storage.c only checks the pointer to know what to do
    //on the next round
    map_internal->base.timeout = NULL;

    tmp_vector = map_internal->base.pending_writes;
    sol_vector_init(&map_internal->base.pending_writes,
        sizeof(struct pending_write_data));

    SOL_VECTOR_FOREACH_IDX (&tmp_vector, pending, i) {
        sol_memmap_impl_write_raw((struct map_internal *)map_internal,
            pending->name, pending->entry, pending->mask, pending->blob,
            pending->cb, pending->data);
        sol_blob_unref(pending->blob);
    }
    sol_vector_clear(&tmp_vector);

    SOL_DBG("Performed pending writes");

    return false;
}

int
sol_memmap_impl_init(void)
{
    return 0;
}

#define DRIVER_NAME_IDX 0
#define MIN_ERASE_SZ_IDX 1
#define MAX_RW_SZ_IDX 2
#define MEM_OFFSET_IDX 3

static int
resolve_map_path(const struct sol_memmap_map *map,
    struct map_internal_zephyr *map_internal,
    char **driver_name)
{
    char *min_erase_size_s = NULL, *max_rw_size_s = NULL, *mem_offset_s = NULL,
    *end_ptr;
    unsigned int min_erase_size, max_rw_size;
    struct sol_vector instructions;
    struct sol_str_slice command;
    int ret = 0;

    SOL_NULL_CHECK(driver_name, -EINVAL);
    *driver_name = NULL;

    command = sol_str_slice_from_str(map->path);

    instructions = sol_str_slice_split(command, ",", 4);
    if (instructions.len < 4)
        goto err_parse;

    *driver_name = sol_str_slice_to_str(
        *(const struct sol_str_slice *)sol_vector_get(&instructions,
        DRIVER_NAME_IDX));
    SOL_NULL_CHECK_GOTO(*driver_name, err_parse);

    min_erase_size_s = sol_str_slice_to_str(
        *(const struct sol_str_slice *)sol_vector_get(&instructions,
        MIN_ERASE_SZ_IDX));
    SOL_NULL_CHECK_GOTO(*min_erase_size_s, err_parse);

    errno = 0;
    min_erase_size = strtoul(min_erase_size_s, &end_ptr, 0);
    if (errno) {
        ret = -errno;
        goto err_parse;
    }
    if (*end_ptr != '\0') {
        for (const char *p = end_ptr; *p; p++) {
            if (!isspace((uint8_t)*p))
                goto err_parse;
        }
    }
    map_internal->min_erase_sz = align_power2(min_erase_size / 2 + 1);

    max_rw_size_s = sol_str_slice_to_str(
        *(const struct sol_str_slice *)sol_vector_get(&instructions,
        MAX_RW_SZ_IDX));
    SOL_NULL_CHECK_GOTO(max_rw_size_s, err_parse);

    errno = 0;
    max_rw_size = strtoul(max_rw_size_s, &end_ptr, 0);
    if (errno) {
        ret = -errno;
        goto err_parse;
    }
    if (*end_ptr != '\0') {
        for (const char *p = end_ptr; *p; p++) {
            if (!isspace((uint8_t)*p))
                goto err_parse;
        }
    }
    map_internal->max_rw_sz = align_power2(max_rw_size / 2 + 1);

    mem_offset_s = sol_str_slice_to_str(
        *(const struct sol_str_slice *)sol_vector_get(&instructions,
        MEM_OFFSET_IDX));
    SOL_NULL_CHECK_GOTO(mem_offset_s, err_parse);

    errno = 0;
    map_internal->mem_offset = strtoul(mem_offset_s, &end_ptr, 0);
    if (errno) {
        ret = -errno;
        goto err_parse;
    }
    if (*end_ptr != '\0') {
        for (const char *p = end_ptr; *p; p++) {
            if (!isspace((uint8_t)*p))
                goto err_parse;
        }
    }

    free(min_erase_size_s);
    free(max_rw_size_s);
    free(mem_offset_s);
    sol_vector_clear(&instructions);
    return ret;

err_parse:
    free(*driver_name);
    free(min_erase_size_s);
    free(max_rw_size_s);
    free(mem_offset_s);
    sol_vector_clear(&instructions);
    SOL_WRN("Invalid create device path. Expected "
        "'<driver_name>,<min_erase_size>,<max_rw_size>,<mem_offset>'");
    return ret;
}

struct map_internal *
sol_memmap_impl_map_new(const struct sol_memmap_map *map)
{
    struct map_internal_zephyr *map_internal;
    char *driver_name;
    int r;

    SOL_NULL_CHECK_ERRNO(map, EINVAL, NULL);

    map_internal = calloc(1, sizeof(*map_internal));
    SOL_NULL_CHECK_ERRNO(map, ENOMEM, NULL);

    map_internal->base.map = map;
    r = resolve_map_path(map_internal->base.map, map_internal, &driver_name);
    if (r < 0) {
        errno = -r;
        free(map_internal);
        return NULL;
    }

    map_internal->flash_dev = device_get_binding(driver_name);
    if (!map_internal->flash_dev) {
        SOL_WRN("SPI flash driver was not found!\n");
        free(driver_name);
        sol_memmap_impl_map_del((struct map_internal *)map_internal);
        errno = ENOSYS;
        return NULL;
    }

    free(driver_name);
    return (struct map_internal *)map_internal;
}

void
sol_memmap_impl_map_del(struct map_internal *map)
{
    struct map_internal_zephyr *map_internal =
        (struct map_internal_zephyr *)map;

    SOL_NULL_CHECK(map_internal);
    free(map_internal);
}
