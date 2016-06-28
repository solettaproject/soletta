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

#include <errno.h>
#include <stdlib.h>

/* Zephyr includes */
#include "i2c.h"

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "i2c");

#include "sol-i2c.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-vector.h"

/* Zephyr exposes, by default, I2C ports, named I2C0 and I2C1. There's
 * also the possibility of exposing the sensor subsystem's ports,
 * named by default I2C_SS_0 and I2C_SS_1, but we're not dealing with
 * SS yet. */

struct i2c_dev {
    const char *name;
};

static struct i2c_dev i2c_0_dev = {
    .name = "I2C_0",
};

static struct i2c_dev i2c_1_dev = {
    .name = "I2C_1",
};

static struct i2c_dev *devs[2] = {
    &i2c_0_dev,
    &i2c_1_dev
};

struct sol_i2c {
    struct device *dev;
    struct i2c_dev *dev_ref;
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
                void (*cb)(void *cb_data,
                    struct sol_i2c *i2c,
                    uint8_t *data,
                    ssize_t status);
            } read_write_cb;
            struct {
                void (*cb)(void *cb_data,
                    struct sol_i2c *i2c,
                    uint8_t reg,
                    uint8_t *data,
                    ssize_t status);
            } read_write_reg_cb;
        };
    } async;
};

void sol_i2c_close_raw(struct sol_i2c *i2c);

