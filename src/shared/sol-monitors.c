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
