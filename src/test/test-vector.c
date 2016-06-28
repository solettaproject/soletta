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

#include "test.h"
#include "sol-vector.h"
#include "sol-util-internal.h"

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
    struct s *s, match;
    uint16_t i;
    int array_unsorted[] = { 5, 3, 2, 9, 4, 3, 12, -1, 8, 30, 19, 10, 13, 2, 2 };
    int array_sorted[] = { -1, 2, 2, 2, 3, 3, 4, 5, 8, 9, 10, 12, 13, 19, 30 };
    int32_t found;

    sol_ptr_vector_init(&pv);
    for (i = 0; i < (sizeof(array_unsorted) / sizeof(int)); i++) {
        int32_t ret;

        s = create_s(array_unsorted[i]);
        s->b = i;
        ret = sol_ptr_vector_insert_sorted(&pv, s, sort_cb);
        ASSERT(ret >= 0);

        found = sol_ptr_vector_find_sorted(&pv, s, sort_cb);
        ASSERT_INT_EQ(ret, found);
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&pv, s, i) {
        ASSERT_INT_EQ(s->a, array_sorted[i]);
        if (i > 0) {
            /* appending already existing elements should be after
             * already existing (stable)
             */
            struct s *prev = sol_ptr_vector_get(&pv, i - 1);
            if (prev->a == s->a)
                ASSERT(prev->b < s->b);
        }

        found = sol_ptr_vector_find_first_sorted(&pv, s, sort_cb);
        ASSERT_INT_EQ(found, (int)i);

        found = sol_ptr_vector_find_last_sorted(&pv, s, sort_cb);
        ASSERT_INT_EQ(found, (int)i);
    }

    match.a = 2;

    found = sol_ptr_vector_match_first(&pv, &match, sort_cb);
    ASSERT_INT_EQ(found, 1);

    found = sol_ptr_vector_match_last(&pv, &match, sort_cb);
    ASSERT_INT_EQ(found, 3);

    match.a = -1;
    found = sol_ptr_vector_match_sorted(&pv, &match, sort_cb);
    ASSERT_INT_EQ(found, 0);

    while (pv.base.len > 0)
        free(sol_ptr_vector_steal(&pv, 0));
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
    free(sol_ptr_vector_steal(&pv, 0));
    free(sol_ptr_vector_steal(&pv, 0));
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
        free(sol_ptr_vector_steal(&pv, 0));
    ASSERT_INT_EQ(pv.base.len, 0);

    sol_ptr_vector_append(&pv, create_s(1));
    ASSERT_INT_EQ(pv.base.len, 1);

    free(sol_ptr_vector_steal(&pv, 0));
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

DEFINE_TEST(vector_take_data);

