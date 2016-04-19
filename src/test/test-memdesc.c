/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#include <errno.h>

#include "sol-memdesc.h"
#include "sol-util-internal.h"

#include "test.h"

#define TEST_SIMPLE_INTEGER(ctype, mdtype, access, defval) \
    DEFINE_TEST(test_simple_ ## mdtype); \
    static void test_simple_ ## mdtype(void) { \
        const struct sol_memdesc desc = { \
            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, ) \
            .type = mdtype, \
            .name = "" # ctype, \
            .defcontent.access = defval \
        }; \
        ctype a, b, c; \
        int r; \
        r = sol_memdesc_init_defaults(&desc, &a); \
        ASSERT_INT_EQ(r, 0); \
        ASSERT(a == desc.defcontent.access); \
        r = sol_memdesc_init_defaults(&desc, &b); \
        ASSERT_INT_EQ(r, 0); \
        ASSERT(b == desc.defcontent.access); \
        r = sol_memdesc_compare(&desc, &a, &b); \
        ASSERT_INT_EQ(r, 0); \
        ASSERT_INT_EQ(errno, 0); \
        c = a + 1; \
        r = sol_memdesc_set_content(&desc, &a, &c); \
        ASSERT_INT_EQ(r, 0); \
        ASSERT(a == c); \
        r = sol_memdesc_compare(&desc, &a, &b); \
        ASSERT(r > 0); \
        sol_memdesc_free_content(&desc, &a); \
        sol_memdesc_free_content(&desc, &b); \
    }

TEST_SIMPLE_INTEGER(uint8_t, SOL_MEMDESC_TYPE_UINT8, u8, 0xf2);
TEST_SIMPLE_INTEGER(uint16_t, SOL_MEMDESC_TYPE_UINT16, u16, 0xf234);
TEST_SIMPLE_INTEGER(uint32_t, SOL_MEMDESC_TYPE_UINT32, u32, 0xf2345678);
TEST_SIMPLE_INTEGER(uint64_t, SOL_MEMDESC_TYPE_UINT64, u64, 0xf234567890123456);
TEST_SIMPLE_INTEGER(unsigned long, SOL_MEMDESC_TYPE_ULONG, ul, ULONG_MAX / 10);
TEST_SIMPLE_INTEGER(size_t, SOL_MEMDESC_TYPE_SIZE, sz, SIZE_MAX / 10);

TEST_SIMPLE_INTEGER(int8_t, SOL_MEMDESC_TYPE_INT8, i8, 0x72);
TEST_SIMPLE_INTEGER(int16_t, SOL_MEMDESC_TYPE_INT16, i16, 0x7234);
TEST_SIMPLE_INTEGER(int32_t, SOL_MEMDESC_TYPE_INT32, i32, 0x72345678);
TEST_SIMPLE_INTEGER(int64_t, SOL_MEMDESC_TYPE_INT64, i64, 0x7234567890123456);
TEST_SIMPLE_INTEGER(long, SOL_MEMDESC_TYPE_LONG, l, LONG_MAX / 10);
TEST_SIMPLE_INTEGER(ssize_t, SOL_MEMDESC_TYPE_SSIZE, ssz, SSIZE_MAX / 10);


