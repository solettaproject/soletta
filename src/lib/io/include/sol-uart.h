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

#include <sol-types.h>
#include <sol-buffer.h>
#include <sol-common-buildopts.h>
#include <sol-macros.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for UART access under Soletta.
 */

/**
 * @defgroup UART UART
 * @ingroup IO
 *
 * @brief UART (Universal Asynchronous Receiver/Transmitter) API for Soletta.
 *
 * @{
 */

/**
 * @typedef sol_uart
 * @brief A handle to a UART device.
 * @see sol_uart_open()
 * @see sol_uart_close()
 * @see sol_uart_feed()
 */
struct sol_uart;
typedef struct sol_uart sol_uart;

/**
 * @brief Baud rate is the number of times the signal can switch states in one second.
 *
 * Need to be defined to set uart config.
 */
enum sol_uart_baud_rate {
    SOL_UART_BAUD_RATE_9600 = 0,
    SOL_UART_BAUD_RATE_19200,
    SOL_UART_BAUD_RATE_38400,
    SOL_UART_BAUD_RATE_57600,
    SOL_UART_BAUD_RATE_115200
};

/**
 * @brief Amount of data bits.
 */
enum sol_uart_data_bits {
    SOL_UART_DATA_BITS_8 = 0, /**< Use 8 data bits */
    SOL_UART_DATA_BITS_7, /**< Use 7 data bits */
    SOL_UART_DATA_BITS_6, /**< Use 6 data bits */
    SOL_UART_DATA_BITS_5 /**< Use 5 data bits */
};

/**
 * @brief The parity characteristic can be even, odd, or none and it
 * influences last trasmitted bit.
 */
enum sol_uart_parity {
    SOL_UART_PARITY_NONE = 0, /**< no parity is used. */
    SOL_UART_PARITY_EVEN, /**< the last data bit transmitted will be a logical 1  if the data transmitted had an even amount of 0 bits. */
    SOL_UART_PARITY_ODD /**< the last data bit transmitted will be a logical 1 if the data transmitted had an odd amount of 0 bits. */
};

/**
 * @brief Amount of stop bits.
 */
enum sol_uart_stop_bits {
    SOL_UART_STOP_BITS_ONE = 0, /**< Use one stop bit*/
    SOL_UART_STOP_BITS_TWO /**< Use two stop bits */
};

/**
 * @brief A configuration struct used to set the UART paramenters.
 *
 * @see sol_uart_open()
 * @note UART follows the Soletta stream design pattern, which can be found here: @ref streams
 */
typedef struct sol_uart_config {
#ifndef SOL_NO_API_VERSION
#define SOL_UART_CONFIG_API_VERSION (1) /**< compile time API version to be checked during runtime */
    uint16_t api_version; /**< must match #SOL_UART_CONFIG_API_VERSION at runtime */
#endif
    /**
     * @brief Callback containing data was read from UART
     *
     * @param data User data as provided in sol_uart_config::user_data
     * @param uart The UART handle
     * @param buf The UART data
     *
     * @return The number of bytes read from @c buf or negative errno on error.
     */
    ssize_t (*on_data)(void *data, struct sol_uart *uart, const struct sol_buffer *buf);
    /**
     * @brief Informs that a feed operation has ended.
     *
     * @param data User data as provided in sol_uart_config::user_data
     * @param uart The UART handle
     * @param blob The blob that was written
     * @param status 0 on success or negative errno on error
     * @note There is no need to call sol_blob_unref().
     * @see sol_uart_feed()
     */
    void (*on_feed_done)(void *data, struct sol_uart *uart, struct sol_blob *blob, int status);
    const void *user_data; /**< User data to sol_uart_config::on_feed_done() and  sol_uart_config::on_data() */
    /**
     * @brief The feed buffer max size. The value @c 0 means unlimited data.
     * Since sol_uart_feed() works with blobs, no extra buffers will be allocated in order
     * to store @c feed_size bytes. All the blobs that are schedule to be written will be referenced
     * and the sum of all queued blobs must not be equal or exceed @c feed_size.
     * If it happens sol_uart_feed() will return @c -ENOSPC and one must start to control the
     * writing flow until @c on_feed_done is called.
     * @see sol_uart_feed()
     */
    size_t feed_size;
    /**
     * @brief The receiving buffer max size. The value @c 0 means unlimited data. In other words, the buffer
     * will always grow in order to store all the data and shrink when data is consumed.
     */
    size_t data_buffer_size;
    enum sol_uart_baud_rate baud_rate; /**< The baud rate value */
    enum sol_uart_data_bits data_bits; /**< The data bits value */
    enum sol_uart_parity parity; /**< The parity value*/
    enum sol_uart_stop_bits stop_bits; /**< The stop bits value */
    bool flow_control; /**< Enables software flow control(XOFF and XON) */
} sol_uart_config;

