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

#include <errno.h>

#include "sol-vector.h"

/*
 * Data structure to hold callbacks to be later notified. It provides means to safely walk the
 * callbacks, and defers deletion of elements until all the walks finish. For convenience, the
 * SOL_MONITORS_WALK macro can be used to walk in the callbacks.
 *
 * A cleanup function can be provided to perform any extra cleanup for each element. This will be
 * called by clear() function as well.
 *
 * By default it stores both a callback and a data pointer, this can be extended by embedding struct
 * sol_monitors_entry in your own structure and use sol_monitors_init_custom() or
 * SOL_MONITORS_INIT_CUSTOM macro.
 */

typedef void (*sol_monitors_cb_t)(const void *);

struct sol_monitors_entry {
    sol_monitors_cb_t cb;
    const void *data;
};

struct sol_monitors;
typedef void (*sol_monitors_cleanup_func_t)(const struct sol_monitors *, const struct sol_monitors_entry *);

struct sol_monitors {
    struct sol_vector entries;
    uint16_t walking;
    uint16_t pending_deletion;
    sol_monitors_cleanup_func_t cleanup;
};

#define SOL_MONITORS_INIT_CUSTOM(TYPE, CLEANUP)              \
    {                                                       \
        .entries = SOL_VECTOR_INIT(TYPE),                    \
        .walking = 0,                                       \
        .pending_deletion = 0,                              \
        .cleanup = CLEANUP,                                 \
    }

#define SOL_MONITORS_INIT(CLEANUP)                               \
    SOL_MONITORS_INIT_CUSTOM(struct sol_monitors_entry, CLEANUP)

void sol_monitors_init_custom(struct sol_monitors *ms, uint16_t elem_size, sol_monitors_cleanup_func_t cleanup);

static inline void
sol_monitors_init(struct sol_monitors *ms, sol_monitors_cleanup_func_t cleanup)
{
    sol_monitors_init_custom(ms, sizeof(struct sol_monitors_entry), cleanup);
}

static inline uint16_t
sol_monitors_count(const struct sol_monitors *ms)
{
    return ms->entries.len;
}

static inline void *
sol_monitors_append(struct sol_monitors *mv, sol_monitors_cb_t cb, const void *data)
{
    struct sol_monitors_entry *e;

    if (!cb)
        return NULL;
    e = sol_vector_append(&mv->entries);
    if (!e)
        return NULL;
    e->cb = cb;
    e->data = data;
    return e;
}

static inline void *
sol_monitors_get(const struct sol_monitors *ms, uint16_t i)
{
    return sol_vector_get(&ms->entries, i);
}

int sol_monitors_find(const struct sol_monitors *ms, sol_monitors_cb_t cb, const void *data);

int sol_monitors_del(struct sol_monitors *ms, uint16_t i);

void sol_monitors_clear(struct sol_monitors *ms);

static inline void
sol_monitors_begin_walk(struct sol_monitors *ms)
{
    ms->walking++;
}

void sol_monitors_end_walk(struct sol_monitors *ms);

#define _SOL_MONITORS_WALK_VAR(X) walk__var__ ## X

#define __SOL_MONITORS_WALK(ms, itrvar, idx, executed)                    \
    for (bool executed = ({ sol_monitors_begin_walk(ms); false; });      \
        !executed;                                                     \
        executed = ({ sol_monitors_end_walk(ms); true; }))              \
        SOL_VECTOR_FOREACH_IDX (&(ms)->entries, itrvar, idx)

#define _SOL_MONITORS_WALK(ms, itrvar, idx, executed) \
    __SOL_MONITORS_WALK(ms, itrvar, idx, _SOL_MONITORS_WALK_VAR(executed))

#define SOL_MONITORS_WALK(ms, itrvar, idx)                               \
    _SOL_MONITORS_WALK(ms, itrvar, idx, __COUNTER__)


#define __SOL_MONITORS_WALK_AND_CALLBACK(ms, e, i)        \
    do {                                                \
        struct sol_monitors_entry *e;                     \
        uint16_t i;                                     \
        SOL_MONITORS_WALK (ms, e, i)                      \
            if (e->cb) e->cb(e->data);                  \
    } while (0)

#define _SOL_MONITORS_WALK_AND_CALLBACK(ms, e, i)        \
    __SOL_MONITORS_WALK_AND_CALLBACK(ms, _SOL_MONITORS_WALK_VAR(e), _SOL_MONITORS_WALK_VAR(i))

#define SOL_MONITORS_WALK_AND_CALLBACK(ms)    \
    _SOL_MONITORS_WALK_AND_CALLBACK(ms, __COUNTER__, __COUNTER__)
