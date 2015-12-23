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
 * @brief These are routines that Soletta provides for its vector implementation.
 */

/**
 * @defgroup Vector Vector
 * @ingroup Datatypes
 *
 * @brief Soletta vector is an array that grows dynamically. It's suited for
 * storing a small set of contiguous data.
 *
 * @warning Its dynamic resize might shuffle the data around, so pointers returned from
 * sol_vector_get() and sol_vector_append() should be considered invalid
 * after the vector size is modified.
 *
 * @see PointerVector
 *
 * @{
 */

/**
 * @struct sol_vector
 *
 * @brief Soletta vector is an array that grows dynamically.
 *
 * For storing pointers, see @ref sol_ptr_vector.
 *
 * @see sol_ptr_vector
 */
struct sol_vector {
    void *data; /**< @brief Vector data */
    uint16_t len; /**< @brief Vector length */
    uint16_t elem_size; /**< @brief Size of each element in bytes */
};

/**
 * @def SOL_VECTOR_INIT(TYPE)
 * @brief Helper macro to initialize a @c sol_vector structure to hold
 * elements of type @c TYPE.
 */
#define SOL_VECTOR_INIT(TYPE) { NULL, 0, sizeof(TYPE) }

/**
 * @brief Initializes a @c sol_vector structure.
 *
 * @param v Pointer to the @c sol_vector structure to be initialized
 * @param elem_size The size of each element in bytes
 */
void sol_vector_init(struct sol_vector *v, uint16_t elem_size);

/**
 * @brief Append an element to the end of the vector.
 *
 * Creates a new element in end of the vector and returns a pointer
 * to it.
 *
 * @param v Vector pointer
 *
 * @return Pointer to the added element
 */
void *sol_vector_append(struct sol_vector *v);

/**
 * @brief Append @c n elements to the end of the vector.
 *
 * Creates @c n new elements in end of the vector and returns a pointer
 * to the first of the @c n elements.
 *
 * @param v Vector pointer
 * @param n Number of elements to be appended
 *
 * @return Pointer to the first of the @c n elements appended
 */
void *sol_vector_append_n(struct sol_vector *v, uint16_t n);

/**
 * @brief Return the element of the vector at the given index.
 *
 * @param v Vector pointer
 * @param i Index of the element to return
 *
 * @return Pointer to the element at the index @c i
 */
static inline void *
sol_vector_get(const struct sol_vector *v, uint16_t i)
{
    const unsigned char *data;

    if (i >= v->len)
        return NULL;
    data = (const unsigned char *)v->data;

    return (void *)&data[v->elem_size * i];
}

/**
 * @brief Remove an element from the vector.
 *
 * Removes the element of index @c i from the vector.
 *
 * @param v Vector pointer
 * @param i Index of the element to remove
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_vector_del(struct sol_vector *v, uint16_t i);

/**
 * @brief Remove an element from the vector.
 *
 * Removes the element pointed by @c elem from the vector.
 *
 * @param v Vector pointer
 * @param elem Pointer of the element to remove
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_vector_del_element(struct sol_vector *v, const void *elem);

/**
 * @brief Remove the last element from the vector.
 *
 * @param v Vector pointer
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
static inline int
sol_vector_del_last(struct sol_vector *v)
{
    if (v->len == 0)
        return 0;
    return sol_vector_del(v, v->len - 1);
}

/**
 * @brief Delete all elements from the vector.
 *
 * And frees the memory allocated for them. The vector returns to the initial state (empty).
 *
 * @param v Vector pointer
 */
void sol_vector_clear(struct sol_vector *v);

/**
 * @brief Steal the memory holding the elements of the vector.
 *
 * And returns the vector to the initial state.
 *
 * @param v Vector pointer
 *
 * @return Pointer to the memory containing the elements
 */
static inline void *
sol_vector_take_data(struct sol_vector *v)
{
    void *data = v->data;

    v->data = NULL;
    v->len = 0;
    return data;
}

/**
 * @def SOL_VECTOR_FOREACH_IDX(vector, itrvar, idx)
 * @brief Macro to iterate over the vector easily.
 *
 * @param vector The vector to iterate over
 * @param itrvar Variable pointing to the current element's data on each iteration
 * @param idx Index integer variable that is increased while iterating
 */
