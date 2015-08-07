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
#include <stdio.h>
#include <stdlib.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-i2c.h"
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-util.h"

#include "periph/i2c.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "i2c");

struct sol_i2c {
    i2c_t dev;
    uint8_t slave_address;
    struct {
        const void *cb_data;
        struct sol_timeout *timeout;
        uint8_t *data;
        size_t count;
        ssize_t status;
        uint8_t reg;
        uint8_t times;
        void (*dispatch)(struct sol_i2c *i2c);
        union {
            struct {
                void (*cb)(void *cb_data, struct sol_i2c *i2c, uint8_t *data, ssize_t status);
            } read_write_cb;
            struct {
                void (*cb)(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status);
            } read_write_reg_cb;
        };
    } async;
};

static i2c_speed_t
sol_speed_to_riot_speed(enum sol_i2c_speed speed)
{
    const uint8_t table[] = {
        [SOL_I2C_SPEED_10KBIT] = I2C_SPEED_LOW,
        [SOL_I2C_SPEED_100KBIT] = I2C_SPEED_NORMAL,
        [SOL_I2C_SPEED_400KBIT] = I2C_SPEED_FAST,
        [SOL_I2C_SPEED_1MBIT] = I2C_SPEED_FAST_PLUS,
        [SOL_I2C_SPEED_3MBIT_400KBIT] = I2C_SPEED_HIGH
    };

    SOL_EXP_CHECK(speed > (sizeof(table) / sizeof(uint8_t)), I2C_SPEED_FAST);
    return table[speed];
}

SOL_API struct sol_i2c *
sol_i2c_open_raw(uint8_t bus, enum sol_i2c_speed speed)
{
    struct sol_i2c *i2c;

    SOL_LOG_INTERNAL_INIT_ONCE;

    i2c_acquire(bus);
    i2c_poweron(bus);
    if (i2c_init_master(bus, sol_speed_to_riot_speed(speed)) != 0) {
        i2c_release(bus);
        return NULL;
    }

    i2c_release(bus);
    i2c = calloc(1, sizeof(struct sol_i2c));
    SOL_NULL_CHECK(i2c, NULL);

    i2c->dev = bus;
    return i2c;
}

SOL_API void
sol_i2c_close_raw(struct sol_i2c *i2c)
{
    SOL_NULL_CHECK(i2c);

    if (i2c->async.timeout)
        sol_i2c_pending_cancel(i2c, i2c->async.timeout);

    i2c_acquire(i2c->dev);
    i2c_poweroff(i2c->dev);
    i2c_release(i2c->dev);
    free(i2c);
}

SOL_API void *
sol_i2c_write_quick(struct sol_i2c *i2c, bool rw, void (*write_quick_cb)(void *cb_data, struct sol_i2c *i2c, ssize_t status), const void *cb_data)
{
    SOL_CRI("Unsupported");
    return NULL;
}

static void
_i2c_read_write_dispatch(struct sol_i2c *i2c)
{
    if (!i2c->async.read_write_cb.cb) return;
    i2c->async.read_write_cb.cb((void *)i2c->async.cb_data, i2c,
        i2c->async.data, i2c->async.status);
}

static bool
i2c_read_timeout_cb(void *data)
{
    struct sol_i2c *i2c = data;

    i2c_acquire(i2c->dev);
    i2c->async.status = i2c_read_bytes(i2c->dev, i2c->slave_address,
        (char *)i2c->async.data, i2c->async.count);
    i2c_release(i2c->dev);

    i2c->async.timeout = NULL;
    i2c->async.dispatch(i2c);
    return false;
}

