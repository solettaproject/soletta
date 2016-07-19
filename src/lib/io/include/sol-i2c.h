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

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include <sol-macros.h>
#include <sol-buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for I2C access under Soletta.
 */

/**
 * @defgroup I2C I2C
 * @ingroup IO
 *
 * @brief I²C (Inter-Integrated Circuit) API for Soletta.
 *
 * @{
 */

/**
 * @typedef sol_i2c
 * @brief I2C handle structure
 * @see sol_i2c_open()
 * @see sol_i2c_open_raw()
 * @see sol_i2c_close()
 * @see sol_i2c_set_slave_address()
 * @see sol_i2c_get_slave_address()
 * @see sol_i2c_get_bus()
 * @see sol_i2c_write_quick()
 * @see sol_i2c_read()
 * @see sol_i2c_write()
 * @see sol_i2c_read_register()
 * @see sol_i2c_write_register()
 * @see sol_i2c_read_register_multiple()
 * @see sol_i2c_pending_cancel()
 */
struct sol_i2c;
typedef struct sol_i2c sol_i2c;


/**
 * @typedef sol_i2c_pending
 * @brief I2C pending operation handle structure
 * @see sol_i2c_write_quick()
 * @see sol_i2c_read()
 * @see sol_i2c_write()
 * @see sol_i2c_read_register()
 * @see sol_i2c_write_register()
 * @see sol_i2c_read_register_multiple()
 * @see sol_i2c_pending_cancel()
 */
struct sol_i2c_pending;
typedef struct sol_i2c_pending sol_i2c_pending;

/**
 * @brief Enum for I2C bus speed.
 *
 * Must be chosen when opening a bus with sol_i2c_open() and
 * sol_i2c_open_raw().
 */
enum sol_i2c_speed {
    SOL_I2C_SPEED_10KBIT = 0, /**< flag for low speed */
    SOL_I2C_SPEED_100KBIT, /**< flag for normal speed */
    SOL_I2C_SPEED_400KBIT, /**< flag for fast speed */
    SOL_I2C_SPEED_1MBIT, /**< flag for fast plus speed */
    SOL_I2C_SPEED_3MBIT_400KBIT /**< flag for high speed */
};

/**
 * @brief Converts a string I2C speed to sol_i2c_speed
 *
 * This function converts a string I2C speed to enumeration sol_i2c_speed.
 *
 * @see sol_i2c_speed_to_str().
 *
 * @param speed Valid values are "10kbps", "100kbps", "400kbps", "1000kbps", "3400kbps".
 *
 * @return enumeration sol_i2c_speed.
 */
enum sol_i2c_speed sol_i2c_speed_from_str(const char *speed)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts sol_i2c_speed to a string name.
 *
 * This function converts sol_i2c_speed enumeration to a string I2C speed name.
 *
 * @see sol_i2c_speed_from_str().
 *
 * @param speed sol_i2c_speed.
 *
 * @return String representation of the sol_i2c_speed.
 */
const char *sol_i2c_speed_to_str(enum sol_i2c_speed speed)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Open an I2C bus.
 *
 * @param bus The I2C bus number to open
 * @param speed The speed to open I2C bus @a bus at
 * @return A new I2C bus handle
 *
 * @note This call won't attempt to make any pin muxing operations
 * underneath. Use sol_i2c_open() for that. Also, this will never
 * cache this I2C handle (or return any previously cached I2C handle).
 *
 */
struct sol_i2c *sol_i2c_open_raw(uint8_t bus, enum sol_i2c_speed speed)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Open an I2C bus.
 *
 * @param bus The I2C bus number to open
 * @param speed The speed to open I2C bus @a bus at
 * @return A new I2C bus handle
 *
 * @note This call will attempt to make pin muxing operations
 * underneath, for the given platform that the code is running in. Use
 * sol_i2c_open_raw() if you want to skip any pin mux operation.
 *
 * @note The same I2C bus is shared between every user, so only the
 * first one opening a bus will be able to set its speed.
 */