/**
 * @brief Converts a string UART baudRate to sol_uart_baud_rate
 *
 * This function converts a string UART baudRate to enumeration sol_uart_baud_rate.
 *
 * @see sol_uart_baud_rate_to_str().
 *
 * @param baud_rate Valid values are "baud-9600", "baud-19200", "baud-38400", "baud-57600", "baud-115200".
 *
 * @return enumeration sol_uart_baud_rate
 */
enum sol_uart_baud_rate sol_uart_baud_rate_from_str(const char *baud_rate)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts sol_uart_baud_rate to a string name.
 *
 * This function converts sol_uart_baud_rate enumeration to a string UART baudRate.
 *
 * @see sol_uart_baud_rate_from_str().
 *
 * @param baud_rate sol_uart_baud_rate
 *
 * @return String representation of the sol_uart_baud_rate
 */
const char *sol_uart_baud_rate_to_str(enum sol_uart_baud_rate baud_rate)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts a string UART dataBits to sol_uart_data_bits
 *
 * This function converts a string UART dataBits to enumeration sol_uart_data_bits.
 *
 * @see sol_uart_data_bits_to_str().
 *
 * @param data_bits Valid values are "databits-5", "databits-6", "databits-7", "databits-8".
 *
 * @return enumeration sol_uart_data_bits
 */
enum sol_uart_data_bits sol_uart_data_bits_from_str(const char *data_bits)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts sol_uart_data_bits to a string name.
 *
 * This function converts sol_uart_data_bits enumeration to a string UART dataBits.
 *
 * @see sol_uart_data_bits_from_str().
 *
 * @param data_bits sol_uart_data_bits
 *
 * @return String representation of the sol_uart_data_bits
 */
const char *sol_uart_data_bits_to_str(enum sol_uart_data_bits data_bits)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts a string UART parity to sol_uart_parity
 *
 * This function converts a string UART parity to enumeration sol_uart_parity.
 *
 * @see sol_uart_parity_to_str().
 *
 * @param parity Valid values are "none", "even", "odd".
 *
 * @return enumeration sol_uart_parity
 */
enum sol_uart_parity sol_uart_parity_from_str(const char *parity)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts sol_uart_parity to a string name.
 *
 * This function converts sol_uart_parity enumeration to a string UART parity.
 *
 * @see sol_uart_parity_from_str().
 *
 * @param parity sol_uart_parity
 *
 * @return String representation of the sol_uart_parity
 */
const char *sol_uart_parity_to_str(enum sol_uart_parity parity)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts a string UART stopBits to sol_uart_stop_bits
 *
 * This function converts a string UART stopBits to enumeration sol_uart_stop_bits.
 *
 * @see sol_uart_stop_bits_to_str().
 *
 * @param stop_bits Valid values are "stopbits-1", "stopbits-2".
 *
 * @return enumeration sol_uart_stop_bits
 */
enum sol_uart_stop_bits sol_uart_stop_bits_from_str(const char *stop_bits)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts sol_uart_stop_bits to a string name.
 *
 * This function converts sol_uart_stop_bits enumeration to a string UART stopBits.
 *
 * @see sol_uart_stop_bits_from_str().
 *
 * @param stop_bits sol_uart_stop_bits
 *
 * @return String representation of the sol_uart_stop_bits
 */
const char *sol_uart_stop_bits_to_str(enum sol_uart_stop_bits stop_bits)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Open an UART bus.
 *
 * @param port_name the name of UART port, on Linux it should be ttyUSB0 or
 * ttyACM0 in small OSes it should be a id number.
 * @param config The UART bus configuration
 * @return A new UART bus handle
 *
 * @note For now it only supports one user of each port at time, 2 or more users
 * on the same port will cause several concurrency errors.
 */
struct sol_uart *sol_uart_open(const char *port_name, const struct sol_uart_config *config);

/**
 * @brief Close an UART bus.
 *
 * It's important to note that after this functions is called pending blobs blobs will not be written
 * and the sol_uart_config::on_feed_done() will be called with @c status set to @c -ECANCELED.
 * The callback sol_uart_config::on_data() will also be called if the @c rx buffer has data in it.
 *
 * @param uart The UART bus handle
 */
void sol_uart_close(struct sol_uart *uart);

/**
 * @brief Perform an UART asynchronous transmission.
 *
 * This function will queue a feed operation on the UART bus. It calls
 * sol_blob_ref(), thus it's safe to call sol_blob_unref() right after this function
 * returns. After a blob is completely written the callback sol_uart_config::on_feed_done() is called, if provided.
 * On errors callback sol_uart_config::on_feed_done() is called with negative status.
 *
 * @param uart The UART bus handle
 * @param blob The blob to be written
 * @return 0 on success, @c -ENOSPC if sol_uart_config::feed_size is not zero and there's no more space left or negative errno on error
 * @see sol_uart_config::on_feed_done()
 * @note UART follows the Soletta stream design pattern, which can be found here: @ref streams
 *
 */
int sol_uart_feed(struct sol_uart *uart, struct sol_blob *blob);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
