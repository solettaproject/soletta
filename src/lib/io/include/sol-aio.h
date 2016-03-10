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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for analog I/O access (reading
 * from analog-to-digital converters) under Soletta.
 */

/**
 * @defgroup AIO AIO
 * @ingroup IO
 *
 * @brief Analog I/O API for Soletta.
 *
 * @{
 */

struct sol_aio; /**< @brief AIO handle structure */

struct sol_aio_pending; /**< @brief AIO pending operation handle structure */

/**
 * @brief Open the given board @c pin by its label to be used as Analog I/O.
 *
 * This function only works when the board was successfully detected by
 * Soletta and a corresponding pin multiplexer module was found.
 *
 * This function also applies any pin multiplexer rules needed if a
 * multiplexer for the current board was previously loaded.
 *
 * @param label Label of the pin on the board.
 * @param precision The number of valid bits on the data received from
 * the analog to digital converter. Some simpler operating systems
 * might have that hardcoded for analog-to-digital converters, in
 * which case this value will be ignored.
 * @return A new AIO handle
 *
 * @see sol_aio_open_raw
 */
struct sol_aio *sol_aio_open_by_label(const char *label, const unsigned int precision);

/**
 * @brief Open the given Analog I/O @c pin on @c device to be used.
 *
 * This function also applies any pin multiplexer rules needed if a
 * multiplexer for the current board was previously loaded.
 *
 * @param device The AIO device number.
 * @param pin The AIO pin on device.
 * @param precision The number of valid bits on the data received from
 * the analog to digital converter. Some simpler operating systems
 * might have that hardcoded for analog-to-digital converters, in
 * which case this value will be ignored.
 * @return A new AIO handle
 *
 * @see sol_aio_open_raw
 */
struct sol_aio *sol_aio_open(const int device, const int pin, const unsigned int precision);

/**
 * @brief Open the given Analog I/O @c pin on @c device to be used.
 *
 * @c precision is used to filter the valid bits from the data
 * received from hardware (which is manufacturer-dependent), therefore
 * it should not be used as a way to change intended device's output
 * range, because it will be applied to the least significant bits of
 * the read data.
 *
 * @param device The AIO device number
 * @param pin The AIO pin on device
 * @param precision The number of valid bits on the data received from
 * the analog to digital converter.
 * @return A new AIO handle
 */
struct sol_aio *sol_aio_open_raw(const int device, const int pin, const unsigned int precision);

/**
 * @brief Close the given AIO handle.
 *
 * @param aio AIO handle to be closed.
 */
void sol_aio_close(struct sol_aio *aio);

/**
 * @brief Return true if AIO handle is busy, processing another
 * operation. This function should be called before issuing any read
 * operation.
 *
 * @param aio The AIO bus handle
 *
 * @return true if busy, false if idle
 */
bool sol_aio_busy(struct sol_aio *aio);

/**
 * @brief Request an (asynchronous) read operation to take place on
 * AIO handle @a aio.
 *
 * @param aio A valid AIO handle for the desired @c 'device/pin' pair.
 * @param read_cb The callback to be issued when the operation
 * finishes. The @a ret parameter will contain the given digital
 * reading (non-negative value) on success or a negative error code,
 * on failure.
 * @param cb_data The data pointer to be passed to @a read_cb.
 *
 * @return pending An AIO pending operation handle on success,
 * otherwise @c NULL. It's only valid before @a read_cb is called. It
 * may be used before that to cancel the read operation.
 */
struct sol_aio_pending *sol_aio_get_value(struct sol_aio *aio, void (*read_cb)(void *cb_data, struct sol_aio *aio, int32_t ret), const void *cb_data);

/**
 * @brief Cancel a pending operation.
 *
 * @param aio the AIO handle
 * @param pending the operation handle
 */
void sol_aio_pending_cancel(struct sol_aio *aio, struct sol_aio_pending *pending);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
