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

#include "sol-flow/iio.h"
#include "sol-flow-internal.h"

#include <sol-types.h>
#include <sol-util-internal.h>
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
    int r;

    r = sol_iio_read_channel_value(mdata->channel_x, &out.x);
    SOL_INT_CHECK_GOTO(r, <0, error);

    r = sol_iio_read_channel_value(mdata->channel_y, &out.y);
    SOL_INT_CHECK_GOTO(r, <0, error);

    r = sol_iio_read_channel_value(mdata->channel_z, &out.z);
    SOL_INT_CHECK_GOTO(r, <0, error);

    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_GYROSCOPE__OUT__OUT, &out);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
create_device_channels(struct gyroscope_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

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

    return true;

error:
    SOL_WRN("Could not create iio/gyroscope node. Failed to open IIO device %d",
        device_id);
    sol_iio_close(mdata->device);
    return false;
}

static int
gyroscope_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct gyroscope_data *mdata = data;
    const struct sol_flow_node_type_iio_gyroscope_options *opts;
    int device_id;

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

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/gyroscope node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    if (!create_device_channels(mdata, device_id))
        goto err;

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
        if (sol_iio_device_trigger(mdata->device) < 0)
            goto error;
    } else {
        reader_cb(node, mdata->device);
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
    int r;

    r = sol_iio_read_channel_value(mdata->channel_x, &out.x);
    SOL_INT_CHECK_GOTO(r, <0, error);

    r = sol_iio_read_channel_value(mdata->channel_y, &out.y);
    SOL_INT_CHECK_GOTO(r, <0, error);

    r = sol_iio_read_channel_value(mdata->channel_z, &out.z);
    SOL_INT_CHECK_GOTO(r, <0, error);

    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_MAGNETOMETER__OUT__OUT, &out);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
magnet_create_channels(struct magnet_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

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

    return true;

error:
    SOL_WRN("Could not create iio/magnet node. Failed to open IIO device %d",
        device_id);
    sol_iio_close(mdata->device);
    return false;
}

static int
magnet_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct magnet_data *mdata = data;
    const struct sol_flow_node_type_iio_magnetometer_options *opts;
    int device_id;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_MAGNETOMETER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_magnetometer_options *)options;

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

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/magnet node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    if (!magnet_create_channels(mdata, device_id))
        goto err;

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
        if (sol_iio_device_trigger(mdata->device) < 0)
            goto error;
    } else {
        magnet_reader_cb(node, mdata->device);
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
    int r;

    r = sol_iio_read_channel_value(mdata->channel_val, &out.val);
    SOL_INT_CHECK_GOTO(r, <0, error);

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_THERMOMETER__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
temp_create_channels(struct temperature_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

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

    return true;

error:
    SOL_WRN("Could not create iio/thermometer node. Failed to open"
        " IIO device %d", device_id);

    sol_iio_close(mdata->device);
    return false;
}

static int
temperature_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct temperature_data *mdata = data;
    const struct sol_flow_node_type_iio_thermometer_options *opts;
    int device_id;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_THERMOMETER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_thermometer_options *)options;

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

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/thermometer node. Failed to"
            " open IIO device %s", opts->iio_device);
        goto err;
    }

    if (!temp_create_channels(mdata, device_id))
        goto err;

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
        if (sol_iio_device_trigger(mdata->device) < 0)
            goto error;
    } else {
        temp_reader_cb(node, mdata->device);
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
    int r;

    r = sol_iio_read_channel_value(mdata->channel_val, &out.val);
    SOL_INT_CHECK_GOTO(r, <0, error);

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_PRESSURE_SENSOR__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
pressure_create_channels(struct pressure_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

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

    return true;

error:
    SOL_WRN("Could not create iio/pressure node. Failed to open IIO device %d",
        device_id);

    sol_iio_close(mdata->device);
    return false;
}

static int
pressure_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct pressure_data *mdata = data;
    const struct sol_flow_node_type_iio_pressure_sensor_options *opts;
    int device_id;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_PRESSURE_SENSOR_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_pressure_sensor_options *)options;

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

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/pressure node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    if (!pressure_create_channels(mdata, device_id))
        goto err;

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
        if (sol_iio_device_trigger(mdata->device) < 0)
            goto error;
    } else {
        pressure_reader_cb(node, mdata->device);
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
    int r;

    r = sol_iio_read_channel_value(mdata->channel_red, &tmp);
    if (r < 0|| tmp < 0 || tmp > UINT32_MAX) goto error;
    out.red = tmp;

    r = sol_iio_read_channel_value(mdata->channel_green, &tmp);
    if (r < 0 || tmp < 0 || tmp > UINT32_MAX) goto error;
    out.green = tmp;

    r = sol_iio_read_channel_value(mdata->channel_blue, &tmp);
    if (r < 0 || tmp < 0 || tmp > UINT32_MAX) goto error;
    out.blue = tmp;

    sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_COLOR_SENSOR__OUT__OUT, &out);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
