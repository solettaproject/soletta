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

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>

#ifndef SOL_LOG_DOMAIN
#define SOL_LOG_DOMAIN &_sol_ipm_log_domain
#endif

#include "sol-log-internal.h"

#include "ipm.h"
#include "ipm/ipm_quark_se.h"

#include "sol-log.h"
#include "sol-macros.h"
#include "sol-mainloop-zephyr.h"
#include "sol-types.h"
#include "sol-util.h"
#include "sol-vector.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_sol_ipm_log_domain, "ipm");

#define SOL_IPM_ID_FREE_REMOTE 0

/* Declare channels we are going to use.
 * Channel 0 is Quark -> ARC
 * Channel 1 is ARC -> Quark
 */
#ifdef CONFIG_X86
QUARK_SE_IPM_DEFINE(message_ipm_receiver, 1, QUARK_SE_IPM_INBOUND);
QUARK_SE_IPM_DEFINE(message_ipm_sender, 0, QUARK_SE_IPM_OUTBOUND);
#elif CONFIG_ARC
#warning "Building for ARC"
QUARK_SE_IPM_DEFINE(message_ipm_receiver, 0, QUARK_SE_IPM_INBOUND);
QUARK_SE_IPM_DEFINE(message_ipm_sender, 1, QUARK_SE_IPM_OUTBOUND);
#endif

/*XXX there MUST be a better way of doing so */
#define AS_PTR(_ptr) *(uint64_t *)_ptr

int sol_ipm_init(void);
void sol_ipm_shutdown(void);

static void free_ipm_blob(struct sol_blob *blob);

struct sol_blob_ipm {
    struct sol_blob base;
    struct sol_blob *remote;
};

static const struct sol_blob_type SOL_BLOB_TYPE_IPM = {
    SOL_SET_API_VERSION(.api_version = SOL_BLOB_TYPE_API_VERSION, )
    SOL_SET_API_VERSION(.sub_api = 0, )
    .free = free_ipm_blob,
};

struct sol_ipm_receiver_entry {
    uint32_t id;
    void (*receive_cb)(void *cb_data, uint32_t id, struct sol_blob *message);
    const void *data;
    struct sol_blob *last_remote_blob; /* Hackish... I store last blob received here, to avoid having yet another list to store them */
};

struct sol_ipm_sent_message {
    uint32_t id;
    struct sol_blob *message;
    void (*message_consumed_cb)(void *cb_data, uint32_t id, struct sol_blob *message);
    const void *data;
};

static struct sol_vector ipm_receivers = SOL_VECTOR_INIT(struct sol_ipm_receiver_entry);
static struct sol_vector ipm_sent_messages = SOL_VECTOR_INIT(struct sol_ipm_sent_message);
static struct device *ipm_sender, *ipm_receiver;
static uint32_t ipm_max_id;

static void
free_ipm_blob(struct sol_blob *blob)
{
    struct sol_blob_ipm *ipm_blob = (struct sol_blob_ipm *)blob;
    int r;

    if (blob->type != &SOL_BLOB_TYPE_IPM) {
        SOL_WRN("Invalid blob type: expected SOL_BLOB_TYPE_IPM(%p), got: %p",
            &SOL_BLOB_TYPE_IPM, blob->type);
        return;
    }

    r = ipm_send(ipm_sender, 1, SOL_IPM_ID_FREE_REMOTE, &ipm_blob->remote,
        sizeof(ipm_blob->remote));
    if (r != 0) {
        /* Should I care if return was EBUSY?*/
        SOL_WRN("Could not send consumed message to remote core: %d", r);
    }

    /* Free local shadow */
    free(blob);
}

static void
ipm_receiver_process(void *data)
{
    int idx;
    uint32_t id = (uint32_t)data;
    struct sol_ipm_receiver_entry *receiver;

    SOL_VECTOR_FOREACH_IDX (&ipm_receivers, receiver, idx) {
        if (receiver->id == id) {
            struct sol_blob_ipm *remote_blob;

            if (!receiver->last_remote_blob) {
                SOL_WRN("Remote blob for IPM id %" PRIu32 " disappered!", id);
                return;
            }

            remote_blob = calloc(1, sizeof(struct sol_blob_ipm));
            if (!remote_blob) {
                SOL_WRN("Could not create blob to deliver IPM message");
                receiver->last_remote_blob = NULL;
                return;
            }

            sol_blob_setup((struct sol_blob *)remote_blob, &SOL_BLOB_TYPE_IPM,
                receiver->last_remote_blob->mem,
                receiver->last_remote_blob->size);

            remote_blob->remote = receiver->last_remote_blob;
            receiver->last_remote_blob = NULL;
            receiver->receive_cb((void *)receiver->data, id,
                (struct sol_blob *)remote_blob);
            return;
        }
    }

    SOL_WRN("Processed IPM id %" PRIu32 " but no receiver was found!", id);
}

