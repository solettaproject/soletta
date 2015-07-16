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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct test {
    const char *name;
    void (*func)(void);
};

#define DEFINE_TEST(_name)                                              \
    static void _name(void);                                            \
    static const struct test _name ## _test_info                          \
    __attribute__((used, section("soletta_tests"), aligned(8))) = {      \
        .name = #_name,                                                 \
        .func = _name,                                                  \
    }

int test_main(struct test *start, struct test *stop, void (*reset_func)(void), int argc, char *argv[]);

#define TEST_MAIN_WITH_RESET_FUNC(_reset)                               \
    extern struct test __start_soletta_tests[] __attribute__((weak, visibility("hidden"))); \
    extern struct test __stop_soletta_tests[] __attribute__((weak, visibility("hidden"))); \
    int                                                                 \
    main(int argc, char *argv[]) {                                      \
        return test_main(__start_soletta_tests, __stop_soletta_tests, _reset, argc, argv); \
    }

#define TEST_MAIN()                             \
    TEST_MAIN_WITH_RESET_FUNC(NULL)

#define FAIL() exit(1);

#define ASSERT(expr)                    \
    do {                                \
        if ((!(expr))) {                                                \
            fprintf(stderr, "%s:%d: %s: Assertion: `" #expr "' failed.\n", \
                __FILE__, __LINE__, __PRETTY_FUNCTION__);           \
            exit(1);                                                    \
        }                                                               \
    } while (0)

#define ASSERT_INT_EQ(expr_a, expr_b)                                   \
    do {                                                                \
        const int __value_expr_a = expr_a;                              \
        const int __value_expr_b = expr_b;                              \
        if ((__value_expr_a) != (__value_expr_b)) {                     \
            fprintf(stderr, "%s:%d: %s: Assertion `" #expr_a "' (%d) == `" #expr_b "' (%d) failed.\n", \
                __FILE__, __LINE__, __PRETTY_FUNCTION__, __value_expr_a, __value_expr_b); \
            exit(-1);                                                   \
        }                                                               \
    } while (0)

#define ASSERT_INT_NE(expr_a, expr_b)                                   \
    do {                                                                \
        const int __value_expr_a = expr_a;                              \
        const int __value_expr_b = expr_b;                              \
        if ((__value_expr_a) == (__value_expr_b)) {                     \
            fprintf(stderr, "%s:%d: %s: Assertion `" #expr_a "' (%d) != `" #expr_b "' (%d) failed.\n", \
                __FILE__, __LINE__, __PRETTY_FUNCTION__, __value_expr_a, __value_expr_b); \
            exit(-1);                                                   \
        }                                                               \
    } while (0)

#define ASSERT_STR_EQ(str_a, str_b)                                     \
    do {                                                                \
        ASSERT((str_a) != NULL);                                        \
        ASSERT((str_b) != NULL);                                        \
        if (strcmp((str_a), (str_b)) != 0) {                            \
            fprintf(stderr, "%s:%d: %s: Assertion string_equal(\"%s\", \"%s\") failed.\n", \
                __FILE__, __LINE__, __PRETTY_FUNCTION__, (str_a), (str_b)); \
            exit(-1);                                                   \
        }                                                               \
    } while (0)

#define ASSERT_STR_NE(str_a, str_b)                                     \
    do {                                                                \
        ASSERT((str_a) != NULL);                                        \
        ASSERT((str_b) != NULL);                                        \
        if (strcmp((str_a), (str_b)) == 0) {                            \
            fprintf(stderr, "%s:%d: %s: Assertion string_different(\"%s\", \"%s\") failed.\n", \
                __FILE__, __LINE__, __PRETTY_FUNCTION__, (str_a), (str_b)); \
            exit(-1);                                                   \
        }                                                               \
    } while (0)