color_create_channels(struct color_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

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

    return true;

error:
    SOL_WRN("Could not create iio/color-sensor node. Failed to open IIO"
        " device %d", device_id);
    sol_iio_close(mdata->device);
    return false;
}

static int
color_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct color_data *mdata = data;
    const struct sol_flow_node_type_iio_color_sensor_options *opts;
    int device_id;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_COLOR_SENSOR_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_color_sensor_options *)options;

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

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/color-sensor node. Failed to open"
            " IIO device %s", opts->iio_device);
        goto err;
    }

    if (!color_create_channels(mdata, device_id))
        goto err;

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
        if (sol_iio_device_trigger(mdata->device) < 0)
            goto error;
    } else {
        color_reader_cb(node, mdata->device);
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
    int r;

    r = sol_iio_read_channel_value(mdata->channel_x, &out.x);
    SOL_INT_CHECK_GOTO(r, <0, error);

    r = sol_iio_read_channel_value(mdata->channel_y, &out.y);
    SOL_INT_CHECK_GOTO(r, <0, error);

    r = sol_iio_read_channel_value(mdata->channel_z, &out.z);
    SOL_INT_CHECK_GOTO(r, <0, error);

    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_ACCELEROMETER__OUT__OUT, &out);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
accelerate_create_channels(struct accelerate_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

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

    return true;

error:
    SOL_WRN("Could not create iio/accelerate node. Failed to open IIO device %d",
        device_id);
    sol_iio_close(mdata->device);
    return false;
}

static int
accelerate_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct accelerate_data *mdata = data;
    const struct sol_flow_node_type_iio_accelerometer_options *opts;
    int device_id;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_ACCELEROMETER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_accelerometer_options *)options;

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

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/accelerate node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    if (!accelerate_create_channels(mdata, device_id))
        goto err;

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
        if (sol_iio_device_trigger(mdata->device) < 0)
            goto error;
    } else {
        accelerate_reader_cb(node, mdata->device);
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
    int r;

    r = sol_iio_read_channel_value(mdata->channel_val, &out.val);
    SOL_INT_CHECK_GOTO(r, <0, error);

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_HUMIDITY_SENSOR__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
humidity_create_channels(struct humidity_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

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

    return true;

error:
    SOL_WRN("Could not create iio/humidity node. Failed to open IIO device %d",
        device_id);

    sol_iio_close(mdata->device);
    return false;
}

static int
humidity_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct humidity_data *mdata = data;
    const struct sol_flow_node_type_iio_humidity_sensor_options *opts;
    int device_id;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_HUMIDITY_SENSOR_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_humidity_sensor_options *)options;

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

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/humidity node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    if (!humidity_create_channels(mdata, device_id))
        goto err;

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
        if (sol_iio_device_trigger(mdata->device) < 0)
            goto error;
    } else {
        humidity_reader_cb(node, mdata->device);
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
    int r;

    r = sol_iio_read_channel_value(mdata->channel_val, &out.val);
    SOL_INT_CHECK_GOTO(r, <0, error);

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_ADC__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
adc_create_channels(struct adc_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

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

    return true;

error:
    SOL_WRN("Could not create iio/adc node. Failed to open IIO device %d",
        device_id);

    sol_iio_close(mdata->device);
    return false;
}

static int
adc_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct adc_data *mdata = data;
    const struct sol_flow_node_type_iio_adc_options *opts;
    int device_id;

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

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/adc node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    if (!adc_create_channels(mdata, device_id))
        goto err;

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
        if (sol_iio_device_trigger(mdata->device) < 0)
            goto error;
    } else {
        adc_reader_cb(node, mdata->device);
    }

    return 0;

error:
    sol_flow_send_error_packet(node, EIO, "%s", errmsg);
    SOL_WRN("%s", errmsg);

    return -EIO;
}

