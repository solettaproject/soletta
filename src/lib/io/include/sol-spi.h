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

#include <stdint.h>
#include <stdbool.h>

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
 * SPI (Serial Peripheral Interface) API for Soletta.
 *
 * @{
 */

struct sol_spi;

enum sol_spi_mode {
    SOL_SPI_MODE_0 = 0, /** CPOL = 0 and CPHA = 0 */
    SOL_SPI_MODE_1, /** CPOL = 0 and CPHA = 1 */
    SOL_SPI_MODE_2, /** CPOL = 1 and CPHA = 0 */
    SOL_SPI_MODE_3 /** CPOL = 1 and CPHA = 1 */
};

#define SOL_SPI_DATA_BITS_DEFAULT 8

struct sol_spi_config {
#define SOL_SPI_CONFIG_API_VERSION (1)
    uint16_t api_version;
    unsigned int chip_select; /** Also know as slave select */
    enum sol_spi_mode mode;
    uint32_t frequency; /** Clock frequency in Hz */
    uint8_t bits_per_word;
};

/**
 * Perform a SPI asynchronous transfer.
 *
 * @param spi The SPI bus handle
 * @param tx The output buffer
 * @param rx The input buffer
 * @param count number of bytes to be transfer
 * @param transfer_cb callback to be called when transmission finish,
 * in case of success the status parameters on callback should be the same
 * value as count, otherwise a error happen.
 * @param cb_data user data, first parameter of transfer_cb
 * @return true if transfer was started.
 *
 * As SPI works in full-duplex, data are going in and out
 * at same time, so both buffers must have the count size.
 * Caller should guarantee that both buffers will not be
 * freed until callback is called.
 * Also there is no transfer queue, calling this function when there is
 * transfer happening would return false.
 */
bool sol_spi_transfer(struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, size_t count, void (*transfer_cb)(void *cb_data, struct sol_spi *spi, const uint8_t *tx, uint8_t *rx, ssize_t status), const void *cb_data);

/**
 * Close an SPI bus.
 *
 * @param spi The SPI bus handle
 */
void sol_spi_close(struct sol_spi *spi);

/**
 * Open an SPI bus.
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
