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

#include <stdbool.h>

#define SOL_LOG_DOMAIN &_sol_memmap_storage_log_domain
extern struct sol_log_domain _sol_memmap_storage_log_domain;
#include "sol-log-internal.h"
#include "sol-memmap-storage.h"

struct pending_write_data {
    const char *name;
    struct sol_blob *blob;
    const struct sol_memmap_entry *entry;
    void (*cb)(void *data, const char *name, struct sol_blob *blob, int status);
    const void *data;
    uint64_t mask;
};

struct map_internal {
    const struct sol_memmap_map *map;
    struct sol_timeout *timeout;
    struct sol_vector pending_writes;
    bool checked;
};

int sol_memmap_impl_read_raw(struct map_internal *map_internal, const struct sol_memmap_entry *entry, uint64_t mask, struct sol_buffer *buffer);

int sol_memmap_impl_write_raw(struct map_internal *map_internal, const char *name, const struct sol_memmap_entry *entry, uint64_t mask, struct sol_blob *blob, void (*cb)(void *data, const char *name, struct sol_blob *blob, int status), const void *data);

bool sol_memmap_impl_perform_pending_writes(void *data);
int sol_memmap_impl_init(void);
struct map_internal *sol_memmap_impl_map_new(const struct sol_memmap_map *map);
void sol_memmap_impl_map_del(struct map_internal *map_internal);

void fill_buffer_using_mask(uint32_t value, uint64_t mask, const struct sol_memmap_entry *entry, struct sol_buffer *buffer);
