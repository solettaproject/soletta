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

#include <stdint.h>
#include <stdbool.h>

#include <sol-common-buildopts.h>
#include <sol-macros.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for SPI access under Soletta.
 */

/**
 * @defgroup SPI SPI
 * @ingroup IO
 *
 * @brief SPI (Serial Peripheral Interface) API for Soletta.
 *
 * @{
 */

/**
 * @typedef sol_spi
 * @brief A handle to a SPI bus
 * @see sol_spi_open()
 * @see sol_spi_close()
 * @see sol_spi_transfer()
 */
struct sol_spi;
typedef struct sol_spi sol_spi;

/**
 * @brief SPI Transfer Modes.
 *
 * It may enable and disable clock polarity (cpol)
 * and clock phase (cpha) to define a clock format to be used by the SPI
 * bus.
 *
 * Depending on CPOL parameter, SPI clock may be inverted or non-inverted.
 *
 * CPHA parameter is used to shift the sampling phase. If CPHA=0 the data
 * are sampled on the leading (first) clock edge.
 *
 * If CPHA=1 the data are sampled on the trailing (second) clock edge,
 * regardless of whether that clock edge is rising or falling.
 */
enum sol_spi_mode {
    SOL_SPI_MODE_0 = 0, /** CPOL = 0 and CPHA = 0 */
    SOL_SPI_MODE_1, /** CPOL = 0 and CPHA = 1 */
    SOL_SPI_MODE_2, /** CPOL = 1 and CPHA = 0 */
    SOL_SPI_MODE_3 /** CPOL = 1 and CPHA = 1 */
};

/**
 * @brief Default value for bits per word when using SPI
 */
#define SOL_SPI_DATA_BITS_DEFAULT 8

/**
 * @brief SPI configuration struct
 *
 * This struct is used to configure an SPI bus during its creation.
 *
 * @see sol_spi_open()
 */
typedef struct sol_spi_config {
#ifndef SOL_NO_API_VERSION
#define SOL_SPI_CONFIG_API_VERSION (1)
    uint16_t api_version; /**< The API version. */
#endif
    unsigned int chip_select; /**< Also know as slave select */
    enum sol_spi_mode mode; /**< The SPI operation mode */
    uint32_t frequency; /**< Clock frequency in Hz */
    uint8_t bits_per_word; /**< Number of bits per word */
} sol_spi_config;

/**
 * @brief Converts a string SPI mode name to sol_spi_mode
 *
 * This function converts a string SPI mode name to enumeration sol_spi_mode.
 *
 * @see sol_spi_mode_to_str().
 *
 * @param spi_mode Valid values are "mode0", "mode1", "mode2", "mode3".
 *
 * @return enumeration sol_spi_mode
 */
enum sol_spi_mode sol_spi_mode_from_str(const char *spi_mode)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts sol_spi_mode to a string name.
 *
 * This function converts sol_spi_mode enumeration to a string SPI mode name.
 *
 * @see sol_spi_mode_from_str().
 *
 * @param spi_mode sol_spi_mode
 *
 * @return String representation of the sol_spi_mode
 */
const char *sol_spi_mode_to_str(enum sol_spi_mode spi_mode)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Perform a SPI asynchronous transfer.
 *
 * @param spi The SPI bus handle
 * @param tx The output buffer
 * @param rx The input buffer
 * @param count number of bytes to be transfer
 * @param transfer_cb callback to be called when transmission finish,
 * in case of success the status parameters on callback should be the same
 * value as count, otherwise a error happen.
 * @param cb_data user data, first parameter of transfer_cb
 * @return 0 if the transfer started, -EBUSY if the bus is busy or -errno on error.
 *
 * As SPI works in full-duplex, data are going in and out
 * at same time, so both buffers must have the same count size.
 * Caller should guarantee that both buffers will not be
 * freed until callback is called.
 * Also there is no transfer queue, calling this function when there is
 * transfer happening would return false.
 */
int sol_spi_transfer(struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, size_t count, void (*transfer_cb)(void *cb_data, struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, ssize_t status), const void *cb_data);

/**
 * @brief Close an SPI bus.
 *
 * @param spi The SPI bus handle
 */
void sol_spi_close(struct sol_spi *spi);

/**
 * @brief Open an SPI bus.
 *
 * @param bus The SPI bus number to open
 * @param config The SPI bus configuration
 * @return A new SPI bus handle
 *
 * @note For now it only supports one user of the bus at time, 2 or mode devices
 * with different chip select on the same bus will cause concurrency errors.
 */
struct sol_spi *sol_spi_open(unsigned int bus, const struct sol_spi_config *config);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
