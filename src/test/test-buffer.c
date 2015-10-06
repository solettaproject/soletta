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

#include "sol-buffer.h"
#include "sol-util.h"

#include "test.h"

DEFINE_TEST(fini_null_is_fine);

static void
fini_null_is_fine(void)
{
    struct sol_buffer buf;

    sol_buffer_init(&buf);
    sol_buffer_fini(&buf);

    /* Finish an already finished buffer. */
    sol_buffer_fini(&buf);

    sol_buffer_fini(NULL);
}


DEFINE_TEST(test_resize);

static void
test_resize(void)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    const int size = 1024;
    char *buf_data;
    int i;

    sol_buffer_resize(&buf, size);

    memset(buf.data, 22, size);

    sol_buffer_resize(&buf, size * 2);
    buf_data = buf.data;
    for (i = 0; i < size; i++) {
        ASSERT_INT_EQ(buf_data[i], 22);
    }

    sol_buffer_resize(&buf, size / 2);
    buf_data = buf.data;
    for (i = 0; i < size / 2; i++) {
        ASSERT_INT_EQ(buf_data[i], 22);
    }

    sol_buffer_fini(&buf);
}


DEFINE_TEST(test_ensure);

static void
test_ensure(void)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    const int size = 1024;
    char *buf_data;
    int i;

    sol_buffer_ensure(&buf, size);

    memset(buf.data, 22, size);

    sol_buffer_ensure(&buf, size * 2);
    buf_data = buf.data;
    for (i = 0; i < size; i++) {
        ASSERT_INT_EQ(buf_data[i], 22);
    }

    sol_buffer_ensure(&buf, size / 2);
    buf_data = buf.data;
    for (i = 0; i < size / 2; i++) {
        ASSERT_INT_EQ(buf_data[i], 22);
    }

    sol_buffer_fini(&buf);
}


DEFINE_TEST(test_set_slice);

static void
test_set_slice(void)
{
    struct sol_buffer buf;
    struct sol_str_slice slice;
    const char *str = "Hello";
    char *backend;
    int err;

    backend = strdup(str);
    slice = sol_str_slice_from_str(backend);

    sol_buffer_init(&buf);
    err = sol_buffer_set_slice(&buf, slice);
    ASSERT(err >= 0);

    ASSERT_INT_EQ(buf.used, strlen(backend));
    ASSERT_STR_EQ(buf.data, backend);

    backend[1] = 'a';
    ASSERT_STR_NE(buf.data, backend);
    ASSERT_STR_EQ(buf.data, str);

    sol_buffer_fini(&buf);

    free(backend);
}


DEFINE_TEST(test_append_slice);

static void
test_append_slice(void)
{
    struct sol_buffer buf;
    struct sol_str_slice slice;
    const char *str = "Hello";
    const char *expected_str = "HelloHello";
    char *backend;
    int err;

    backend = strdup(str);
    slice = sol_str_slice_from_str(backend);

    sol_buffer_init(&buf);
    err = sol_buffer_set_slice(&buf, slice);
    ASSERT(err >= 0);

    ASSERT_INT_EQ(buf.used, strlen(backend));
    ASSERT_STR_EQ(buf.data, backend);

    err = sol_buffer_append_slice(&buf, slice);
    ASSERT(err >= 0);
    ASSERT_INT_EQ(buf.used, strlen(expected_str));

    backend[1] = 'a';
    ASSERT_STR_NE(buf.data, backend);
    ASSERT_STR_EQ(buf.data, expected_str);

    slice = sol_buffer_get_slice(&buf);
    ASSERT_INT_EQ(slice.len, buf.used);
    ASSERT_STR_EQ(slice.data, buf.data);

    slice = sol_buffer_get_slice_at(&buf, 2);
    ASSERT_INT_EQ(slice.len, buf.used - 2);
    ASSERT_STR_EQ(slice.data, (char *)buf.data + 2);

    sol_buffer_fini(&buf);

    free(backend);
}

DEFINE_TEST(test_insert_slice);