DEFINE_TEST(test_simple_SOL_MEMDESC_TYPE_BOOLEAN);
static void
test_simple_SOL_MEMDESC_TYPE_BOOLEAN(void)
{
    const struct sol_memdesc desc = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
        .type = SOL_MEMDESC_TYPE_BOOLEAN,
        .name = "SOL_MEMDESC_TYPE_BOOLEAN",
        .defcontent.b = true,
    };
    bool a, b, c;
    int r;

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a == desc.defcontent.b);

    r = sol_memdesc_init_defaults(&desc, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT(b == desc.defcontent.b);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    c = false;
    r = sol_memdesc_set_content(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a == c);

    r = sol_memdesc_compare(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT(r < 0);

    sol_memdesc_free_content(&desc, &a);
    sol_memdesc_free_content(&desc, &b);
}

DEFINE_TEST(test_simple_SOL_MEMDESC_TYPE_DOUBLE);
static void
test_simple_SOL_MEMDESC_TYPE_DOUBLE(void)
{
    const struct sol_memdesc desc = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
        .type = SOL_MEMDESC_TYPE_DOUBLE,
        .name = "SOL_MEMDESC_TYPE_DOUBLE",
        .defcontent.d = 1.2345e-67,
    };
    double a, b, c;
    int r;

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_util_double_equal(a, desc.defcontent.d));

    r = sol_memdesc_init_defaults(&desc, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_util_double_equal(b, desc.defcontent.d));

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    c = a + 1;
    r = sol_memdesc_set_content(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT(sol_util_double_equal(a, c));

    r = sol_memdesc_compare(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT(r > 0);

    sol_memdesc_free_content(&desc, &a);
    sol_memdesc_free_content(&desc, &b);
}

DEFINE_TEST(test_simple_SOL_MEMDESC_TYPE_STRING);
static void
test_simple_SOL_MEMDESC_TYPE_STRING(void)
{
    const struct sol_memdesc desc = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
        .type = SOL_MEMDESC_TYPE_STRING,
        .name = "SOL_MEMDESC_TYPE_STRING",
        .defcontent.s = "hello world"
    };
    char *a, *b;
    const char *c;
    int r;

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a != desc.defcontent.s);
    ASSERT_STR_EQ(a, "hello world");

    r = sol_memdesc_init_defaults(&desc, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT(b != desc.defcontent.s);
    ASSERT_STR_EQ(b, "hello world");

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    c = "other string";
    r = sol_memdesc_set_content(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a != c);
    ASSERT_STR_EQ(a, "other string");

    r = sol_memdesc_compare(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT(r > 0);

    sol_memdesc_free_content(&desc, &a);
    sol_memdesc_free_content(&desc, &b);
}

DEFINE_TEST(test_simple_SOL_MEMDESC_TYPE_CONST_STRING);
static void
test_simple_SOL_MEMDESC_TYPE_CONST_STRING(void)
{
    const struct sol_memdesc desc = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
        .type = SOL_MEMDESC_TYPE_CONST_STRING,
        .name = "SOL_MEMDESC_TYPE_CONST_STRING",
        .defcontent.s = "hello world"
    };
    const char *a, *b, *c;
    int r;

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a == desc.defcontent.s);

    r = sol_memdesc_init_defaults(&desc, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT(b == desc.defcontent.s);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    c = "other const string";
    r = sol_memdesc_set_content(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a == c);

    r = sol_memdesc_compare(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT(r > 0);

    sol_memdesc_free_content(&desc, &a);
    sol_memdesc_free_content(&desc, &b);
}

DEFINE_TEST(test_simple_SOL_MEMDESC_TYPE_PTR);
static void
test_simple_SOL_MEMDESC_TYPE_PTR(void)
{
    const struct sol_memdesc desc = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
        .type = SOL_MEMDESC_TYPE_PTR,
        .name = "SOL_MEMDESC_TYPE_PTR",
        .defcontent.p = (void *)0x1234
    };
    const char *a, *b, *c;
    int r;

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a == desc.defcontent.s);

    r = sol_memdesc_init_defaults(&desc, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT(b == desc.defcontent.s);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    c = a + 1;
    r = sol_memdesc_set_content(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a == c);

    r = sol_memdesc_compare(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT(r > 0);

    sol_memdesc_free_content(&desc, &a);
    sol_memdesc_free_content(&desc, &b);
}

DEFINE_TEST(test_SOL_MEMDESC_TYPE_PTR_of_uint64);
static void
test_SOL_MEMDESC_TYPE_PTR_of_uint64(void)
{
    const uint64_t defval = 0xf234567890123456;
    struct sol_memdesc desc = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
        .type = SOL_MEMDESC_TYPE_PTR,
        .name = "SOL_MEMDESC_TYPE_PTR",
        .defcontent.p = &defval,
        .children = &(const struct sol_memdesc){
            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
            .type = SOL_MEMDESC_TYPE_UINT64,
            .name = "SOL_MEMDESC_TYPE_UINT64",
            .defcontent.u64 = 0xdeadbeaf, /* not used due &defval */
        },
    };
    uint64_t *a, *b, *c, d;
    int r;

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT(*a == defval);

    r = sol_memdesc_init_defaults(&desc, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT(*b == defval);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);
    ASSERT(a != b);

    d = *a + 1;
    c = &d;
    r = sol_memdesc_set_content(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT(*a == d);

    c = NULL;
    r = sol_memdesc_set_content(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a == NULL);

    c = &d;
    r = sol_memdesc_set_content(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT(*a == d);

    r = sol_memdesc_compare(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT(r > 0);

    sol_memdesc_free_content(&desc, &a);
    sol_memdesc_free_content(&desc, &b);

    desc.defcontent.p = NULL; /* no value to set, pointer is null */
    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a == NULL);
}

DEFINE_TEST(test_SOL_MEMDESC_TYPE_STRUCTURE);
static void
test_SOL_MEMDESC_TYPE_STRUCTURE(void)
{
    struct myst {
        int64_t i64;
        char *s;
        uint8_t u8;
    };
    const struct myst defval = {
        .i64 = 0x7234567890123456,
        .s = (char *)"hello world",
        .u8 = 0xf2
    };
    struct sol_memdesc desc = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
        .size = sizeof(struct myst),
        .type = SOL_MEMDESC_TYPE_STRUCTURE,
        .name = "struct myst",
        .defcontent.p = &defval,
        .children = (const struct sol_memdesc[]){
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct myst, i64),
                .type = SOL_MEMDESC_TYPE_INT64,
                .name = "i64",
                .defcontent.i64 = 0xdeadbeaf,
            },
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct myst, s),
                .type = SOL_MEMDESC_TYPE_STRING,
                .name = "s",
                .defcontent.s = "xxx",
            },
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct myst, u8),
                .type = SOL_MEMDESC_TYPE_UINT8,
                .name = "u8",
                .defcontent.u8 = 0x12,
            },
            {}
        },
    };
    struct myst a, b, c;
    int r;

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a.i64 == defval.i64);
    ASSERT_STR_EQ(a.s, defval.s);
    ASSERT(a.u8 == defval.u8);

    r = sol_memdesc_compare(&desc, &a, &defval);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_init_defaults(&desc, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT(b.i64 == defval.i64);
    ASSERT_STR_EQ(b.s, defval.s);
    ASSERT(b.u8 == defval.u8);

    r = sol_memdesc_compare(&desc, &b, &defval);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    c.i64 = a.i64 + 1;
    c.s = (char *)"other string";
    c.u8 = a.u8 + 1;
    r = sol_memdesc_set_content(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a.i64 == c.i64);
    ASSERT_STR_EQ(a.s, c.s);
    ASSERT(a.u8 == c.u8);

    r = sol_memdesc_compare(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT(r > 0);

    sol_memdesc_free_content(&desc, &a);
    sol_memdesc_free_content(&desc, &b);

    desc.defcontent.p = NULL; /* use defcontent of each member */

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a.i64 == desc.children[0].defcontent.i64);
    ASSERT_STR_EQ(a.s, desc.children[1].defcontent.s);
    ASSERT(a.u8 == desc.children[2].defcontent.u8);

    sol_memdesc_free_content(&desc, &a);
}

