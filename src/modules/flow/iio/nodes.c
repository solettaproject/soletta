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

#include "iio-gen.h"
#include "sol-flow-internal.h"

#include <sol-util.h>
#include <errno.h>

#include <sol-iio.h>

struct gyroscope_data {
    struct sol_flow_node *node;
    struct sol_iio_device *device;
    struct sol_iio_channel *channel_x;
    struct sol_iio_channel *channel_y;
    struct sol_iio_channel *channel_z;
    bool buffer_enabled : 1;
};

static void
reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct gyroscope_data *mdata = data;
    struct sol_direction_vector out = { .min = DBL_MAX, .max = -DBL_MAX };
    bool b;

    b = sol_iio_read_channel_value(mdata->channel_x, &out.x);
    if (!b) goto error;
    if (out.x > out.max) out.max = out.x;
    if (out.x < out.min) out.min = out.x;

    b = sol_iio_read_channel_value(mdata->channel_y, &out.y);
    if (!b) goto error;
    if (out.y > out.max) out.max = out.y;
    if (out.y < out.min) out.min = out.y;

    b = sol_iio_read_channel_value(mdata->channel_z, &out.z);
    if (!b) goto error;
    if (out.z > out.max) out.max = out.z;
    if (out.z < out.min) out.min = out.z;

    sol_flow_send_direction_vector_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_IIO_GYROSCOPE__OUT__OUT, &out);

    return;

error:
    sol_flow_send_error_packet(mdata->node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static int
gyroscope_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct gyroscope_data *mdata = data;
    const struct sol_flow_node_type_iio_gyroscope_options *opts;
    struct sol_iio_config config;
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_GYROSCOPE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_gyroscope_options *)options;

    mdata->buffer_enabled = opts->buffer_size.val > -1;

    config.api_version = SOL_IIO_CONFIG_API_VERSION;
    config.trigger_name = opts->iio_trigger_name;
    config.buffer_size = opts->buffer_size.val;
    config.sampling_frequency = opts->sampling_frequency.val;
    if (mdata->buffer_enabled) {
        config.sol_iio_reader_cb = reader_cb;
        config.data = mdata;
    }

    mdata->device = sol_iio_open(opts->iio_device.val, &config);
    SOL_NULL_CHECK(mdata->device, -EINVAL);

#define ADD_CHANNEL(_axis) \
    if (!opts->use_device_default_scale) \
        channel_config.scale = opts->scale._axis; \
    if (!opts->use_device_default_offset) \
        channel_config.offset = opts->offset._axis; \
    mdata->channel_ ## _axis = sol_iio_add_channel(mdata->device, "in_anglvel_" # _axis, &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channel_ ## _axis, error);

    ADD_CHANNEL(x);
    ADD_CHANNEL(y);
    ADD_CHANNEL(z);

#undef ADD_CHANNEL

    sol_iio_device_start_buffer(mdata->device);
    mdata->node = node;

    return 0;

error:
    SOL_WRN("Could not open create iio/gyroscope node. Failed to open IIO device %d",
        opts->iio_device.val);
    sol_iio_close(mdata->device);

    return -EINVAL;
}

static void
gyroscope_close(struct sol_flow_node *node, void *data)
{
    struct gyroscope_data *mdata = data;

    if (mdata->device)
        sol_iio_close(mdata->device);
}

static int
gyroscope_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const char *errmsg = "Could not read channel values";
    struct gyroscope_data *mdata = data;

    if (mdata->buffer_enabled) {
        if (!sol_iio_device_trigger_now(mdata->device))
            goto error;
    } else {
        reader_cb(mdata, mdata->device);
    }

    return 0;

error:
    sol_flow_send_error_packet(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);

    return -EIO;
}

#include "iio-gen.c"
