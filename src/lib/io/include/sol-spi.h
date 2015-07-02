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

struct sol_spi;

int32_t sol_spi_get_transfer_mode(const struct sol_spi *spi);
bool sol_spi_set_transfer_mode(struct sol_spi *spi, uint32_t mode);

int8_t sol_spi_get_bit_justification(const struct sol_spi *spi);
bool sol_spi_set_bit_justification(struct sol_spi *spi, uint8_t justification);

int8_t sol_spi_get_bits_per_word(const struct sol_spi *spi);
bool sol_spi_set_bits_per_word(struct sol_spi *spi, uint8_t bits_per_word);

int32_t sol_spi_get_max_speed(const struct sol_spi *spi);
bool sol_spi_set_max_speed(struct sol_spi *spi, uint32_t speed);

bool sol_spi_transfer(const struct sol_spi *spi, uint8_t *tx, uint8_t *rx, size_t count);
bool sol_spi_raw_transfer(const struct sol_spi *spi, void *tr, size_t count);

void sol_spi_close(struct sol_spi *spi);
struct sol_spi *sol_spi_open(unsigned int bus, unsigned int chip_select);

#ifdef __cplusplus
}
#endif
