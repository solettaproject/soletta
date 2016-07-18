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
#include <stddef.h>
#include <stdint.h>
#include <errno.h>

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
 * @brief Soletta vector is an array that grows dynamically.
 *
 * For storing pointers, see @ref sol_ptr_vector.
 *
 * @see sol_ptr_vector
 */
typedef struct sol_vector {
    void *data; /**< @brief Vector data */
    uint16_t len; /**< @brief Vector length */
    uint16_t elem_size; /**< @brief Size of each element in bytes */
} sol_vector;


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
 *
 * @remark Time complexity: amortized constant
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
 *
 * @remark Time complexity: amortized linear in the number of elements appended
 */
void *sol_vector_append_n(struct sol_vector *v, uint16_t n);

/**
 * @brief Return the element of the vector at the given index (no safety checks).
 *
 * This is similar to sol_vector_get(), but does no safety checks such
 * as array boundaries. Only use this whenever you're sure the index
 * @a i exists.
 *
 * @param v Vector pointer
 * @param i Index of the element to return
 *
 * @return Pointer to the element at the index @c i
 *
 * @remark Time complexity: constant
 *
 * @see sol_vector_get()
 */
static inline void *
sol_vector_get_no_check(const struct sol_vector *v, uint16_t i)
{
    const unsigned char *data;

    data = (const unsigned char *)v->data;

    return (void *)&data[v->elem_size * i];
}

/**
 * @brief Return the element of the vector at the given index.
 *
 * @param v Vector pointer
 * @param i Index of the element to return
 *
 * @return Pointer to the element at the index @c i or @c NULL on errors.
 *
 * @remark Time complexity: constant
 */
static inline void *
sol_vector_get(const struct sol_vector *v, uint16_t i)
{
    if (i >= v->len)
        return NULL;

    return sol_vector_get_no_check(v, i);
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
 *
 * @remark Time complexity: linear in distance between @a i and the end of the vector
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
 *
 * @remark Time complexity: linear in distance between @a elem and the end of the vector
 */
int sol_vector_del_element(struct sol_vector *v, const void *elem);

/**
 * @brief Remove the last element from the vector.
 *
 * @param v Vector pointer
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @remark Time complexity: amortized constant
 */
static inline int
sol_vector_del_last(struct sol_vector *v)
{
    if (v->len == 0)
        return 0;
    return sol_vector_del(v, v->len - 1);
}

/**
 * @brief Remove an range of element from the vector.
 *
 * Removes the range starting at index @a start from the vector and
 * goes until @a start + @a len.
 *
 * @param v Vector pointer
 * @param start Index of the first element to remove
 * @param len the number of elements to remover
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_vector_del_range(struct sol_vector *v, uint16_t start, uint16_t len);

/**
 * @brief Delete all elements from the vector.
 *
 * And frees the memory allocated for them. The vector returns to the initial state (empty).
 *
 * @param v Vector pointer
 *
 * @remark Time complexity: constant
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
 *
 * @remark Time complexity: constant
 */
static inline void *
sol_vector_steal_data(struct sol_vector *v)
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
        idx < (vector)->len && (itrvar = (__typeof__(itrvar))sol_vector_get_no_check((vector), idx), true); \
        idx++)

/**
 * @def SOL_VECTOR_FOREACH_IDX_UNTIL(vector, itrvar, idx, until)
 * @brief Macro to iterate over the vector until a index.
 *
 * @param vector The vector to iterate over
 * @param itrvar Variable pointing to the current element's data on each iteration
 * @param idx Index integer variable that is increased while iterating
 * @param until The index that the iteration should stop
 */
