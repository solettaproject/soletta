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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These are routines that Solleta provides for its vector implementation.
 */

/**
 * @defgroup Vector Vector
 * @ingroup Datatypes
 *
 * It's an array that grows dynamically. It's suited for
 * storing a small set of contiguous data.
 *
 * @{
 */

/**
 * @struct sol_vector
 *
 * Soletta vector is an array that grows dynamically. It's suited for
 * storing a small set of contiguous data. Its dynamic resize might
 * shuffle the data around, so pointers returned from sol_vector_get()
 * and sol_vector_append() should be considered invalid after the
 * vector size is modified.
 *
 * For storing pointers, see sol_ptr_vector().
 */
struct sol_vector {
    void *data;
    uint16_t len;
    uint16_t elem_size;
};

#define SOL_VECTOR_INIT(TYPE) { NULL, 0, sizeof(TYPE) }

void sol_vector_init(struct sol_vector *v, uint16_t elem_size);

// Returns pointer to added element.
void *sol_vector_append(struct sol_vector *v);

// Returns pointer to the first element appended.
void *sol_vector_append_n(struct sol_vector *v, uint16_t n);

static inline void *
sol_vector_get(const struct sol_vector *v, uint16_t i)
{
    const unsigned char *data;

    if (i >= v->len)
        return NULL;
    data = (const unsigned char *)v->data;

    return (void *)&data[v->elem_size * i];
}

int sol_vector_del(struct sol_vector *v, uint16_t i);

void sol_vector_clear(struct sol_vector *v);

static inline void *
sol_vector_take_data(struct sol_vector *v)
{
    void *data = v->data;

    v->data = NULL;
    v->len = 0;
    return data;
}

#define SOL_VECTOR_FOREACH_IDX(vector, itrvar, idx)                      \
    for (idx = 0;                                                       \
        idx < (vector)->len && (itrvar = sol_vector_get((vector), idx), true); \
        idx++)

#define SOL_VECTOR_FOREACH_REVERSE_IDX(vector, itrvar, idx)              \
    for (idx = (vector)->len - 1;                                       \
        idx != ((typeof(idx)) - 1) && (itrvar = sol_vector_get((vector), idx), true); \
        idx--)


// sol_ptr_vector is like vector but with an API more convenient for storing pointers.

struct sol_ptr_vector {
    struct sol_vector base;
};

#define SOL_PTR_VECTOR_INIT { { NULL, 0, sizeof(void *) } }

static inline void
sol_ptr_vector_init(struct sol_ptr_vector *pv)
{
    sol_vector_init(&pv->base, sizeof(void *));
}

static inline uint16_t
sol_ptr_vector_get_len(const struct sol_ptr_vector *pv)
{
    return pv->base.len;
}

int sol_ptr_vector_append(struct sol_ptr_vector *pv, void *ptr);

static inline void *
sol_ptr_vector_get(const struct sol_ptr_vector *pv, uint16_t i)
{
    void **data;

    data = (void **)sol_vector_get(&pv->base, i);
    if (!data)
        return NULL;
    return *data;
}

int sol_ptr_vector_set(struct sol_ptr_vector *pv, uint16_t i, void *ptr);

int sol_ptr_vector_insert_sorted(struct sol_ptr_vector *pv, void *ptr, int (*compare_cb)(const void *data1, const void *data2));

int sol_ptr_vector_remove(struct sol_ptr_vector *pv, const void *ptr);

static inline int
sol_ptr_vector_del(struct sol_ptr_vector *pv, uint16_t i)
{
    return sol_vector_del(&pv->base, i);
}

static inline void *
sol_ptr_vector_take(struct sol_ptr_vector *pv, uint16_t i)
{
    void *result = sol_ptr_vector_get(pv, i);

    sol_ptr_vector_del(pv, i);
    return result;
}

static inline void
sol_ptr_vector_clear(struct sol_ptr_vector *pv)
{
    sol_vector_clear(&pv->base);
}

static inline void *
sol_ptr_vector_take_data(struct sol_ptr_vector *pv)
{
    return sol_vector_take_data(&pv->base);
}

#define SOL_PTR_VECTOR_FOREACH_IDX(vector, itrvar, idx) \
    for (idx = 0; \
        idx < (vector)->base.len && \
        ((itrvar = *(((void **)(vector)->base.data) + idx)), true); \
        idx++)

#define SOL_PTR_VECTOR_FOREACH_REVERSE_IDX(vector, itrvar, idx) \
    for (idx = (vector)->base.len - 1; \
        idx != ((typeof(idx)) - 1) && \
        (itrvar = *(((void **)(vector)->base.data) + idx), true); \
        idx--)

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
