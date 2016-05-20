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


#define MAX_CHANNEL         (3)

struct iiodevice_data {
    struct sol_iio_config config;
    struct sol_drange_spec out_range;
    struct sol_iio_device *device;
    uint8_t channel_count;
    bool buffer_enabled : 1;
    bool use_device_default_scale : 1;
    bool use_device_default_offset : 1;
    struct sol_iio_channel *channels[MAX_CHANNEL];
    double scales[MAX_CHANNEL];
    double offsets[MAX_CHANNEL];
};

static void
reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct iiodevice_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_direction_vector out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channels[0], &out.x);
    if (!b) goto error;

    b = sol_iio_read_channel_value(mdata->channels[1], &out.y);
    if (!b) goto error;

    b = sol_iio_read_channel_value(mdata->channels[2], &out.z);
    if (!b) goto error;

    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_GYROSCOPE__OUT__OUT, &out);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
create_device_channels(struct iiodevice_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

#define ADD_CHANNEL(_axis, _index) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scales[_index]; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offsets[_index]; \
    mdata->channels[_index] = sol_iio_add_channel(mdata->device, "in_anglvel_" # _axis, &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channels[_index], error);

    ADD_CHANNEL(x, 0);
    ADD_CHANNEL(y, 1);
    ADD_CHANNEL(z, 2);

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
    struct iiodevice_data *mdata = data;
    const struct sol_flow_node_type_iio_gyroscope_options *opts;
    int device_id;

    SOL_WRN("%s-%s:%s\n", __func__, __DATE__, __TIME__);

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

    mdata->config.sol_iio_reader_cb = reader_cb;
    mdata->config.data = node;

    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scales[0] = opts->scale.x;
    mdata->scales[1] = opts->scale.y;
    mdata->scales[2] = opts->scale.z;
    mdata->offsets[0] = opts->offset.x;
    mdata->offsets[1] = opts->offset.y;
    mdata->offsets[2] = opts->offset.z;
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
iiodevice_close(struct sol_flow_node *node, void *data)
{
    struct iiodevice_data *mdata = data;

    free((char *)mdata->config.trigger_name);
    if (mdata->device)
        sol_iio_close(mdata->device);
}

static int
iiodevice_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const char *errmsg = "Could not read channel values";
    struct iiodevice_data *mdata = data;

    SOL_WRN("%s mdata=%p \n", __func__, mdata);

    if (mdata->buffer_enabled) {
        if (!sol_iio_device_trigger_now(mdata->device))
            goto error;
    } else {
        mdata->config.sol_iio_reader_cb(node, mdata->device);
    }

    return 0;

error:
    sol_flow_send_error_packet(node, EIO, "%s", errmsg);
    SOL_WRN("%s", errmsg);

    return -EIO;
}

static void
magnet_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct iiodevice_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_direction_vector out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channels[0], &out.x);
    if (!b) goto error;

    b = sol_iio_read_channel_value(mdata->channels[1], &out.y);
    if (!b) goto error;

    b = sol_iio_read_channel_value(mdata->channels[2], &out.z);
    if (!b) goto error;

    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_MAGNETOMETER__OUT__OUT, &out);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
