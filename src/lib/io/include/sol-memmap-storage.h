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
#include "sol-str-table.h"
#include "sol-types.h"
#include "sol-log.h"

#define MEMMAP_VERSION_ENTRY "_version"

#define SOL_MEMMAP_ENTRY(_name, _offset, _size) \
    SOL_STR_TABLE_PTR_ITEM(_name, &((struct sol_memmap_entry){.offset = (_offset), .size = (_size) }))

#define SOL_MEMMAP_BOOL_ENTRY(_name, _offset, _bit_offset) \
    SOL_STR_TABLE_PTR_ITEM(_name, &((struct sol_memmap_entry){.offset = (_offset), .size = 1, .bit_offset = (_bit_offset), .is_bool = true }))

struct sol_memmap_map {
    uint8_t version;
    char *path;
    struct sol_str_table_ptr entries[];
};

struct sol_memmap_entry {
    size_t offset;
    size_t size;
    uint8_t bit_offset;
    bool is_bool;
};

int sol_memmap_write_raw(const char *name, struct sol_buffer *buffer);
int sol_memmap_read_raw(const char *name, struct sol_buffer *buffer);
int sol_memmap_add_map(const struct sol_memmap_map *map);
int sol_memmap_remove_map(const struct sol_memmap_map *map);

#define CREATE_BUFFER(_val, _empty) \
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(_val, \
    sizeof(*(_val)), SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED); \
    buf.used = (_empty) ? 0 : sizeof(*(_val));

static inline int
sol_memmap_read_uint8(const char *name, uint8_t *value)
{
    CREATE_BUFFER(value, true);

    return sol_memmap_read_raw(name, &buf);
}

static inline int
sol_memmap_write_uint8(const char *name, uint8_t value)
{
    CREATE_BUFFER(&value, false);

    return sol_memmap_write_raw(name, &buf);
}

static inline int
sol_memmap_read_bool(const char *name, bool *value)
{
    CREATE_BUFFER(value, true);

    return sol_memmap_read_raw(name, &buf);
}

static inline int
sol_memmap_write_bool(const char *name, bool value)
{
    CREATE_BUFFER(&value, false);

    return sol_memmap_write_raw(name, &buf);
}

static inline int
sol_memmap_read_int32(const char *name, int32_t *value)
{
    CREATE_BUFFER(value, true);

    return sol_memmap_read_raw(name, &buf);
}

static inline int
sol_memmap_write_int32(const char *name, int32_t value)
{
    CREATE_BUFFER(&value, false);

    return sol_memmap_write_raw(name, &buf);
}

static inline int
sol_memmap_read_irange(const char *name, struct sol_irange *value)
{
    CREATE_BUFFER(value, true);

    return sol_memmap_read_raw(name, &buf);
}

static inline int
sol_memmap_write_irange(const char *name, struct sol_irange *value)
{
    CREATE_BUFFER(value, false);

    return sol_memmap_write_raw(name, &buf);
}

static inline int
sol_memmap_read_drange(const char *name, struct sol_drange *value)
{
    CREATE_BUFFER(value, true);

    return sol_memmap_read_raw(name, &buf);
}

static inline int
sol_memmap_write_drange(const char *name, struct sol_drange *value)
{
    CREATE_BUFFER(value, false);

    return sol_memmap_write_raw(name, &buf);
}

static inline int
sol_memmap_read_double(const char *name, double *value)
{
    CREATE_BUFFER(value, true);

    return sol_memmap_read_raw(name, &buf);
}

static inline int
sol_memmap_write_double(const char *name, double value)
{
    CREATE_BUFFER(&value, false);

    return sol_memmap_write_raw(name, &buf);
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

    *value = sol_buffer_steal(&buf, NULL);

    return 0;
}

static inline int
sol_memmap_write_string(const char *name, const char *value)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS((void *)value, strlen(value),
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);

    buf.used = buf.capacity;

    return sol_memmap_write_raw(name, &buf);
}

#undef CREATE_BUFFER
