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

#include "sol-flow/iio.h"
#include "sol-flow-internal.h"

#include <sol-types.h>
#include <sol-util.h>
#include <errno.h>

#include <sol-iio.h>

struct gyroscope_data {
    struct sol_iio_config config;
    struct sol_direction_vector scale;
    struct sol_direction_vector offset;
    struct sol_drange_spec out_range;
    struct sol_iio_device *device;
    struct sol_iio_channel *channel_x;
    struct sol_iio_channel *channel_y;
    struct sol_iio_channel *channel_z;
    bool buffer_enabled : 1;
    bool use_device_default_scale : 1;
    bool use_device_default_offset : 1;
};

static void
reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct gyroscope_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_direction_vector out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channel_x, &out.x);
    if (!b) goto error;

    b = sol_iio_read_channel_value(mdata->channel_y, &out.y);
    if (!b) goto error;

    b = sol_iio_read_channel_value(mdata->channel_z, &out.z);
    if (!b) goto error;

    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_GYROSCOPE__OUT__OUT, &out);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static void
create_device_cb(void *data, int device_id)
{
    struct gyroscope_data *mdata = data;
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device);

#define ADD_CHANNEL(_axis) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scale._axis; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offset._axis; \
    mdata->channel_ ## _axis = sol_iio_add_channel(mdata->device, "in_anglvel_" # _axis, &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channel_ ## _axis, error);

    ADD_CHANNEL(x);
    ADD_CHANNEL(y);
    ADD_CHANNEL(z);

#undef ADD_CHANNEL

    sol_iio_device_start_buffer(mdata->device);

    return;

error:
    SOL_WRN("Could not create iio/gyroscope node. Failed to open IIO device %d",
        device_id);
    sol_iio_close(mdata->device);
}

static int
gyroscope_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct gyroscope_data *mdata = data;
    const struct sol_flow_node_type_iio_gyroscope_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_GYROSCOPE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_gyroscope_options *)options;

    mdata->buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->config.trigger_name, -ENOMEM);
    }

    mdata->config.buffer_size = opts->buffer_size;
    mdata->config.sampling_frequency = opts->sampling_frequency;

    if (mdata->buffer_enabled) {
        mdata->config.sol_iio_reader_cb = reader_cb;
        mdata->config.data = node;
    }
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->out_range = opts->out_range;

    if (!sol_iio_address_device(opts->iio_device, create_device_cb, mdata)) {
        SOL_WRN("Could not create iio/gyroscope node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    return 0;

err:
    free((char *)mdata->config.trigger_name);
    return -EINVAL;
}

static void
gyroscope_close(struct sol_flow_node *node, void *data)
{
    struct gyroscope_data *mdata = data;

    free((char *)mdata->config.trigger_name);
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
    sol_flow_send_error_packet(node, EIO, "%s", errmsg);
    SOL_WRN("%s", errmsg);

    return -EIO;
}

struct magnet_data {
    struct sol_iio_config config;
    struct sol_direction_vector scale;
    struct sol_direction_vector offset;
    struct sol_drange_spec out_range;
    struct sol_iio_device *device;
    struct sol_iio_channel *channel_x;
    struct sol_iio_channel *channel_y;
    struct sol_iio_channel *channel_z;
    bool buffer_enabled : 1;
    bool use_device_default_scale : 1;
    bool use_device_default_offset : 1;
};

static void
magnet_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct magnet_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_direction_vector out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channel_x, &out.x);
    if (!b) goto error;

    b = sol_iio_read_channel_value(mdata->channel_y, &out.y);
    if (!b) goto error;

    b = sol_iio_read_channel_value(mdata->channel_z, &out.z);
    if (!b) goto error;

    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_MAGNET__OUT__OUT, &out);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static void
magnet_create_device_cb(void *data, int device_id)
{
    struct magnet_data *mdata = data;
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device);

#define ADD_CHANNEL(_axis) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scale._axis; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offset._axis; \
    mdata->channel_ ## _axis = sol_iio_add_channel(mdata->device, "in_magn_" # _axis, &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channel_ ## _axis, error);

    ADD_CHANNEL(x);
    ADD_CHANNEL(y);
    ADD_CHANNEL(z);

#undef ADD_CHANNEL

    sol_iio_device_start_buffer(mdata->device);

    return;

error:
    SOL_WRN("Could not create iio/magnet node. Failed to open IIO device %d",
        device_id);
    sol_iio_close(mdata->device);
}