DEFINE_TEST(test_SOL_MEMDESC_TYPE_STRUCTURE_of_struct);
static void
test_SOL_MEMDESC_TYPE_STRUCTURE_of_struct(void)
{
    struct otherst {
        bool b;
        char *s;
        long l;
    };
    struct myst {
        int64_t i64;
        char *s;
        struct otherst st;
        struct otherst *pst;
        uint8_t u8;
    };
    const struct otherst defvalother = {
        .b = true,
        .s = (char *)"other st here",
        .l = LONG_MAX / 10
    };
    const struct myst defval = {
        .i64 = 0x7234567890123456,
        .s = (char *)"hello world",
        .st = defvalother,
        .pst = (struct otherst *)&defvalother,
        .u8 = 0xf2
    };
    const struct sol_memdesc otherdesc = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
        .size = sizeof(struct otherst),
        .type = SOL_MEMDESC_TYPE_STRUCTURE,
        .name = "struct otherst",
        .children = (const struct sol_memdesc[]){
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct otherst, b),
                .type = SOL_MEMDESC_TYPE_BOOLEAN,
                .name = "b",
                .defcontent.i64 = true,
            },
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct otherst, s),
                .type = SOL_MEMDESC_TYPE_STRING,
                .name = "s",
                .defcontent.s = "other st default value",
            },
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct otherst, l),
                .type = SOL_MEMDESC_TYPE_LONG,
                .name = "l",
                .defcontent.l = LONG_MAX / 20
            },
            { }
        },

    };
    struct sol_memdesc desc = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
        .size = sizeof(struct myst),
        .type = SOL_MEMDESC_TYPE_STRUCTURE,
        .name = "struct myst",
        .defcontent.p = &defval,
        .children = (const struct sol_memdesc[]){
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct myst, i64),
                .type = SOL_MEMDESC_TYPE_INT64,
                .name = "i64",
                .defcontent.i64 = 0xdeadbeaf,
            },
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct myst, s),
                .type = SOL_MEMDESC_TYPE_STRING,
                .name = "s",
                .defcontent.s = "xxx",
            },
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct myst, st),
                .size = sizeof(struct otherst),
                .type = SOL_MEMDESC_TYPE_STRUCTURE,
                .name = "st",
                .children = otherdesc.children,
            },
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct myst, pst),
                .type = SOL_MEMDESC_TYPE_PTR,
                .name = "pst",
                .children = (const struct sol_memdesc []){
                    otherdesc,
                    {}
                },
            },
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct myst, u8),
                .type = SOL_MEMDESC_TYPE_UINT8,
                .name = "u8",
                .defcontent.u8 = 0x12,
            },
            {}
        },
    };
    struct myst a, b, c;
    int r;

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a.i64 == defval.i64);
    ASSERT_STR_EQ(a.s, defval.s);
    ASSERT(a.st.b == defval.st.b);
    ASSERT_STR_EQ(a.st.s, defval.st.s);
    ASSERT(a.st.l == defval.st.l);
    ASSERT(a.pst);
    ASSERT(a.pst->b == defval.st.b);
    ASSERT_STR_EQ(a.pst->s, defval.st.s);
    ASSERT(a.pst->l == defval.st.l);
    ASSERT(a.u8 == defval.u8);

    r = sol_memdesc_compare(&desc, &a, &defval);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_init_defaults(&desc, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT(b.i64 == defval.i64);
    ASSERT_STR_EQ(b.s, defval.s);
    ASSERT(b.st.b == defval.st.b);
    ASSERT_STR_EQ(b.st.s, defval.st.s);
    ASSERT(b.st.l == defval.st.l);
    ASSERT(b.pst);
    ASSERT(b.pst->b == defval.st.b);
    ASSERT_STR_EQ(b.pst->s, defval.st.s);
    ASSERT(b.pst->l == defval.st.l);
    ASSERT(b.u8 == defval.u8);

    r = sol_memdesc_compare(&desc, &b, &defval);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    c = a;
    c.st.l = a.st.l + 1;
    c.st.s = (char *)"x: a is not c"; /* makes compare() return 1 */
    c.pst = NULL;
    r = sol_memdesc_set_content(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a.i64 == c.i64);
    ASSERT_STR_EQ(a.s, c.s);
    ASSERT(a.st.b == c.st.b);
    ASSERT_STR_EQ(a.st.s, c.st.s);
    ASSERT(a.st.l == c.st.l);
    ASSERT(a.pst == NULL);
    ASSERT(a.u8 == c.u8);

    r = sol_memdesc_compare(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT(r > 0);


    sol_memdesc_free_content(&desc, &a);
    sol_memdesc_free_content(&desc, &b);

    desc.defcontent.p = NULL; /* use defcontent of each member */

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a.i64 == desc.children[0].defcontent.i64);
    ASSERT_STR_EQ(a.s, desc.children[1].defcontent.s);
    ASSERT(a.st.b == desc.children[2].children[0].defcontent.b);
    ASSERT_STR_EQ(a.st.s, desc.children[2].children[1].defcontent.s);
    ASSERT(a.st.l == desc.children[2].children[2].defcontent.l);
    ASSERT(!a.pst);
    ASSERT(a.u8 == desc.children[4].defcontent.u8);

    sol_memdesc_free_content(&desc, &a);
}