SOL_API void *
sol_i2c_read(struct sol_i2c *i2c, uint8_t *data, size_t count, void (*read_cb)(void *cb_data, struct sol_i2c *i2c, uint8_t *data, ssize_t status), const void *cb_data)
{
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(data, NULL);
    SOL_INT_CHECK(count, == 0, NULL);
    SOL_EXP_CHECK(i2c->async.timeout, NULL);

    i2c->async.data = data;
    i2c->async.count = count;
    i2c->async.status = -1;
    i2c->async.read_write_cb.cb = read_cb;
    i2c->async.dispatch = _i2c_read_write_dispatch;
    i2c->async.cb_data = cb_data;

    i2c->async.timeout = sol_timeout_add(0, i2c_read_timeout_cb, i2c);
    SOL_NULL_CHECK(i2c->async.timeout, NULL);

    return i2c->async.timeout;
}

static bool
i2c_write_timeout_cb(void *data)
{
    struct sol_i2c *i2c = data;

    i2c_acquire(i2c->dev);
    i2c->async.status = i2c_write_bytes(i2c->dev, i2c->slave_address,
        (char *)i2c->async.data, i2c->async.count);
    i2c_release(i2c->dev);

    i2c->async.timeout = NULL;
    i2c->async.dispatch(i2c);
    return false;
}

SOL_API void *
sol_i2c_write(struct sol_i2c *i2c, uint8_t *data, size_t count, void (*write_cb)(void *cb_data, struct sol_i2c *i2c, uint8_t *data, ssize_t status), const void *cb_data)
{
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(data, NULL);
    SOL_INT_CHECK(count, == 0, NULL);
    SOL_EXP_CHECK(i2c->async.timeout, NULL);

    i2c->async.data = data;
    i2c->async.count = count;
    i2c->async.status = -1;
    i2c->async.read_write_cb.cb = write_cb;
    i2c->async.dispatch = _i2c_read_write_dispatch;
    i2c->async.cb_data = cb_data;

    i2c->async.timeout = sol_timeout_add(0, i2c_write_timeout_cb, i2c);
    SOL_NULL_CHECK(i2c->async.timeout, NULL);

    return i2c->async.timeout;
}

static void
_i2c_read_write_reg_dispatch(struct sol_i2c *i2c)
{
    if (!i2c->async.read_write_reg_cb.cb) return;
    i2c->async.read_write_reg_cb.cb((void *)i2c->async.cb_data, i2c,
        i2c->async.reg, i2c->async.data, i2c->async.status);
}

static bool
i2c_read_reg_timeout_cb(void *data)
{
    struct sol_i2c *i2c = data;

    i2c_acquire(i2c->dev);
    i2c->async.status = i2c_read_regs(i2c->dev, i2c->slave_address,
        i2c->async.reg, (char *)i2c->async.data, i2c->async.count);
    i2c_release(i2c->dev);

    i2c->async.timeout = NULL;
    i2c->async.dispatch(i2c);
    return false;
}

SOL_API void *
sol_i2c_read_register(struct sol_i2c *i2c, uint8_t command, uint8_t *values, size_t count, void (*read_reg_cb)(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status), const void *cb_data)
{
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(values, NULL);
    SOL_INT_CHECK(count, == 0, NULL);
    SOL_EXP_CHECK(i2c->async.timeout, NULL);

    i2c->async.data = values;
    i2c->async.count = count;
    i2c->async.status = -1;
    i2c->async.read_write_reg_cb.cb = read_reg_cb;
    i2c->async.dispatch = _i2c_read_write_reg_dispatch;
    i2c->async.reg = command;
    i2c->async.cb_data = cb_data;

    i2c->async.timeout = sol_timeout_add(0, i2c_read_reg_timeout_cb, i2c);
    SOL_NULL_CHECK(i2c->async.timeout, NULL);

    return i2c->async.timeout;
}

static bool
i2c_read_reg_multiple_timeout_cb(void *data)
{
    struct sol_i2c *i2c = data;
    uint8_t i;
    size_t ret;

    i2c_acquire(i2c->dev);
    for (i = 0; i < i2c->async.times; i++) {
        ret = i2c_read_regs(i2c->dev, i2c->slave_address, i2c->async.reg,
            (char *)(i2c->async.data + (i2c->async.count * i)),
            i2c->async.count);
        if (ret != i2c->async.count)
            break;
    }
    i2c_release(i2c->dev);

    if (ret == i2c->async.count)
        i2c->async.status = i2c->async.count * i2c->async.times;

    i2c->async.timeout = NULL;
    i2c->async.dispatch(i2c);
    return false;
}

