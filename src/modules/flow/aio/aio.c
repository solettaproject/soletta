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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "sol-flow/aio.h"

#include "sol-aio.h"
#include "sol-flow-internal.h"
#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-util-internal.h"
#include "sol-types.h"

struct aio_data {
    struct sol_flow_node *node;
    struct sol_timeout *timer;
    struct sol_aio *aio;
    struct sol_aio_pending *pending;
    char *pin;
    int mask;
    int last_value;
    bool is_first;
};

static void
aio_close(struct sol_flow_node *node, void *data)
{
    struct aio_data *mdata = data;

    SOL_NULL_CHECK(mdata);

    free(mdata->pin);

    if (mdata->aio)
        sol_aio_close(mdata->aio);
    if (mdata->timer)
        sol_timeout_del(mdata->timer);
}

// =============================================================================
// AIO READER
// =============================================================================

static void
read_cb(void *cb_data, struct sol_aio *aio, int32_t ret)
{
    struct sol_irange i;
    struct aio_data *mdata = cb_data;

    i.val = ret;
    if (i.val < 0) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "AIO (%s): Failed on read operation: %s.", mdata->pin,
            sol_util_strerrora(-i.val));
        return;
    }

    if (mdata->is_first || i.val != mdata->last_value) {
        mdata->is_first = false;
        mdata->last_value = i.val;
        i.max = mdata->mask;
        i.min = 0;
        i.step = 1;
        sol_flow_send_irange_packet(mdata->node,
            SOL_FLOW_NODE_TYPE_AIO_READER__OUT__OUT, &i);
    }

    mdata->pending = NULL;
}

static bool
_on_reader_timeout(void *data)
{
    struct aio_data *mdata = data;

    mdata->pending = sol_aio_get_value(mdata->aio, read_cb, mdata);
    if (!mdata->pending && errno != EBUSY) {
        sol_flow_send_error_packet(mdata->node, errno,
            "AIO (%s): Failed to issue read operation.", mdata->pin);
        return false;
    }

    return true;
}

static int
aio_reader_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int device, pin;
    struct aio_data *mdata = data;
    const struct sol_flow_node_type_aio_reader_options *opts =
        (const struct sol_flow_node_type_aio_reader_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(opts,
        SOL_FLOW_NODE_TYPE_AIO_READER_OPTIONS_API_VERSION, -EINVAL);

    mdata->aio = NULL;
    mdata->is_first = true;
    mdata->node = node;

    if (!opts->pin || *opts->pin == '\0') {
        SOL_WRN("aio: Option 'pin' cannot be neither 'null' nor empty.");
        return -EINVAL;
    }

    if (opts->mask <= 0) {
        SOL_WRN("aio (%s): Invalid bit mask value=%" PRId32 ".", opts->pin, opts->mask);
        return -EINVAL;
    }

    if (opts->poll_timeout <= 0) {
        SOL_WRN("aio (%s): Invalid polling time=%" PRId32 ".", opts->pin, opts->poll_timeout);
        return -EINVAL;
    }

    if (opts->raw) {
        if (sscanf(opts->pin, "%d %d", &device, &pin) == 2) {
            mdata->aio = sol_aio_open(device, pin, opts->mask);
        } else {
            SOL_WRN("aio (%s): 'raw' option was set, but 'pin' value=%s couldn't be parsed as "
                "\"<device> <pin>\" pair.", opts->pin, opts->pin);
        }
    } else {
        mdata->aio = sol_aio_open_by_label(opts->pin, opts->mask);
    }

    SOL_NULL_CHECK_MSG(mdata->aio, -EINVAL,
        "aio (%s): Couldn't be open. Maybe you used an invalid 'pin'=%s?", opts->pin, opts->pin);

    mdata->pin = opts->pin ? strdup(opts->pin) : NULL;
    mdata->mask = (0x01 << opts->mask) - 1;
    mdata->timer = sol_timeout_add(opts->poll_timeout, _on_reader_timeout, mdata);

    return 0;
}

#include "aio-gen.c"
