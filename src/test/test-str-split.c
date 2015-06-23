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

#include "sol-str-slice.h"
#include "sol-util.h"

#include "test.h"

DEFINE_TEST(test_str_to_slice);

static void
test_str_to_slice(void)
{
    unsigned int i;
    struct {
        const struct sol_str_slice slice;
        const char *delim;
        const char * const *strings;
        unsigned int len;
    } items[] = {
        {
            { strlen("Using space -l :q"), "Using space -l :q" },
            " ",
            (const char * const []){ "Using", "space", "-l", ":q" },
            4
        },
        {
            { strlen("Using space -l :q"), "Using space -l :q" },
            " ",
            (const char * const []){ "Using", "space" },
            2
        },
        {
            { strlen("Using{{{{{{{{{{brackets{ test{{{{{{{{{"), "Using{{{{{{{{{{brackets{ test{{{{{{{{{" },
            "{",
            (const char * const []){ "Using", "brackets", " test" },
            3
        },
        {
            { strlen("Using comma test"), "Using comma test" },
            ",",
            NULL,
            0
        },
        {
            { strlen("Using42brackets42 test42"), "Using42brackets42 test42" },
            "42",
            (const char * const []){ "Using", "brackets", " test" },
            3
        },
        {
            { strlen("Using42brackets42 test42"), "Using42brackets42 test42" },
            NULL,
            NULL,
            0
        },
    };

    for (i = 0; i < ARRAY_SIZE(items); i++) {
        uint16_t j;
        struct sol_vector tokens;
        struct sol_str_slice *s;

        tokens = sol_util_str_split(items[i].slice, items[i].delim, items[i].len);
        ASSERT_INT_EQ(tokens.len, items[i].len);

        SOL_VECTOR_FOREACH_IDX (&tokens, s, j)
            ASSERT(!strncmp(s->data, items[i].strings[j], s->len));

        sol_vector_clear(&tokens);
    }
}

TEST_MAIN();