static int
magnet_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct magnet_data *mdata = data;
    const struct sol_flow_node_type_iio_magnet_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_MAGNET_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_magnet_options *)options;

    mdata->buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->config.trigger_name, -ENOMEM);
    }

    mdata->config.buffer_size = opts->buffer_size;
    mdata->config.sampling_frequency = opts->sampling_frequency;
    if (mdata->buffer_enabled) {
        mdata->config.sol_iio_reader_cb = magnet_reader_cb;
        mdata->config.data = node;
    }
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->out_range = opts->out_range;

    if (!sol_iio_address_device(opts->iio_device, magnet_create_device_cb, mdata)) {
        SOL_WRN("Could not create iio/magnet node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    return 0;

err:
    free((char *)mdata->config.trigger_name);
    return -EINVAL;
}

static void
magnet_close(struct sol_flow_node *node, void *data)
{
    struct magnet_data *mdata = data;

    free((char *)mdata->config.trigger_name);
    if (mdata->device)
        sol_iio_close(mdata->device);
}

static int
magnet_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const char *errmsg = "Could not read channel values";
    struct magnet_data *mdata = data;

    if (mdata->buffer_enabled) {
        if (!sol_iio_device_trigger_now(mdata->device))
            goto error;
    } else {
        magnet_reader_cb(mdata, mdata->device);
    }

    return 0;

error:
    sol_flow_send_error_packet(node, EIO, "%s", errmsg);
    SOL_WRN("%s", errmsg);

    return -EIO;
}

struct temperature_data {
    struct sol_iio_config config;
    struct sol_drange_spec out_range;
    double scale;
    double offset;
    struct sol_iio_device *device;
    struct sol_iio_channel *channel_val;
    bool buffer_enabled : 1;
    bool use_device_default_scale : 1;
    bool use_device_default_offset : 1;
};

static void
temp_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct temperature_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max,
        .step = mdata->out_range.step
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channel_val, &out.val);
    if (!b) goto error;

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_TEMPERATURE__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static void
temp_create_device_cb(void *data, int device_id)
{
    struct temperature_data *mdata = data;
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device);

#define ADD_CHANNEL(_val) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scale; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offset; \
    mdata->channel_ ## _val = sol_iio_add_channel(mdata->device, "in_temp", &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channel_ ## _val, error);

    ADD_CHANNEL(val);

#undef ADD_CHANNEL

    sol_iio_device_start_buffer(mdata->device);

    return;

error:
    SOL_WRN("Could not create iio/temperature node. Failed to open IIO device %d",
        device_id);

    sol_iio_close(mdata->device);
}

static int
temperature_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct temperature_data *mdata = data;
    const struct sol_flow_node_type_iio_temperature_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_TEMPERATURE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_temperature_options *)options;

    mdata->buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->config.trigger_name, -ENOMEM);
    }

    mdata->config.buffer_size = opts->buffer_size;
    mdata->config.sampling_frequency = opts->sampling_frequency;
    if (mdata->buffer_enabled) {
        mdata->config.sol_iio_reader_cb = temp_reader_cb;
        mdata->config.data = node;
    }
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->out_range = opts->out_range;

    if (!sol_iio_address_device(opts->iio_device, temp_create_device_cb, mdata)) {
        SOL_WRN("Could not create iio/temperature node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    return 0;

err:
    free((char *)mdata->config.trigger_name);
    return -EINVAL;

}

static void
temperature_close(struct sol_flow_node *node, void *data)
{
    struct temperature_data *mdata = data;

    free((char *)mdata->config.trigger_name);
    if (mdata->device)
        sol_iio_close(mdata->device);
}

static int
temperature_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const char *errmsg = "Could not read channel values";
    struct temperature_data *mdata = data;

    if (mdata->buffer_enabled) {
        if (!sol_iio_device_trigger_now(mdata->device))
            goto error;
    } else {
        temp_reader_cb(mdata, mdata->device);
    }

    return 0;

error:
    sol_flow_send_error_packet(node, EIO, "%s", errmsg);
    SOL_WRN("%s", errmsg);

    return -EIO;
}

struct pressure_data {
    struct sol_iio_config config;
    struct sol_drange_spec out_range;
    double scale;
    double offset;
    struct sol_iio_device *device;
    struct sol_iio_channel *channel_val;
    bool buffer_enabled : 1;
    bool use_device_default_scale : 1;
    bool use_device_default_offset : 1;
};

static void
pressure_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct pressure_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max,
        .step = mdata->out_range.step
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channel_val, &out.val);
    if (!b) goto error;

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_PRESSURE__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static void
pressure_create_device_cb(void *data, int device_id)
{
    struct pressure_data *mdata = data;
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device);