static void
ipm_reaper_process(void *data)
{
    int idx;
    struct sol_ipm_sent_message *sent;
    struct sol_blob *message = data;

    SOL_VECTOR_FOREACH_IDX (&ipm_sent_messages, sent, idx) {
        if (sent->message == message) {
            if (sent->message_consumed_cb)
                sent->message_consumed_cb((void *)sent->data, sent->id, message);
            sol_blob_unref(message);
            sol_vector_del(&ipm_sent_messages, idx);
            return;
        }
    }
}

/* Note: this function runs in interrupt context */
static void
ipm_receiver_cb(void *context, uint32_t id, volatile void *data)
{
    int idx;
    struct sol_blob *remote_blob = (struct sol_blob *)AS_PTR(data);
    struct sol_ipm_receiver_entry *receiver;
    struct mainloop_event me;

    /* id == 0 messages. Those messages signal sender
    * core that blob was consumed and may be freed */
    if (id == SOL_IPM_ID_FREE_REMOTE) {
        me.cb = ipm_reaper_process;
        me.data = remote_blob;
        sol_mainloop_event_post(&me);
        return;
    }

    //TODO rethink this. What if we receive another message with same id
    //before processing current one?
    me.cb = ipm_receiver_process,
    me.data = (void *)id; /* I could send `receiver` entry, and avoid yet
                             another loop to find receiver. Although currently,
                             Zephyr implementation does process events before
                             timeouts, I'm not sure we can count on that
                             `ipm_vectors` is not going to be changed in the
                             meanwhile.*/

    SOL_VECTOR_FOREACH_IDX (&ipm_receivers, receiver, idx) {
        if (receiver->id == id) {
            receiver->last_remote_blob = remote_blob;
            int r = sol_mainloop_event_post(&me);
            return;
        }
    }

    /* Is it really OK/safe to print from isr? */
    SOL_INF("Got message with id %" PRIu32 " but there's no receiver associated to it",
        id);
}

SOL_API int
sol_ipm_set_receiver(uint32_t id,
    void (*receive_cb)(void *cb_data, uint32_t id, struct sol_blob *message),
    const void *data)
{
    int idx;
    struct sol_ipm_receiver_entry *receiver;

    SOL_INT_CHECK(id, > ipm_max_id, -EINVAL);
    SOL_INT_CHECK(id, == 0, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&ipm_receivers, receiver, idx) {
        if (receiver->id == id) {
            if (receive_cb) {
                receiver->receive_cb = receive_cb;
                return 0;
            } else {
                return sol_vector_del(&ipm_receivers, idx);
            }
        }
    }

    /* Not changing or deleting. Must have a receive_cb*/
    SOL_NULL_CHECK(receive_cb, -EINVAL);

    receiver = sol_vector_append(&ipm_receivers);
    SOL_NULL_CHECK_MSG(receiver, -ENOMEM, "Could not add receiver to id %" PRIu32,
        id);

    receiver->id = id;
    receiver->receive_cb = receive_cb;
    receiver->data = data;

    return 0;
}

SOL_API int
sol_ipm_send(uint32_t id, struct sol_blob *message,
    void (*message_consumed_cb)(void *cb_data, uint32_t id, struct sol_blob *message),
    const void *data)
{
    int r;
    struct sol_ipm_sent_message *sent;

    SOL_INT_CHECK(id, > ipm_max_id, -EINVAL);
    SOL_INT_CHECK(id, == 0, -EINVAL);
    SOL_NULL_CHECK(message, -EINVAL);

    sent = sol_vector_append(&ipm_sent_messages);
    SOL_NULL_CHECK(sent, -ENOMEM);

    r = ipm_send(ipm_sender, 1, id, &message, sizeof(message));
    SOL_INT_CHECK_GOTO(r, > 0, fail);

    sent->id = id;
    sent->message_consumed_cb = message_consumed_cb;
    sent->data = data;
    sent->message = sol_blob_ref(message); /* Hold a reference to it so it doesn't die early*/

    return 0;

fail:
    sol_vector_del_last(&ipm_sent_messages);
    return -r;
}


SOL_API uint32_t
sol_ipm_max_id(void)
{
    return ipm_max_id;
}

int
sol_ipm_init(void)
{
    int max_data_size = 0, r;

    sol_log_domain_init_level(SOL_LOG_DOMAIN);

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

    ipm_register_callback(ipm_receiver, ipm_receiver_cb, NULL);
    r = ipm_set_enabled(ipm_receiver, 1);
    if (r != 0) {
        SOL_WRN("Could not enable IPM receiver: %s", sol_util_strerrora(r));
        return -r;
    }

    return 0;
}

void
sol_ipm_shutdown(void)
{
    sol_vector_clear(&ipm_receivers);
    sol_vector_clear(&ipm_sent_messages);
}
