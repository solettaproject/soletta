/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#include <signal.h>
#include "sol-aio.h"
#include "sol-log.h"
#include "sol-mainloop.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct sol_aio *aio = NULL;
struct sol_aio_pending *pending = NULL;
struct sol_timeout *timer = NULL;

static void
sigint_handler(int signo)
{
    if (signo == SIGINT)
        sol_quit();
}

static void
read_cb(void *cb_data, struct sol_aio *_aio, int32_t ret)
{
    pending = NULL;

    if (ret < 0)
        fprintf(stderr, "%s\n", strerror(-ret));
    else
        fprintf(stdout, "value = %"PRId32"\n", ret);
}

static bool
on_timeout(void *data)
{
    if (sol_aio_busy(aio))
        return true;

    pending = sol_aio_get_value(aio, read_cb, NULL);
    if (!pending) {
        fprintf(stderr, "Failed to request read operation.");
        return false;
    }

    return true;
}

static void
usage(const char *program)
{
     fprintf(stdout,
        "Usage: %s [device] [pin]]\n",
        program);
}

int
main(int argc, char *argv[])
{
    int device, pin;

    if (SOL_UNLIKELY(sol_init() < 0)) {
        fprintf(stderr, "Can't initialize Soletta.\n");
        return EXIT_FAILURE;
    }

    if (argc < 3)
        goto err_usage;

    if (!sscanf(argv[1], "%d", &device))
        goto err_usage;

    if (!sscanf(argv[2], "%d", &pin))
        goto err_usage;

    aio = sol_aio_open(device, pin, 12);
    SOL_NULL_CHECK(aio, EXIT_FAILURE);

    timer = sol_timeout_add(100, on_timeout, NULL);

    signal(SIGINT, sigint_handler);
    sol_run();

    if (pending)
        sol_aio_pending_cancel(aio, pending);

    sol_aio_close(aio);
    sol_shutdown();
    return EXIT_SUCCESS;

err_usage:
    usage(argv[0]);
    sol_shutdown();
    return EXIT_FAILURE;
}