/* sol_vector links elem_size and len to access data. */
/*
 * SOL_MEMDESC_TYPE_ARRAY with strdup()/free()/strcmp()/strlen() as
 * operations should behave the same as SOL_MEMDESC_TYPE_STRING.
 */
static int
vector_ops_set_content(const struct sol_memdesc *desc, void *container, const void *ptr_content)
{
    const struct sol_vector *pv = ptr_content;
    struct sol_vector *v = sol_memdesc_get_memory(desc, container);
    void *m;

    sol_vector_clear(v);
    v->elem_size = pv->elem_size;

    m = sol_vector_append_n(v, pv->len);
    if (!m)
        return -ENOMEM;

    memcpy(m, pv->data, pv->len * pv->elem_size);

    return 0;
}

static int
vector_ops_free_content(const struct sol_memdesc *desc, void *container)
{
    struct sol_vector *v = sol_memdesc_get_memory(desc, container);

    sol_vector_clear(v);
    return 0;
}


DEFINE_TEST(test_vector_SOL_MEMDESC_TYPE_STRUCTURE);
static void
test_vector_SOL_MEMDESC_TYPE_STRUCTURE(void)
{
    struct sol_vector defval = SOL_VECTOR_INIT(int32_t);
    const struct sol_memdesc_ops vector_ops = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_OPS_API_VERSION, )
        .set_content = vector_ops_set_content,
        .free_content = vector_ops_free_content,
    };
    struct sol_memdesc desc = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
        .size = sizeof(struct sol_vector),
        .type = SOL_MEMDESC_TYPE_STRUCTURE,
        .name = "struct sol_vector",
        .defcontent.p = &defval,
        .children = (const struct sol_memdesc[]){
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct sol_vector, data),
                .type = SOL_MEMDESC_TYPE_PTR,
                .name = "data",
            },
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct sol_vector, len),
                .type = SOL_MEMDESC_TYPE_UINT16,
                .name = "len",
            },
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct sol_vector, elem_size),
                .type = SOL_MEMDESC_TYPE_UINT16,
                .name = "elem_size",
            },
            { }
        },
        .ops = &vector_ops,
    };
    uint32_t i, *pv;
    struct sol_vector a, b, c = {};
    int r;

    pv = sol_vector_append_n(&defval, 16);
    ASSERT(pv);
    for (i = 0; i < defval.len; i++)
        pv[i] = i;

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(a.len, defval.len);
    ASSERT_INT_EQ(a.elem_size, defval.elem_size);
    ASSERT(memcmp(a.data, defval.data, defval.len * defval.elem_size) == 0);

    r = sol_memdesc_init_defaults(&desc, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(b.len, defval.len);
    ASSERT_INT_EQ(b.elem_size, defval.elem_size);
    ASSERT(memcmp(b.data, defval.data, defval.len * defval.elem_size) == 0);

    r = sol_memdesc_copy(&desc, &defval, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(c.len, defval.len);
    ASSERT_INT_EQ(c.elem_size, defval.elem_size);
    ASSERT(memcmp(c.data, defval.data, defval.len * defval.elem_size) == 0);

    pv = sol_vector_append(&c);
    ASSERT(pv);
    *pv = 1234;

    r = sol_memdesc_set_content(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(a.len, defval.len + 1);
    ASSERT_INT_EQ(a.elem_size, defval.elem_size);
    ASSERT(memcmp(a.data, defval.data, defval.len * defval.elem_size) == 0);
    pv = sol_vector_get(&a, defval.len);
    ASSERT(pv);
    ASSERT_INT_EQ(*pv, 1234);

    sol_memdesc_free_content(&desc, &c);

    sol_memdesc_free_content(&desc, &a);
    sol_memdesc_free_content(&desc, &b);
    sol_vector_clear(&defval);
}

/*
 * SOL_MEMDESC_TYPE_ARRAY with strdup()/free()/strcmp()/strlen() as
 * operations should behave the same as SOL_MEMDESC_TYPE_STRING.
 */
static int
array_char_ops_set_content(const struct sol_memdesc *desc, void *container, const void *ptr_content)
{
    const char *const *pv = ptr_content;
    char **m = sol_memdesc_get_memory(desc, container);

    if (*m == *pv)
        return 0;

    free(*m);
    if (!*pv) {
        *m = NULL;
        return 0;
    }

    *m = strdup(*pv);
    if (!*m)
        return -errno;
    return 0;
}

static int
array_char_ops_compare(const struct sol_memdesc *desc, const void *a_container, const void *b_container)
{
    const char *const *a = sol_memdesc_get_memory(desc, a_container);
    const char *const *b = sol_memdesc_get_memory(desc, b_container);

    if (!*a && *b)
        return -1;
    else if (*a && !*b)
        return 1;
    else if (!*a && !*b)
        return 0;
    else
        return strcmp(*a, *b);
}

static int
array_char_ops_free_content(const struct sol_memdesc *desc, void *container)
{
    char **m = sol_memdesc_get_memory(desc, container);

    free(*m);
    return 0;
}

static ssize_t
array_char_ops_get_array_length(const struct sol_memdesc *desc, const void *memory)
{
    const char *const *m = memory;

    if (!*m)
        return 0;
    return strlen(*m);
}

static void *
array_char_ops_get_array_element(const struct sol_memdesc *desc, const void *memory, size_t idx)
{
    const char *const *m = memory;

    if (!*m)
        return 0;
    return (void *)(*m + idx);
}

static int
array_char_ops_resize_array(const struct sol_memdesc *desc, void *memory, size_t len)
{
    char **m = memory;
    char *tmp;
    size_t oldlen;

    if (!len) {
        free(*m);
        *m = NULL;
        return 0;
    }

    if (!*m)
        oldlen = 0;
    else
        oldlen = strlen(*m);

    tmp = realloc(*m, len + 1);
    if (!tmp)
        return -errno;

    *m = tmp;
    if (oldlen < len)
        memset(tmp + oldlen, 0, (len - oldlen));
    tmp[len] = '\0';
    return 0;
}

DEFINE_TEST(test_simple_SOL_MEMDESC_TYPE_ARRAY);
static void
test_simple_SOL_MEMDESC_TYPE_ARRAY(void)
{
    const char *defval = "hello world";
    const struct sol_memdesc_ops array_char_ops = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_OPS_API_VERSION, )
        .set_content = array_char_ops_set_content,
        .compare = array_char_ops_compare,
        .free_content = array_char_ops_free_content,
        .array = {
            .get_length = array_char_ops_get_array_length,
            .get_element = array_char_ops_get_array_element,
            .resize = array_char_ops_resize_array,
        },
    };
    const struct sol_memdesc desc = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
        .size = sizeof(char *),
        .type = SOL_MEMDESC_TYPE_ARRAY,
        .name = "SOL_MEMDESC_TYPE_ARRAY",
        .defcontent.p = &defval,
        .ops = &array_char_ops,
        .children = &(const struct sol_memdesc){
            SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
            .type = SOL_MEMDESC_TYPE_INT8,
            .name = "char",
        },
    };
    char *a, *b;
    const char *c;
    size_t i, len;
    int r;

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a != defval);
    ASSERT_STR_EQ(a, defval);

    r = sol_memdesc_compare(&desc, &a, &defval);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_init_defaults(&desc, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a != defval);
    ASSERT_STR_EQ(a, defval);

    r = sol_memdesc_compare(&desc, &b, &defval);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    c = "other string";
    r = sol_memdesc_set_content(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT(a != c);
    ASSERT_STR_EQ(a, "other string");

    r = sol_memdesc_compare(&desc, &a, &c);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT(r > 0);

    len = sol_memdesc_get_array_length(&desc, &b);
    ASSERT_INT_EQ(len, strlen(defval));
    for (i = 0; i < len; i++) {
        const char *elem = sol_memdesc_get_array_element(&desc, &b, i);

        ASSERT(elem);
        ASSERT_INT_EQ(*elem, defval[i]);
    }

    r = sol_memdesc_resize_array(&desc, &b, len + 1);
    ASSERT_INT_EQ(r, 0);
    {
        char *elem = sol_memdesc_get_array_element(&desc, &b, len);

        ASSERT(elem);
        *elem = '!';
        ASSERT_STR_EQ(b, "hello world!");
    }

    {
        int8_t chr = '?';

        r = sol_memdesc_append_array_element(&desc, &b, &chr);
        ASSERT_INT_EQ(r, 0);
        len = sol_memdesc_get_array_length(&desc, &b);
        ASSERT_INT_EQ(len, strlen(defval) + 2);
        ASSERT_STR_EQ(b, "hello world!?");
    }

    sol_memdesc_free_content(&desc, &a);
    sol_memdesc_free_content(&desc, &b);
}

