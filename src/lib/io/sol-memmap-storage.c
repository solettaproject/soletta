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

#include "sol-util-internal.h"

#include "sol-memmap-storage-impl.h"

struct sol_log_domain _sol_memmap_storage_log_domain;

#include "sol-buffer.h"
#include "sol-mainloop.h"
#include "sol-vector.h"

static struct sol_ptr_vector memory_maps = SOL_PTR_VECTOR_INIT;
static bool initialised = false;

static bool
get_entry_metadata_on_map(const char *name,
    const char **out_name,
    const struct sol_memmap_map *map,
    const struct sol_memmap_entry **entry,
    uint64_t *mask)
{
    uint32_t bit_size;
    const struct sol_str_table_ptr *table_entry = sol_str_table_ptr_entry_lookup
            (map->entries, sol_str_slice_from_str(name));

    if (table_entry) {
        *entry = table_entry->val;
        if (out_name)
            *out_name = table_entry->key;
        bit_size = (*entry)->bit_size;
        /* No mask if bit_size equal or greater than 64. Such data
         * should not be read as an int */
        if (bit_size && (bit_size != (*entry)->size * 8) && bit_size < 64)
            *mask = (((uint64_t)1 << bit_size) - 1) << (*entry)->bit_offset;
        else
            *mask = 0;

        return true;
    }

    return false;
}

static bool
get_entry_metadata(const char *name,
    const char **out_name,
    struct map_internal **map_internal,
    const struct sol_memmap_entry **entry,
    uint64_t *mask)
{
    int i;

    SOL_PTR_VECTOR_FOREACH_IDX (&memory_maps, *map_internal, i) {
        if (get_entry_metadata_on_map(name, out_name, (*map_internal)->map,
            entry, mask))
            return true;
    }

    *entry = NULL;
    *map_internal = NULL;
    if (out_name)
        *out_name = NULL;

    return false;
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

    if (!map_internal->map->version || map_internal->map->version == UINT8_MAX) {
        SOL_WRN("Invalid memory_map_version. Should be between 1 and %" PRIu8 ". Found %" PRIu8,
            UINT8_MAX, map_internal->map->version);
        return false;
    }

    if (!get_entry_metadata_on_map
            (SOL_MEMMAP_VERSION_ENTRY, NULL, map_internal->map, &entry,
        &mask)) {

        SOL_WRN("No entry on memory map to property [%s]", SOL_MEMMAP_VERSION_ENTRY);
        return false;
    }

    ret = sol_memmap_impl_read_raw(map_internal, entry, mask, &buf);
    if (ret >= 0 && (version == 0 || version == 255)) {
        blob = sol_blob_new(&SOL_BLOB_TYPE_NO_FREE_DATA, NULL, &map_internal->map->version, sizeof(uint16_t));
        SOL_NULL_CHECK(blob, false);

        /* No version on file, we should be initialising it */
        version = map_internal->map->version;
        if ((ret = sol_memmap_impl_write_raw(map_internal,
                SOL_MEMMAP_VERSION_ENTRY, entry, mask, blob, version_write_cb,
                NULL)) < 0) {
            SOL_WRN("Could not write current map version (path is %s): %s",
                map_internal->map->path, sol_util_strerrora(-ret));
            sol_blob_unref(blob);
            return false;
        }
    } else if (ret < 0) {
        SOL_WRN("Could not read current map version (path is %s): %s",
            map_internal->map->path, sol_util_strerrora(-ret));
        return false;
    }

    if (version != map_internal->map->version) {
        SOL_WRN("Memory map version mismatch. Expected %" PRIu8 "but found %" PRIu8,
            map_internal->map->version, version);
        return false;
    }

    map_internal->checked = true;
    return true;
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

    pending->name = name;
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
            sol_memmap_impl_perform_pending_writes, map_internal);

    SOL_NULL_CHECK(map_internal->timeout, -ENOMEM);

    return 0;
}

SOL_API int
sol_memmap_write_raw(const char *name, struct sol_blob *blob,
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status),
    const void *data)
{
    const struct sol_memmap_entry *entry;
    struct map_internal *map_internal;
    const char *entry_name;
    uint64_t mask;

    SOL_NULL_CHECK(name, -EINVAL);
    SOL_NULL_CHECK(blob, -EINVAL);

    if (!get_entry_metadata(name, &entry_name, &map_internal, &entry, &mask)) {
        SOL_WRN("No entry on memory map to property [%s]", name);
        return -ENOENT;
    }

    if (!check_version(map_internal))
        return -EINVAL;

    if (blob->size > entry->size)
        SOL_INF("Mapped size for [%s] is %zd, smaller than buffer contents: %zd",
            name, entry->size, blob->size);

    return add_write(map_internal, entry_name, entry, mask, blob, cb, data);
}