struct sol_i2c *sol_i2c_open(uint8_t bus, enum sol_i2c_speed speed)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Close an I2C bus.
 *
 * @param i2c The I2C bus handle to close
 *
 */
void sol_i2c_close(struct sol_i2c *i2c);

/**
 * @brief Set a (slave) device address on a I2C bus to deliver commands
 * to.
 *
 * All other I2C functions, after this call, will act on the given
 * @a slave_address device address. Since other I2C calls might happen
 * in between your own ones, though, it's highly advisable that you
 * issue this call before using any of the I2C read/write functions.
 *
 * @param i2c The I2C bus handle
 * @param slave_address The slave device address to deliver commands to
 *
 * @return @c 0 on success, @c -EBUSY if the device is busy or -errno on error.
 *
 */
int sol_i2c_set_slave_address(struct sol_i2c *i2c, uint8_t slave_address);

/**
 * @brief Get the (slave) device address set on an I2C bus (to deliver I2C
 * commands to)
 *
 * @param i2c The I2C bus handle
 * @return The slave device address set on @a bus. @c 0x0 means @a bus
 *         was not set to any device yet
 */
uint8_t sol_i2c_get_slave_address(struct sol_i2c *i2c);

/**
 * @brief Perform a I2C write quick operation
 *
 * This sends a single bit to a device (command designed to turn on
 * and off simple devices)
 *
 * @param i2c The I2C bus handle
 * @param rw The value to write
 * @param write_quick_cb The callback to be issued when the operation
 * finishes. The status parameter should be equal to one in case of
 * success (or a negative error code, on failure)
 * @param cb_data Data to be passed to @a write_quick_cb
 *
 * @return pending An I2C pending operation handle on success,
 * otherwise @c NULL. It's only valid before @a write_quick_cb is
 * called. It may be used before that to cancel the read operation.
 * If @c NULL is returned, the errno variable will be set with the correct error value.
 * In case that the I2C device is in use, the errno variable is set to EBUSY.
 */
struct sol_i2c_pending *sol_i2c_write_quick(struct sol_i2c *i2c, bool rw, void (*write_quick_cb)(void *cb_data, struct sol_i2c *i2c, ssize_t status), const void *cb_data);

/**
 * @brief Perform successive asynchronous I2C byte read operations,
 * with no specified register
 *
 * This makes @a count read byte I2C operations on the device @a bus
 * is set to operate on, at no specific register. Some devices are so
 * simple that this interface is enough. For others, it is a shorthand
 * if you want to read the same register as in the previous I2C
 * command.
 *
 * @param i2c The I2C bus handle
 * @param data The output buffer for the read operation
 * @param count The bytes count for the read operation
 * @param read_cb The callback to be called when operation finish, the
 * status parameter should be equal to count in case of success (or a
 * negative error code, on failure)
 * @param cb_data The first parameter of callback
 *
 * @note The caller should guarantee that data will not be freed until the
 * callback is called.
 * Also there is no transfer queue, calling this function when there is
 * another I2C operation running will return false.
 *
 * @return pending An I2C pending operation handle on success,
 * otherwise @c NULL. It's only valid before @a read_cb is called. It
 * may be used before that to cancel the read operation.
 * If @c NULL is returned, the errno variable will be set with the correct error value.
 * In case that the I2C device is in use, the errno variable is set to EBUSY.
 */
struct sol_i2c_pending *sol_i2c_read(struct sol_i2c *i2c, uint8_t *data, size_t count, void (*read_cb)(void *cb_data, struct sol_i2c *i2c, uint8_t *data, ssize_t status), const void *cb_data);