#define SOL_VECTOR_FOREACH_IDX(vector, itrvar, idx) \
    for (idx = 0; \
        idx < (vector)->len && (itrvar = sol_vector_get((vector), idx), true); \
        idx++)

/**
 * @def SOL_VECTOR_FOREACH_REVERSE_IDX(vector, itrvar, idx)
 * @brief Macro to iterate over the vector easily in the reverse order.
 *
 * @param vector The vector to iterate over
 * @param itrvar Variable pointing to the current element's data on each iteration
 * @param idx Index integer variable that is decreased while iterating
 */
#define SOL_VECTOR_FOREACH_REVERSE_IDX(vector, itrvar, idx) \
    for (idx = (vector)->len - 1; \
        idx != ((typeof(idx)) - 1) && (itrvar = sol_vector_get((vector), idx), true); \
        idx--)
/**
 * @}
 */

/**
 * @defgroup PointerVector Pointer Vector
 * @ingroup Datatypes
 *
 * @brief Soletta pointer vector is a convenience API to manipulate
 * vectors storing pointers that is based on Soletta Vector.
 *
 *  @see Vector
 *
 * @{
 */

/**
 * @struct sol_ptr_vector
 *
 * @brief Soletta pointer vector is a wrapper around vector with an API
 * more convenient to handle pointers.
 *
 * @warning Be careful when storing @c NULL pointers in the vector since
 * some functions will return @c NULL as an error when failing to retrieve
 * the data from vector elements.
 *
 * @see struct sol_vector.
 */
struct sol_ptr_vector {
    struct sol_vector base;
};

/**
 * @def SOL_PTR_VECTOR_INIT
 * @brief Helper macro to initialize a @c struct @c sol_ptr_vector.
 */
#define SOL_PTR_VECTOR_INIT { { NULL, 0, sizeof(void *) } }

/**
 * @brief Initializes a @c sol_ptr_vector structure.
 *
 * @param pv Pointer to the struct @c sol_ptr_vector to be initialized
 */
static inline void
sol_ptr_vector_init(struct sol_ptr_vector *pv)
{
    sol_vector_init(&pv->base, sizeof(void *));
}

/**
 * @brief Initializes a @c sol_ptr_vector structure and
 * preallocates @c n elements.
 *
 * @param pv Pointer to the struct @c sol_ptr_vector to be initialized
 * @param n Number of elements that should be preallocated
 */
int sol_ptr_vector_init_n(struct sol_ptr_vector *pv, uint16_t n);

/**
 * @brief Returns the number of pointers stored in the vector.
 *
 * @param pv Pointer to the @c struct @c sol_ptr_vector to be initialized
 *
 * @return Number of elements in the vector
 */
static inline uint16_t
sol_ptr_vector_get_len(const struct sol_ptr_vector *pv)
{
    return pv->base.len;
}

/**
 * @brief Append a pointer to the end of the vector.
 *
 * Creates a new element in end of the vector and stores @c ptr on it.
 *
 * @param pv Pointer Vector pointer
 * @param ptr Pointer to be stored
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_ptr_vector_append(struct sol_ptr_vector *pv, void *ptr);

/**
 * @brief Return the element of the vector at the given index.
 *
 * @param pv Pointer Vector pointer
 * @param i Index of the element to return
 *
 * @return Pointer at the index @c i and @c NULL on errors.
 */
static inline void *
sol_ptr_vector_get(const struct sol_ptr_vector *pv, uint16_t i)
{
    void **data;

    data = (void **)sol_vector_get(&pv->base, i);
    if (!data)
        return NULL;
    return *data;
}

/**
 * @brief Set the element at index @c i to be @c ptr.
 *
 * @param pv Pointer Vector pointer
 * @param i Index of the element to set
 * @param ptr The pointer
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_ptr_vector_set(struct sol_ptr_vector *pv, uint16_t i, void *ptr);

/**
 * @brief Insert a pointer in the pointer vector, using the given comparison function
 * to determine its position.
 *
 * @param pv Pointer Vector pointer
 * @param ptr The pointer
 * @param compare_cb Function to compare elements in the list. It should return an integer
 * less than, equal to, or greater than zero if @c data1 is found, respectively,
 * to be less than, to match, or be greater than @c data2 in the sort order
 */
