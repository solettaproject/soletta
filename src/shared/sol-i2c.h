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

/**
 * Open an I2C bus.
 *
 * @param type bus The I2C bus number to open
 * @param speed The speed to open I2C bus @a bus at
 * @return A new I2C bus handle
 *
 * @note This call won't attempt to make any pin muxing operations
 * underneath. Use sol_i2c_open() for that.
 *
 */
struct sol_i2c *sol_i2c_open_raw(uint8_t bus, enum sol_i2c_speed speed) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * Open an I2C bus.
 *
 * @param type bus The I2C bus number to open
 * @param speed The speed to open I2C bus @a bus at
 * @return A new I2C bus handle
 *
 * @note This call will attempt to make pin muxing operations
 * underneath, for the given platform that the code is running in. Use
 * sol_i2c_open_raw() if you want to skip any pin mux operation.
 *
 */
struct sol_i2c *sol_i2c_open(uint8_t bus, enum sol_i2c_speed speed) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * Close an I2C bus.
 *
 * @param i2c bus The I2C bus handle to close
 *
 */
void sol_i2c_close(struct sol_i2c *i2c);

/**
 * Set a (slave) device address on a I2C bus to deliver SMBus commands
 * to.
 *
 * All other SMBus functions, after this call, will act on the given
 * @a slave_address device address. Since other I2C calls might happen
 * in between your own ones, though, it's highly advisable that you
 * issue this call before using any of the SMBus read/write functions.
 *
 * @param i2c bus The I2C bus handle
 * @param slave_address The slave device address to deliver commands to
 *
 * @return @c true on success, @c false otherwise.
 *
 */
bool sol_i2c_set_slave_address(struct sol_i2c *i2c, uint8_t slave_address);

/**
 * Get the (slave) device address set on an I2C bus (to deliver SMBus
 * commands)
 *
 * @param i2c bus The I2C bus handle
 * @return The slave device address set on @a bus. @c 0x0 means @a bus
 *         was not set to any device yet
 */
uint8_t sol_i2c_get_slave_address(struct sol_i2c *i2c);

/**
 * Perform a SMBus write quick operation
 *
 * This sends a single bit to a device (command designed to turn on
 * and off simple devices)
 *
 * @param i2c bus The I2C bus handle
 * @param rw The value to write
 *
 * @return @c true on succes, @c false otherwise
 */
bool sol_i2c_write_quick(const struct sol_i2c *i2c, bool rw);

/**
 * Perform successive SMBus byte read operations, with no specified
 * register
 *
 * This makes @a count read byte SMBus operations on the device @a bus
 * is set to operate on, at no specific register. Some devices are so
 * simple that this interface is enough. For others, it is a shorthand
 * if you want to read the same register as in the previous SMBus
 * command.
 *
 * @param i2c bus The I2C bus handle
 * @param data The output buffer for the read operation
 * @param count The bytes count for the read operation
 *
 * @return The number of bytes read, on success, or a negative value,
 *         on errors.
 */
ssize_t sol_i2c_read(const struct sol_i2c *i2c, uint8_t *data, size_t count);

/**
 * Perform successive SMBus byte write operations, with no specified
 * register
 *
 * This makes @a count write byte SMBus operations on the device @a
 * bus is set to operate on, at no specific register. Some devices are
 * so simple that this interface is enough. For others, it is a
 * shorthand if you want to write the same register as in the previous
 * SMBus command.
 *
 * @param i2c bus The I2C bus handle
 * @param data The output buffer for the write operation
 * @param count The bytes count for the write operation
 *
 * @return The number of bytes write, on success, or a negative value,
 *         on errors.
 */
bool sol_i2c_write(const struct sol_i2c *i2c, uint8_t *data, size_t count);

/**
 * Perform a SMBus (byte/word/block) read operation on a given device
 * register
 *
 * This reads a block of up to 32 bytes from a device, at the
 * specified register @a reg. Depending on @a count, the underlying
 * bus message might be SMBbus read byte (@a count is 1), SMBbus read
 * word (@a count is 2) or SMBbus read block (@a count is greater than
 * 2 and less than 33). If @count is 33 or greater, this will issue a
 * plain-I2C read/write transaction, with the first (write) message
 * specifying the register to operate on and the second (read) message
 * specifying the length and the destination of the read operation.
 *
 * @param i2c bus The I2C bus handle
 * @param data The output buffer for the read operation
 * @param count The bytes count for the read operation
 *
 * @note For the case of reading more the 32 of bytes from a register,
 * this call is useful for auto-increment capable devices. If the
 * device in question does not have that feature, one must issue
 * sol_i2c_read_register_multiple() instead.
 *
 * @warn This function will fail if @count is bigger than 32 and the
 *       target I2C device does not accept plain-I2C messages
 *
 * @return The number of bytes read, on success, or a negative value,
 *         on errors.
 */
ssize_t sol_i2c_read_register(const struct sol_i2c *i2c, uint8_t reg, uint8_t *data, size_t count);

/**
 * Perform a SMBus (byte/word/block) write operation on a given device
 * register
 *
 * This writes a block of up to 32 bytes from a device, at the
 * specified register @a reg. Depending on @a count, the underlying
 * SMBus call might be write byte (@a count is 1), write word (@a
 * count is 2) or write block (@a count is greater than 2 and less
 * than 33). If @count is 33 or greater, this will issue a plain-I2C
 * write transaction.
 *
 * @param i2c bus The I2C bus handle
 * @param data The output buffer for the write operation
 * @param count The bytes count for the write operation
 *
 * @return The number of bytes write, on success, or a negative value,
 *         on errors.
 */
bool sol_i2c_write_register(const struct sol_i2c *i2c, uint8_t reg, const uint8_t *data, size_t count);

/**
 * Read an arbitrary number of bytes from a register in repeated
 * bursts of a given length (that start always on the provided
 * register address)
 *
 * This is so because a lot of devices will, after a read operation,
 * update its register values with new data to be read on subsequent
 * operations, until the total data lenght the user requested is read.
 * If the device has the auto-increment feature,
 * sol_i2c_read_register() might be a better call than this function.
 *
 * This will issue multiple plain-I2C read/write transaction with the
 * first (write) message specifying the register to operate on and the
 * second (read) message specifying the length (always @a len per
 * read) and the destination of the read operation.
 *
 * @param i2c bus The I2C bus handle
 * @param reg The register to start reading from
 * @param values Where to store the read bytes
 * @param len The size of a single read block
 * @param times How many reads of size @a len to perform (on success,
 *              @a len * @a times bytes will be read)
 *
 * @warn This function will fail if the target I2C device does not
 *       accept plain-I2C messages
 *
 * @return @c true on succes, @c false otherwise
 */
bool sol_i2c_read_register_multiple(const struct sol_i2c *i2c, uint8_t reg, uint8_t *values, uint8_t len, uint8_t times);
