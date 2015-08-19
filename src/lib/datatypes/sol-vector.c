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

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "sol-util.h"
#include "sol-vector.h"

SOL_API void
sol_vector_init(struct sol_vector *v, uint16_t elem_size)
{
    v->data = NULL;
    v->len = 0;
    v->elem_size = elem_size;
}

static int
sol_vector_grow(struct sol_vector *v, uint16_t amount)
{
    unsigned int new_cap, old_cap;
    uint16_t new_len;

    if (v->len > UINT16_MAX - amount)
        return -EOVERFLOW;

    new_len = v->len + amount;
    old_cap = align_power2(v->len);
    new_cap = align_power2(new_len);

    if (new_cap != old_cap) {
        void *data = realloc(v->data, new_cap * v->elem_size);
        if (!data)
            return -ENOMEM;
        v->data = data;
    }

    v->len = new_len;
    return 0;
}

SOL_API void *
sol_vector_append(struct sol_vector *v)
{
    unsigned char *data;

    if (sol_vector_grow(v, 1) != 0)
        return NULL;

    data = v->data;

    return &data[v->elem_size * (v->len - 1)];
}

static void
sol_vector_shrink(struct sol_vector *v)
{
    unsigned int new_cap, old_cap;
    void *data;

    if (v->len == 0) {
        free(v->data);
        v->data = NULL;
        return;
    }

    old_cap = align_power2(v->len + 1);
    new_cap = align_power2(v->len);
    if (new_cap == old_cap)
        return;

    data = realloc(v->data, new_cap * v->elem_size);
    if (!data)
        return;

    v->data = data;
}

SOL_API int
sol_vector_del(struct sol_vector *v, uint16_t i)
{
    size_t tail_len;

    if (i >= v->len)
        return -EINVAL;

    v->len--;
    tail_len = v->len - i;

    if (tail_len) {
        unsigned char *data, *dst, *src;
        data = v->data;
        dst = &data[v->elem_size * i];
        src = dst + v->elem_size;
        memmove(dst, src, v->elem_size * tail_len);
    }

    sol_vector_shrink(v);
    return 0;
}

SOL_API void
sol_vector_clear(struct sol_vector *v)
{
    free(v->data);
    v->data = NULL;
    v->len = 0;
}

static int
ptr_vector_find_sorted(struct sol_ptr_vector *pv, unsigned int low, unsigned int high, void *ptr, int (*compare)(const void *data1, const void *data2), int *dir)
{
    unsigned int mid;

    while (true) {
        if (low == high) {
            *dir = compare(ptr, sol_ptr_vector_get(pv, low));
            return low;
        }

        mid = (low + high) >> 1;
        *dir = compare(ptr, sol_ptr_vector_get(pv, mid));
        if (*dir == 0)
            return mid;
        if (*dir < 0)
            high = mid;
        else
            low = mid + 1;
    }
}

static int
ptr_vector_insert_at(struct sol_ptr_vector *pv, void *ptr, unsigned int index, int dir)
{
    unsigned char *data, *dst, *src;

    if (sol_vector_grow(&pv->base, 1) != 0)
        return -1;

    data = pv->base.data;
    dst = &data[pv->base.elem_size * (index + 1)];
    src = &data[pv->base.elem_size * index];
    memmove(dst, src, pv->base.elem_size * (pv->base.len - 1 - index));

    if (dir < 0) {
        sol_ptr_vector_set(pv, index, ptr);
    } else {
        sol_ptr_vector_set(pv, index + 1, ptr);
    }
    return 0;
}

SOL_API int
sol_ptr_vector_insert_sorted(struct sol_ptr_vector *pv, void *ptr, int (*compare)(const void *data1, const void *data2))
{
    int dir;
    unsigned int index;

    if (!pv->base.len) {
        return sol_ptr_vector_append(pv, ptr);
    }

    index = ptr_vector_find_sorted(pv, 0, pv->base.len - 1, ptr, compare, &dir);
    return ptr_vector_insert_at(pv, ptr, index, dir);
}

SOL_API int
sol_ptr_vector_append(struct sol_ptr_vector *pv, void *ptr)
{
    void **data;

    data = sol_vector_append(&pv->base);
    if (!data)
        return -ENODATA;
    *data = ptr;
    return 0;
}

SOL_API int
sol_ptr_vector_set(struct sol_ptr_vector *pv, uint16_t i, void *ptr)
{
    void **data;

    data = sol_vector_get(&pv->base, i);
    if (!data)
        return -ENODATA;
    *data = ptr;
    return 0;
}

SOL_API int
sol_ptr_vector_remove(struct sol_ptr_vector *pv, const void *ptr)
{
    uint16_t i;
    void *p;

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (pv, p, i) {
        if (ptr == p) {
            sol_ptr_vector_del(pv, i);
            return 0;
        }
    }
    return -ENODATA;
}
