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

#include <string.h>

#include "sol-mainloop.h"

#include "test.h"

int
test_main(struct test *start, struct test *stop, void (*reset_func)(void), int argc, char *argv[])
{
    const struct test *t;
    const char *pattern = NULL;
    int err, count = 0;

    err = sol_init();
    ASSERT(!err);

    if (argc > 1) {
        pattern = argv[1];
        fprintf(stderr, "Running only tests that match '%s'\n", pattern);
    }

    for (t = start; t < stop; t++) {
        if (!pattern || strstr(t->name, pattern)) {
            fprintf(stderr, "- %s\n", t->name);
            t->func();
            if (reset_func)
                reset_func();
            count++;
        }
    }

    if (count == 0) {
        fprintf(stderr, "No tests found!\n");
        return EXIT_FAILURE;
    }

    sol_shutdown();
    fprintf(stderr, "OK!\n");

    return 0;
}