DEFINE_TEST(test_vector_SOL_MEMDESC_TYPE_ARRAY);
static void
test_vector_SOL_MEMDESC_TYPE_ARRAY(void)
{
    struct myst {
        uint64_t u64;
        struct sol_vector v;
        uint8_t u8;
    };
    struct myst defval = {
        .u64 = 0xf234567890123456,
        .v = SOL_VECTOR_INIT(struct sol_vector),
        .u8 = 0x72,
    };
    struct sol_memdesc desc = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
        .size = sizeof(struct myst),
        .type = SOL_MEMDESC_TYPE_STRUCTURE,
        .name = "struct myst",
        .defcontent.p = &defval,
        .children = (const struct sol_memdesc[]){
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct myst, u64),
                .type = SOL_MEMDESC_TYPE_UINT64,
                .name = "u64",
            },
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct myst, v),
                .size = sizeof(struct sol_vector),
                .type = SOL_MEMDESC_TYPE_ARRAY,
                .name = "v",
                .ops = &SOL_MEMDESC_OPS_VECTOR,
                .children = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .size = sizeof(struct sol_vector),
                    .type = SOL_MEMDESC_TYPE_ARRAY,
                    .name = "struct sol_key_value",
                    .ops = &SOL_MEMDESC_OPS_VECTOR,
                    .children = &(const struct sol_memdesc){
                        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                        .size = sizeof(struct sol_key_value),
                        .type = SOL_MEMDESC_TYPE_STRUCTURE,
                        .name = "SOL_MEMDESC_TYPE_STRUCTURE",
                        .children = (const struct sol_memdesc[]){
                            {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .offset = offsetof(struct sol_key_value, key),
                                .type = SOL_MEMDESC_TYPE_STRING,
                                .name = "key",
                            },
                            {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .offset = offsetof(struct sol_key_value, value),
                                .type = SOL_MEMDESC_TYPE_STRING,
                                .name = "value",
                            },
                            {}
                        },
                    },
                },
            },
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct myst, u8),
                .type = SOL_MEMDESC_TYPE_UINT8,
                .name = "u8",
            },
            {}
        },
    };
    struct myst a, b;
    struct sol_key_value *kv;
    size_t i, j;
    int r;

    for (j = 0; j < 4; j++) {
        struct sol_vector *vec = sol_vector_append(&defval.v);

        ASSERT(vec);
        sol_vector_init(vec, sizeof(struct sol_key_value));
        for (i = 0; i < (j + 1); i++) {
            char *k, *v;

            r = asprintf(&k, "key%zd", i + j * 100);
            ASSERT(r > 0);

            r = asprintf(&v, "value%zd", i + j * 100);
            ASSERT(r > 0);

            kv = sol_vector_append(vec);
            ASSERT(kv);
            kv->key = k;
            kv->value = v;
        }
    }

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(a.v.len, defval.v.len);

    for (j = 0; j < defval.v.len; j++) {
        const struct sol_vector *vec_a = sol_vector_get(&a.v, j);
        const struct sol_vector *vec_b = sol_vector_get(&defval.v, j);

        ASSERT(vec_a);
        ASSERT(vec_b);
        ASSERT_INT_EQ(vec_a->len, vec_b->len);
    }

    r = sol_memdesc_init_defaults(&desc, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(b.v.len, defval.v.len);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    r = sol_memdesc_compare(&desc, &a, &defval);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    for (j = 0; j < defval.v.len; j++) {
        const struct sol_vector *vec_a = sol_vector_get(&a.v, j);
        const struct sol_vector *vec_b = sol_vector_get(&defval.v, j);

        ASSERT(vec_a);
        ASSERT(vec_b);

        for (i = 0; i < vec_b->len; i++) {
            const struct sol_key_value *ita, *itb;

            ita = sol_vector_get(vec_a, i);
            ASSERT(ita);

            itb = sol_vector_get(vec_b, i);
            ASSERT(itb);

            ASSERT_STR_EQ(ita->key, itb->key);
            ASSERT_STR_EQ(ita->value, itb->value);
        }
    }

    {
        struct sol_vector it = SOL_VECTOR_INIT(struct sol_key_value);
        struct sol_key_value kv_tmp = {
            .key = "otherkey",
            .value = "othervalue",
        };
        struct sol_vector *vec;
        ssize_t len;

        kv = sol_vector_append(&it);
        ASSERT(kv);

        kv->key = "somekey";
        kv->value = "somevalue";

        r = sol_memdesc_append_array_element(&desc.children[1], &a.v, &it);
        ASSERT_INT_EQ(r, 0);

        len = sol_memdesc_get_array_length(&desc.children[1], &a.v);
        ASSERT_INT_EQ(len, defval.v.len + 1);
        ASSERT_INT_EQ(a.v.len, defval.v.len + 1);

        vec = sol_memdesc_get_array_element(&desc.children[1], &a.v, defval.v.len);
        ASSERT(vec);
        ASSERT_INT_EQ(vec->len, it.len);

        kv = sol_memdesc_get_array_element(desc.children[1].children, vec, 0);
        ASSERT(kv);

        ASSERT_STR_EQ(kv->key, "somekey");
        ASSERT_STR_EQ(kv->value, "somevalue");

        r = sol_memdesc_append_array_element(desc.children[1].children, vec, &kv_tmp);
        ASSERT_INT_EQ(r, 0);
        ASSERT_INT_EQ(vec->len, it.len + 1);

        kv = sol_memdesc_get_array_element(desc.children[1].children, vec, it.len);
        ASSERT(kv);

        ASSERT_STR_EQ(kv->key, kv_tmp.key);
        ASSERT(kv->key != kv_tmp.key);

        ASSERT_STR_EQ(kv->value, kv_tmp.value);
        ASSERT(kv->value != kv_tmp.value);

        sol_vector_clear(&it);
    }

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT(r > 0);

    r = sol_memdesc_resize_array(&desc.children[1], &a.v, defval.v.len);
    ASSERT_INT_EQ(r, 0);

    r = sol_memdesc_compare(&desc, &a, &b);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(errno, 0);

    sol_memdesc_free_content(&desc, &a);
    sol_memdesc_free_content(&desc, &b);

    /* no default means an empty array, but elem_size must be set from children size */
    desc.defcontent.p = NULL;
    memset(&a, 0xff, sizeof(a));

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(a.v.len, 0);
    ASSERT_INT_EQ(a.v.elem_size, sizeof(struct sol_vector));
    ASSERT(!a.v.data);

    sol_memdesc_free_content(&desc, &a);

    for (j = 0; j < defval.v.len; j++) {
        struct sol_vector *vec = sol_vector_get(&defval.v, j);

        for (i = 0; i <  vec->len; i++) {
            kv = sol_vector_get(vec, i);
            free((void *)kv->key);
            free((void *)kv->value);
        }

        sol_vector_clear(vec);
    }
    sol_vector_clear(&defval.v);
}

