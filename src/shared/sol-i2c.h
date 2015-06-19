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

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "sol-macros.h"

struct sol_i2c;

enum sol_i2c_speed {
    SOL_I2C_SPEED_10KBIT = 0,
    SOL_I2C_SPEED_100KBIT,
    SOL_I2C_SPEED_400KBIT,
    SOL_I2C_SPEED_1MBIT,
    SOL_I2C_SPEED_3MBIT_400KBIT
};

struct sol_i2c *sol_i2c_open(uint8_t bus, enum sol_i2c_speed speed) SOL_ATTR_WARN_UNUSED_RESULT;

struct sol_i2c *sol_i2c_open_raw(uint8_t bus, enum sol_i2c_speed speed) SOL_ATTR_WARN_UNUSED_RESULT;

void sol_i2c_close(struct sol_i2c *i2c);

bool sol_i2c_write_quick(const struct sol_i2c *i2c, bool rw);

ssize_t sol_i2c_read(const struct sol_i2c *i2c, uint8_t *data, size_t count);
bool sol_i2c_write(const struct sol_i2c *i2c, uint8_t *data, size_t count);

/* Returns the number of read bytes */
ssize_t sol_i2c_read_register(const struct sol_i2c *i2c, uint8_t reg, uint8_t *data, size_t count);
bool sol_i2c_write_register(const struct sol_i2c *i2c, uint8_t reg, const uint8_t *data, size_t count);

bool sol_i2c_set_slave_address(struct sol_i2c *i2c, uint8_t slave_address);
uint8_t sol_i2c_get_slave_address(struct sol_i2c *i2c);
