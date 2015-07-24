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

#include <stdbool.h>
#include <limits.h>

#include "sol-util.h"

#include "test.h"

DEFINE_TEST(test_align_power2);

static void
test_align_power2(void)
{
    unsigned int i;

    static const struct {
        unsigned int input;
        unsigned int output;
    } table[] = {
        { 0, 0 },
        { 1, 1 },
        { 2, 2 },
        { 3, 4 },
        { 4, 4 },
        { 5, 8 },
        { 6, 8 },
        { 7, 8 },
        { 8, 8 },
        { 15, 16 },
        { 16, 16 },
        { 17, 32 },
    };

    for (i = 0; i < ARRAY_SIZE(table); i++) {
        unsigned int actual;
        actual = align_power2(table[i].input);

        if (actual != table[i].output) {
            fprintf(stderr, "Error calling align_power2(%u), got %u but expected %u\n",
                table[i].input, actual, table[i].output);
            ASSERT(false);
        }
    }
}


DEFINE_TEST(test_size_mul);

static void
test_size_mul(void)
{
    const size_t half_size = SIZE_MAX / 2;
    size_t out;

    ASSERT(sol_util_size_mul(half_size, 2, &out) == 0);
    ASSERT(out == SIZE_MAX);

    ASSERT(sol_util_size_mul(half_size, 4, &out) < 0);
}


TEST_MAIN();
