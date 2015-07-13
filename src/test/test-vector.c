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

#include "test.h"
#include "sol-vector.h"
#include "sol-util.h"

struct s {
    int a;
    int b;
    int c;
};

DEFINE_TEST(test_vector);

static void
test_vector(void)
{
    static const unsigned int N = 16;
    struct sol_vector v;
    struct s *s;
    uint16_t i;

    sol_vector_init(&v, sizeof(struct s));

    // Add two elements.
    s = sol_vector_append(&v);
    s->a = 1;
    s->b = 1;
    s->c = 1;
    s = sol_vector_append(&v);
    s->a = 2;
    s->b = 2;
    s->c = 2;
    ASSERT_INT_EQ(v.len, 2);

    // Add more elements.
    for (i = 0; i < N; i++) {
        s = sol_vector_append(&v);
        s->a = i * 100;
    }
    ASSERT_INT_EQ(v.len, N + 2);

    // Delete two first elements.
    sol_vector_del(&v, 0);
    sol_vector_del(&v, 0);
    ASSERT_INT_EQ(v.len, N);

    // Verify elements.
    for (i = 0; i < N; i++) {
        s = sol_vector_get(&v, i);
        ASSERT_INT_EQ(s->a, i * 100);
    }

    SOL_VECTOR_FOREACH_IDX (&v, s, i) {
        ASSERT_INT_EQ(s->a, i * 100);
    }

    SOL_VECTOR_FOREACH_REVERSE_IDX (&v, s, i) {
        ASSERT_INT_EQ(s->a, i * 100);
    }

    // Delete remaining elements.
    while (v.len > 0)
        sol_vector_del(&v, 0);
    ASSERT_INT_EQ(v.len, 0);

    s = sol_vector_append(&v);
    s->a = 1;
    s->b = 1;
    s->c = 1;
    ASSERT_INT_EQ(v.len, 1);

    sol_vector_clear(&v);
    ASSERT_INT_EQ(v.len, 0);
}

static struct s *
create_s(int value)
{
    struct s *result;

    result = malloc(sizeof(*result));
    if (!result)
        return NULL;
    result->a = result->b = result->c = value;
    return result;
}

static int
sort_cb(const void *data1, const void *data2)
{
    const struct s *s1 = data1;
    const struct s *s2 = data2;

    return sol_util_int_compare(s1->a, s2->a);
}

DEFINE_TEST(test_ptr_vector_sorted);

static void
test_ptr_vector_sorted(void)
{
    struct sol_ptr_vector pv;
    struct s *s;
    uint16_t i;
    int array_unsorted[] = { 5, 3, 2, 9, 4, 3, 12, -1, 8, 30, 19, 10, 13 };
    int array_sorted[] = { -1, 2, 3, 3, 4, 5, 8, 9, 10, 12, 13, 19, 30 };

    sol_ptr_vector_init(&pv);
    for (i = 0; i < (sizeof(array_unsorted) / sizeof(int)); i++) {
        sol_ptr_vector_insert_sorted(&pv, create_s(array_unsorted[i]), sort_cb);
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&pv, s, i) {
        ASSERT_INT_EQ(s->a, array_sorted[i]);
    }

    while (pv.base.len > 0)
        free(sol_ptr_vector_take(&pv, 0));
}

DEFINE_TEST(test_ptr_vector);

static void
test_ptr_vector(void)
{
    static const unsigned int N = 16;
    struct sol_ptr_vector pv;
    struct s *s;
    uint16_t i;

    sol_ptr_vector_init(&pv);

    // Add two elements.
    sol_ptr_vector_append(&pv, create_s(1));
    sol_ptr_vector_append(&pv, create_s(2));
    ASSERT_INT_EQ(pv.base.len, 2);

    // Add more elements.
    for (i = 0; i < N; i++)
        sol_ptr_vector_append(&pv, create_s(i * 100));
    ASSERT_INT_EQ(pv.base.len, N + 2);

    // Delete two first elements.
    free(sol_ptr_vector_take(&pv, 0));
    free(sol_ptr_vector_take(&pv, 0));
    ASSERT_INT_EQ(pv.base.len, N);

    // Verify elements.
    SOL_PTR_VECTOR_FOREACH_IDX (&pv, s, i) {
        ASSERT_INT_EQ(s->a, i * 100);
    }
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&pv, s, i) {
        ASSERT_INT_EQ(s->a, i * 100);
    }

    // Delete remaining elements.
    while (pv.base.len > 0)
        free(sol_ptr_vector_take(&pv, 0));
    ASSERT_INT_EQ(pv.base.len, 0);

    sol_ptr_vector_append(&pv, create_s(1));
    ASSERT_INT_EQ(pv.base.len, 1);

    free(sol_ptr_vector_take(&pv, 0));
    ASSERT_INT_EQ(pv.base.len, 0);
}


struct custom {
    int value;

    /* Enough to cause noise if traversal is wrong. */
    int noise[16];
};

DEFINE_TEST(vector_iterates_correctly_using_void_as_iterator);

static void
vector_iterates_correctly_using_void_as_iterator(void)
{
    struct sol_vector v_ = SOL_VECTOR_INIT(struct custom), *v = &v_;
    struct custom *c;
    void *ptr;
    uint16_t i;

    for (i = 0; i < 16; i++) {
        c = sol_vector_append(v);
        c->value = i;
        memset(c->noise, -1, 16 * sizeof(int));
    }

    for (i = 0; i < v->len; i++) {
        c = sol_vector_get(v, i);
        ASSERT_INT_EQ(c->value, i);
    }

    SOL_VECTOR_FOREACH_IDX (v, ptr, i) {
        c = sol_vector_get(v, i);
        ASSERT(c == ptr);
    }

    SOL_VECTOR_FOREACH_REVERSE_IDX (v, ptr, i) {
        c = sol_vector_get(v, i);
        ASSERT(c == ptr);
    }

    sol_vector_clear(v);
}

DEFINE_TEST(vector_take_all);

static void
vector_take_all(void)
{
    struct sol_vector v_ = SOL_VECTOR_INIT(int), *v = &v_;
    int *elem, *taken;
    uint16_t i;

    for (i = 0; i < 16; i++) {
        elem = sol_vector_append(v);
        *elem = i;
    }

    ASSERT_INT_EQ(v->len, 16);

    taken = sol_vector_take_all(v);

    ASSERT(taken);
    ASSERT_INT_EQ(v->len, 0);

    for (i = 0; i < 16; i++)
        ASSERT_INT_EQ(taken[i], i);

    for (i = 0; i < 16; i++) {
        elem = sol_vector_append(v);
        *elem = -1;
    }

    ASSERT_INT_EQ(v->len, 16);

    for (i = 0; i < 16; i++)
        ASSERT_INT_EQ(taken[i], i);

    sol_vector_clear(v);
    free(taken);
}


TEST_MAIN();
