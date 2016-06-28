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

#include <libgen.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <float.h>
#ifdef HAVE_LOCALE
#include <locale.h>
#endif

#include "sol-util-internal.h"
#include "sol-log.h"

#include "test.h"

DEFINE_TEST(test_basename);

static void
test_basename(void)
{
    const char *path[] = { "/", "../test1", "test2", "/test3/",
                           "////foo////bar///test4////", "/a", "b/" };
    char *tmp;
    unsigned int i;
    bool b;
    struct sol_str_slice base;

    for (i = 0; i < sizeof(path) / sizeof(char *); i++) {
        tmp = strdup(path[i]);
        base = sol_util_file_get_basename(sol_str_slice_from_str(path[i]));
        b = sol_str_slice_str_eq(base, basename(tmp));
        free(tmp);
        ASSERT(b);
    }
}

TEST_MAIN();
