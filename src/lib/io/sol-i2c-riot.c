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
#include "sol-util.h"

#include "periph/i2c.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "i2c");

struct sol_i2c {
    i2c_t dev;
    uint8_t slave_address;
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
sol_i2c_close(struct sol_i2c *i2c)
{
    SOL_NULL_CHECK(i2c);
    i2c_acquire(i2c->dev);
    i2c_poweroff(i2c->dev);
    i2c_release(i2c->dev);
    free(i2c);
}

SOL_API bool
sol_i2c_write_quick(const struct sol_i2c *i2c, bool rw)
{
    SOL_CRI("Unsupported");
    return false;
}

SOL_API ssize_t
sol_i2c_read(const struct sol_i2c *i2c, uint8_t *data, size_t count)
{
    ssize_t ret;

    SOL_NULL_CHECK(i2c, -EINVAL);

    i2c_acquire(i2c->dev);
    ret = i2c_read_bytes(i2c->dev, i2c->slave_address, (char *)data, count);
    i2c_release(i2c->dev);
    return ret;
}

SOL_API bool
sol_i2c_write(const struct sol_i2c *i2c, uint8_t *data, size_t count)
{
    int write;

    SOL_NULL_CHECK(i2c, false);

    i2c_acquire(i2c->dev);
    write = i2c_write_bytes(i2c->dev, i2c->slave_address, (char *)data, count);
    i2c_release(i2c->dev);
    if (write > -1)
        return write == count;
    return false;
}

SOL_API ssize_t
sol_i2c_read_register(const struct sol_i2c *i2c, uint8_t reg, uint8_t *data, size_t count)
{
    ssize_t ret;

    SOL_NULL_CHECK(i2c, -EINVAL);

    i2c_acquire(i2c->dev);
    ret = i2c_read_regs(i2c->dev, i2c->slave_address, reg, (char *)data, count);
    i2c_release(i2c->dev);
    return ret;
}

SOL_API bool
sol_i2c_read_register_multiple(const struct sol_i2c *i2c, uint8_t command, uint8_t *values, uint8_t count, uint8_t times)
{
    uint8_t i;
    ssize_t ret;

    SOL_NULL_CHECK(i2c, false);

    i2c_acquire(i2c->dev);
    for (i = 0; i < times; i++) {
        ret = i2c_read_regs(i2c->dev, i2c->slave_address, command,
            (char *)(values + (count * i)), count);
        if (ret != count)
            break;
    }
    i2c_release(i2c->dev);

    return ret == count;
}

SOL_API bool
sol_i2c_write_register(const struct sol_i2c *i2c, uint8_t reg, const uint8_t *data, size_t count)
{
    int write;

    SOL_NULL_CHECK(i2c, false);

    i2c_acquire(i2c->dev);
    write = i2c_write_regs(i2c->dev, i2c->slave_address, reg, (char *)data, count);
    i2c_release(i2c->dev);
    if (write > -1)
        return write == count;
    return false;
}

SOL_API bool
sol_i2c_set_slave_address(struct sol_i2c *i2c, uint8_t slave_address)
{
    SOL_NULL_CHECK(i2c, false);
    i2c->slave_address = slave_address;
    return true;
}

SOL_API uint8_t
sol_i2c_get_slave_address(struct sol_i2c *i2c)
{
    SOL_NULL_CHECK(i2c, false);
    return i2c->slave_address;
}