#define ADD_CHANNEL(_val) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scale; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offset; \
    mdata->channel_ ## _val = sol_iio_add_channel(mdata->device, "in_pressure", &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channel_ ## _val, error);

    ADD_CHANNEL(val);

#undef ADD_CHANNEL

    sol_iio_device_start_buffer(mdata->device);

    return;

error:
    SOL_WRN("Could not create iio/pressure node. Failed to open IIO device %d",
        device_id);

    sol_iio_close(mdata->device);
}

static int
pressure_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct pressure_data *mdata = data;
    const struct sol_flow_node_type_iio_pressure_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_PRESSURE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_pressure_options *)options;

    mdata->buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->config.trigger_name, -ENOMEM);
    }

    mdata->config.buffer_size = opts->buffer_size;
    mdata->config.sampling_frequency = opts->sampling_frequency;
    if (mdata->buffer_enabled) {
        mdata->config.sol_iio_reader_cb = pressure_reader_cb;
        mdata->config.data = node;
    }
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->out_range = opts->out_range;

    if (!sol_iio_address_device(opts->iio_device, pressure_create_device_cb, mdata)) {
        SOL_WRN("Could not create iio/pressure node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    return 0;

err:
    free((char *)mdata->config.trigger_name);
    return -EINVAL;

}

static void
pressure_close(struct sol_flow_node *node, void *data)
{
    struct pressure_data *mdata = data;

    free((char *)mdata->config.trigger_name);
    if (mdata->device)
        sol_iio_close(mdata->device);
}

static int
pressure_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const char *errmsg = "Could not read channel values";
    struct pressure_data *mdata = data;

    if (mdata->buffer_enabled) {
        if (!sol_iio_device_trigger_now(mdata->device))
            goto error;
    } else {
        pressure_reader_cb(mdata, mdata->device);
    }

    return 0;

error:
    sol_flow_send_error_packet(node, EIO, "%s", errmsg);
    SOL_WRN("%s", errmsg);

    return -EIO;
}

struct color_data {
    struct sol_iio_config config;
    double scale;
    double offset;
    struct sol_drange_spec out_range;
    struct sol_iio_device *device;
    struct sol_iio_channel *channel_red;
    struct sol_iio_channel *channel_green;
    struct sol_iio_channel *channel_blue;
    bool buffer_enabled : 1;
    bool use_device_default_scale : 1;
    bool use_device_default_offset : 1;
};

static void
color_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct color_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_rgb out = {
        .red_max = mdata->out_range.max,
        .green_max = mdata->out_range.max,
        .blue_max = mdata->out_range.max
    };
    double tmp;
    bool b;

    b = sol_iio_read_channel_value(mdata->channel_red, &tmp);
    if (!b || tmp < 0 || tmp > UINT32_MAX) goto error;
    out.red = tmp;

    b = sol_iio_read_channel_value(mdata->channel_green, &tmp);
    if (!b || tmp < 0 || tmp > UINT32_MAX) goto error;
    out.green = tmp;

    b = sol_iio_read_channel_value(mdata->channel_blue, &tmp);
    if (!b || tmp < 0 || tmp > UINT32_MAX) goto error;
    out.blue = tmp;

    sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_COLOR__OUT__OUT, &out);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static void
color_create_device_cb(void *data, int device_id)
{
    struct color_data *mdata = data;
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device);

#define ADD_CHANNEL(_axis) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scale; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offset; \
    mdata->channel_ ## _axis = sol_iio_add_channel(mdata->device, "in_intensity_" # _axis, &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channel_ ## _axis, error);

    ADD_CHANNEL(red);
    ADD_CHANNEL(green);
    ADD_CHANNEL(blue);

#undef ADD_CHANNEL

    sol_iio_device_start_buffer(mdata->device);

    return;

error:
    SOL_WRN("Could not create iio/color node. Failed to open IIO device %d",
        device_id);
    sol_iio_close(mdata->device);
}