SOL_API void *
sol_i2c_read_register_multiple(struct sol_i2c *i2c, uint8_t reg, uint8_t *data, size_t count, uint8_t times, void (*read_reg_multiple_cb)(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status), const void *cb_data)
{
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(data, NULL);
    SOL_INT_CHECK(count, == 0, NULL);
    SOL_EXP_CHECK(i2c->async.timeout, NULL);

    i2c->async.data = data;
    i2c->async.count = count;
    i2c->async.status = -1;
    i2c->async.read_write_reg_cb.cb = read_reg_multiple_cb;
    i2c->async.dispatch = _i2c_read_write_reg_dispatch;
    i2c->async.reg = reg;
    i2c->async.cb_data = cb_data;
    i2c->async.times = times;

    i2c->async.timeout = sol_timeout_add(0, i2c_read_reg_multiple_timeout_cb, i2c);
    SOL_NULL_CHECK(i2c->async.timeout, NULL);
    return i2c->async.timeout;
}

static bool
i2c_write_reg_timeout_cb(void *data)
{
    struct sol_i2c *i2c = data;

    i2c_acquire(i2c->dev);
    i2c->async.status = i2c_write_regs(i2c->dev, i2c->slave_address,
        i2c->async.reg, (char *)i2c->async.data, i2c->async.count);
    i2c_release(i2c->dev);

    i2c->async.timeout = NULL;
    i2c->async.dispatch(i2c);
    return false;
}

SOL_API void *
sol_i2c_write_register(struct sol_i2c *i2c, uint8_t reg, const uint8_t *data, size_t count, void (*write_reg_cb)(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status), const void *cb_data)
{
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(data, NULL);
    SOL_INT_CHECK(count, == 0, NULL);
    SOL_EXP_CHECK(i2c->async.timeout, NULL);

    i2c->async.data = (uint8_t *)data;
    i2c->async.count = count;
    i2c->async.status = -1;
    i2c->async.read_write_reg_cb.cb = write_reg_cb;
    i2c->async.dispatch = _i2c_read_write_reg_dispatch;
    i2c->async.reg = reg;
    i2c->async.cb_data = cb_data;

    i2c->async.timeout = sol_timeout_add(0, i2c_write_reg_timeout_cb, i2c);
    SOL_NULL_CHECK(i2c->async.timeout, false);
    return i2c->async.timeout;
}

SOL_API bool
sol_i2c_set_slave_address(struct sol_i2c *i2c, uint8_t slave_address)
{
    SOL_NULL_CHECK(i2c, false);
    SOL_EXP_CHECK(i2c->async.timeout, NULL);

    i2c->slave_address = slave_address;
    return true;
}

SOL_API uint8_t
sol_i2c_get_slave_address(struct sol_i2c *i2c)
{
    SOL_NULL_CHECK(i2c, false);
    return i2c->slave_address;
}

SOL_API uint8_t
sol_i2c_bus_get(const struct sol_i2c *i2c)
{
    SOL_NULL_CHECK(i2c, 0);
    return i2c->dev;
}

SOL_API bool
sol_i2c_busy(struct sol_i2c *i2c)
{
    SOL_NULL_CHECK(i2c, true);
    return i2c->async.timeout;
}

SOL_API void
sol_i2c_pending_cancel(struct sol_i2c *i2c, void *pending)
{
    SOL_NULL_CHECK(i2c);
    SOL_NULL_CHECK(pending);

    if (i2c->async.timeout == pending) {
        sol_timeout_del(i2c->async.timeout);
        i2c->async.dispatch(i2c);
        i2c->async.timeout = NULL;
    } else
        SOL_WRN("Invalid I2C pending handle.");
}
