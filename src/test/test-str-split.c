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

#include "sol-str-slice.h"
#include "sol-util-internal.h"

#include "test.h"

DEFINE_TEST(test_str_to_slice);

static void
test_str_to_slice(void)
{
    unsigned int i;

    struct {
        const struct sol_str_slice slice;
        const char *delim;
        const char *const *strings;
        unsigned int max_split;
        unsigned int n_splits;
    } items[] = {
        {
            { strlen("Using space -l :q"), "Using space -l :q"
              "dsdsdsdkjskdjksjdksjdksjdksjd" },
            " ",
            (const char *const []){ "Using", "space", "-l", ":q" },
            0,
            4
        },
        {
            { strlen("Using space -l :q"), "Using space -l :q" },
            " ",
            (const char *const []){ "Using", "space -l :q" },
            1,
            2
        },
        {
            { strlen("Using{{brackets{ {{"), "Using{{brackets{ {{" },
            "{",
            (const char *const []){ "Using", "", "brackets", " ", "", "" },
            5,
            6
        },
        {
            { strlen("Using comma test"), "Using comma test" },
            ",",
            (const char *const []){ "Using comma test" },
            0,
            1
        },
        {
            { strlen("Using42brackets42 test42"), "Using42brackets42 test42" },
            "42",
            (const char *const []){ "Using", "brackets", " test", "" },
            3,
            4
        },
        {
            { strlen("Using42brackets42 test42"), "Using42brackets42 test42" },
            NULL,
            NULL,
            0,
            0
        },
    };

    for (i = 0; i < sol_util_array_size(items); i++) {
        uint16_t j;
        struct sol_vector tokens;
        struct sol_str_slice *s;

        tokens = sol_str_slice_split(items[i].slice, items[i].delim,
            items[i].max_split);

        ASSERT_INT_EQ(tokens.len, items[i].n_splits);

        SOL_VECTOR_FOREACH_IDX (&tokens, s, j)
            ASSERT(!strncmp(s->data, items[i].strings[j], s->len));

        sol_vector_clear(&tokens);
    }
}

TEST_MAIN();
