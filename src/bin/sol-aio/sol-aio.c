/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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
#include <stdio.h>
#include <string.h>

#include "soletta.h"
#include "sol-aio.h"
#include "sol-util.h"

static int device, pin;
static struct sol_aio *aio = NULL;
static struct sol_aio_pending *pending = NULL;
static struct sol_timeout *timer = NULL;

static void
read_cb(void *cb_data, struct sol_aio *_aio, int32_t ret)
{
    pending = NULL;

    if (ret < 0)
        fprintf(stderr, "ERROR: Couldn't ready AIO:<%d, %d>.\n    %s\n",
            device, pin, sol_util_strerrora(-ret));
    else
        fprintf(stdout, "value = %" PRId32 "\n", ret);
}

static bool
on_timeout(void *data)
{
    pending = sol_aio_get_value(aio, read_cb, NULL);
    if (!pending && errno != EBUSY)
        fprintf(stderr, "ERROR: Failed to request read operation to <%d, %d>.\n", device, pin);

    return true;
}

static void
usage(const char *program)
{
    fprintf(stdout, "Usage: %s [device] [pin]\n", program);
}

static void
startup(void)
{
    int argc = sol_argc();
    char **argv = sol_argv();

    if (argc < 3)
        goto err_usage;

    if (!sscanf(argv[1], "%d", &device))
        goto err_usage;

    if (!sscanf(argv[2], "%d", &pin))
        goto err_usage;

    aio = sol_aio_open(device, pin, 12);
    SOL_NULL_CHECK_GOTO(aio, err);

    timer = sol_timeout_add(100, on_timeout, NULL);
    SOL_NULL_CHECK_GOTO(timer, err);

    return;

err_usage:
    usage(argv[0]);
err:
    sol_quit_with_code(EXIT_FAILURE);
}

static void
shutdown(void)
{
    sol_timeout_del(timer);

    if (aio) {
        if (pending)
            sol_aio_pending_cancel(aio, pending);

        sol_aio_close(aio);
    }
}

SOL_MAIN_DEFAULT(startup, shutdown);