static int
sol_speed_to_zephyr_speed(enum sol_i2c_speed speed)
{
    /* Zephyr does not bother implementing the 10KBIT speed, it seems.
     * There's also an extra I2C_SPEED_ULTRA entry there, with no
     * counterpart here. Since the I2C specs generally map these names
     * to the corresponding speeds, we chose to not expose the higher
     * speed and repeat the lower one. */
    const uint8_t table[] = {
        [SOL_I2C_SPEED_10KBIT] = I2C_SPEED_STANDARD,
        [SOL_I2C_SPEED_100KBIT] = I2C_SPEED_STANDARD,
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
    struct sol_i2c *i2c = NULL;
    struct device *dev = NULL;
    union dev_config config;
    int ret;

    SOL_LOG_INTERNAL_INIT_ONCE;

    if (bus != 0 && bus != 1) {
        SOL_WRN("Unsupported I2C bus %d", bus);
        goto err;
    }

    config.raw = 0;
    config.bits.use_10_bit_addr = 0;
    config.bits.is_master_device = 1;
    config.bits.speed = sol_speed_to_zephyr_speed(speed);
    config.bits.is_slave_read = 0;

    dev = device_get_binding((char *)devs[bus]->name);
    if (!dev) {
        SOL_WRN("Failed to open I2C device %s", devs[bus]->name);
        return NULL;
    }

    ret = i2c_configure(dev, config.raw);
    if (ret < 0) {
        SOL_WRN("Failed to configure I2C device %s: %s", devs[bus]->name,
            sol_util_strerrora(-ret));
        return NULL;
    }

    i2c = calloc(1, sizeof(struct sol_i2c));
    SOL_NULL_CHECK(i2c, NULL);

    i2c->dev = dev;
    i2c->dev_ref = devs[bus];

err:
    return i2c;
}

void
sol_i2c_close_raw(struct sol_i2c *i2c)
{
    if (i2c->async.timeout)
        sol_i2c_pending_cancel
            (i2c, (struct sol_i2c_pending *)i2c->async.timeout);

    free(i2c);
}

SOL_API struct sol_i2c_pending *
sol_i2c_write_quick(struct sol_i2c *i2c,
    bool rw,
    void (*write_quick_cb)(void *cb_data,
    struct sol_i2c *i2c,
    ssize_t status),
    const void *cb_data)
{
    SOL_WRN("Unsupported");
    errno = ENOSYS;
    return NULL;
}

static void
_i2c_read_write_dispatch(struct sol_i2c *i2c)
{
    if (!i2c->async.read_write_cb.cb)
        return;

    i2c->async.read_write_cb.cb((void *)i2c->async.cb_data, i2c,
        i2c->async.data, i2c->async.status);
}

static bool
i2c_read_timeout_cb(void *data)
{
    struct sol_i2c *i2c = data;
    int ret;

    ret = i2c_read(i2c->dev, i2c->async.data, i2c->async.count,
        i2c->slave_address);
    if (ret < 0)
        i2c->async.status = ret;
    else
        i2c->async.status = i2c->async.count;

    i2c->async.timeout = NULL;
    i2c->async.dispatch(i2c);
    return false;
}

SOL_API struct sol_i2c_pending *
sol_i2c_read(struct sol_i2c *i2c,
    uint8_t *data,
    size_t count,
    void (*read_cb)(void *cb_data,
    struct sol_i2c *i2c,
    uint8_t *data,
    ssize_t status),
    const void *cb_data)
{
    errno = EINVAL;
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(data, NULL);
    SOL_INT_CHECK(count, == 0, NULL);
    if (i2c->async.timeout) {
        SOL_WRN("There's an ongoing operation for the given I2C handle (%p), "
            "wait for it to finish or cancel it to make this call", i2c);
        errno = EBUSY;
        return NULL;
    }

    i2c->async.data = data;
    i2c->async.count = count;
    i2c->async.status = -EIO;
    i2c->async.read_write_cb.cb = read_cb;
    i2c->async.dispatch = _i2c_read_write_dispatch;
    i2c->async.cb_data = cb_data;

    i2c->async.timeout = sol_timeout_add(0, i2c_read_timeout_cb, i2c);
    errno = ENOMEM;
    SOL_NULL_CHECK(i2c->async.timeout, NULL);

    errno = 0;
    return (struct sol_i2c_pending *)i2c->async.timeout;
}

static bool
i2c_write_timeout_cb(void *data)
{
    struct sol_i2c *i2c = data;
    int ret;

    ret = i2c_write(i2c->dev, i2c->async.data, i2c->async.count,
        i2c->slave_address);
    if (ret < 0)
        i2c->async.status = ret;
    else
        i2c->async.status = i2c->async.count;

    i2c->async.timeout = NULL;
    i2c->async.dispatch(i2c);
    return false;
}

SOL_API struct sol_i2c_pending *
sol_i2c_write(struct sol_i2c *i2c,
    uint8_t *data,
    size_t count,
    void (*write_cb)(void *cb_data,
    struct sol_i2c *i2c,
    uint8_t *data,
    ssize_t status),
    const void *cb_data)
{
    errno = EINVAL;
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(data, NULL);
    SOL_INT_CHECK(count, == 0, NULL);
    if (i2c->async.timeout) {
        SOL_WRN("There's an ongoing operation for the given I2C handle (%p), "
            "wait for it to finish or cancel it to make this call", i2c);
        errno = EBUSY;
        return NULL;
    }

    i2c->async.data = data;
    i2c->async.count = count;
    i2c->async.status = -EIO;
    i2c->async.read_write_cb.cb = write_cb;
    i2c->async.dispatch = _i2c_read_write_dispatch;
    i2c->async.cb_data = cb_data;

    i2c->async.timeout = sol_timeout_add(0, i2c_write_timeout_cb, i2c);
    errno = ENOMEM;
    SOL_NULL_CHECK(i2c->async.timeout, NULL);

    errno = 0;
    return (struct sol_i2c_pending *)i2c->async.timeout;
}

static void
_i2c_read_write_reg_dispatch(struct sol_i2c *i2c)
{
    if (!i2c->async.read_write_reg_cb.cb)
        return;

    i2c->async.read_write_reg_cb.cb((void *)i2c->async.cb_data, i2c,
        i2c->async.reg, i2c->async.data, i2c->async.status);
}

static bool
i2c_read_reg_timeout_cb(void *data)
{
    struct sol_i2c *i2c = data;
    struct i2c_msg msg[2];
    int ret;

    msg[0].flags = I2C_MSG_WRITE | I2C_MSG_RESTART;
    msg[0].buf = &i2c->async.reg;
    msg[0].len = sizeof(i2c->async.reg);

    msg[1].flags = I2C_MSG_READ | I2C_MSG_STOP;
    msg[1].buf = i2c->async.data;
    msg[1].len = i2c->async.count;

    ret = i2c_transfer(i2c->dev, msg, 2, i2c->slave_address);
    if (ret < 0)
        i2c->async.status = ret;
    else
        i2c->async.status = i2c->async.count;

    i2c->async.timeout = NULL;
    i2c->async.dispatch(i2c);
    return false;
}

SOL_API struct sol_i2c_pending *
sol_i2c_read_register(struct sol_i2c *i2c,
    uint8_t command,
    uint8_t *values,
    size_t count,
    void (*read_reg_cb)(void *cb_data,
    struct sol_i2c *i2c,
    uint8_t reg,
    uint8_t *data,
    ssize_t status),
    const void *cb_data)
{
    errno = EINVAL;
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(values, NULL);
    SOL_INT_CHECK(count, == 0, NULL);
    if (i2c->async.timeout) {
        SOL_WRN("There's an ongoing operation for the given I2C handle (%p), "
            "wait for it to finish or cancel it to make this call", i2c);
        errno = EBUSY;
        return NULL;
    }

    i2c->async.data = values;
    i2c->async.count = count;
    i2c->async.status = -EIO;
    i2c->async.read_write_reg_cb.cb = read_reg_cb;
    i2c->async.dispatch = _i2c_read_write_reg_dispatch;
    i2c->async.reg = command;
    i2c->async.cb_data = cb_data;

    i2c->async.timeout = sol_timeout_add(0, i2c_read_reg_timeout_cb, i2c);
    errno = ENOMEM;
    SOL_NULL_CHECK(i2c->async.timeout, NULL);

    errno = 0;
    return (struct sol_i2c_pending *)i2c->async.timeout;
}

static bool
i2c_read_reg_multiple_timeout_cb(void *data)
{
    struct sol_i2c *i2c = data;
    struct i2c_msg msg;
    uint8_t i;
    size_t ret = 0;

    /* First, write destination register */
    msg.flags = I2C_MSG_WRITE | I2C_MSG_RESTART;
    msg.buf = &i2c->async.reg;
    msg.len = sizeof(i2c->async.reg);

    ret = i2c_transfer(i2c->dev, &msg, 1, i2c->slave_address);
    if (ret < 0)
        goto end;

    for (i = 0; i < i2c->async.times; i++) {
        msg.flags = I2C_MSG_WRITE | (i == (i2c->async.times - 1)) ? I2C_MSG_STOP : 0;
        msg.buf = i2c->async.data + (i2c->async.count * i);
        msg.len = i2c->async.count;

        SOL_WRN("Flags: %d %d", msg.flags, (msg.flags & I2C_MSG_STOP) == I2C_MSG_STOP);

        ret = i2c_transfer(i2c->dev, &msg, 1, i2c->slave_address);
        if (ret < 0)
            break;
    }

    if (ret == 0)
        i2c->async.status = i2c->async.count * i2c->async.times;
    else
        i2c->async.status = ret;

end:
    i2c->async.timeout = NULL;
    i2c->async.dispatch(i2c);
    return false;
}

SOL_API struct sol_i2c_pending *
sol_i2c_read_register_multiple(struct sol_i2c *i2c,
    uint8_t reg,
    uint8_t *data,
    size_t count,
    uint8_t times,
    void (*read_reg_multiple_cb)(void *cb_data,
    struct sol_i2c *i2c,
    uint8_t reg,
    uint8_t *data,
    ssize_t status),
    const void *cb_data)
{
    errno = EINVAL;
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(data, NULL);
    SOL_INT_CHECK(count, == 0, NULL);
    if (i2c->async.timeout) {
        SOL_WRN("There's an ongoing operation for the given I2C handle (%p), "
            "wait for it to finish or cancel it to make this call", i2c);
        errno = EBUSY;
        return NULL;
    }

    i2c->async.data = data;
    i2c->async.count = count;
    i2c->async.status = -EIO;
    i2c->async.read_write_reg_cb.cb = read_reg_multiple_cb;
    i2c->async.dispatch = _i2c_read_write_reg_dispatch;
    i2c->async.reg = reg;
    i2c->async.cb_data = cb_data;
    i2c->async.times = times;

    i2c->async.timeout = sol_timeout_add
            (0, i2c_read_reg_multiple_timeout_cb, i2c);
    errno = EINVAL;
    SOL_NULL_CHECK(i2c->async.timeout, NULL);

    errno = 0;
    return (struct sol_i2c_pending *)i2c->async.timeout;
}

static bool
i2c_write_reg_timeout_cb(void *data)
{
    struct sol_i2c *i2c = data;
    struct i2c_msg msg[2];
    int ret;

    msg[0].flags = I2C_MSG_WRITE | I2C_MSG_RESTART;
    msg[0].buf = &i2c->async.reg;
    msg[0].len = sizeof(i2c->async.reg);

    msg[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;
    msg[1].buf = i2c->async.data;
    msg[1].len = i2c->async.count;

    ret = i2c_transfer(i2c->dev, msg, 2, i2c->slave_address);
    if (ret < 0)
        i2c->async.status = ret;
    else
        i2c->async.status = i2c->async.count;

    i2c->async.timeout = NULL;
    i2c->async.dispatch(i2c);
    return false;
}

SOL_API struct sol_i2c_pending *
sol_i2c_write_register(struct sol_i2c *i2c,
    uint8_t reg,
    const uint8_t *data,
    size_t count,
    void (*write_reg_cb)(void *cb_data,
    struct sol_i2c *i2c,
    uint8_t reg,
    uint8_t *data,
    ssize_t status),
    const void *cb_data)
{
    errno = 0;
    SOL_NULL_CHECK(i2c, NULL);
    SOL_NULL_CHECK(data, NULL);
    SOL_INT_CHECK(count, == 0, NULL);
    if (i2c->async.timeout) {
        SOL_WRN("There's an ongoing operation for the given I2C handle (%p), "
            "wait for it to finish or cancel it to make this call", i2c);
        errno = EBUSY;
        return NULL;
    }

    i2c->async.data = (uint8_t *)data;
    i2c->async.count = count;
    i2c->async.status = -1;
    i2c->async.read_write_reg_cb.cb = write_reg_cb;
    i2c->async.dispatch = _i2c_read_write_reg_dispatch;
    i2c->async.reg = reg;
    i2c->async.cb_data = cb_data;

    i2c->async.timeout = sol_timeout_add(0, i2c_write_reg_timeout_cb, i2c);
    errno = ENOMEM;
    SOL_NULL_CHECK(i2c->async.timeout, NULL);
    errno = 0;
    return (struct sol_i2c_pending *)i2c->async.timeout;
}

SOL_API int
sol_i2c_set_slave_address(struct sol_i2c *i2c, uint8_t slave_address)
{
    SOL_NULL_CHECK(i2c, -EINVAL);
    if (i2c->async.timeout) {
        SOL_WRN("There's an ongoing operation for the given I2C handle (%p), "
            "wait for it to finish or cancel it to make this call", i2c);
        return -EBUSY;
    }

    i2c->slave_address = slave_address;
    return 0;
}

SOL_API uint8_t
sol_i2c_get_slave_address(struct sol_i2c *i2c)
{
    SOL_NULL_CHECK(i2c, false);
    return i2c->slave_address;
}

SOL_API uint8_t
sol_i2c_get_bus(const struct sol_i2c *i2c)
{
    SOL_NULL_CHECK(i2c, 0);
    return i2c->dev_ref - devs[0];
}

SOL_API void
sol_i2c_pending_cancel(struct sol_i2c *i2c, struct sol_i2c_pending *pending)
{
    SOL_NULL_CHECK(i2c);
    SOL_NULL_CHECK(pending);

    if (i2c->async.timeout == (struct sol_timeout *)pending) {
        sol_timeout_del(i2c->async.timeout);
        i2c->async.timeout = NULL;
    } else
        SOL_WRN("Invalid I2C pending handle.");
}