static int
color_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct color_data *mdata = data;
    const struct sol_flow_node_type_iio_color_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_COLOR_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_color_options *)options;

    mdata->buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->config.trigger_name, -ENOMEM);
    }

    mdata->config.buffer_size = opts->buffer_size;
    mdata->config.sampling_frequency = opts->sampling_frequency;
    if (mdata->buffer_enabled) {
        mdata->config.sol_iio_reader_cb = color_reader_cb;
        mdata->config.data = node;
    }
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->out_range = opts->out_range;

    if (!sol_iio_address_device(opts->iio_device, color_create_device_cb, mdata)) {
        SOL_WRN("Could not create iio/color node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    return 0;

err:
    free((char *)mdata->config.trigger_name);
    return -EINVAL;
}

static void
color_close(struct sol_flow_node *node, void *data)
{
    struct color_data *mdata = data;

    free((char *)mdata->config.trigger_name);
    if (mdata->device)
        sol_iio_close(mdata->device);
}

static int
color_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const char *errmsg = "Could not read channel values";
    struct color_data *mdata = data;

    if (mdata->buffer_enabled) {
        if (!sol_iio_device_trigger_now(mdata->device))
            goto error;
    } else {
        color_reader_cb(mdata, mdata->device);
    }

    return 0;

error:
    sol_flow_send_error_packet(node, EIO, "%s", errmsg);
    SOL_WRN("%s", errmsg);

    return -EIO;
}

struct accelerate_data {
    struct sol_iio_config config;
    struct sol_direction_vector scale;
    struct sol_direction_vector offset;
    struct sol_drange_spec out_range;
    struct sol_iio_device *device;
    struct sol_iio_channel *channel_x;
    struct sol_iio_channel *channel_y;
    struct sol_iio_channel *channel_z;
    bool buffer_enabled : 1;
    bool use_device_default_scale : 1;
    bool use_device_default_offset : 1;
};

static void
accelerate_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct accelerate_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_direction_vector out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channel_x, &out.x);
    if (!b) goto error;

    b = sol_iio_read_channel_value(mdata->channel_y, &out.y);
    if (!b) goto error;

    b = sol_iio_read_channel_value(mdata->channel_z, &out.z);
    if (!b) goto error;

    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_ACCELERATE__OUT__OUT, &out);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static void
accelerate_create_device_cb(void *data, int device_id)
{
    struct accelerate_data *mdata = data;
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device);

#define ADD_CHANNEL(_axis) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scale._axis; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offset._axis; \
    mdata->channel_ ## _axis = sol_iio_add_channel(mdata->device, "in_accel_" # _axis, &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channel_ ## _axis, error);

    ADD_CHANNEL(x);
    ADD_CHANNEL(y);
    ADD_CHANNEL(z);

#undef ADD_CHANNEL

    sol_iio_device_start_buffer(mdata->device);

    return;

error:
    SOL_WRN("Could not create iio/accelerate node. Failed to open IIO device %d",
        device_id);
    sol_iio_close(mdata->device);
}

static int
accelerate_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct accelerate_data *mdata = data;
    const struct sol_flow_node_type_iio_accelerate_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_ACCELERATE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_accelerate_options *)options;

    mdata->buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->config.trigger_name, -ENOMEM);
    }

    mdata->config.buffer_size = opts->buffer_size;
    mdata->config.sampling_frequency = opts->sampling_frequency;
    if (mdata->buffer_enabled) {
        mdata->config.sol_iio_reader_cb = accelerate_reader_cb;
        mdata->config.data = node;
    }
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->out_range = opts->out_range;

    if (!sol_iio_address_device(opts->iio_device, accelerate_create_device_cb, mdata)) {
        SOL_WRN("Could not create iio/accelerate node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    return 0;

err:
    free((char *)mdata->config.trigger_name);
    return -EINVAL;
}

static void
accelerate_close(struct sol_flow_node *node, void *data)
{
    struct accelerate_data *mdata = data;

    free((char *)mdata->config.trigger_name);
    if (mdata->device)
        sol_iio_close(mdata->device);
}

static int
accelerate_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const char *errmsg = "Could not read channel values";
    struct accelerate_data *mdata = data;

    if (mdata->buffer_enabled) {
        if (!sol_iio_device_trigger_now(mdata->device))
            goto error;
    } else {
        accelerate_reader_cb(mdata, mdata->device);
    }

    return 0;

error:
    sol_flow_send_error_packet(node, EIO, "%s", errmsg);
    SOL_WRN("%s", errmsg);

    return -EIO;
}

struct humidity_data {
    struct sol_iio_config config;
    struct sol_drange_spec out_range;
    double scale;
    double offset;
    struct sol_iio_device *device;
    struct sol_iio_channel *channel_val;
    bool buffer_enabled : 1;
    bool use_device_default_scale : 1;
    bool use_device_default_offset : 1;
};

static void
humidity_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct humidity_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max,
        .step = mdata->out_range.step
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channel_val, &out.val);
    if (!b) goto error;

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_HUMIDITY__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static void
humidity_create_device_cb(void *data, int device_id)
{
    struct humidity_data *mdata = data;
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device);

