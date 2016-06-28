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

#include <stdlib.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "i2c");

#include "sol-i2c.h"
#include "sol-mainloop.h"
#ifdef USE_PIN_MUX
#include "sol-pin-mux.h"
#endif
#include "sol-str-table.h"
#include "sol-util.h"
#include "sol-vector.h"

struct sol_i2c_dispatcher {
    struct sol_timeout *timer;
    struct sol_i2c_pending *pending;
    struct sol_ptr_vector queue;
    uint16_t set_idx;
    uint8_t retry;
};

struct sol_i2c_shared {
    struct sol_i2c *i2c;
    struct sol_i2c_dispatcher dispatcher;
    uint16_t refcount;
};

extern void sol_i2c_close_raw(struct sol_i2c *i2c);

static struct sol_vector i2c_shared_vector = SOL_VECTOR_INIT(struct sol_i2c_shared);

// ============================================================================
// I2C Dispatcher
// ============================================================================

#define SOL_I2C_MAX_RETRIES 3

struct sol_i2c_op_set {
    struct sol_vector *set;
    void (*cb)(void *cb_data, ssize_t status);
    void *cb_data;
    uint32_t delay;
    uint8_t addr;
    bool delete_me;
};

static bool i2c_dispatcher_exec_op(void *data);

static void
i2c_dispatcher_end_set(struct sol_i2c_dispatcher *dispatcher, ssize_t end_status)
{
    struct sol_i2c_op_set *set = sol_ptr_vector_get(&dispatcher->queue, 0);

    if (!set->delete_me && set->cb)
        set->cb(set->cb_data, end_status);

    free(set);
    sol_ptr_vector_del(&dispatcher->queue, 0);

    dispatcher->set_idx = 0;
}

static void
i2c_dispatcher_op_done(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data,
    ssize_t status)
{
    struct sol_i2c_shared *shared = cb_data;
    struct sol_i2c_dispatcher *dispatcher = &shared->dispatcher;
    struct sol_i2c_op_set *op_set = sol_ptr_vector_get(&dispatcher->queue, 0);

    dispatcher->pending = NULL;
    dispatcher->retry = 0;

    if (!op_set->delete_me) {
        if (status <= 0) {
            SOL_ERR("[bus=%d addr=0x%02x reg=0x%02x] I2C operation failed!",
                sol_i2c_get_bus(i2c), op_set->addr, reg);
        } else if (++dispatcher->set_idx < op_set->set->len) {
            goto proceed;
        }
    }

    i2c_dispatcher_end_set(dispatcher, status);
    op_set = sol_ptr_vector_get(&dispatcher->queue, 0);

    if (!op_set)
        return; // We finished for now

proceed:
    if (dispatcher->timer)
        SOL_ERR("Dispatcher timer should be NULL in this point.");
    dispatcher->timer = sol_timeout_add(op_set->delay, i2c_dispatcher_exec_op, shared);
    if (!dispatcher->timer) {
        SOL_ERR("Failed to schedule I2C operation.");
        status = -1;
        if (op_set->cb)
            op_set->cb(op_set->cb_data, status);
    }
}

static bool
i2c_dispatcher_exec_op(void *data)
{
    struct sol_i2c_op *op;
    struct sol_i2c_shared *shared = data;
    struct sol_i2c_dispatcher *dispatcher = &shared->dispatcher;
    struct sol_i2c_op_set *op_set = sol_ptr_vector_get(&dispatcher->queue, 0);
    int r;

    if (!op_set)
        goto exit;

    if (dispatcher->pending) {
        if (++dispatcher->retry >= SOL_I2C_MAX_RETRIES) {
            SOL_ERR("Failed to schedule I2C operation.");
            goto exit;
        }

        return true;
    }

    r = sol_i2c_set_slave_address(shared->i2c, op_set->addr);
    if (r < 0) {
        if (r == -EBUSY) {
            if (++dispatcher->retry >= SOL_I2C_MAX_RETRIES) {
                SOL_ERR("Failed to schedule I2C operation.");
                goto exit;
            }

            return true;
        }

        SOL_ERR("Failed to set slave address 0x%02x on I2C bus: %d.",
            op_set->addr, sol_i2c_get_bus(shared->i2c));
        i2c_dispatcher_end_set(dispatcher, -1);

        return true;
    }

    op = sol_vector_get(op_set->set, dispatcher->set_idx);

    dispatcher->pending = (op->type == SOL_I2C_WRITE) ?
        sol_i2c_write_register(shared->i2c, op->reg, &op->value, 1,
        i2c_dispatcher_op_done, shared) :
        sol_i2c_read_register(shared->i2c, op->reg, &op->value, 1,
        i2c_dispatcher_op_done, shared);

    if (!dispatcher->pending) {
        dispatcher->retry++;
        return true;
    }

exit:
    dispatcher->timer = NULL;
    return false;
}