static void
vector_take_data(void)
{
    struct sol_vector v_ = SOL_VECTOR_INIT(int), *v = &v_;
    int *elem, *taken;
    uint16_t i;

    for (i = 0; i < 16; i++) {
        elem = sol_vector_append(v);
        *elem = i;
    }

    ASSERT_INT_EQ(v->len, 16);

    taken = sol_vector_steal_data(v);

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

DEFINE_TEST(vector_append_n);

static void
vector_append_n(void)
{
    struct sol_vector v_ = SOL_VECTOR_INIT(int), *v = &v_;
    int *elem;
    uint16_t i;

    elem = sol_vector_append_n(v, 16);
    ASSERT(elem);
    ASSERT_INT_EQ(v->len, 16);
    ASSERT(elem == sol_vector_get(v, v->len - 16));

    for (i = 0; i < 16; i++) {
        *elem = i;
        elem++;
    }

    elem = sol_vector_append(v);
    ASSERT(elem);
    ASSERT(elem == sol_vector_get(v, v->len - 1));
    ASSERT_INT_EQ(v->len, 17);

    elem = sol_vector_append_n(v, 1);
    ASSERT(elem);
    ASSERT(elem == sol_vector_get(v, v->len - 1));
    ASSERT_INT_EQ(v->len, 18);

    errno = 0;
    elem = sol_vector_append_n(v, 0);
    ASSERT(!elem);
    ASSERT_INT_EQ(errno, EINVAL);

    errno = 0;
    elem = sol_vector_append_n(v, UINT16_MAX);
    ASSERT(!elem);
    ASSERT_INT_EQ(errno, EOVERFLOW);
    ASSERT_INT_EQ(v->len, 18);

    errno = 0;
    elem = sol_vector_append_n(v, UINT16_MAX - v->len + 1);
    ASSERT(!elem);
    ASSERT_INT_EQ(errno, EOVERFLOW);
    ASSERT_INT_EQ(v->len, 18);

    errno = 0;
    elem = sol_vector_append_n(NULL, 1);
    ASSERT(!elem);
    ASSERT_INT_EQ(errno, EINVAL);

    sol_vector_clear(v);
}


DEFINE_TEST(vector_initializes_elements_to_zero);

static void
vector_initializes_elements_to_zero(void)
{
    struct sol_vector v_ = SOL_VECTOR_INIT(int), *v = &v_;
    int *elem;
    uint16_t i;

    elem = sol_vector_append_n(v, 16);
    ASSERT(elem);
    ASSERT_INT_EQ(v->len, 16);
    ASSERT(elem == sol_vector_get(v, v->len - 16));

    for (i = 0; i < 16; i++) {
        ASSERT_INT_EQ(*elem, 0);
        elem++;
    }

    elem = sol_vector_append(v);
    ASSERT(elem);
    ASSERT(elem == sol_vector_get(v, v->len - 1));
    ASSERT_INT_EQ(v->len, 17);
    ASSERT_INT_EQ(*elem, 0);

    sol_vector_clear(v);
}


DEFINE_TEST(test_vector_del_range);

static void
test_vector_del_range(void)
{
    static const unsigned int N = 16;
    struct sol_vector v;
    uint32_t *item;
    uint16_t i;

    sol_vector_init(&v, sizeof(uint32_t));

    // Add elements.
    for (i = 0; i < N; i++) {
        item = sol_vector_append(&v);
        ASSERT(item);
        *item = i;
    }
    ASSERT_INT_EQ(v.len, N);

    // Delete elements.
    sol_vector_del_range(&v, 0, 2);
    ASSERT_INT_EQ(v.len, N - 2);

    // Verify elements.
    for (i = 0; i < N - 2; i++) {
        item = sol_vector_get(&v, i);
        ASSERT(item);
        ASSERT_INT_EQ(*item, i + 2);
    }

    // Delete elements.
    sol_vector_del_range(&v, N - 4, 2);
    ASSERT_INT_EQ(v.len, N - 4);

    // Verify elements.
    for (i = 0; i < N - 4; i++) {
        item = sol_vector_get(&v, i);
        ASSERT(item);
        ASSERT_INT_EQ(*item, i + 2);
    }

    // Delete elements.
    sol_vector_del_range(&v, N / 2, 3);
    ASSERT_INT_EQ(v.len, N - 7);

    // Verify elements.
    for (i = 0; i < N - 7; i++) {
        item = sol_vector_get(&v, i);
        ASSERT(item);
        if (i < N / 2) {
            ASSERT_INT_EQ(*item, i + 2);
        } else {
            ASSERT_INT_EQ(*item, i + 2 + 3);
        }
    }

    sol_vector_clear(&v);
}

DEFINE_TEST(test_vector_del);

static void
test_vector_del(void)
{
    static const unsigned int N = 16;
    struct sol_vector v;
    struct s *s;
    uint16_t i;
    int r;

    sol_vector_init(&v, sizeof(struct s));

    // Add elements.
    s = sol_vector_append(&v);
    s->a = 999;
    for (i = 0; i < N; i++) {
        if (i == 10) {
            s = sol_vector_append(&v);
            s->a = 999;
        }
        s = sol_vector_append(&v);
        s->a = i;
    }
    s = sol_vector_append(&v);
    s->a = 999;
    s = sol_vector_append(&v);
    s->a = 999;
    ASSERT_INT_EQ(v.len, N + 4);

    // Delete elements.
    sol_vector_del_element(&v, sol_vector_get(&v, 0));
    sol_vector_del_element(&v, sol_vector_get(&v, 10));
    sol_vector_del_element(&v, sol_vector_get(&v, N));
    sol_vector_del_last(&v);
    ASSERT_INT_EQ(v.len, N);

    // Verify elements.
    for (i = 0; i < N; i++) {
        s = sol_vector_get(&v, i);
        ASSERT_INT_EQ(s->a, i);
    }

    r = sol_vector_del_element(&v, 0);
    ASSERT_INT_EQ(r, -ENOENT);

    r = sol_vector_del_element(&v, (const char *)v.data + v.elem_size * N);
    ASSERT_INT_EQ(r, -ENOENT);

    r = sol_vector_del_element(&v, (const char *)v.data + v.elem_size * (-1));
    ASSERT_INT_EQ(r, -ENOENT);

    r = sol_vector_del_element(&v, (const char *)v.data +
        (int)(v.elem_size / 2));
    ASSERT_INT_EQ(r, -ENOENT);

    ASSERT_INT_EQ(v.len, N);
    sol_vector_clear(&v);
}


DEFINE_TEST(test_ptr_vector_del);

static void
test_ptr_vector_del(void)
{
    static const unsigned int N = 16;
    struct sol_ptr_vector pv;
    struct s *s;
    uint16_t i;
    int r;

    sol_ptr_vector_init(&pv);

    // Add two elements.
    sol_ptr_vector_append(&pv, create_s(999));

    // Add more elements.
    for (i = 0; i < N; i++) {
        if (i == 10)
            sol_ptr_vector_append(&pv, create_s(999));
        sol_ptr_vector_append(&pv, create_s(i));
    }
    sol_ptr_vector_append(&pv, create_s(999));
    sol_ptr_vector_append(&pv, create_s(999));
    ASSERT_INT_EQ(pv.base.len, N + 4);

    // Delete elements
    s = sol_ptr_vector_get(&pv, 0);
    r = sol_ptr_vector_del_element(&pv, s);
    ASSERT_INT_EQ(r, 0);
    free(s);

    s = sol_ptr_vector_get(&pv, 10);
    r = sol_ptr_vector_del_element(&pv, s);
    ASSERT_INT_EQ(r, 0);
    free(s);

    s = sol_ptr_vector_get(&pv, N);
    r = sol_ptr_vector_del_element(&pv, s);
    ASSERT_INT_EQ(r, 0);
    free(s);

    free(sol_ptr_vector_steal_last(&pv));
    ASSERT_INT_EQ(pv.base.len, N);

    s = create_s(999);
    sol_ptr_vector_append(&pv, s);
    ASSERT_INT_EQ(pv.base.len, N + 1);
    sol_ptr_vector_del_last(&pv);
    ASSERT_INT_EQ(pv.base.len, N);
    free(s);

    r = sol_ptr_vector_del_element(&pv, &pv);
    ASSERT_INT_EQ(r, -ENOENT);
    ASSERT_INT_EQ(pv.base.len, N);

    s = create_s(999);
    sol_ptr_vector_append(&pv, s);
    sol_ptr_vector_append(&pv, s);
    sol_ptr_vector_append(&pv, s);
    r = sol_ptr_vector_del_element(&pv, s);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(pv.base.len, N);
    free(s);

    // Verify elements.
    SOL_PTR_VECTOR_FOREACH_IDX (&pv, s, i) {
        ASSERT_INT_EQ(s->a, i);
    }

    // Delete remaining elements.
    while (pv.base.len > 0)
        free(sol_ptr_vector_steal(&pv, 0));
    ASSERT_INT_EQ(pv.base.len, 0);

    r = sol_ptr_vector_del_element(&pv, &pv);
    ASSERT_INT_EQ(r, -ENOENT);

    r = sol_ptr_vector_del_last(&pv);
    ASSERT_INT_EQ(r, 0);

    s = sol_ptr_vector_steal_last(&pv);
    ASSERT(s == NULL);

    s = sol_ptr_vector_steal_last(&pv);
    ASSERT(s == NULL);
}

TEST_MAIN();