#define ADD_CHANNEL(_val) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scale; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offset; \
    mdata->channel_ ## _val = sol_iio_add_channel(mdata->device, "in_humidityrelative", &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channel_ ## _val, error);

    ADD_CHANNEL(val);

#undef ADD_CHANNEL

    sol_iio_device_start_buffer(mdata->device);

    return;

error:
    SOL_WRN("Could not create iio/humidity node. Failed to open IIO device %d",
        device_id);

    sol_iio_close(mdata->device);
}

static int
humidity_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct humidity_data *mdata = data;
    const struct sol_flow_node_type_iio_humidity_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_HUMIDITY_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_humidity_options *)options;

    mdata->buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->config.trigger_name, -ENOMEM);
    }

    mdata->config.buffer_size = opts->buffer_size;
    mdata->config.sampling_frequency = opts->sampling_frequency;
    if (mdata->buffer_enabled) {
        mdata->config.sol_iio_reader_cb = humidity_reader_cb;
        mdata->config.data = node;
    }
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->out_range = opts->out_range;

    if (!sol_iio_address_device(opts->iio_device, humidity_create_device_cb, mdata)) {
        SOL_WRN("Could not create iio/humidity node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    return 0;

err:
    free((char *)mdata->config.trigger_name);
    return -EINVAL;

}

static void
humidity_close(struct sol_flow_node *node, void *data)
{
    struct humidity_data *mdata = data;

    free((char *)mdata->config.trigger_name);
    if (mdata->device)
        sol_iio_close(mdata->device);
}

static int
humidity_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const char *errmsg = "Could not read channel values";
    struct humidity_data *mdata = data;

    if (mdata->buffer_enabled) {
        if (!sol_iio_device_trigger_now(mdata->device))
            goto error;
    } else {
        humidity_reader_cb(mdata, mdata->device);
    }

    return 0;

error:
    sol_flow_send_error_packet(node, EIO, "%s", errmsg);
    SOL_WRN("%s", errmsg);

    return -EIO;
}

struct adc_data {
    struct sol_iio_config config;
    struct sol_drange_spec out_range;
    double scale;
    double offset;
    struct sol_iio_device *device;
    struct sol_iio_channel *channel_val;
    bool buffer_enabled : 1;
    bool use_device_default_scale : 1;
    bool use_device_default_offset : 1;
};

static void
adc_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct adc_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max,
        .step = mdata->out_range.step
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channel_val, &out.val);
    if (!b) goto error;

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_ADC__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static void
adc_create_device_cb(void *data, int device_id)
{
    struct adc_data *mdata = data;
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device);

#define ADD_CHANNEL(_val) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scale; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offset; \
    mdata->channel_ ## _val = sol_iio_add_channel(mdata->device, "in_voltage0", &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channel_ ## _val, error);

    ADD_CHANNEL(val);

#undef ADD_CHANNEL

    sol_iio_device_start_buffer(mdata->device);

    return;

error:
    SOL_WRN("Could not create iio/adc node. Failed to open IIO device %d",
        device_id);

    sol_iio_close(mdata->device);
}

static int
adc_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct adc_data *mdata = data;
    const struct sol_flow_node_type_iio_adc_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_ADC_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_adc_options *)options;

    mdata->buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->config.trigger_name, -ENOMEM);
    }

    mdata->config.buffer_size = opts->buffer_size;
    mdata->config.sampling_frequency = opts->sampling_frequency;
    if (mdata->buffer_enabled) {
        mdata->config.sol_iio_reader_cb = adc_reader_cb;
        mdata->config.data = node;
    }
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->out_range = opts->out_range;

    if (!sol_iio_address_device(opts->iio_device, adc_create_device_cb, mdata)) {
        SOL_WRN("Could not create iio/adc node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    return 0;

err:
    free((char *)mdata->config.trigger_name);
    return -EINVAL;

}

static void
adc_close(struct sol_flow_node *node, void *data)
{
    struct adc_data *mdata = data;

    free((char *)mdata->config.trigger_name);
    if (mdata->device)
        sol_iio_close(mdata->device);
}

static int
adc_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const char *errmsg = "Could not read channel values";
    struct adc_data *mdata = data;

    if (mdata->buffer_enabled) {
        if (!sol_iio_device_trigger_now(mdata->device))
            goto error;
    } else {
        adc_reader_cb(mdata, mdata->device);
    }

    return 0;

error:
    sol_flow_send_error_packet(node, EIO, "%s", errmsg);
    SOL_WRN("%s", errmsg);

    return -EIO;
}

#include "iio-gen.c"
