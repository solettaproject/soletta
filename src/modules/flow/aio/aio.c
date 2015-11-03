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

#include "sol-flow/aio.h"

#include "sol-aio.h"
#include "sol-flow-internal.h"
#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-types.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <limits.h>

struct aio_data {
    struct sol_flow_node *node;
    struct sol_timeout *timer;
    struct sol_aio *aio;
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

static bool
_on_reader_timeout(void *data)
{
    struct sol_irange i;
    struct aio_data *mdata = data;

    SOL_NULL_CHECK(data, true);

    i.val = sol_aio_get_value(mdata->aio);
    if (i.val < 0) {
        sol_flow_send_error_packet(mdata->node, EINVAL,
            "aio (%s): Could not read value.", mdata->pin);
        return false;
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
