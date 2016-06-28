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

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>

#ifndef SOL_LOG_DOMAIN
#define SOL_LOG_DOMAIN &_sol_ipm_log_domain
#endif

#include "sol-log-internal.h"

#include "ipm.h"
#include "ipm/ipm_quark_se.h"

#include "sol-ipm.h"
#include "sol-log.h"
#include "sol-macros.h"
#include "sol-mainloop-zephyr.h"
#include "sol-types.h"
#include "sol-util.h"
#include "sol-vector.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_sol_ipm_log_domain, "ipm");

#define SOL_IPM_ID_FREE_REMOTE 0

/* Declare channels we are going to use.
 */
#ifdef CONFIG_X86
QUARK_SE_IPM_DEFINE(message_ipm_receiver, IPM_CHANNEL_ARC_TO_X86, QUARK_SE_IPM_INBOUND);
QUARK_SE_IPM_DEFINE(message_ipm_sender, IPM_CHANNEL_X86_TO_ARC, QUARK_SE_IPM_OUTBOUND);
#elif CONFIG_ARC
QUARK_SE_IPM_DEFINE(message_ipm_receiver, IPM_CHANNEL_X86_TO_ARC, QUARK_SE_IPM_INBOUND);
QUARK_SE_IPM_DEFINE(message_ipm_sender, IPM_CHANNEL_ARC_TO_X86, QUARK_SE_IPM_OUTBOUND);
#endif

#define AS_PTR(_ptr) *(uintptr_t *)_ptr

int sol_ipm_init(void);
void sol_ipm_shutdown(void);

/***** IPM Blobs *****/

static void free_ipm_blob(struct sol_blob *blob);

struct sol_blob_ipm_received {
    struct sol_blob base;
    struct sol_blob *remote;
};

struct sol_blob_ipm_sent {
    struct sol_blob base;
    uint32_t id;
};

static const struct sol_blob_type SOL_BLOB_TYPE_IPM_RECEIVED = {
    SOL_SET_API_VERSION(.api_version = SOL_BLOB_TYPE_API_VERSION, )
    SOL_SET_API_VERSION(.sub_api = 0, )
    .free = free_ipm_blob,
};

/*** IPM Stuff ***/

struct sol_ipm_receiver_entry {
    uint32_t id;
    void (*receive_cb)(void *cb_data, uint32_t id, struct sol_blob *message);
    const void *data;
};

struct sol_ipm_consumed_handler {
    uint32_t id;
    void (*message_consumed_cb)(void *data, uint32_t id, struct sol_blob *message);
    const void *data;
};

static struct sol_vector ipm_receivers;
static struct sol_vector ipm_consumed_handlers;
static struct device *ipm_sender, *ipm_receiver;
static uint32_t ipm_max_id;
static bool initialised;

static void
free_remote_blob(struct sol_blob *blob)
{
    int r;

    r = ipm_send(ipm_sender, 1, SOL_IPM_ID_FREE_REMOTE, &blob,
        sizeof(uintptr_t));
    if (r != 0) {
        /* Should I care if return was EBUSY?*/
        SOL_WRN("Could not send consumed message to remote core: %d", r);
    }
}

static void
free_ipm_blob(struct sol_blob *blob)
{
    struct sol_blob_ipm_received *ipm_blob = (struct sol_blob_ipm_received *)blob;

    free_remote_blob(ipm_blob->remote);

    /* Free local shadow */
    free(blob);
}

static void
ipm_receiver_process(void *data)
{
    uint16_t idx;
    struct sol_blob_ipm_sent *message = data;
    struct sol_ipm_receiver_entry *receiver;

    SOL_VECTOR_FOREACH_IDX (&ipm_receivers, receiver, idx) {
        if (receiver->id == message->id) {
            struct sol_blob_ipm_received *remote_blob;

            /* Setup local shadow blob whose `mem` points to the same `mem` as remote one*/
            remote_blob = calloc(1, sizeof(struct sol_blob_ipm_received));
            if (!remote_blob) {
                SOL_WRN("Could not create blob to deliver IPM message %p (id %" PRIu32 ")",
                    message, message->id);
                goto fail;
            }

            sol_blob_setup(&remote_blob->base, &SOL_BLOB_TYPE_IPM_RECEIVED,
                message->base.mem,
                message->base.size);

            remote_blob->remote = &message->base;
            receiver->receive_cb((void *)receiver->data, message->id,
                &remote_blob->base);
            return;
        }
    }

    SOL_INF("Processed IPM id %" PRIu32 " but no receiver was found!",
        message->id);

fail:
    /* Let's unref this blob on remote */
    free_remote_blob(&message->base);
}

static void
ipm_reaper_process(void *data)
{
    uint16_t idx;
    struct sol_blob_ipm_sent *message = data;
    struct sol_ipm_consumed_handler *handler;

    SOL_VECTOR_FOREACH_IDX (&ipm_consumed_handlers, handler, idx) {
        if (handler->id == message->id) {
            handler->message_consumed_cb((void *)handler->data, message->id,
                message->base.parent);
            sol_blob_unref(&message->base);
            return;
        }
    }
}

/* Note: this function runs in interrupt context */
static void
ipm_receiver_isr_cb(void *context, uint32_t id, volatile void *data)
{
    struct sol_blob_ipm_sent *remote_blob = (struct sol_blob_ipm_sent *)AS_PTR(data);
    struct mainloop_event me;

    /* id == 0 messages. Those messages signal sender
    * core that blob was consumed and may be freed */
    if (id == SOL_IPM_ID_FREE_REMOTE) {
        me.cb = ipm_reaper_process;
        me.data = remote_blob;
        sol_mainloop_event_post(&me);
        return;
    }

    me.cb = ipm_receiver_process,
    me.data = remote_blob;
    sol_mainloop_event_post(&me);
}

