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

    sol_buffer_fini(&buf);

    free(backend);
}

DEFINE_TEST(test_fixed_size);

static void
test_fixed_size(void)
{
    struct sol_buffer buf;
    struct sol_str_slice slice;
    char backend[10];
    int err;

    sol_buffer_init_fixed_size(&buf, backend, sizeof(backend));

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

    sol_buffer_fini_nofree(&buf);
}


TEST_MAIN();
