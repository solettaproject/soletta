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

#include <errno.h>

#include "sol-arena.h"
#include "sol-util.h"

#include "test.h"

#define CONVERT_OK(X) { SOL_STR_SLICE_LITERAL(#X), 0, X }

DEFINE_TEST(test_simple);

static void
test_simple(void)
{
    static const char *gladiators[] = {
        "Spartacus",
        "C r i x u s",
        "Priscus and Verus"
    };

    struct sol_arena *arena;
    struct sol_str_slice dst;
    char *dst_str;
    unsigned int i;

    arena = sol_arena_new();
    ASSERT(arena);

    for (i = 0; i < ARRAY_SIZE(gladiators); i++) {
        struct sol_str_slice gladiator_slice = sol_str_slice_from_str(gladiators[i]);

        ASSERT_INT_EQ(sol_arena_slice_dup_str(arena, &dst, gladiators[i]), 0);
        ASSERT(sol_str_slice_eq(dst, gladiator_slice));

        ASSERT_INT_EQ(sol_arena_slice_dup_str_n(arena, &dst, gladiators[i], strlen(gladiators[i])), 0);
        ASSERT(sol_str_slice_eq(dst, gladiator_slice));

        ASSERT_INT_EQ(sol_arena_slice_dup(arena, &dst, sol_str_slice_from_str(gladiators[i])), 0);
        ASSERT(sol_str_slice_eq(dst, gladiator_slice));

        dst_str = sol_arena_strdup(arena, gladiators[i]);
        ASSERT(dst_str);
        ASSERT(streq(gladiators[i], dst_str));

        dst_str = sol_arena_strndup(arena, gladiators[i], strlen(gladiators[i]));
        ASSERT(dst_str);
        ASSERT(streq(gladiators[i], dst_str));
    }

    sol_arena_del(arena);
}

DEFINE_TEST(test_null);

static void
test_null(void)
{
    struct sol_arena *arena;
    struct sol_str_slice dst;

    arena = sol_arena_new();
    ASSERT(arena);

    ASSERT(sol_arena_slice_dup_str(arena, &dst, NULL));
    ASSERT(sol_arena_slice_dup_str_n(arena, &dst, NULL, 0));
    ASSERT(sol_arena_slice_dup(arena, &dst, SOL_STR_SLICE_STR(NULL, 0)));
    ASSERT(!sol_arena_strdup(arena, NULL));
    ASSERT(!sol_arena_strndup(arena, NULL, 0));

    sol_arena_del(arena);
}

DEFINE_TEST(test_check_slices_after_adding_all);

static void
test_check_slices_after_adding_all(void)
{
    static const char *gladiators[] = { "Spartacus", "C r i x u s", "Priscus and Verus", "Tetraites",
                                        "Spiculus", "Marcus Attilius", "Carpophorus", "Flamma", "Commodus", "Mevia", "Hoplomachus",
                                        "Laquearius", "Lorarius", "Paegniarius", "Sagittarius", "Pegasasu no Seiya", "Thraex", "Gladiatrix",
                                        "Crupellarii", "Cestus", "Arbelas", "Retiarius", "Samnite", "Venator", "Dimachaerus", "Bustuarius",
                                        "This is a loooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooong name for a gladiator" };

    struct sol_arena *arena;
    struct sol_str_slice results[ARRAY_SIZE(gladiators)];
    unsigned int i;

    arena = sol_arena_new();
    ASSERT(arena);

    for (i = 0; i < ARRAY_SIZE(gladiators); i++)
        ASSERT_INT_EQ(sol_arena_slice_dup_str(arena, &results[i], gladiators[i]), 0);

    for (i = 0; i < ARRAY_SIZE(gladiators); i++)
        ASSERT(sol_str_slice_eq(results[i], sol_str_slice_from_str(gladiators[i])));

    sol_arena_del(arena);
}


TEST_MAIN();