struct light_data {
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
light_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct light_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max,
        .step = mdata->out_range.step
    };
    int r;

    r = sol_iio_read_channel_value(mdata->channel_val, &out.val);
    SOL_INT_CHECK_GOTO(r, <0, error);

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_LIGHT_SENSOR__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
light_create_channels(struct light_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

#define ADD_CHANNEL(_val) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scale; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offset; \
    mdata->channel_ ## _val = sol_iio_add_channel(mdata->device, "in_illuminance0", &channel_config); \
    if (!mdata->channel_ ## _val) \
        mdata->channel_ ## _val = sol_iio_add_channel(mdata->device, "in_illuminance", &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channel_ ## _val, error);

    ADD_CHANNEL(val);

#undef ADD_CHANNEL

    sol_iio_device_start_buffer(mdata->device);

    return true;

error:
    SOL_WRN("Could not create iio/light-sensor node. Failed to open IIO"
        " device %d", device_id);

    sol_iio_close(mdata->device);
    return false;
}

static int
light_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct light_data *mdata = data;
    const struct sol_flow_node_type_iio_light_sensor_options *opts;
    int device_id;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_LIGHT_SENSOR_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_light_sensor_options *)options;

    mdata->buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->config.trigger_name, -ENOMEM);
    }

    mdata->config.buffer_size = opts->buffer_size;
    mdata->config.sampling_frequency = opts->sampling_frequency;
    if (mdata->buffer_enabled) {
        mdata->config.sol_iio_reader_cb = light_reader_cb;
        mdata->config.data = node;
    }
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->out_range = opts->out_range;

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/light-sensor node. Failed to open"
            " IIO device %s", opts->iio_device);
        goto err;
    }

    if (!light_create_channels(mdata, device_id))
        goto err;

    return 0;

err:
    free((char *)mdata->config.trigger_name);
    return -EINVAL;

}

static void
light_close(struct sol_flow_node *node, void *data)
{
    struct light_data *mdata = data;

    free((char *)mdata->config.trigger_name);
    if (mdata->device)
        sol_iio_close(mdata->device);
}

static int
light_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const char *errmsg = "Could not read channel values";
    struct light_data *mdata = data;

    if (mdata->buffer_enabled) {
        if (sol_iio_device_trigger(mdata->device) < 0)
            goto error;
    } else {
        light_reader_cb(node, mdata->device);
    }

    return 0;

error:
    sol_flow_send_error_packet(node, EIO, "%s", errmsg);
    SOL_WRN("%s", errmsg);

    return -EIO;
}

struct proximity_data {
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
proximity_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct proximity_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max,
        .step = mdata->out_range.step
    };
    int r;

    r = sol_iio_read_channel_value(mdata->channel_val, &out.val);
    SOL_INT_CHECK_GOTO(r, <0, error);

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_PROXIMITY_SENSOR__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
proximity_create_channels(struct proximity_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

#define ADD_CHANNEL(_val) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scale; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offset; \
    mdata->channel_ ## _val = sol_iio_add_channel(mdata->device, "in_proximity", &channel_config); \
    if (!mdata->channel_ ## _val) \
        mdata->channel_ ## _val = sol_iio_add_channel(mdata->device, "in_proximity2", &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channel_ ## _val, error);

    ADD_CHANNEL(val);

#undef ADD_CHANNEL

    sol_iio_device_start_buffer(mdata->device);

    return true;

error:
    SOL_WRN("Could not create iio/proximity-sensor node. Failed to open"
        " IIO device %d", device_id);

    sol_iio_close(mdata->device);
    return false;
}

static int
proximity_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct proximity_data *mdata = data;
    const struct sol_flow_node_type_iio_proximity_sensor_options *opts;
    int device_id;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_PROXIMITY_SENSOR_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_proximity_sensor_options *)options;

    mdata->buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->config.trigger_name, -ENOMEM);
    }

    mdata->config.buffer_size = opts->buffer_size;
    mdata->config.sampling_frequency = opts->sampling_frequency;
    if (mdata->buffer_enabled) {
        mdata->config.sol_iio_reader_cb = proximity_reader_cb;
        mdata->config.data = node;
    }
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->out_range = opts->out_range;

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/proximity-sensor node. Failed to open"
            " IIO device %s", opts->iio_device);
        goto err;
    }

    if (!proximity_create_channels(mdata, device_id))
        goto err;

    return 0;

err:
    free((char *)mdata->config.trigger_name);
    return -EINVAL;

}

static void
proximity_close(struct sol_flow_node *node, void *data)
{
    struct proximity_data *mdata = data;

    free((char *)mdata->config.trigger_name);
    if (mdata->device)
        sol_iio_close(mdata->device);
}

static int
proximity_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const char *errmsg = "Could not read channel values";
    struct proximity_data *mdata = data;

    if (mdata->buffer_enabled) {
        if (sol_iio_device_trigger(mdata->device) < 0)
            goto error;
    } else {
        proximity_reader_cb(node, mdata->device);
    }

    return 0;

error:
    sol_flow_send_error_packet(node, EIO, "%s", errmsg);
    SOL_WRN("%s", errmsg);

    return -EIO;
}

#include "iio-gen.c"
