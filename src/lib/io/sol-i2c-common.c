/*
 * This file is part of the Soletta Project
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

#include <stdlib.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "i2c");

#include "sol-i2c.h"
#ifdef USE_PIN_MUX
#include "sol-pin-mux.h"
#endif
#include "sol-vector.h"

struct sol_i2c_shared {
    struct sol_i2c *i2c;
    uint16_t refcount;
};

static struct sol_vector i2c_shared_vector = SOL_VECTOR_INIT(struct sol_i2c_shared);

SOL_API struct sol_i2c *
sol_i2c_open(uint8_t bus, enum sol_i2c_speed speed)
{
    struct sol_i2c *i2c;
    struct sol_i2c_shared *i2c_shared;
    uint8_t i;

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_VECTOR_FOREACH_IDX (&i2c_shared_vector, i2c_shared, i) {
        if (sol_i2c_bus_get(i2c_shared->i2c) == bus) {
            i2c_shared->refcount++;
            return i2c_shared->i2c;
        }
    }

    i2c = sol_i2c_open_raw(bus, speed);
    SOL_NULL_CHECK(i2c, NULL);
#ifdef USE_PIN_MUX
    if (sol_pin_mux_setup_i2c(bus) < 0) {
        SOL_ERR("Pin Multiplexer Recipe for i2c bus=%u found, but couldn't be applied.", bus);
        sol_i2c_close(i2c);
        i2c = NULL;
    }
#endif

    i2c_shared = sol_vector_append(&i2c_shared_vector);
    SOL_NULL_CHECK_GOTO(i2c_shared, vector_append_fail);
    i2c_shared->i2c = i2c;
    i2c_shared->refcount = 1;

    return i2c;

vector_append_fail:
    sol_i2c_close(i2c);
    return NULL;
}

SOL_API void
sol_i2c_close(struct sol_i2c *i2c)
{
    struct sol_i2c_shared *i2c_shared;
    uint8_t i;

    SOL_NULL_CHECK(i2c);

    SOL_VECTOR_FOREACH_IDX (&i2c_shared_vector, i2c_shared, i) {
        if (i2c_shared->i2c == i2c) {
            i2c_shared->refcount--;
            if (!i2c_shared->refcount) {
                sol_vector_del(&i2c_shared_vector, i);
                sol_i2c_close_raw(i2c);
            }
            break;
        }
    }
}
