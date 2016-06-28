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

#include <stdbool.h>

#include "sol-mainloop.h"

#include "test.h"

static int timeout_called = 0;
static int timeout_renewed = 0;

static int idler_renewed = 0;
static int idler_sequence[10];

static bool
on_timeout_chained(void *data)
{
    timeout_called++;
    return true;
}

static bool
on_timeout_quit(void *data)
{
    timeout_called++;
    sol_quit();
    return true;
}

static bool
on_timeout_del_and_new(void *data)
{
    struct sol_timeout *timeout_to_del = data;

    timeout_called++;
    ASSERT_INT_EQ(sol_timeout_del(timeout_to_del), 1);
    sol_timeout_add(250, on_timeout_quit, NULL);
    sol_timeout_add(200, on_timeout_chained, NULL);
    return false;
}

static bool
timeout_never_called(void *data)
{
    fputs("this timeout should never run.\n", stderr);
    abort();
    return true;
}

static bool
on_timeout_renew_twice(void *data)
{
    timeout_renewed++;
    return timeout_renewed < 2;
}

static bool
on_idler(void *data)
{
    int i = (intptr_t)data;

    idler_sequence[i] = i;

    if (i < 5)
        sol_idle_add(on_idler, (void *)(intptr_t)(i + 5));

    return false;
}

static bool
on_idler_never_called(void *data)
{
    fputs("this idler should never run.\n", stderr);
    abort();
    return false;
}

static bool
on_idler_del_another(void *data)
{
    struct sol_idle **p_idler = data;

    sol_idle_del(*p_idler);
    return false;
}

static bool
on_idler_renew_twice(void *data)
{
    idler_renewed++;
    return idler_renewed < 2;
}

#ifndef TEST_MAINLOOP_MAIN_FN
#define TEST_MAINLOOP_MAIN_FN main
#endif
int
TEST_MAINLOOP_MAIN_FN(int argc, char *argv[])
{
    int err, i;
    struct sol_timeout *timeout_to_del;
    struct sol_idle *idler_to_del;

    err = sol_init();
    ASSERT(!err);

    timeout_to_del = sol_timeout_add(100, timeout_never_called, NULL);
    sol_timeout_add(20, on_timeout_del_and_new, timeout_to_del);

    sol_timeout_add(1, on_timeout_renew_twice, NULL);
    sol_idle_add(on_idler_renew_twice, NULL);

    for (i = 0; i < 5; i++)
        sol_idle_add(on_idler, (void *)(intptr_t)i);

    sol_idle_add(on_idler_del_another, &idler_to_del);
    idler_to_del = sol_idle_add(on_idler_never_called, NULL);

    sol_run();
    ASSERT_INT_EQ(timeout_called, 3);
    ASSERT_INT_EQ(timeout_renewed, 2);
    ASSERT_INT_EQ(idler_renewed, 2);

    for (i = 0; i < 10; i++)
        ASSERT_INT_EQ(idler_sequence[i], i);

    sol_shutdown();

    return 0;
}
