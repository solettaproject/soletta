/*
 * This file is part of the Soletta Project
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
 * @brief These routines are used for Inter Processor Messaging
 * on Soletta.
 */

/**
 * @defgroup IPM IPM
 * @ingroup IO
 *
 * @brief Inter Processor Messaging API for Soletta.
 *
 * Some platforms may have more than one core available to use. This API
 * provides a way to send data from one to another, assuming shared
 * memory among cores and a mechanism to send messages from one core to
 * another, like mailbox.
 *
 * @{
 */

/**
 * @brief Set receiver to IPM messages.
 *
 * Receives messages sent with id @a id on callback @receive_cb. Message
 * content is on callback parameter @a message.
 *
 * @param id id of messages to listen to. Only messages sent with @a id
 * will be sent to this receiver.
 * @param receive_cb callback to be called with message sent with @a id.
 * If @c NULL, deletes a previously set callback.
 * @param data user defined data that will be sent to callback @cb
 *
 * @return 0 if receiver was set (or unset if @receive_cb is @c NULL).
 * Negative number otherwise.
 *
 * @note message received is a @c sol_blob. Only when all references to this
 * blob are gone sender will receive its consumed confirmation.
 *
 * @attention Soletta reserves id 0 - do not use it.
 *
 * @see sol_blob
 * @see sol_ipm_send
 */
int sol_ipm_set_receiver(uint32_t id,
    void (*receive_cb)(void *cb_data, uint32_t id, struct sol_blob *message),
    const void *data);

/**
 * @brief Send IPM message.
 *
 * Sends messages with id @id. Optional callback @message_consumed_cb is
 * called when receiver consumed message, i.e. it's OK to free its
 * contents. This happens when last reference to @c sol_blob is gone.
 *
 * @param id id of message. Only receivers listening to the same id will
 * receive this message.
 * @param message message to be sent.
 * @param message_consumed_cb optional callback to be called when receiver
 * consumed this message. This happens when last reference to it is gone.
 * @param data user defined data to be sent to @message_consumed_cb
 *
 * @attention Soletta reserves id 0 - do not use it.
 *
 * @return 0 if sucess. Negative number otherwise
 */
int sol_ipm_send(uint32_t id, struct sol_blob *message,
    void (*message_consumed_cb)(void *cb_data, uint32_t id, struct sol_blob *message),
    const void *data);

/**
 * @brief Get maximum IPM message ID for the platform.
 *
 * Some platforms limit maximum number that can be used for message ID.
 * This function retrieves such value.
 *
 * @return Maximum message ID on current platform. If return 0, an error
 * happened.
 *
 * @note Soletta reserves id 0.
 */
uint32_t sol_ipm_max_id(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
