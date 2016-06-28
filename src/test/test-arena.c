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

#include <errno.h>

#include "sol-arena.h"
#include "sol-util-internal.h"

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

    for (i = 0; i < sol_util_array_size(gladiators); i++) {
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

        dst_str = sol_arena_str_dup_n(arena, gladiators[i], strlen(gladiators[i]));
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
    ASSERT(!sol_arena_str_dup_n(arena, NULL, 0));

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
    struct sol_str_slice results[sol_util_array_size(gladiators)];
    unsigned int i;

    arena = sol_arena_new();
    ASSERT(arena);

    for (i = 0; i < sol_util_array_size(gladiators); i++)
        ASSERT_INT_EQ(sol_arena_slice_dup_str(arena, &results[i], gladiators[i]), 0);

    for (i = 0; i < sol_util_array_size(gladiators); i++)
        ASSERT(sol_str_slice_eq(results[i], sol_str_slice_from_str(gladiators[i])));

    sol_arena_del(arena);
}


TEST_MAIN();
