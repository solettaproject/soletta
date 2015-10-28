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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for analog I/O access under Soletta.
 */

/**
 * @defgroup AIO AIO
 * @ingroup IO
 *
 * Analog I/O API for Soletta.
 *
 * @{
 */

struct sol_aio; /**< Structure of the AIO handler */

/**
 * Open the given board 'pin' by its label to be used as Analog I/O.
 *
 * This function only works when the board was successfully detect by Soletta and a corresponding
 * pin multiplexer module was found.
 *
 * This function also applies any Pin Multiplexer rules needed if a multiplexer for
 * the current board was previously loaded.
 *
 * @param label Label of the pin on the board.
 * @param precision The number of valid bits on the data received from the analog to digital converter.
 * @return A new AIO handler
 *
 * @see sol_aio_open_raw
 */
struct sol_aio *sol_aio_open_by_label(const char *label, const unsigned int precision);

/**
 * Open the given Analog I/O 'pin' on 'device' to be used.
 *
 * This function also applies any Pin Multiplexer rules needed if a multiplexer for
 * the current board was previously loaded.
 *
 * @param device The AIO device number.
 * @param pin The AIO pin on device.
 * @param precision The number of valid bits on the data received from the analog to digital converter.
 * @return A new AIO handler
 *
 * @see sol_aio_open_raw
 */
struct sol_aio *sol_aio_open(const int device, const int pin, const unsigned int precision);

/**
 * Open the given Analog I/O 'pin' on 'device' to be used.
 *
 * 'precision' is used to filter the valid bits from the data received from hardware
 * (which is manufacturer dependent), therefore should not be used as a way to change
 * the output range because is applied to the least significant bits.
 *
 * @param device The AIO device number
 * @param pin The AIO pin on device
 * @param precision The number of valid bits on the data received from the analog to digital converter.
 * @return A new AIO handler
 */
struct sol_aio *sol_aio_open_raw(const int device, const int pin, const unsigned int precision);

/**
 * Close the given AIO handler.
 *
 * @param aio AIO handler to be closed.
 */
void sol_aio_close(struct sol_aio *aio);

/**
 * Read the value of AIO 'pin' on 'device'.
 *
 * @param aio A valid AIO handler for the desired 'device'/'pin' pair.
 * @return The value read. -1 in case of error.
 */
int32_t sol_aio_get_value(const struct sol_aio *aio);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
