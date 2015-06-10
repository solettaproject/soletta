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

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <errno.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "i2c");

#include "sol-i2c.h"
#include "sol-macros.h"
#include "sol-util.h"

#ifdef HAVE_PLATFORM_GALILEO
#include "sol-galileo.h"
#endif

struct sol_i2c {
    int dev;
    uint8_t bus;
    uint8_t addr;
};

struct sol_i2c *
sol_i2c_open(uint8_t bus, enum sol_i2c_speed speed)
{
    struct sol_i2c *i2c;
    char i2c_dev_path[PATH_MAX];
    int len, dev;

    SOL_LOG_INTERNAL_INIT_ONCE;

    len = snprintf(i2c_dev_path, sizeof(i2c_dev_path), "/dev/i2c-%u", bus);
    if (len < 0 || len >= PATH_MAX) {
        SOL_WRN("i2c #%u: could not format device path", bus);
        return NULL;
    }

    i2c = calloc(1, sizeof(*i2c));
    if (!i2c) {
        SOL_WRN("i2c #%u: could not allocate i2c context", bus);
        errno = ENOMEM;
        return NULL;
    }

    dev = open(i2c_dev_path, O_RDWR | O_CLOEXEC);
    if (dev < 0) {
        SOL_WRN("i2c #%u: could not open device file", bus);
        goto open_error;
    }
    i2c->bus = bus;
    i2c->dev = dev;

#ifdef HAVE_PLATFORM_GALILEO
    i2c_setup();
#endif
    return i2c;

open_error:
    free(i2c);
    return NULL;
}

void
sol_i2c_close(struct sol_i2c *i2c)
{
    SOL_NULL_CHECK(i2c);

    close(i2c->dev);
    free(i2c);
}

static int32_t
_i2c_smbus_ioctl(int dev, uint8_t rw, uint8_t command, size_t size, union i2c_smbus_data *data)
{
    struct i2c_smbus_ioctl_data ioctldata = {
        .read_write = rw,
        .command = command,
        .size = 0,
        .data = data
    };

    switch (size) {
    case 1:
        ioctldata.size = I2C_SMBUS_BYTE_DATA;
        break;
    case 2:
        ioctldata.size = I2C_SMBUS_WORD_DATA;
        break;
    default:
        ioctldata.size = I2C_SMBUS_BLOCK_DATA;
    }

    if (ioctl(dev, I2C_SMBUS, &ioctldata) == -1) {
        return -errno;
    }

    return 0;
}

bool
sol_i2c_write_quick(const struct sol_i2c *i2c, bool rw)
{
    struct i2c_smbus_ioctl_data ioctldata = {
        .read_write = rw,
        .command = 0,
        .size = I2C_SMBUS_QUICK,
        .data = NULL
    };

    SOL_NULL_CHECK(i2c, false);

    if (ioctl(i2c->dev, I2C_SMBUS, &ioctldata) == -1) {
        SOL_WRN("Unable to perform SMBus write quick (bus = %u,"
            " device address = %u): %s", i2c->bus, i2c->addr,
            sol_util_strerrora(errno));
        return false;
    }

    return true;
}

static bool
write_byte(const struct sol_i2c *i2c, uint8_t byte)
{
    struct i2c_smbus_ioctl_data ioctldata = {
        .read_write = I2C_SMBUS_WRITE,
        .command = byte,
        .size = I2C_SMBUS_BYTE,
        .data = NULL
    };

    if (ioctl(i2c->dev, I2C_SMBUS, &ioctldata) == -1) {
        SOL_WRN("Unable to perform SMBus write byte (bus = %u,"
            " device address = %u): %s",
            i2c->bus, i2c->addr, sol_util_strerrora(errno));
        return false;
    }
    return true;
}

static bool
read_byte(const struct sol_i2c *i2c, uint8_t *byte)
{
    union i2c_smbus_data data;
    struct i2c_smbus_ioctl_data ioctldata = {
        .read_write = I2C_SMBUS_READ,
        .command = 0,
        .size = I2C_SMBUS_BYTE,
        .data = &data,
    };

    if (ioctl(i2c->dev, I2C_SMBUS, &ioctldata) == -1) {
        SOL_WRN("Unable to perform SMBus read byte (bus = %u,"
            " device address = %u): %s",
            i2c->bus, i2c->addr, sol_util_strerrora(errno));
        return false;
    }

    *byte = data.byte;

    return true;
}

