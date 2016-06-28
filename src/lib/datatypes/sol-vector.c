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

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "sol-log.h"
#include "sol-util-internal.h"
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
    size_t new_cap, old_cap;
    uint16_t new_len;

    if (v->len > UINT16_MAX - amount)
        return -EOVERFLOW;

    new_len = v->len + amount;
    old_cap = align_power2(v->len);
    new_cap = align_power2(new_len);

    if (new_cap != old_cap) {
        void *data;
        int r;
        size_t data_size;

        r = sol_util_size_mul(v->elem_size, new_cap, &data_size);
        SOL_INT_CHECK(r, < 0, r);

        data = realloc(v->data, data_size);
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
    return sol_vector_append_n(v, 1);
}

SOL_API void *
sol_vector_append_n(struct sol_vector *v, uint16_t n)
{
    void *new_elems;
    int err;

    if (!v || n == 0) {
        errno = EINVAL;
        return NULL;
    }

    err = sol_vector_grow(v, n);
    if (err < 0) {
        errno = -err;
        return NULL;
    }

    new_elems = (unsigned char *)v->data + (v->elem_size * (v->len - n));
    memset(new_elems, 0, (size_t)v->elem_size * n);

    return new_elems;
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

    old_cap = align_power2(v->len + 1U);
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

SOL_API int
sol_vector_del_range(struct sol_vector *v, uint16_t start, uint16_t len)
{
    size_t tail_len;

    if (start >= v->len)
        return -EINVAL;

    if ((uint32_t)start + len >= (uint32_t)v->len)
        len = v->len - start;

    v->len -= len;
    tail_len = v->len - start;

    if (tail_len) {
        unsigned char *data, *dst, *src;

        data = v->data;
        dst = &data[v->elem_size * start];
        src = dst + len * v->elem_size;
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

/* this function returns an approximate match if dir != 0. */
static uint16_t
ptr_vector_find_sorted(const struct sol_ptr_vector *pv, uint16_t low, uint16_t high, const void *ptr, int (*compare)(const void *data1, const void *data2), int *dir)
{
    uint16_t mid;

    *dir = compare(ptr, sol_ptr_vector_get_no_check(pv, low));
    if (*dir <= 0 || low == high)
        return low;

    *dir = compare(ptr, sol_ptr_vector_get_no_check(pv, high));
    if (*dir >= 0)
        return high;

    while (true) {
        if (low == high) {
            *dir = compare(ptr, sol_ptr_vector_get_no_check(pv, low));
            return low;
        }

        mid = ((uint32_t)low + (uint32_t)high) >> 1;
        *dir = compare(ptr, sol_ptr_vector_get_no_check(pv, mid));
        if (*dir == 0)
            return mid;
        if (*dir < 0)
            high = mid;
        else
            low = mid + 1;
    }
}

SOL_API int
sol_ptr_vector_insert_at(struct sol_ptr_vector *pv, uint16_t i, const void *ptr)
{
    unsigned char *data, *dst, *src;

    SOL_INT_CHECK(i, > pv->base.len, -ERANGE);

    if (i == pv->base.len)
        return sol_ptr_vector_append(pv, ptr);

    if (sol_vector_grow(&pv->base, 1) != 0)
        return -ENOMEM;

    data = pv->base.data;
    dst = &data[pv->base.elem_size * (i + 1)];
    src = &data[pv->base.elem_size * i];
    memmove(dst, src, (size_t)pv->base.elem_size * (pv->base.len - 1 - i));

    return sol_ptr_vector_set(pv, i, ptr);
}

SOL_API int32_t
sol_ptr_vector_insert_sorted(struct sol_ptr_vector *pv, const void *ptr, int (*compare)(const void *data1, const void *data2))
{
    int dir;
    uint32_t index;

    if (!pv->base.len) {
        dir = sol_ptr_vector_append(pv, ptr);
        if (dir < 0)
            return dir;
        return sol_ptr_vector_get_len(pv) - 1;
    }

    index = ptr_vector_find_sorted(pv, 0, pv->base.len - 1, ptr, compare, &dir);
    while (dir == 0 && index < pv->base.len - 1U) {
        index++;
        dir = compare(ptr, sol_ptr_vector_get_no_check(pv, index));
    }

    if (dir >= 0)
        index++;

    dir = sol_ptr_vector_insert_at(pv, index, ptr);
    if (dir < 0)
        return dir;
    return index;
}

SOL_API int32_t
sol_ptr_vector_update_sorted(struct sol_ptr_vector *pv, uint16_t i, int (*compare_cb)(const void *data1, const void *data2))
{
    void *ptr, *other;
    size_t tail_len;

    if (i >= pv->base.len)
        return -ERANGE;

    if (pv->base.len == 1)
        return 0;

    ptr = sol_ptr_vector_get_no_check(pv, i);
    if (i > 0) {
        other = sol_ptr_vector_get_no_check(pv, i - 1);
        if (compare_cb(other, ptr) > 0)
            goto fix_it;
    }

    if (i + 1 < pv->base.len) {
        other = sol_ptr_vector_get_no_check(pv, i + 1);
        if (compare_cb(ptr, other) >= 0)
            goto fix_it;
    }

    return i;

fix_it:
    pv->base.len--;
    tail_len = pv->base.len - i;

    if (tail_len) {
        unsigned char *data, *dst, *src;
        data = pv->base.data;
        dst = &data[pv->base.elem_size * i];
        src = dst + pv->base.elem_size;
        memmove(dst, src, pv->base.elem_size * tail_len);
    }

    return sol_ptr_vector_insert_sorted(pv, ptr, compare_cb);
}

SOL_API int32_t
sol_ptr_vector_match_sorted(const struct sol_ptr_vector *pv, const void *elem, int (*compare_cb)(const void *data1, const void *data2))
{
    int dir;
    uint16_t i;

    if (!pv->base.len) {
        return -ENODATA;
    }

    i = ptr_vector_find_sorted(pv, 0, pv->base.len - 1, elem, compare_cb, &dir);
    if (dir != 0)
        return -ENODATA;

    return i;
}

SOL_API int
sol_ptr_vector_append(struct sol_ptr_vector *pv, const void *ptr)
{
    void **data;

    data = sol_vector_append(&pv->base);
    if (!data)
        return -errno;
    *data = (void *)ptr;
    return 0;
}

SOL_API int
sol_ptr_vector_set(struct sol_ptr_vector *pv, uint16_t i, const void *ptr)
{
    void **data;

    data = sol_vector_get(&pv->base, i);
    if (!data)
        return -ENODATA;
    *data = (void *)ptr;
    return 0;
}

SOL_API int
sol_ptr_vector_remove(struct sol_ptr_vector *pv, const void *ptr)
{
    int32_t i = sol_ptr_vector_find_last(pv, ptr);

    if (i < 0)
        return i;

    return sol_ptr_vector_del(pv, i);
}

SOL_API int
sol_ptr_vector_init_n(struct sol_ptr_vector *pv, uint16_t n)
{
    sol_vector_init(&pv->base, sizeof(void *));
    if (!sol_vector_append_n(&pv->base, n))
        return -errno;
    return 0;
}

SOL_API int
sol_vector_del_element(struct sol_vector *v, const void *elem)
{
    ssize_t offset, index;

    offset = (const char *)elem - (const char *)v->data;
    if (offset % v->elem_size || offset < 0)
        return -ENOENT;

    index = offset / v->elem_size;
    if (index >= v->len)
        return -ENOENT;

    return sol_vector_del(v, index);
}

SOL_API int
sol_ptr_vector_del_element(struct sol_ptr_vector *pv, const void *elem)
{
    uint16_t i = 0, offset = 0;
    void **v = pv->base.data;

    while (i < pv->base.len && v[i] != elem)
        i++;

    offset = i;

    for (; i < pv->base.len; i++)
        if (v[i] != elem)
            v[offset++] = v[i];

    if (offset == pv->base.len)
        return -ENOENT;

    pv->base.len = offset;
    sol_vector_shrink(&pv->base);

    return 0;
}
