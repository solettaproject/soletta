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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sol-util.h"

struct test {
    const char *name;
    void (*func)(void);
};

#define DEFINE_TEST(_name) \
    static void _name(void); \
    static const struct test _name ## _test_info \
    __attribute__((used, section("soletta_tests"), aligned(8))) = { \
        .name = # _name, \
        .func = _name, \
    }

int test_main(struct test *start, struct test *stop, void (*reset_func)(void), int argc, char *argv[]);

#define TEST_MAIN_WITH_RESET_FUNC(_reset) \
    extern struct test __start_soletta_tests[] __attribute__((weak, visibility("hidden"))); \
    extern struct test __stop_soletta_tests[] __attribute__((weak, visibility("hidden"))); \
    int \
    main(int argc, char *argv[]) { \
        return test_main(__start_soletta_tests, __stop_soletta_tests, _reset, argc, argv); \
    }

#define TEST_MAIN() \
    TEST_MAIN_WITH_RESET_FUNC(NULL)

#define FAIL() exit(1);

#define ASSERT(expr) \
    do { \
        if ((!(expr))) { \
            fprintf(stderr, "%s:%d: %s: Assertion: `" # expr "' failed.\n", \
                __FILE__, __LINE__, __PRETTY_FUNCTION__); \
            exit(1); \
        } \
    } while (0)

#define ASSERT_INT_EQ(expr_a, expr_b) \
    do { \
        const int __value_expr_a = expr_a; \
        const int __value_expr_b = expr_b; \
        if ((__value_expr_a) != (__value_expr_b)) { \
            fprintf(stderr, "%s:%d: %s: Assertion `" # expr_a "' (%d) == `" # expr_b "' (%d) failed.\n", \
                __FILE__, __LINE__, __PRETTY_FUNCTION__, __value_expr_a, __value_expr_b); \
            exit(-1); \
        } \
    } while (0)

#define ASSERT_INT_NE(expr_a, expr_b) \
    do { \
        const int __value_expr_a = expr_a; \
        const int __value_expr_b = expr_b; \
        if ((__value_expr_a) == (__value_expr_b)) { \
            fprintf(stderr, "%s:%d: %s: Assertion `" # expr_a "' (%d) != `" # expr_b "' (%d) failed.\n", \
                __FILE__, __LINE__, __PRETTY_FUNCTION__, __value_expr_a, __value_expr_b); \
            exit(-1); \
        } \
    } while (0)

#define ASSERT_STR_EQ(str_a, str_b) \
    do { \
        ASSERT((str_a) != NULL); \
        ASSERT((str_b) != NULL); \
        if (strcmp((str_a), (str_b)) != 0) { \
            fprintf(stderr, "%s:%d: %s: Assertion string_eq(\"%s\", \"%s\") failed.\n", \
                __FILE__, __LINE__, __PRETTY_FUNCTION__, SOL_TYPE_CHECK(const char *, str_a), SOL_TYPE_CHECK(const char *, str_b)); \
            exit(-1); \
        } \
    } while (0)

#define ASSERT_STR_NE(str_a, str_b) \
    do { \
        ASSERT((str_a) != NULL); \
        ASSERT((str_b) != NULL); \
        if (strcmp((str_a), (str_b)) == 0) { \
            fprintf(stderr, "%s:%d: %s: Assertion string_different(\"%s\", \"%s\") failed.\n", \
                __FILE__, __LINE__, __PRETTY_FUNCTION__, SOL_TYPE_CHECK(const char *, str_a), SOL_TYPE_CHECK(const char *, str_b)); \
            exit(-1); \
        } \
    } while (0)