SOL_API struct sol_i2c_op_set_pending *
sol_i2c_dispatcher_add_op_set(struct sol_i2c *i2c, uint8_t addr, struct sol_vector *set,
    void (*cb)(void *cb_data, ssize_t status), void *cb_data, uint32_t delay)
{
    struct sol_i2c_shared *tmp, *shared = NULL;
    struct sol_i2c_op_set *op_set;
    uint8_t i;

    SOL_VECTOR_FOREACH_IDX (&i2c_shared_vector, tmp, i) {
        if (tmp->i2c == i2c) {
            shared = tmp;
            break;
        }
    }
    SOL_NULL_CHECK_MSG(shared, NULL, "Internal I2C bus handle not found.");

    op_set = calloc(1, sizeof(struct sol_i2c_op_set));
    SOL_NULL_CHECK_MSG(op_set, NULL, "Could not allocate memory for the I2C Operation Set.");

    op_set->set = set;
    op_set->cb = cb;
    op_set->cb_data = cb_data;
    op_set->delay = delay;
    op_set->addr = addr;

    if (sol_ptr_vector_append(&shared->dispatcher.queue, op_set)) {
        SOL_ERR("Couldn't append operation set on dispatcher queue.");
        goto error;
    }

    if (sol_ptr_vector_get_len(&shared->dispatcher.queue) == 1) {
        // Don't check on timer to verify that the dispatcher engine needs to be started,
        // it can be between exec -> done operation and this would set a timer with incorrect
        // timeout value for operations set that uses specific time in between.

        // This should always be null.
        SOL_NULL_CHECK_MSG_GOTO(!shared->dispatcher.timer, start_error,
            "Dispatcher timer should always be NULL in this point.");

        shared->dispatcher.timer = sol_timeout_add(0, i2c_dispatcher_exec_op, shared);
        SOL_NULL_CHECK_MSG_GOTO(shared->dispatcher.timer, start_error,
            "Couldn't start I2C Dispatcher.");
    }

    return (struct sol_i2c_op_set_pending *)op_set;

start_error:
    sol_ptr_vector_del(&shared->dispatcher.queue, 0);
error:
    free(op_set);
    return NULL;
}

SOL_API void
sol_i2c_dispatcher_remove_op_set(struct sol_i2c *i2c, struct sol_i2c_op_set_pending *pending)
{
    struct sol_i2c_shared *tmp, *shared = NULL;
    struct sol_ptr_vector *pv = NULL;
    struct sol_i2c_op_set *op_set = NULL;
    void *t;
    uint8_t i;

    SOL_VECTOR_FOREACH_IDX (&i2c_shared_vector, tmp, i) {
        if (tmp->i2c == i2c) {
            shared = tmp;
            break;
        }
    }
    if (!shared)
        return;

    pv = &shared->dispatcher.queue;

    SOL_PTR_VECTOR_FOREACH_IDX (pv, t, i) {
        if (t == pending) {
            op_set = t;
            break;
        }
    }
    if (!op_set)
        return;

    if (!i) {
        // current set is in execution, delay the deletion until current operation finishes
        op_set->delete_me = true;
    } else {
        sol_ptr_vector_del(pv, i);
        free(op_set);
    }
}

static void
i2c_dispatcher_close(struct sol_i2c *i2c, struct sol_i2c_dispatcher *dispatcher)
{
    struct sol_i2c_op_set *op_set;
    uint8_t i;

    if (dispatcher->timer)
        sol_timeout_del(dispatcher->timer);

    if (dispatcher->pending)
        sol_i2c_pending_cancel(i2c, dispatcher->pending);

    SOL_PTR_VECTOR_FOREACH_IDX (&dispatcher->queue, op_set, i) {
        free(op_set);
    }

    sol_ptr_vector_clear(&dispatcher->queue);
}

// ============================================================================
// I2C Bus API
// ============================================================================

SOL_API struct sol_i2c *
sol_i2c_open(uint8_t bus, enum sol_i2c_speed speed)
{
    struct sol_i2c *i2c;
    struct sol_i2c_shared *i2c_shared;
    uint8_t i;

    SOL_LOG_INTERNAL_INIT_ONCE;

    SOL_VECTOR_FOREACH_IDX (&i2c_shared_vector, i2c_shared, i) {
        if (sol_i2c_get_bus(i2c_shared->i2c) == bus) {
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
    sol_ptr_vector_init(&i2c_shared->dispatcher.queue);

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
                i2c_dispatcher_close(i2c, &i2c_shared->dispatcher);
                sol_vector_del(&i2c_shared_vector, i);
                sol_i2c_close_raw(i2c);
            }
            break;
        }
    }
}

SOL_API enum sol_i2c_speed
sol_i2c_speed_from_str(const char *speed)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("10kbps", SOL_I2C_SPEED_10KBIT),
        SOL_STR_TABLE_ITEM("100kbps", SOL_I2C_SPEED_100KBIT),
        SOL_STR_TABLE_ITEM("400kbps", SOL_I2C_SPEED_400KBIT),
        SOL_STR_TABLE_ITEM("1000kbps", SOL_I2C_SPEED_1MBIT),
        SOL_STR_TABLE_ITEM("3400kbps", SOL_I2C_SPEED_3MBIT_400KBIT),
        { }
    };

    SOL_NULL_CHECK(speed, SOL_I2C_SPEED_10KBIT);

    return sol_str_table_lookup_fallback(table,
        sol_str_slice_from_str(speed), SOL_I2C_SPEED_10KBIT);
}

SOL_API const char *
sol_i2c_speed_to_str(enum sol_i2c_speed speed)
{
    static const char *speed_names[] = {
        [SOL_I2C_SPEED_10KBIT] = "10kbps",
        [SOL_I2C_SPEED_100KBIT] = "100kbps",
        [SOL_I2C_SPEED_400KBIT] = "400kbps",
        [SOL_I2C_SPEED_1MBIT] = "1000kbps",
        [SOL_I2C_SPEED_3MBIT_400KBIT] = "3400kbps"
    };

    if (speed < sol_util_array_size(speed_names))
        return speed_names[speed];

    return NULL;
}