magnet_create_channels(struct iiodevice_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

#define ADD_CHANNEL(_axis, _index) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scales[_index]; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offsets[_index]; \
    mdata->channels[_index] = sol_iio_add_channel(mdata->device, "in_magn_" # _axis, &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channels[_index], error);

    ADD_CHANNEL(x, 0);
    ADD_CHANNEL(y, 1);
    ADD_CHANNEL(z, 2);

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
    struct iiodevice_data *mdata = data;
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
    mdata->config.sol_iio_reader_cb = magnet_reader_cb;
    mdata->config.data = node;
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scales[0] = opts->scale.x;
    mdata->scales[1] = opts->scale.y;
    mdata->scales[2] = opts->scale.z;
    mdata->offsets[0] = opts->offset.x;
    mdata->offsets[1] = opts->offset.y;
    mdata->offsets[2] = opts->offset.z;
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
temp_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct iiodevice_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max,
        .step = mdata->out_range.step
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channels[0], &out.val);
    if (!b) goto error;

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_THERMOMETER__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
temp_create_channels(struct iiodevice_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

#define ADD_CHANNEL(_val, _index) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scales[_index]; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offsets[_index]; \
    mdata->channels[_index] = sol_iio_add_channel(mdata->device, "in_temp", &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channels[_index], error);

    ADD_CHANNEL(val, 0);

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
    struct iiodevice_data *mdata = data;
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
    mdata->config.sol_iio_reader_cb = temp_reader_cb;
    mdata->config.data = node;
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scales[0] = opts->scale;
    mdata->offsets[0] = opts->offset;
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
pressure_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct iiodevice_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max,
        .step = mdata->out_range.step
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channels[0], &out.val);
    if (!b) goto error;

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_PRESSURE_SENSOR__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
pressure_create_channels(struct iiodevice_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

#define ADD_CHANNEL(_val, _index) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scales[_index]; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offsets[_index]; \
    mdata->channels[_index] = sol_iio_add_channel(mdata->device, "in_pressure", &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channels[_index], error);

    ADD_CHANNEL(val, 0);

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
    struct iiodevice_data *mdata = data;
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
    mdata->config.sol_iio_reader_cb = pressure_reader_cb;
    mdata->config.data = node;
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scales[0] = opts->scale;
    mdata->offsets[0] = opts->offset;
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
color_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct iiodevice_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_rgb out = {
        .red_max = mdata->out_range.max,
        .green_max = mdata->out_range.max,
        .blue_max = mdata->out_range.max
    };
    double tmp;
    bool b;

    b = sol_iio_read_channel_value(mdata->channels[0], &tmp);
    if (!b || tmp < 0 || tmp > UINT32_MAX) goto error;
    out.red = tmp;

    b = sol_iio_read_channel_value(mdata->channels[1], &tmp);
    if (!b || tmp < 0 || tmp > UINT32_MAX) goto error;
    out.green = tmp;

    b = sol_iio_read_channel_value(mdata->channels[2], &tmp);
    if (!b || tmp < 0 || tmp > UINT32_MAX) goto error;
    out.blue = tmp;

    sol_flow_send_rgb_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_COLOR_SENSOR__OUT__OUT, &out);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
color_create_channels(struct iiodevice_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

#define ADD_CHANNEL(_axis, _index) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scales[_index]; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offsets[_index]; \
    mdata->channels[_index] = sol_iio_add_channel(mdata->device, "in_intensity_" # _axis, &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channels[_index], error);

    ADD_CHANNEL(red, 0);
    ADD_CHANNEL(green, 1);
    ADD_CHANNEL(blue, 2);

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
    struct iiodevice_data *mdata = data;
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
    mdata->config.sol_iio_reader_cb = color_reader_cb;
    mdata->config.data = node;
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scales[0] = opts->scale.x;
    mdata->scales[1] = opts->scale.y;
    mdata->scales[2] = opts->scale.z;
    mdata->offsets[0] = opts->offset.x;
    mdata->offsets[1] = opts->offset.y;
    mdata->offsets[2] = opts->offset.z;

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
accelerate_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct iiodevice_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_direction_vector out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channels[0], &out.x);
    if (!b) goto error;

    b = sol_iio_read_channel_value(mdata->channels[1], &out.y);
    if (!b) goto error;

    b = sol_iio_read_channel_value(mdata->channels[2], &out.z);
    if (!b) goto error;

    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_ACCELEROMETER__OUT__OUT, &out);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
accelerate_create_channels(struct iiodevice_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

#define ADD_CHANNEL(_axis, _index) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scales[_index]; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offsets[_index]; \
    mdata->channels[_index] = sol_iio_add_channel(mdata->device, "in_accel_" # _axis, &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channels[_index], error);

    ADD_CHANNEL(x, 0);
    ADD_CHANNEL(y, 1);
    ADD_CHANNEL(z, 2);

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
    struct iiodevice_data *mdata = data;
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
    mdata->config.sol_iio_reader_cb = accelerate_reader_cb;
    mdata->config.data = node;
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;

    mdata->scales[0] = opts->scale.x;
    mdata->scales[1] = opts->scale.y;
    mdata->scales[2] = opts->scale.z;
    mdata->offsets[0] = opts->offset.x;
    mdata->offsets[1] = opts->offset.y;
    mdata->offsets[2] = opts->offset.z;

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
humidity_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct iiodevice_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max,
        .step = mdata->out_range.step
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channels[0], &out.val);
    if (!b) goto error;

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_HUMIDITY_SENSOR__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
humidity_create_channels(struct iiodevice_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

#define ADD_CHANNEL(_val, _index) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scales[_index]; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offsets[_index]; \
    mdata->channels[_index] = sol_iio_add_channel(mdata->device, "in_humidityrelative", &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channels[_index], error);

    ADD_CHANNEL(val, 0);

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
    struct iiodevice_data *mdata = data;
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
    mdata->config.sol_iio_reader_cb = humidity_reader_cb;
    mdata->config.data = node;
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scales[0] = opts->scale;
    mdata->offsets[0] = opts->offset;
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
adc_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct iiodevice_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max,
        .step = mdata->out_range.step
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channels[0], &out.val);
    if (!b) goto error;

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_ADC__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
adc_create_channels(struct iiodevice_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

#define ADD_CHANNEL(_val, _index) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scales[_index]; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offsets[_index]; \
    mdata->channels[_index] = sol_iio_add_channel(mdata->device, "in_voltage0", &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channels[_index], error);

    ADD_CHANNEL(val, 0);

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
    struct iiodevice_data *mdata = data;
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
    mdata->config.sol_iio_reader_cb = adc_reader_cb;
    mdata->config.data = node;
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scales[0] = opts->scale;
    mdata->offsets[0] = opts->offset;
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
light_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct iiodevice_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max,
        .step = mdata->out_range.step
    };
    bool b;

    SOL_WRN("%s mdata=%p \n", __func__, mdata);

    b = sol_iio_read_channel_value(mdata->channels[0], &out.val);
    if (!b) goto error;

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_LIGHT_SENSOR__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
light_create_channels(struct iiodevice_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

#define ADD_CHANNEL(_val, _index) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scales[_index]; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offsets[_index]; \
    mdata->channels[_index] = sol_iio_add_channel(mdata->device, "in_illuminance0", &channel_config); \
    if (!mdata->channels[_index]) \
        mdata->channels[_index] = sol_iio_add_channel(mdata->device, "in_illuminance", &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channels[_index], error);

    ADD_CHANNEL(val, 0);

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
    struct iiodevice_data *mdata = data;
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
    mdata->config.sol_iio_reader_cb = light_reader_cb;
    mdata->config.data = node;
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scales[0] = opts->scale;
    mdata->offsets[0] = opts->offset;
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
proximity_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct iiodevice_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange out = {
        .min = mdata->out_range.min,
        .max = mdata->out_range.max,
        .step = mdata->out_range.step
    };
    bool b;

    b = sol_iio_read_channel_value(mdata->channels[0], &out.val);
    if (!b) goto error;

    sol_flow_send_drange_value_packet(node,
        SOL_FLOW_NODE_TYPE_IIO_PROXIMITY_SENSOR__OUT__OUT, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
proximity_create_channels(struct iiodevice_data *mdata, int device_id)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    mdata->device = sol_iio_open(device_id, &mdata->config);
    SOL_NULL_CHECK(mdata->device, false);

#define ADD_CHANNEL(_val, _index) \
    if (!mdata->use_device_default_scale) \
        channel_config.scale = mdata->scales[_index]; \
    if (!mdata->use_device_default_offset) \
        channel_config.offset = mdata->offsets[_index]; \
    mdata->channels[_index] = sol_iio_add_channel(mdata->device, "in_proximity", &channel_config); \
    if (!mdata->channels[_index]) \
        mdata->channels[_index] = sol_iio_add_channel(mdata->device, "in_proximity2", &channel_config); \
    SOL_NULL_CHECK_GOTO(mdata->channels[_index], error);

    ADD_CHANNEL(val, 0);

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
    struct iiodevice_data *mdata = data;
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
    mdata->config.sol_iio_reader_cb = proximity_reader_cb;
    mdata->config.data = node;
    mdata->use_device_default_scale = opts->use_device_default_scale;
    mdata->use_device_default_offset = opts->use_device_default_offset;
    mdata->scales[0] = opts->scale;
    mdata->offsets[1] = opts->offset;
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

#include "iio-gen.c"