DEFINE_TEST(test_serialize);
static void
test_serialize(void)
{
    struct myst {
        uint64_t u64;
        struct sol_vector v;
        uint8_t u8;
    };
    struct myst defval = {
        .u64 = 0xf234567890123456,
        .v = SOL_VECTOR_INIT(struct sol_vector),
        .u8 = 0x72,
    };
    struct sol_memdesc desc = {
        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
        .size = sizeof(struct myst),
        .type = SOL_MEMDESC_TYPE_STRUCTURE,
        .name = "struct myst",
        .defcontent.p = &defval,
        .children = (const struct sol_memdesc[]){
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct myst, u64),
                .type = SOL_MEMDESC_TYPE_UINT64,
                .name = "u64",
            },
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct myst, v),
                .size = sizeof(struct sol_vector),
                .type = SOL_MEMDESC_TYPE_ARRAY,
                .name = "v",
                .ops = &SOL_MEMDESC_OPS_VECTOR,
                .children = &(const struct sol_memdesc){
                    SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                    .size = sizeof(struct sol_vector),
                    .type = SOL_MEMDESC_TYPE_ARRAY,
                    .name = "struct sol_key_value",
                    .ops = &SOL_MEMDESC_OPS_VECTOR,
                    .children = &(const struct sol_memdesc){
                        SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                        .size = sizeof(struct sol_key_value),
                        .type = SOL_MEMDESC_TYPE_STRUCTURE,
                        .name = "SOL_MEMDESC_TYPE_STRUCTURE",
                        .children = (const struct sol_memdesc[]){
                            {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .offset = offsetof(struct sol_key_value, key),
                                .type = SOL_MEMDESC_TYPE_STRING,
                                .name = "key",
                            },
                            {
                                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                                .offset = offsetof(struct sol_key_value, value),
                                .type = SOL_MEMDESC_TYPE_STRING,
                                .name = "value",
                            },
                            {}
                        },
                    },
                },
            },
            {
                SOL_SET_API_VERSION(.api_version = SOL_MEMDESC_API_VERSION, )
                .offset = offsetof(struct myst, u8),
                .type = SOL_MEMDESC_TYPE_UINT8,
                .name = "u8",
            },
            {}
        },
    };
    const char expected[] = ""
        "{\n"
        "    .u64 = 17452669531780691030,\n"
        "    .v = {\n"
        "        [0] = {\n"
        "            [0] = {\n"
        "                .key = \"key\\t0\",\n"
        "                .value = \"value\\\"0\\\"\"}},\n"
        "        [1] = {\n"
        "            [0] = {\n"
        "                .key = \"key\\t100\",\n"
        "                .value = \"value\\\"100\\\"\"},\n"
        "            [1] = {\n"
        "                .key = \"key\\t101\",\n"
        "                .value = \"value\\\"101\\\"\"}},\n"
        "        [2] = {\n"
        "            [0] = {\n"
        "                .key = \"key\\t200\",\n"
        "                .value = \"value\\\"200\\\"\"},\n"
        "            [1] = {\n"
        "                .key = \"key\\t201\",\n"
        "                .value = \"value\\\"201\\\"\"},\n"
        "            [2] = {\n"
        "                .key = \"key\\t202\",\n"
        "                .value = \"value\\\"202\\\"\"}},\n"
        "        [3] = {\n"
        "            [0] = {\n"
        "                .key = \"key\\t300\",\n"
        "                .value = \"value\\\"300\\\"\"},\n"
        "            [1] = {\n"
        "                .key = \"key\\t301\",\n"
        "                .value = \"value\\\"301\\\"\"},\n"
        "            [2] = {\n"
        "                .key = \"key\\t302\",\n"
        "                .value = \"value\\\"302\\\"\"},\n"
        "            [3] = {\n"
        "                .key = \"key\\t303\",\n"
        "                .value = \"value\\\"303\\\"\"}}},\n"
        "    .u8 = 114}"
        "";
    struct sol_buffer out = SOL_BUFFER_INIT_EMPTY;
    struct myst a;
    struct sol_key_value *kv;
    size_t i, j;
    int r;

    for (j = 0; j < 4; j++) {
        struct sol_vector *vec = sol_vector_append(&defval.v);

        ASSERT(vec);
        sol_vector_init(vec, sizeof(struct sol_key_value));
        for (i = 0; i < (j + 1); i++) {
            char *k, *v;

            r = asprintf(&k, "key\t%zd", i + j * 100);
            ASSERT(r > 0);

            r = asprintf(&v, "value\"%zd\"", i + j * 100);
            ASSERT(r > 0);

            kv = sol_vector_append(vec);
            ASSERT(kv);
            kv->key = k;
            kv->value = v;
        }
    }

    r = sol_memdesc_init_defaults(&desc, &a);
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(a.v.len, defval.v.len);

    for (j = 0; j < defval.v.len; j++) {
        const struct sol_vector *vec_a = sol_vector_get(&a.v, j);
        const struct sol_vector *vec_b = sol_vector_get(&defval.v, j);

        ASSERT(vec_a);
        ASSERT(vec_b);
        ASSERT_INT_EQ(vec_a->len, vec_b->len);
    }

    r = sol_memdesc_serialize(&desc, &a, &out, NULL, NULL);
    ASSERT_INT_EQ(r, 0);

    ASSERT_STR_EQ(out.data, expected);

    sol_memdesc_free_content(&desc, &a);
    sol_buffer_fini(&out);

}

TEST_MAIN();
