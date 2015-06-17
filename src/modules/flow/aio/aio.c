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

#include "aio-gen.h"

#include "sol-flow-internal.h"
#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-types.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <limits.h>

#define AIO_BASE_PATH "/sys/bus/iio/devices/iio:device0/in_voltage%d_raw"

struct aio_data {
    struct sol_flow_node *node;
    struct sol_timeout *timer;
    FILE *fp;
    int pin;
    int mask;
    int last_value;
    bool is_first;
};

static bool
_aio_open_fd(struct aio_data *mdata)
{
    char path[PATH_MAX];
    int len;

    len = snprintf(path, sizeof(path), AIO_BASE_PATH, mdata->pin);
    if (len < 0 || len > PATH_MAX)
        return false;

    mdata->fp = fopen(path, "re");
    if (!mdata->fp)
        return false;
    setvbuf(mdata->fp, NULL, _IONBF, 0);

    return true;
}

static void
aio_close(struct sol_flow_node *node, void *data)
{
    struct aio_data *mdata = data;

    SOL_NULL_CHECK(mdata);

    if (mdata->fp)
        fclose(mdata->fp);
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

    if (!mdata->fp) {
        if (!_aio_open_fd(mdata)) {
            SOL_WRN("aio #%d: Could not open file.", mdata->pin);
            return false;
        }
    }

    rewind(mdata->fp);

    if (fscanf(mdata->fp, "%d", &i.val) < 1) {
        SOL_WRN("aio #%d: Could not read value.", mdata->pin);
        return false;
    }

    i.val &= mdata->mask;

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
    char path[PATH_MAX];
    struct stat st;
    struct aio_data *mdata = data;
    const struct sol_flow_node_type_aio_reader_options *opts =
        (const struct sol_flow_node_type_aio_reader_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(opts, SOL_FLOW_NODE_TYPE_AIO_READER_OPTIONS_API_VERSION, -EINVAL);

    mdata->is_first = true;
    mdata->node = node;
    mdata->pin = opts->pin.val;

    snprintf(path, sizeof(path), AIO_BASE_PATH, mdata->pin);
    if (stat(path, &st) == -1) {
        SOL_WRN("aio #%d: Couldn't open pin.", mdata->pin);
        return -EINVAL;
    }

    if (opts->poll_timeout.val <= 0) {
        SOL_WRN("aio #%d: Invalid polling time=%" PRId32 ".", mdata->pin, opts->poll_timeout.val);
        return -EINVAL;
    }

    if (opts->mask.val == 0) {
        SOL_WRN("aio #%d: Invalid bit mask value=%" PRId32 ".", mdata->pin, opts->mask.val);
        return -EINVAL;
    }

    mdata->mask = (0x01 << opts->mask.val) - 1;
    mdata->timer = sol_timeout_add(opts->poll_timeout.val, _on_reader_timeout, mdata);

    return 0;
}

#include "aio-gen.c"