ssize_t
sol_i2c_read(const struct sol_i2c *i2c, uint8_t *values, size_t count)
{
    size_t i;

    SOL_NULL_CHECK(i2c, -EINVAL);
    SOL_NULL_CHECK(values, -EINVAL);
    SOL_INT_CHECK(count, == 0, -EINVAL);

    for (i = 0; i < count; i++) {
        uint8_t byte;
        if (!read_byte(i2c, &byte))
            return -errno;
        *values = byte;
        values++;
    }
    return i;
}

bool
sol_i2c_write(const struct sol_i2c *i2c, uint8_t *values, size_t count)
{
    size_t i;

    SOL_NULL_CHECK(i2c, false);
    SOL_NULL_CHECK(values, false);
    SOL_INT_CHECK(count, == 0, false);

    for (i = 0; i < count; i++) {
        if (!write_byte(i2c, *values))
            return false;
        values++;
    }
    return true;
}

ssize_t
sol_i2c_read_register(const struct sol_i2c *i2c, uint8_t command, uint8_t *values, size_t count)
{
    union i2c_smbus_data data;
    ssize_t length;
    int32_t error;

    SOL_NULL_CHECK(i2c, -EINVAL);
    SOL_NULL_CHECK(values, -EINVAL);
    SOL_INT_CHECK(count, == 0, -EINVAL);

    if ((error = _i2c_smbus_ioctl(i2c->dev, I2C_SMBUS_READ, command, count, &data)) < 0) {
        SOL_WRN("Unable to perform SMBus read (byte/word/block) data "
            "(bus = %u, device address = 0x%x, register = 0x%x): %s",
            i2c->bus, i2c->addr, command, sol_util_strerrora(-error));
        return error;
    }

    // block[0] is the data block length. Up to I2C_SMBUS_BLOCK_MAX.
    length = count < data.block[0] ? count : data.block[0];
    length = length < I2C_SMBUS_BLOCK_MAX ? length : I2C_SMBUS_BLOCK_MAX;

    if (length == 1)
        *values = data.byte;
    else if (length == 2) {
        values[0] = data.word >> 8;
        values[1] = data.word & 0x0FF;
    } else
        memcpy(values, data.block + 1, length);

    return length;
}

bool
sol_i2c_write_register(const struct sol_i2c *i2c, uint8_t command, const uint8_t *values, size_t count)
{
    int32_t error;
    union i2c_smbus_data data = { 0 };

    SOL_NULL_CHECK(i2c, false);
    SOL_NULL_CHECK(values, false);
    SOL_INT_CHECK(count, == 0, false);

    if (count > I2C_SMBUS_BLOCK_MAX) {
        SOL_WRN("Block data limited to %d bytes, writing only up to that"
            " (bus = %u, device address = 0x%x, register = 0x%x: )",
            I2C_SMBUS_BLOCK_MAX, i2c->bus, i2c->addr, command);
        count = I2C_SMBUS_BLOCK_MAX;
    }

    switch (count) {
    case 1:
        data.byte = values[0];
        break;
    case 2:
        memcpy(&data.word, values, 2);
        break;
    default:
        data.block[0] = count; // linux/i2c.h: block[0] is used for length
        memcpy(data.block + 1, values, count);
    }

    if ((error = _i2c_smbus_ioctl(i2c->dev, I2C_SMBUS_WRITE, command, count, &data)) < 0) {
        SOL_WRN("Unable to perform SMBus write (byte/word/block) data "
            " (bus = %u, device address = 0x%x, register = 0x%x:): %s",
            i2c->bus, i2c->addr, command, sol_util_strerrora(-error));
        return false;
    }

    return true;
}

bool
sol_i2c_set_slave_address(struct sol_i2c *i2c, uint8_t slave_address)
{
    SOL_NULL_CHECK(i2c, false);

    if (ioctl(i2c->dev, I2C_SLAVE, slave_address) == -1) {
        SOL_WRN("I2C (bus = %u): could not specify device address 0x%x",
            i2c->bus, slave_address);
        return false;
    }
    i2c->addr = slave_address;
    return true;
}

uint8_t
sol_i2c_get_slave_address(struct sol_i2c *i2c)
{
    SOL_NULL_CHECK(i2c, 0);
    return i2c->addr;
}
