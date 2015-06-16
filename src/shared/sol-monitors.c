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

#include <assert.h>

#include "sol-monitors.h"

void
sol_monitors_init_custom(struct sol_monitors *ms, uint16_t elem_size, sol_monitors_cleanup_func_t cleanup)
{
    sol_vector_init(&ms->entries, elem_size);
    ms->walking = 0;
    ms->pending_deletion = 0;
    ms->cleanup = cleanup;
}

int
sol_monitors_find(const struct sol_monitors *ms, sol_monitors_cb_t cb, const void *data)
{
    int r = -ENOENT;
    uint16_t i;

    if (!cb)
        return r;

    for (i = 0; i < ms->entries.len; i++) {
        struct sol_monitors_entry *e;
        e = sol_monitors_get(ms, i);
        if (e->cb == cb && e->data == data) {
            r = i;
            break;
        }
    }

    return r;
}

static void
delete_pending_elements(struct sol_monitors *ms)
{
    int i;

    if (ms->walking > 0)
        return;

    assert(ms->pending_deletion <= ms->entries.len);

    /* Traverse backwards so deletion doesn't impact the indices. */
    for (i = ms->entries.len - 1; i >= 0 && ms->pending_deletion > 0; i--) {
        struct sol_monitors_entry *e;
        e = sol_monitors_get(ms, i);
        if (e->cb)
            continue;

        /* Skip extra check done by sol_monitors_del(). */
        sol_vector_del(&ms->entries, i);
        ms->pending_deletion--;
    }

    assert(ms->pending_deletion == 0);
}

int
sol_monitors_del(struct sol_monitors *ms, uint16_t i)
{
    struct sol_monitors_entry *e;

    e = sol_vector_get(&ms->entries, i);

    if (!e)
        return -ENOENT;

    e->cb = NULL;
    ms->pending_deletion++;

    ms->walking++;
    if (ms->cleanup)
        ms->cleanup(ms, e);
    ms->walking--;

    if (ms->walking == 0)
        delete_pending_elements(ms);

    return 0;
}

void
sol_monitors_clear(struct sol_monitors *ms)
{
    if (ms->walking > 0 || ms->entries.len == 0)
        return;

    if (ms->cleanup) {
        int i;
        ms->walking++;
        for (i = 0; i < ms->entries.len; i++) {
            struct sol_monitors_entry *e;
            e = sol_monitors_get(ms, i);
            if (e->cb) {
                e->cb = NULL;
                ms->cleanup(ms, e);
            }
        }
        ms->walking--;
    }

    ms->pending_deletion = 0;
    sol_vector_clear(&ms->entries);
}

void
sol_monitors_end_walk(struct sol_monitors *ms)
{
    assert(ms->walking > 0);
    ms->walking--;
    if (ms->walking == 0)
        delete_pending_elements(ms);
}