#define SOL_VECTOR_FOREACH_IDX_UNTIL(vector, itrvar, idx, until) \
    for (idx = 0; \
        idx < until && (itrvar = (__typeof__(itrvar))sol_vector_get_no_check((vector), idx), true); \
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
        idx != ((__typeof__(idx)) - 1) && (itrvar = (__typeof__(itrvar))sol_vector_get_no_check((vector), idx), true); \
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
 * @brief Soletta pointer vector is a wrapper around vector with an API
 * more convenient to handle pointers.
 *
 * @warning Be careful when storing @c NULL pointers in the vector since
 * some functions will return @c NULL as an error when failing to retrieve
 * the data from vector elements.
 *
 * @see struct sol_vector
 */
typedef struct sol_ptr_vector {
    struct sol_vector base;
} sol_ptr_vector;

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
 *
 * @remark Time complexity: linear in the number of elements @c n
 */
int sol_ptr_vector_init_n(struct sol_ptr_vector *pv, uint16_t n);

/**
 * @brief Returns the number of pointers stored in the vector.
 *
 * @param pv Pointer to the @c struct @c sol_ptr_vector to be initialized
 *
 * @return Number of elements in the vector
 *
 * @remark Time complexity: constant
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
 *
 * @remark Time complexity: amortized constant
 */
int sol_ptr_vector_append(struct sol_ptr_vector *pv, const void *ptr);

/**
 * @brief Return the element of the vector at the given index (no safety checks).
 *
 * This is similar to sol_ptr_vector_get(), but does no safety checks
 * such as array boundaries. Only use this whenever you're sure the
 * index @a i exists.
 *
 * @param pv Pointer Vector pointer
 * @param i Index of the element to return
 *
 * @return Pointer at the index @c i.
 *
 * @remark Time complexity: constant
 *
 * @see sol_ptr_vector_get()
 */
static inline void *
sol_ptr_vector_get_no_check(const struct sol_ptr_vector *pv, uint16_t i)
{
    void **data;

    data = (void **)sol_vector_get_no_check(&pv->base, i);
    return *data;
}

/**
 * @brief Return the element of the vector at the given index.
 *
 * @param pv Pointer Vector pointer
 * @param i Index of the element to return
 *
 * @return Pointer at the index @c i and @c NULL on errors.
 *
 * @remark Time complexity: constant
 */
static inline void *
sol_ptr_vector_get(const struct sol_ptr_vector *pv, uint16_t i)
{
    if (i >= pv->base.len)
        return NULL;

    return sol_ptr_vector_get_no_check(pv, i);
}

/**
 * @brief Set the element at index @c i to be @c ptr.
 *
 * @param pv Pointer Vector pointer
 * @param i Index of the element to set
 * @param ptr The pointer
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @remark Time complexity: constant
 */
int sol_ptr_vector_set(struct sol_ptr_vector *pv, uint16_t i, const void *ptr);

/**
 * @brief Insert a pointer in the pointer vector, using the given comparison function
 * to determine its position.
 *
 * This function should be stable, if the new element @a ptr matches
 * an existing in the vector (ie: @a compare_cb returns 0), then it
 * will insert the new element @b after the last matching, keeping
 * instances in a stable order.
 *
 * @param pv Pointer Vector pointer
 * @param ptr The pointer
 * @param compare_cb Function to compare elements in the list. It should return an integer
 * less than, equal to, or greater than zero if @c data1 is found, respectively,
 * to be less than, to match, or be greater than @c data2 in the sort order
 *
 * @return index (>=0) or negative errno on errors.
 *
 * @remark Time complexity: linear in number of elements between inserted position and the
 * end of the vector or logarithmic in the size of the vector, whichever is greater
 *
 * @see sol_ptr_vector_append()
 * @see sol_ptr_vector_insert_at()
 * @see sol_ptr_vector_match_sorted()
 * @see sol_ptr_vector_find_sorted()
 * @see sol_ptr_vector_find_first_sorted()
 * @see sol_ptr_vector_find_last_sorted()
 */
int32_t sol_ptr_vector_insert_sorted(struct sol_ptr_vector *pv, const void *ptr, int (*compare_cb)(const void *data1, const void *data2));

/**
 * @brief Update sorted pointer vector so the element is still in order.
 *
 * This function takes an index @c i and checks it is in correct order
 * in the previously sorted array. It is an optimized version to be
 * used instead of deleting and inserting it again, so array size is
 * untouched.
 *
 * @param pv Pointer Vector pointer
 * @param i The index that was updated and may be repositioned
 * @param compare_cb Function to compare elements in the list. It should return an integer
 * less than, equal to, or greater than zero if @c data1 is found, respectively,
 * to be less than, to match, or be greater than @c data2 in the sort order
 *
 * @return the index (>=0) on success, which may be the same if it
 * wasn't changed, or negative errno on failure.
 *
 * @remark Time complexity: linear in number of elements between updated position and the
 * end of the vector or logarithmic in the size of the vector, whichever is greater
 */
int32_t sol_ptr_vector_update_sorted(struct sol_ptr_vector *pv, uint16_t i, int (*compare_cb)(const void *data1, const void *data2));

/**
 * @brief Insert a pointer in the pointer vector at a given position.
 *
 * This function inserts a new element @a ptr at index @a i. All
 * existing elements with index greater than @a i will be moved, thus
 * their index will increase by one. If the index is the last position
 * (sol_ptr_vector_get_len()), then it will have the same effect as
 * sol_ptr_vector_append().
 *
 * @param pv Pointer Vector pointer
 * @param i Index to insert element at.
 * @param ptr The pointer
 *
 * @return 0 on success or negative errno on errors.
 *
 * @remark Time complexity: amortized linear in number of elements between
 * @a i and the end of the vector
 *
 * @see sol_ptr_vector_append()
 * @see sol_ptr_vector_match_sorted()
 * @see sol_ptr_vector_find_sorted()
 * @see sol_ptr_vector_find_first_sorted()
 * @see sol_ptr_vector_find_last_sorted()
 */
int sol_ptr_vector_insert_at(struct sol_ptr_vector *pv, uint16_t i, const void *ptr);

/**
 * @brief Remove an pointer from the vector.
 *
 * Removes the last occurrence of the pointer @c ptr from the vector. To delete all
 * use sol_ptr_vector_del_element()
 *
 * @param pv Pointer Vector pointer
 * @param ptr Pointer to remove
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @remark Time complexity: linear in number of elements between the @c ptr
 * position and the end of the vector
 *
 * @see sol_ptr_vector_del()
 * @see sol_ptr_vector_del_element()
 */
int sol_ptr_vector_remove(struct sol_ptr_vector *pv, const void *ptr);

/**
 * @brief Remove the pointer of index @c i from the vector.
 *
 * @param pv Pointer Vector pointer
 * @param i Index of the element to remove
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @remark Time complexity: linear in distance between @a i and the end of the vector
 *
 * @see sol_ptr_vector_del_element()
 * @see sol_ptr_vector_remove()
 * @see sol_ptr_vector_find()
 * @see sol_ptr_vector_match_sorted()
 * @see sol_ptr_vector_find_sorted()
 */
static inline int
sol_ptr_vector_del(struct sol_ptr_vector *pv, uint16_t i)
{
    return sol_vector_del(&pv->base, i);
}

/**
 * @brief Remove an range of pointers from the vector.
 *
 * Removes the range starting at index @a start from the vector and
 * goes until @a start + @a len.
 *
 * @param pv Pointer Vector pointer
 * @param start Index of the first element to remove
 * @param len the number of elements to remover
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
static inline int
sol_ptr_vector_del_range(struct sol_ptr_vector *pv, uint16_t start, uint16_t len)
{
    return sol_vector_del_range(&pv->base, start, len);
}

/**
 * @brief Remove all occurrences of @c elem from the vector @c pv.
 *
 * @param pv Pointer Vector pointer
 * @param elem Element to remove
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @remark Time complexity: linear in vector size
 *
 * @see sol_ptr_vector_del()
 * @see sol_ptr_vector_remove()
 */
int sol_ptr_vector_del_element(struct sol_ptr_vector *pv, const void *elem);

/**
 * @brief Remove the last element from the vector.
 *
 * @param pv Pointer Vector pointer
 *
 * @return @c 0 on success, error code (always negative) otherwise
 *
 * @remark Time complexity: amortized constant
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
 *
 * @remark Time complexity: linear in distance between @a i and the end of the vector
 */
static inline void *
sol_ptr_vector_steal(struct sol_ptr_vector *pv, uint16_t i)
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
 *
 * @remark Time complexity: amortized constant
 */
static inline void *
sol_ptr_vector_steal_last(struct sol_ptr_vector *pv)
{
    if (pv->base.len == 0)
        return NULL;
    return sol_ptr_vector_steal(pv, pv->base.len - 1);
}

/**
 * @brief Delete all elements from the vector.
 *
 * And frees the memory allocated for them. The vector returns to the initial state (empty).
 *
 * @param pv Pointer Vector pointer
 *
 * @remark Time complexity: constant
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
 *
 * @remark Time complexity: constant
 */
static inline void *
sol_ptr_vector_steal_data(struct sol_ptr_vector *pv)
{
    return sol_vector_steal_data(&pv->base);
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
        ((itrvar = (__typeof__(itrvar))sol_ptr_vector_get_no_check((vector), idx)), true); \
        idx++)

/**
 * @def SOL_PTR_VECTOR_FOREACH_IDX_UNTIL(vector, itrvar, idx, until)
 * @brief Macro to iterate over the pointer vector until a index.
 *
 * @param vector The pointer vector to iterate over
 * @param itrvar Variable pointing to the current element's data on each iteration
 * @param idx Index integer variable that is increased while iterating
 * @param until The index that the iteration should stop
 */
#define SOL_PTR_VECTOR_FOREACH_IDX_UNTIL(vector, itrvar, idx, until) \
    for (idx = 0; \
        idx < until && \
        ((itrvar = (__typeof__(itrvar))sol_ptr_vector_get_no_check((vector), idx)), true); \
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
        idx != ((__typeof__(idx)) - 1) && \
        (itrvar = (__typeof__(itrvar))sol_ptr_vector_get_no_check((vector), idx), true); \
        idx--)

/**
 * @brief Find the last occurrence of @c elem from the vector @c pv.
 *
 * @param pv Pointer Vector pointer
 * @param elem Element to find
 *
 * @return index (>=0) on success, error code (always negative) otherwise
 *
 * @remark Time complexity: linear in distance between found element and
 * the end of the vector
 *
 * @see sol_ptr_vector_find_first()
 * @see sol_ptr_vector_find_first_sorted()
 * @see sol_ptr_vector_match_first()
 * @see sol_ptr_vector_match_last()
 * @see sol_ptr_vector_find_last_sorted()
 * @see sol_ptr_vector_match_sorted()
 * @see sol_ptr_vector_find_sorted()
 */
static inline int32_t
sol_ptr_vector_find_last(const struct sol_ptr_vector *pv, const void *elem)
{
    uint16_t i;
    const void *p;

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (pv, p, i) {
        if (elem == p)
            return i;
    }

    return -ENODATA;
}

/**
 * @brief Find the first occurrence of @c elem from the vector @c pv.
 *
 * @param pv Pointer Vector pointer
 * @param elem Element to find
 *
 * @return index (>=0) on success, error code (always negative) otherwise
 *
 * @remark Time complexity: linear between found element and the beginning
 * of the vector
 *
 * @see sol_ptr_vector_find_last()
 * @see sol_ptr_vector_find_first_sorted()
 * @see sol_ptr_vector_find_last_sorted()
 * @see sol_ptr_vector_match_first()
 * @see sol_ptr_vector_match_last()
 * @see sol_ptr_vector_match_sorted()
 * @see sol_ptr_vector_find_sorted()
 */
static inline int32_t
sol_ptr_vector_find_first(const struct sol_ptr_vector *pv, const void *elem)
{
    uint16_t i;
    const void *p;

    SOL_PTR_VECTOR_FOREACH_IDX (pv, p, i) {
        if (elem == p)
            return i;
    }

    return -ENODATA;
}

/**
 * @brief Match for the first occurrence matching template @c tempt.
 *
 * @note this function returns a match given @a compare_cb, that is,
 *       one that returns 0. To find the actual pointer element, use
 *       sol_ptr_vector_find_first().
 *
 * @param pv Pointer Vector pointer
 * @param tempt The template used to find, the returned index may not
 * be of @a tempt pointer, but to another element which makes @a
 * compare_cb return 0.
 * @param compare_cb Function to compare elements in the list. It should return an integer
 * less than, equal to, or greater than zero if @c data1 is found, respectively,
 * to be less than, to match, or be greater than @c data2 in the sort order
 *
 * @return index (>=0) on success, error code (always negative) otherwise
 *
 * @remark Time complexity: linear in distance between the beginning of the vector
 * and the matched element
 *
 * @see sol_ptr_vector_match_last()
 * @see sol_ptr_vector_find_first()
 * @see sol_ptr_vector_find_first_sorted()
 */
static inline int32_t
sol_ptr_vector_match_first(const struct sol_ptr_vector *pv, const void *tempt, int (*compare_cb)(const void *data1, const void *data2))
{
    uint16_t i;
    const void *p;

    SOL_PTR_VECTOR_FOREACH_IDX (pv, p, i) {
        if (compare_cb(tempt, p) == 0)
            return i;
    }

    return -ENODATA;
}

/**
 * @brief Match for the last occurrence matching template @c tempt.
 *
 * @note this function returns a match given @a compare_cb, that is,
 *       one that returns 0. To find the actual pointer element, use
 *       sol_ptr_vector_find_first() or sol_ptr_vector_find_last().
 *
 * @param pv Pointer Vector pointer
 * @param tempt The template used to find, the returned index may not
 * be of @a tempt pointer, but to another element which makes @a
 * compare_cb return 0.
 * @param compare_cb Function to compare elements in the list. It should return an integer
 * less than, equal to, or greater than zero if @c data1 is found, respectively,
 * to be less than, to match, or be greater than @c data2 in the sort order
 *
 * @return index (>=0) on success, error code (always negative) otherwise
 *
 * @remark Time complexity: linear in distance between the matched element and the
 * end of the vector
 *
 * @see sol_ptr_vector_match_first()
 * @see sol_ptr_vector_find_last()
 * @see sol_ptr_vector_find_last_sorted()
 */
static inline int32_t
sol_ptr_vector_match_last(const struct sol_ptr_vector *pv, const void *tempt, int (*compare_cb)(const void *data1, const void *data2))
{
    uint16_t i;
    const void *p;

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (pv, p, i) {
        if (compare_cb(tempt, p) == 0)
            return i;
    }

    return -ENODATA;
}

/**
 * @brief Match for occurrence matching template @c tempt in the sorted vector @c pv.
 *
 * @note this function returns a match given @a compare_cb, that is,
 *       one that returns 0. To find the actual pointer element, use
 *       sol_ptr_vector_find_sorted().
 *
 * @param pv Pointer Vector pointer (already sorted)
 * @param tempt The template used to find, the returned index may not
 * be of @a tempt pointer, but to another element which makes @a
 * compare_cb return 0.
 * @param compare_cb Function to compare elements in the list. It should return an integer
 * less than, equal to, or greater than zero if @c data1 is found, respectively,
 * to be less than, to match, or be greater than @c data2 in the sort order
 *
 * @return index (>=0) on success, error code (always negative) otherwise
 *
 * @remark Time complexity: logarithmic in vector size
 *
 * @see sol_ptr_vector_find_first()
 * @see sol_ptr_vector_find_last()
 * @see sol_ptr_vector_find_first_sorted()
 * @see sol_ptr_vector_find_last_sorted()
 * @see sol_ptr_vector_find_sorted()
 * @see sol_ptr_vector_insert_sorted()
 * @see sol_ptr_vector_match_first()
 * @see sol_ptr_vector_match_last()
 */
int32_t sol_ptr_vector_match_sorted(const struct sol_ptr_vector *pv, const void *tempt, int (*compare_cb)(const void *data1, const void *data2));

/**
 * @brief Find the exact occurrence of @c elem in the sorted vector @c pv.
 *
 * This function will find the exact pointer to @a elem, so an
 * existing element must be used. For a query-element (only used for
 * reference in the @a compare_cb), use sol_ptr_vector_match_sorted().
 *
 * Unlike sol_ptr_vector_find_first_sorted() and
 * sol_ptr_vector_find_last_sorted(), it will do a binary search and
 * return the first occurrence of the pointer @a elem. In the case of
 * multiple occurrences, it may be an element in the middle of those
 * that would match (@a compare_cb returns 0).
 *
 * @param pv Pointer Vector pointer (already sorted)
 * @param elem Element to find
 * @param compare_cb Function to compare elements in the list. It should return an integer
 * less than, equal to, or greater than zero if @c data1 is found, respectively,
 * to be less than, to match, or be greater than @c data2 in the sort order
 *
 * @return index (>=0) on success, error code (always negative) otherwise
 *
 * @remark Time complexity: logarithmic in number of elements
 *
 * @see sol_ptr_vector_find_first()
 * @see sol_ptr_vector_find_last()
 * @see sol_ptr_vector_find_first_sorted()
 * @see sol_ptr_vector_find_last_sorted()
 * @see sol_ptr_vector_find_sorted()
 * @see sol_ptr_vector_insert_sorted()
 * @see sol_ptr_vector_match_first()
 * @see sol_ptr_vector_match_last()
 * @see sol_ptr_vector_match_sorted()
 */
static inline int32_t
sol_ptr_vector_find_sorted(const struct sol_ptr_vector *pv, const void *elem, int (*compare_cb)(const void *data1, const void *data2))
{
    uint16_t i;
    int32_t r;

    r = sol_ptr_vector_match_sorted(pv, elem, compare_cb);
    if (r < 0)
        return r;

    for (i = r; i < pv->base.len; i++) {
        const void *other = sol_ptr_vector_get_no_check(pv, i);

        if (compare_cb(elem, other) != 0)
            break;

        if (elem == other)
            return i;
    }

    for (i = r; i > 0; i--) {
        const void *other = sol_ptr_vector_get_no_check(pv, i - 1);

        if (compare_cb(elem, other) != 0)
            break;

        if (elem == other)
            return i - 1;
    }

    return -ENODATA;
}

/**
 * @brief Find the last occurrence of @c elem in the sorted vector @c pv.
 *
 * @param pv Pointer Vector pointer (already sorted)
 * @param elem Element to find
 * @param compare_cb Function to compare elements in the list. It should return an integer
 * less than, equal to, or greater than zero if @c data1 is found, respectively,
 * to be less than, to match, or be greater than @c data2 in the sort order
 *
 * @return index (>=0) on success, error code (always negative) otherwise
 *
 * @remark Time complexity: logarithmic in vector size or linear in number of elements
 * equal to @a elem, whichever is greater
 *
 * @see sol_ptr_vector_find_first()
 * @see sol_ptr_vector_find_last()
 * @see sol_ptr_vector_find_first_sorted()
 * @see sol_ptr_vector_match_sorted()
 * @see sol_ptr_vector_match_first()
 * @see sol_ptr_vector_match_last()
 * @see sol_ptr_vector_find_sorted()
 * @see sol_ptr_vector_insert_sorted()
 */
static inline int32_t
sol_ptr_vector_find_last_sorted(const struct sol_ptr_vector *pv, const void *elem, int (*compare_cb)(const void *data1, const void *data2))
{
    uint16_t i;
    int32_t r, found_i = -ENODATA;

    r = sol_ptr_vector_match_sorted(pv, elem, compare_cb);
    if (r < 0)
        return r;

    for (i = r; i < pv->base.len; i++) {
        const void *other = sol_ptr_vector_get_no_check(pv, i);

        if (compare_cb(elem, other) != 0)
            break;

        if (elem == other)
            found_i = i;
    }

    if (found_i >= 0)
        return found_i;

    for (i = r; i > 0; i--) {
        const void *other = sol_ptr_vector_get_no_check(pv, i - 1);

        if (compare_cb(elem, other) != 0)
            break;

        if (elem == other)
            return i - 1;
    }

    return -ENODATA;
}

/**
 * @brief Find the first occurrence of @c elem in the sorted vector @c pv.
 *
 * @param pv Pointer Vector pointer (already sorted)
 * @param elem Element to find
 * @param compare_cb Function to compare elements in the list. It should return an integer
 * less than, equal to, or greater than zero if @c data1 is found, respectively,
 * to be less than, to match, or be greater than @c data2 in the sort order
 *
 * @return index (>=0) on success, error code (always negative) otherwise
 *
 * @remark Time complexity: logarithmic in vector size or linear in number of elements
 * equal to @a elem, whichever is greater
 *
 * @see sol_ptr_vector_find_first()
 * @see sol_ptr_vector_find_first()
 * @see sol_ptr_vector_find_first_sorted()
 * @see sol_ptr_vector_match_sorted()
 * @see sol_ptr_vector_match_first()
 * @see sol_ptr_vector_match_last()
 * @see sol_ptr_vector_find_sorted()
 * @see sol_ptr_vector_insert_sorted()
 */
static inline int32_t
sol_ptr_vector_find_first_sorted(const struct sol_ptr_vector *pv, const void *elem, int (*compare_cb)(const void *data1, const void *data2))
{
    uint16_t i;
    int32_t r, found_i = -ENODATA;

    r = sol_ptr_vector_match_sorted(pv, elem, compare_cb);
    if (r < 0)
        return r;

    for (i = r;;) {
        const void *other = sol_ptr_vector_get_no_check(pv, i);

        if (compare_cb(elem, other) != 0)
            break;

        if (elem == other)
            found_i = i;

        if (i == 0)
            break;
        i--;
    }

    if (found_i >= 0)
        return found_i;

    for (i = r; i + 1 < pv->base.len; i++) {
        const void *other = sol_ptr_vector_get_no_check(pv, i + 1);

        if (compare_cb(elem, other) != 0)
            break;

        if (elem == other)
            return i + 1;
    }

    return -ENODATA;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
