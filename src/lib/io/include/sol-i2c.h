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

#include <sol-macros.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for I2C access under Solleta.
 */

/**
 * @ingroup IO
 *
 * @{
 */

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
 * @param bus The I2C bus number to open
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
 * @param bus The I2C bus number to open
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
 * Set a (slave) device address on a I2C bus to deliver commands
 * to.
 *
 * All other I2C functions, after this call, will act on the given
 * @a slave_address device address. Since other I2C calls might happen
 * in between your own ones, though, it's highly advisable that you
 * issue this call before using any of the I2C read/write functions.
 *
 * @param i2c bus The I2C bus handle
 * @param slave_address The slave device address to deliver commands to
 *
 * @return @c true on success, @c false otherwise.
 *
 */
bool sol_i2c_set_slave_address(struct sol_i2c *i2c, uint8_t slave_address);

/**
 * Get the (slave) device address set on an I2C bus (to deliver I2C
 * commands)
 *
 * @param i2c bus The I2C bus handle
 * @return The slave device address set on @a bus. @c 0x0 means @a bus
 *         was not set to any device yet
 */
uint8_t sol_i2c_get_slave_address(struct sol_i2c *i2c);

/**
 * Perform a I2C write quick operation
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
 * Perform successive I2C byte read operations, with no specified
 * register
 *
 * This makes @a count read byte I2C operations on the device @a bus
 * is set to operate on, at no specific register. Some devices are so
 * simple that this interface is enough. For others, it is a shorthand
 * if you want to read the same register as in the previous I2C
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
 * Perform successive I2C byte write operations, with no specified
 * register
 *
 * This makes @a count write byte I2C operations on the device @a
 * bus is set to operate on, at no specific register. Some devices are
 * so simple that this interface is enough. For others, it is a
 * shorthand if you want to write the same register as in the previous
 * I2C command.
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
 * Perform a I2C read operation on a given device register
 *
 * @param i2c bus The I2C bus handle
 * @param reg The I2C register for the read operation
 * @param data The output buffer for the read operation
 * @param count The bytes count for the read operation
 *
 * @return The number of bytes read, on success, or a negative value,
 *         on errors.
 */
ssize_t sol_i2c_read_register(const struct sol_i2c *i2c, uint8_t reg, uint8_t *data, size_t count);

/**
 * Perform a I2C write operation on a given device register
 *
 * @param i2c bus The I2C bus handle
 * @param reg The I2C register for the write operation
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
 * operations, until the total data length the user requested is read.
 * If the device has the auto-increment feature,
 * sol_i2c_read_register() might be a better call than this function.
 *
 * This will issue multiple I2C read/write transactions with the
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
 * @return @c true on success, @c false otherwise
 */
bool sol_i2c_read_register_multiple(const struct sol_i2c *i2c, uint8_t reg, uint8_t *values, uint8_t len, uint8_t times);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
