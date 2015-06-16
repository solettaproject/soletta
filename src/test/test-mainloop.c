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

int
main(int argc, char *argv[])
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