/**
 * @brief Perform successive asynchronous I2C byte write operations,
 * with no specified register
 *
 * This makes @a count write byte I2C operations on the device @a
 * bus is set to operate on, at no specific register. Some devices are
 * so simple that this interface is enough. For others, it is a
 * shorthand if you want to write the same register as in the previous
 * I2C command.
 *
 * @param i2c The I2C bus handle
 * @param data The output buffer for the write operation
 * @param count The bytes count for the write operation
 * @param write_cb The callback to be called when operation finish,
 * the status parameter should be equal to count in case of success
 * (or a negative error code, on failure)
 * @param cb_data The first parameter of callback
 *
 * @note The caller should guarantee that data will not be freed until
 * the callback is called. Also there is no transfer queue, calling
 * this function when there is another I2C operation running will
 * return false.
 *
 * @return pending An I2C pending operation handle on success,
 * otherwise @c NULL. It's only valid before @a write_cb is called. It
 * may be used before that to cancel the read operation.
 * If @c NULL is returned, the errno variable will be set with the correct error value.
 * In case that the I2C device is in use, the errno variable is set to EBUSY.
 */
struct sol_i2c_pending *sol_i2c_write(struct sol_i2c *i2c, uint8_t *data, size_t count, void (*write_cb)(void *cb_data, struct sol_i2c *i2c, uint8_t *data, ssize_t status), const void *cb_data);

/**
 * @brief Perform an asynchronous I2C read operation on a given device
 * register
 *
 * @param i2c The I2C bus handle
 * @param reg The I2C register for the read operation
 * @param data The output buffer for the read operation
 * @param count The bytes count for the read operation
 * @param read_reg_cb The callback to be called when operation finish,
 * the status parameter should be equal to count in case of success
 * (or a negative error code, on failure)
 * @param cb_data The first parameter of callback
 *
 * @note The caller should guarantee that data will not be freed until
 * the callback is called. Also there is no transfer queue, calling
 * this function when there is another I2C operation running will
 * return false.
 *
 * @return pending An I2C pending operation handle on success,
 * otherwise @c NULL. It's only valid before @a read_reg_cb is called.
 * It may be used before that to cancel the read operation.
 * If @c NULL is returned, the errno variable will be set with the correct error value.
 * In case that the I2C device is in use, the errno variable is set to EBUSY.
 */
struct sol_i2c_pending *sol_i2c_read_register(struct sol_i2c *i2c, uint8_t reg, uint8_t *data, size_t count, void (*read_reg_cb)(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status), const void *cb_data);

/**
 * @brief Perform an asynchronous I2C write operation on a given
 * device register
 *
 * @param i2c The I2C bus handle
 * @param reg The I2C register for the write operation
 * @param data The output buffer for the write operation
 * @param count The bytes count for the write operation
 * @param write_reg_cb The callback to be called when operation
 * finish, the status parameter should be equal to count in case of
 * success (or a negative error code, on failure)
 * @param cb_data The first parameter of callback
 *
 * @note The caller should guarantee that data will not be freed until
 * the callback is called. Also there is no transfer queue, calling
 * this function when there is another I2C operation running will
 * return false.
 *
 * @return pending An I2C pending operation handle on success,
 * otherwise @c NULL. It's only valid before @a write_reg_cb is
 * called. It may be used before that to cancel the read operation.
 * If @c NULL is returned, the errno variable will be set with the correct error value.
 * In case that the I2C device is in use, the errno variable is set to EBUSY.
 */
struct sol_i2c_pending *sol_i2c_write_register(struct sol_i2c *i2c, uint8_t reg, const uint8_t *data, size_t count, void (*write_reg_cb)(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status), const void *cb_data);

/**
 * @brief Asynchronous read of an arbitrary number of bytes from a register in
 * repeated bursts of a given length (that start always on the provided
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
 * @param i2c The I2C bus handle
 * @param reg The register to start reading from
 * @param values Where to store the read bytes
 * @param count The size of a single read block
 * @param times How many reads of size @a len to perform (on success,
 *              @a len * @a times bytes will be read)
 * @param read_reg_multiple_cb The callback to be called when the
 * operation finishes. The status parameter should be equal to count
 * in case of success (or a negative error code, on failure)
 * @param cb_data The first parameter of callback
 *
 * @note The caller should guarantee that data will not be freed until
 * the callback is called. Also there is no transfer queue, calling
 * this function when there is another I2C operation running will
 * return false.
 *
 * @return pending An I2C pending operation handle on success,
 * otherwise @c NULL. It's only valid before @a read_reg_multiple_cb
 * is called. It may be used before that to cancel the read operation.
 * If @c NULL is returned, the errno variable will be set with the correct error value.
 * In case that the I2C device is in use, the errno variable is set to EBUSY.
 */
