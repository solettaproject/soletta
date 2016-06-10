/*
 * This file is part of the Soletta™ Project
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
#include <pthread.h>

static bool done = false;

static void *
thr_run(void *data)
{
    sol_run();
    return NULL;
}

static bool
mark_timeout(void *data)
{
    done = true;
    return false;
}

int
main(int argc, char *argv[])
{
    pthread_t thr1;

    ASSERT(sol_init() == 0);

    ASSERT(sol_timeout_add(1000, mark_timeout, NULL));

    pthread_create(&thr1, NULL, thr_run, NULL);
    pthread_join(thr1, NULL);

    ASSERT(!done);

    sol_shutdown();

    return 0;
}