static int
init(void)
{
    int r;
    uint32_t max_data_size;

    ipm_receiver = device_get_binding("message_ipm_receiver");
    SOL_NULL_CHECK_MSG(ipm_receiver, -EINVAL, "Could not get IPM receiver channel");

    ipm_sender = device_get_binding("message_ipm_sender");
    SOL_NULL_CHECK_MSG(ipm_sender, -EINVAL, "Could not get IPM sender channel");

    max_data_size = ipm_max_data_size_get(ipm_sender);
    if (max_data_size < sizeof(void *)) {
        SOL_WRN("IPM max data size < sizeof(void *)");
        return -EINVAL;
    }

    ipm_max_id = ipm_max_id_val_get(ipm_sender);

    ipm_register_callback(ipm_receiver, ipm_receiver_isr_cb, NULL);
    r = ipm_set_enabled(ipm_receiver, 1);
    if (r != 0) {
        SOL_WRN("Could not enable IPM receiver: %s", sol_util_strerrora(r));
        return -r;
    }

    sol_vector_init(&ipm_receivers, sizeof(struct sol_ipm_receiver_entry));
    sol_vector_init(&ipm_consumed_handlers, sizeof(struct sol_ipm_consumed_handler));

    initialised = true;

    return 0;
}

SOL_API int
sol_ipm_set_receiver(uint32_t id,
    void (*receive_cb)(void *cb_data, uint32_t id, struct sol_blob *message),
    const void *data)
{
    uint16_t idx;
    struct sol_ipm_receiver_entry *receiver;

    if (!initialised) {
        int r = init();

        SOL_INT_CHECK(r, != 0, r);
    }

    SOL_INT_CHECK(id, > ipm_max_id, -EINVAL);
    SOL_INT_CHECK(id, == 0, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&ipm_receivers, receiver, idx) {
        if (receiver->id == id) {
            if (receive_cb)
                return -EEXIST; /* One can not override current receiver. */
            else
                return sol_vector_del(&ipm_receivers, idx);
        }
    }

    /* NULL receive_cb means it's trying to remove a receiver, but none was found */
    SOL_NULL_CHECK(receive_cb, -ENOENT);

    receiver = sol_vector_append(&ipm_receivers);
    SOL_NULL_CHECK_MSG(receiver, -ENOMEM, "Could not add receiver to id %" PRIu32,
        id);

    receiver->id = id;
    receiver->receive_cb = receive_cb;
    receiver->data = data;

    return 0;
}

SOL_API int
sol_ipm_send(uint32_t id, struct sol_blob *message)
{
    struct sol_blob_ipm_sent *blob;
    int r;

    if (!initialised) {
        r = init();
        SOL_INT_CHECK(r, != 0, r);
    }

    SOL_INT_CHECK(id, > ipm_max_id, -EINVAL);
    SOL_INT_CHECK(id, == 0, -EINVAL);
    SOL_NULL_CHECK(message, -EINVAL);

    blob = calloc(1, sizeof(struct sol_blob_ipm_sent));
    SOL_NULL_CHECK(blob, -ENOMEM);

    sol_blob_setup(&blob->base, &SOL_BLOB_TYPE_NO_FREE_DATA, message->mem, message->size);
    sol_blob_set_parent(&blob->base, message);
    blob->id = id;

    r = ipm_send(ipm_sender, 1, id, &blob, sizeof(uintptr_t));
    SOL_INT_CHECK_GOTO(r, != 0, fail);

    return 0;

fail:
    sol_blob_unref(&blob->base);
    return -r;
}

SOL_API int
sol_ipm_set_consumed_callback(uint32_t id,
    void (*message_consumed_cb)(void *data, uint32_t id, struct sol_blob *message),
    const void *data)
{
    uint16_t idx;
    struct sol_ipm_consumed_handler *handler;

    if (!initialised) {
        int r = init();

        SOL_INT_CHECK(r, != 0, r);
    }

    SOL_INT_CHECK(id, > ipm_max_id, -EINVAL);
    SOL_INT_CHECK(id, == 0, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&ipm_consumed_handlers, handler, idx) {
        if (handler->id == id) {
            if (message_consumed_cb)
                return -EEXIST;
            else
                return sol_vector_del(&ipm_consumed_handlers, idx);
        }
    }

    /* NULL message_consumed_cb means it's trying to remove a handler, but none was found */
    SOL_NULL_CHECK(message_consumed_cb, -ENOENT);

    handler = sol_vector_append(&ipm_consumed_handlers);
    SOL_NULL_CHECK(handler, -ENOMEM);

    handler->id = id;
    handler->message_consumed_cb = message_consumed_cb;
    handler->data = data;

    return 0;
}

SOL_API uint32_t
sol_ipm_get_max_id(void)
{
    if (!initialised) {
        int r = init();

        SOL_INT_CHECK(r, != 0, r);
    }

    return ipm_max_id;
}

int
sol_ipm_init(void)
{
    sol_log_domain_init_level(SOL_LOG_DOMAIN);

    return 0;
}

void
sol_ipm_shutdown(void)
{
    sol_vector_clear(&ipm_receivers);
    sol_vector_clear(&ipm_consumed_handlers);
}