struct sol_i2c_pending *sol_i2c_read_register_multiple(struct sol_i2c *i2c, uint8_t reg, uint8_t *values, size_t count, uint8_t times, void (*read_reg_multiple_cb)(void *cb_data, struct sol_i2c *i2c, uint8_t reg, uint8_t *data, ssize_t status), const void *cb_data);

/**
 * @brief Get the I2C bus ID
 *
 * @param i2c The I2C bus handle
 *
 * @return the bus id
 */
uint8_t sol_i2c_get_bus(const struct sol_i2c *i2c);

/**
 * @brief Cancel a pending operation.
 *
 * @param i2c the I2C bus handle
 * @param pending the operation handle
 */
void sol_i2c_pending_cancel(struct sol_i2c *i2c, struct sol_i2c_pending *pending);

#ifdef SOL_PLATFORM_LINUX

/**
 * @brief Create a new I2C device.
 *
 * Iterates through @a address on '/sys/devices/', looking for @c
 * 'i2c-X' directories and adding the contents "@a dev_name
 * @dev_number" to their 'new_device' file.
 *
 * @param address bus on '/sys/devices' where to add new I2C device.
 * @param dev_name name of device. Usually it's the one its driver expects.
 * @param dev_number number of device on the bus.
 * @param result_path resulting path of new device. It's a convenience
 * to retrieve new device's path. Note that the device directory may
 * take some time to appear on sysfs - it may be necessary to wait
 * some time before trying to access it.
 *
 * @return a positive value if everything was OK or a negative one if
 * some error happened. Watch out for -EEXIST return: it means that
 * device could not be created because it already exists.
 */
int sol_i2c_create_device(const char *address, const char *dev_name, unsigned int dev_number, struct sol_buffer *result_path);
#endif

/**
 * @}
 */

/**
 * @ingroup I2C
 *
 * @brief I²C operation dispatcher.
 *
 * @{
 */

/**
 * @brief These routines are used manipulate groups of I2C operations to slave devices under Soletta.
 */

/**
 * @brief Enum for the dispatcher operation type.
 *
 * If a given operation in the set to either read or write a data.
 */
enum sol_i2c_op_type {
    SOL_I2C_READ,
    SOL_I2C_WRITE
};

/**
 * @brief Structure to describe an operation that should be executed by the I2C Dispatcher.
 *
 * Each operation is intended to read/write a single byte on the slave device.
 */
struct sol_i2c_op {
    enum sol_i2c_op_type type; /**< @brief I2C Operation type. */
    uint8_t reg; /**< @brief I2C register in the slave device */
    uint8_t value; /**< @brief Operation data */
};

/**
 * @typedef sol_i2c_op_set_pending
 * @brief I2C Dispatcher pending operation set handle structure
 */
struct sol_i2c_op_set_pending;
typedef struct sol_i2c_op_set_pending sol_i2c_op_set_pending;

/**
 * @brief Add an operation set in the dispatcher's queue of a given I2C bus.
 *
 * It adds an operation set scheduling it for execution by the dispatcher handling i2c opeations
 * at bus @a i2c.
 *
 * @param i2c The i2c bus handle
 * @param addr The slave device address
 * @param set Operation set to be added
 * @param cb Callback to be called after the operation set is executed
 * @param cb_data Callback context data
 * @param delay Time, in milliseconds, to wait between two consecutive operations of this set
 */
struct sol_i2c_op_set_pending *sol_i2c_dispatcher_add_op_set(struct sol_i2c *i2c, uint8_t addr, struct sol_vector *set, void (*cb)(void *cb_data, ssize_t status), void *cb_data, uint32_t delay);

/**
 * @brief Cancel the execution of the pending operation set.
 *
 * @param i2c The I2C bus handle
 * @param pending The operation set pending handle
 */
void sol_i2c_dispatcher_remove_op_set(struct sol_i2c *i2c, struct sol_i2c_op_set_pending *pending);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