static bool
read_from_pending(const char *name, struct sol_buffer *buffer)
{
    struct pending_write_data *pending;
    struct map_internal *map_internal;
    int i, j;

    SOL_PTR_VECTOR_FOREACH_IDX (&memory_maps, map_internal, i) {
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

    if (!get_entry_metadata(name, NULL, &map_internal, &entry, &mask)) {
        SOL_WRN("No entry on memory map to property [%s]", name);
        return -ENOENT;
    }

    if (!check_version(map_internal))
        return -EINVAL;

    if (read_from_pending(name, buffer))
        return 0;

    return sol_memmap_impl_read_raw(map_internal, entry, mask, buffer);
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

static int
check_map(const struct sol_memmap_map *map)
{
    const struct sol_str_table_ptr *iter;
    struct sol_memmap_entry *entry;
    const char *failed_entry;
    uint32_t last_offset = 0;

    SOL_DBG("Checking memory map whose path is [%s]", map->path);

    /* First, calculate any offset that was not set */
    for (iter = map->entries; iter->key; iter++) {
        entry = (void *)iter->val;
        if (entry->bit_offset > 7) {
            SOL_WRN("Entry [%s] with bit_offset greater than 7 found: %d",
                iter->key, entry->bit_offset);
            return -EINVAL;
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
            return -EINVAL;
        }
    }

    return 0;
}

SOL_API int
sol_memmap_add_map(const struct sol_memmap_map *map)
{
    struct map_internal *map_internal;
    int r;

    SOL_NULL_CHECK(map, -EINVAL);

    if (!initialised) {
        r = sol_memmap_impl_init();
        SOL_INT_CHECK(r, != 0, r);
    }

    r = check_map(map);
    if (r < 0) {
        SOL_WRN("Invalid memory map. Map->path: [%s]", map->path);
        return r;
    }

    map_internal = sol_memmap_impl_map_new(map);
    SOL_NULL_CHECK(map_internal, -errno);

    r = sol_ptr_vector_append(&memory_maps, map_internal);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    sol_vector_init(&map_internal->pending_writes, sizeof(struct pending_write_data));

    return 0;

error:
    sol_memmap_impl_map_del(map_internal);
    return r;
}

SOL_API int
sol_memmap_remove_map(const struct sol_memmap_map *map)
{
    struct map_internal *map_internal;
    int i;

    SOL_NULL_CHECK(map, -EINVAL);

    SOL_PTR_VECTOR_FOREACH_IDX (&memory_maps, map_internal, i) {
        if (map_internal->map == map) {
            if (map_internal->timeout) {
                sol_timeout_del(map_internal->timeout);
                sol_memmap_impl_perform_pending_writes(map_internal);
            }
            sol_memmap_impl_map_del(map_internal);
            return sol_ptr_vector_del(&memory_maps, i);
        }
    }

    return -ENOENT;
}

SOL_API int
sol_memmap_set_timeout(struct sol_memmap_map *map, uint32_t timeout)
{
    struct map_internal *map_internal;
    int i;

    SOL_NULL_CHECK(map, -EINVAL);

    /* Rememeber, as we may have a copy of map (due to device resolving),
     * we need to update our proper copy */
    SOL_PTR_VECTOR_FOREACH_IDX (&memory_maps, map_internal, i) {
        if (map_internal->map == map) {
            map->timeout = timeout;
            return 0;
        }
    }

    SOL_WRN("Map %p was not previously added. Call 'sol_memmap_add_map' before.",
        map);
    return -ENOENT;
}

SOL_API uint32_t
sol_memmap_get_timeout(const struct sol_memmap_map *map)
{
    struct map_internal *map_internal;
    int i;

    SOL_NULL_CHECK(map, false);

    /* Rememeber, as we may have a copy of map (due to device resolving),
     * we need to check our proper copy */
    SOL_PTR_VECTOR_FOREACH_IDX (&memory_maps, map_internal, i) {
        if (map_internal->map == map) {
            return map->timeout;
        }
    }

    SOL_WRN("Map %p was not previously added. Call 'sol_memmap_add_map' before.",
        map);
    return 0;
}

void
fill_buffer_using_mask(uint32_t value,
    uint64_t mask,
    const struct sol_memmap_entry *entry,
    struct sol_buffer *buffer)
{
    uint32_t i, j;

    for (i = 0, j = 0; i < entry->size; i++, j += 8)
        value |= (uint64_t)((uint8_t *)buffer->data)[i] << j;

    value &= mask;
    value >>= entry->bit_offset;

    memset(buffer->data, 0, buffer->capacity);
    for (i = 0; i < entry->size; i++, value >>= 8)
        ((uint8_t *)buffer->data)[i] = value & 0xff;
}