static void
test_insert_slice(void)
{
    struct sol_buffer buf;
    struct sol_str_slice slice;
    int err;

    sol_buffer_init(&buf);
    slice = sol_str_slice_from_str("World");
    err = sol_buffer_insert_slice(&buf, 0, slice);
    ASSERT_INT_EQ(err, 0);
    ASSERT_INT_EQ(buf.used, strlen("World"));
    ASSERT_STR_EQ(buf.data, "World");

    slice = sol_str_slice_from_str("Hello");
    err = sol_buffer_insert_slice(&buf, 0, slice);
    ASSERT_INT_EQ(err, 0);
    ASSERT_INT_EQ(buf.used, strlen("HelloWorld"));
    ASSERT_STR_EQ(buf.data, "HelloWorld");

    slice = sol_str_slice_from_str(" -*- ");
    err = sol_buffer_insert_slice(&buf, strlen("Hello"), slice);
    ASSERT_INT_EQ(err, 0);
    ASSERT_INT_EQ(buf.used, strlen("Hello -*- World"));
    ASSERT_STR_EQ(buf.data, "Hello -*- World");

    sol_buffer_fini(&buf);
}

DEFINE_TEST(test_set_slice_at);

static void
test_set_slice_at(void)
{
    struct sol_buffer buf;
    struct sol_str_slice slice;
    int err;

    sol_buffer_init(&buf);
    slice = sol_str_slice_from_str("World");
    err = sol_buffer_set_slice_at(&buf, 0, slice);
    ASSERT_INT_EQ(err, 0);
    ASSERT_INT_EQ(buf.used, strlen("World"));
    ASSERT_STR_EQ(buf.data, "World");

    slice = sol_str_slice_from_str("Hello");
    err = sol_buffer_set_slice_at(&buf, 0, slice);
    ASSERT_INT_EQ(err, 0);
    ASSERT_INT_EQ(buf.used, strlen("Hello"));
    ASSERT_STR_EQ(buf.data, "Hello");

    slice = sol_str_slice_from_str("World");
    err = sol_buffer_set_slice_at(&buf, strlen("Hello"), slice);
    ASSERT_INT_EQ(err, 0);
    ASSERT_INT_EQ(buf.used, strlen("HelloWorld"));
    ASSERT_STR_EQ(buf.data, "HelloWorld");

    slice = sol_str_slice_from_str(" -*- ");
    err = sol_buffer_set_slice_at(&buf, 2, slice);
    ASSERT_INT_EQ(err, 0);
    ASSERT_INT_EQ(buf.used, strlen("He -*- rld"));
    ASSERT_STR_EQ(buf.data, "He -*- rld");

    //overlapping
    slice = SOL_STR_SLICE_STR((char *)buf.data + 3, 3);
    err = sol_buffer_set_slice_at(&buf, 7, slice);
    ASSERT_INT_EQ(err, 0);
    ASSERT_INT_EQ(buf.used, strlen("He -*- -*-"));
    ASSERT_STR_EQ(buf.data, "He -*- -*-");

    slice = sol_str_slice_from_str("whatever");
    err = sol_buffer_set_slice_at(&buf, 222, slice);
    ASSERT_INT_EQ(err, -EINVAL);

    sol_buffer_fini(&buf);
}

DEFINE_TEST(test_set_char_at);

static void
test_set_char_at(void)
{
    struct sol_buffer buf;
    int err;

    sol_buffer_init(&buf);
    err = sol_buffer_set_char_at(&buf, 0, 'a');
    ASSERT_INT_EQ(err, 0);
    ASSERT_INT_EQ(buf.used, 1);
    ASSERT_STR_EQ(buf.data, "a");

    err = sol_buffer_set_char_at(&buf, 0, 'b');
    ASSERT_INT_EQ(err, 0);
    ASSERT_INT_EQ(buf.used, 1);
    ASSERT_STR_EQ(buf.data, "b");

    err = sol_buffer_set_char_at(&buf, 1, 'c');
    ASSERT_INT_EQ(err, 0);
    ASSERT_INT_EQ(buf.used, strlen("bc"));
    ASSERT_STR_EQ(buf.data, "bc");

    err = sol_buffer_set_char_at(&buf, 0, 'a');
    ASSERT_INT_EQ(err, 0);
    ASSERT_INT_EQ(buf.used, strlen("ac"));
    ASSERT_STR_EQ(buf.data, "ac");

    //growing
    err = sol_buffer_set_char_at(&buf, 2, 'd');
    ASSERT_INT_EQ(err, 0);
    ASSERT_INT_EQ(buf.used, strlen("acd"));
    ASSERT_STR_EQ(buf.data, "acd");

    err = sol_buffer_set_char_at(&buf, 222, 'e');
    ASSERT_INT_EQ(err, -EINVAL);

    sol_buffer_fini(&buf);
}

