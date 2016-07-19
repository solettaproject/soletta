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

/**
 * @typedef sol_aio
 * @brief AIO handle structure
 * @see sol_aio_open_by_label()
 * @see sol_aio_open()
 * @see sol_aio_open_raw()
 * @see sol_aio_close()
 * @see sol_aio_get_value()
 * @see sol_aio_pending_cancel()
 */
struct sol_aio;
typedef struct sol_aio sol_aio;

/**
 * @typedef sol_aio_pending
 * @brief AIO pending operation handle structure
 * @see sol_aio_get_value()
 * @see sol_aio_pending_cancel()
 */
struct sol_aio_pending;
typedef struct sol_aio_pending sol_aio_pending;

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
struct sol_aio *sol_aio_open_by_label(const char *label, unsigned int precision);

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
struct sol_aio *sol_aio_open(int device, int pin, unsigned int precision);

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
struct sol_aio *sol_aio_open_raw(int device, int pin, unsigned int precision);

/**
 * @brief Close the given AIO handle.
 *
 * @param aio AIO handle to be closed.
 */
void sol_aio_close(struct sol_aio *aio);

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
 * @return pending An AIO pending operation handle on success and errno is set to @c 0,
 * otherwise @c NULL. It's only valid before @a read_cb is called. It
 * may be used before that to cancel the read operation.
 * If @c NULL is returned, the errno variable will be set with the correct error value.
 * In case that the AIO device is in use, the errno variable is set to EBUSY.
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