int sol_ptr_vector_insert_sorted(struct sol_ptr_vector *pv, void *ptr, int (*compare_cb)(const void *data1, const void *data2));

/**
 * @brief Remove an pointer from the vector.
 *
 * Removes the pointer @c ptr from the vector. It stops when the
 * first occurrence is found.
 *
 * @param pv Pointer Vector pointer
 * @param ptr Pointer to remove
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_ptr_vector_remove(struct sol_ptr_vector *pv, const void *ptr);

/**
 * @brief Remove the pointer of index @c i from the vector.
 *
 * @param pv Pointer Vector pointer
 * @param i Index of the element to remove
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
static inline int
sol_ptr_vector_del(struct sol_ptr_vector *pv, uint16_t i)
{
    return sol_vector_del(&pv->base, i);
}

/**
 * @brief Remove all occurrences of @c elem from the vector @c pv.
 *
 * @param pv Pointer Vector pointer
 * @param elem Element to remove
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_ptr_vector_del_element(struct sol_ptr_vector *pv, const void *elem);

/**
 * @brief Remove the last element from the vector.
 *
 * @param pv Pointer Vector pointer
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
static inline int
sol_ptr_vector_del_last(struct sol_ptr_vector *pv)
{
    if (pv->base.len == 0)
        return 0;
    return sol_ptr_vector_del(pv, pv->base.len - 1);
}

/**
 * @brief Remove and return the element at index @c i from the vector.
 *
 * @param pv Pointer Vector pointer
 * @param i Index of the element to retrieved
 *
 * @return Pointer that was at index @c i, @c NULL otherwise
 */
static inline void *
sol_ptr_vector_take(struct sol_ptr_vector *pv, uint16_t i)
{
    void *result = sol_ptr_vector_get(pv, i);

    sol_ptr_vector_del(pv, i);
    return result;
}

/**
 * @brief Remove and return the last element from the vector.
 *
 * @param pv Pointer Vector pointer
 *
 * @return Pointer that was in the last position from the vector, @c NULL otherwise
 */
static inline void *
sol_ptr_vector_take_last(struct sol_ptr_vector *pv)
{
    if (pv->base.len == 0)
        return NULL;
    return sol_ptr_vector_take(pv, pv->base.len - 1);
}

/**
 * @brief Delete all elements from the vector.
 *
 * And frees the memory allocated for them. The vector returns to the initial state (empty).
 *
 * @param pv Pointer Vector pointer
 */
static inline void
sol_ptr_vector_clear(struct sol_ptr_vector *pv)
{
    sol_vector_clear(&pv->base);
}

/**
 * @brief Steal the memory holding the elements of the vector.
 *
 * And returns the vector to the initial state.
 *
 * @param pv Pointer Vector pointer
 *
 * @return Pointer to the memory containing the elements
 */
static inline void *
sol_ptr_vector_take_data(struct sol_ptr_vector *pv)
{
    return sol_vector_take_data(&pv->base);
}

/**
 * @def SOL_PTR_VECTOR_FOREACH_IDX(vector, itrvar, idx)
 * @brief Macro to iterate over the pointer vector easily.
 *
 * @param vector The pointer vector to iterate over
 * @param itrvar Variable pointing to the data on each iteration
 * @param idx Index integer variable that is increased while iterating
 */
#define SOL_PTR_VECTOR_FOREACH_IDX(vector, itrvar, idx) \
    for (idx = 0; \
        idx < (vector)->base.len && \
        ((itrvar = *(((void **)(vector)->base.data) + idx)), true); \
        idx++)

/**
 * @def SOL_PTR_VECTOR_FOREACH_REVERSE_IDX(vector, itrvar, idx)
 * @brief Macro to iterate over the pointer vector easily in the reverse order.
 *
 * @param vector The pointer vector to iterate over
 * @param itrvar Variable pointing to the data on each iteration
 * @param idx Index integer variable that is decreased while iterating
 */
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