DEFINE_TEST(test_append_printf);

static void
test_append_printf(void)
{
    struct sol_buffer buf;
    int err;

    sol_buffer_init(&buf);
    err = sol_buffer_append_printf(&buf, "[%03d]", 1);
    ASSERT_INT_EQ(err, 0);
    ASSERT_STR_EQ(buf.data, "[001]");

    err = sol_buffer_append_printf(&buf, "'%s'", "This is a longer string, bla bla bla, bla bla bla");
    ASSERT_INT_EQ(err, 0);
    ASSERT_STR_EQ(buf.data, "[001]'This is a longer string, bla bla bla, bla bla bla'");

    err = sol_buffer_append_printf(&buf, ".");
    ASSERT_INT_EQ(err, 0);
    ASSERT_STR_EQ(buf.data, "[001]'This is a longer string, bla bla bla, bla bla bla'.");

    sol_buffer_fini(&buf);
}

DEFINE_TEST(test_insert_printf);

static void
test_insert_printf(void)
{
    struct sol_buffer buf;
    int err;

    sol_buffer_init(&buf);
    err = sol_buffer_insert_printf(&buf, 0, "'%s'", "This is a longer string, bla bla bla, bla bla bla");
    ASSERT_INT_EQ(err, 0);
    ASSERT_STR_EQ(buf.data, "'This is a longer string, bla bla bla, bla bla bla'");

    err = sol_buffer_insert_printf(&buf, 0, "[%03d]", 1);
    ASSERT_INT_EQ(err, 0);
    ASSERT_STR_EQ(buf.data, "[001]'This is a longer string, bla bla bla, bla bla bla'");

    err = sol_buffer_insert_printf(&buf, strlen("[001]"), " ### ");
    ASSERT_INT_EQ(err, 0);
    ASSERT_STR_EQ(buf.data, "[001] ### 'This is a longer string, bla bla bla, bla bla bla'");

    sol_buffer_fini(&buf);
}

DEFINE_TEST(test_memory_not_owned);

static void
test_memory_not_owned(void)
{
    struct sol_buffer buf;
    struct sol_str_slice slice;
    char backend[10];
    int err;

    sol_buffer_init_flags(&buf, backend, sizeof(backend), SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);

    err = sol_buffer_ensure(&buf, 0);
    ASSERT_INT_EQ(err, 0);

    err = sol_buffer_ensure(&buf, sizeof(backend));
    ASSERT_INT_EQ(err, 0);

    err = sol_buffer_ensure(&buf, sizeof(backend) * 2);
    ASSERT_INT_EQ(err, -ENOMEM);

    err = sol_buffer_resize(&buf, 0);
    ASSERT_INT_EQ(err, -EPERM);

    slice = sol_str_slice_from_str("test");
    err = sol_buffer_append_slice(&buf, slice);
    ASSERT_INT_EQ(err, 0);
    ASSERT_STR_EQ(buf.data, "test");

    slice = sol_str_slice_from_str("other");
    err = sol_buffer_append_slice(&buf, slice);
    ASSERT_INT_EQ(err, 0);
    ASSERT_STR_EQ(buf.data, "testother");

    slice = sol_str_slice_from_str("OVERFLOW");
    err = sol_buffer_append_slice(&buf, slice);
    ASSERT_INT_EQ(err, -ENOMEM);

    sol_buffer_fini(&buf);
}

DEFINE_TEST(test_no_nul_byte);

static void
test_no_nul_byte(void)
{
    struct sol_buffer buf;
    int32_t backend;
    int32_t value = 0xdeadbeef;
    int err;

    sol_buffer_init_flags(&buf, &backend, sizeof(backend),
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);

    err = sol_buffer_ensure(&buf, sizeof(int32_t));
    ASSERT_INT_EQ(err, 0);

    err = sol_buffer_append_slice(&buf, SOL_STR_SLICE_STR((const char *)&value, sizeof(value)));
    ASSERT_INT_EQ(err, 0);

    err = sol_buffer_append_slice(&buf, SOL_STR_SLICE_STR((const char *)&value, sizeof(value)));
    ASSERT_INT_NE(err, 0);

    sol_buffer_fini(&buf);
}

TEST_MAIN();
