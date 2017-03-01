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

#include "sol-flow/iio.h"
#include "sol-flow-internal.h"

#include <sol-types.h>
#include <sol-util-internal.h>
#include <errno.h>

#include <sol-iio.h>

#define GEN_CHANNEL_NAME(_channel_name, _name, _error, _id) \
    if (_id >= 0) { \
        int _ret; \
        _ret = snprintf(_channel_name, sizeof(_channel_name), _name "%d", _id); \
        SOL_EXP_CHECK_GOTO(_ret < 0 || _ret >= (int)sizeof(_channel_name), _error); \
    } else { \
        strncpy(_channel_name, _name, sizeof(_channel_name) - 1); \
        _channel_name[sizeof(_channel_name) - 1] = '\0'; \
    }

#define GEN_SOL_STR_TABLE(key, len, val, name, value) \
    do { \
        key = strdup(name); \
        SOL_NULL_CHECK(key, -ENOMEM); \
        len = strlen(key); \
        val = value; \
    } while (0)

struct iio_device_config {
    struct sol_iio_config config;
    struct sol_drange_spec out_range;
    struct sol_iio_device *device;
    bool buffer_enabled : 1;
    bool use_device_default_scale : 1;
    bool use_device_default_offset : 1;
    enum iio_data_type {
        DOUBLE, DIRECTION_VECTOR, COLOR,
    } data_type;
};

/* Make sure the iio_device_config is the first element */
struct iio_double_data {
    struct iio_device_config iio_base;
    double scale;
    double offset;
    struct sol_iio_channel *channel_val;
};

struct iio_direction_vector_data {
    struct iio_device_config iio_base;
    struct sol_direction_vector scale;
    struct sol_direction_vector offset;
    struct sol_iio_channel *channel_x;
    struct sol_iio_channel *channel_y;
    struct sol_iio_channel *channel_z;
};

struct iio_color_data {
    struct iio_device_config iio_base;
    double scale_red;
    double offset_red;
    double scale_green;
    double offset_green;
    double scale_blue;
    double offset_blue;
    struct sol_iio_channel *channel_red;
    struct sol_iio_channel *channel_green;
    struct sol_iio_channel *channel_blue;
};

struct iio_node_type {
    struct sol_flow_node_type base;
    uint16_t out_port;
    uint16_t scale_port;
    uint16_t scale_red_port;
    uint16_t scale_green_port;
    uint16_t scale_blue_port;
    uint16_t offset_port;
    uint16_t offset_red_port;
    uint16_t offset_green_port;
    uint16_t offset_blue_port;
    uint16_t sampling_frequency_port;
    void (*reader_cb)(void *data, struct sol_iio_device *device);
};

static inline struct sol_iio_channel *
iio_add_channel(double scale, double offset, const char *name, struct iio_device_config *base)
{
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;

    if (!base->use_device_default_scale)
        channel_config.scale = scale;

    if (!base->use_device_default_offset)
        channel_config.offset = offset;

    return sol_iio_add_channel(base->device, name, &channel_config);
}

static void
iio_common_close(struct sol_flow_node *node, void *data)
{
    struct iio_device_config *mdata = data;
    struct sol_str_table *iter;

    free((char *)mdata->config.trigger_name);

    if (!mdata->config.oversampling_ratio_table)
        goto end;

    for (iter = mdata->config.oversampling_ratio_table; iter->key; iter++)
        free((void *)iter->key);

    free(mdata->config.oversampling_ratio_table);

end:
    if (mdata->device)
        sol_iio_close(mdata->device);
}

static int
iio_common_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const char *errmsg = "Could not read channel values";
    struct iio_device_config *mdata = data;
    struct iio_node_type *type;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    if (mdata->buffer_enabled) {
        if (sol_iio_device_trigger(mdata->device) < 0)
            goto error;
    } else {
        type->reader_cb(node, mdata->device);
    }

    return 0;

error:
    sol_flow_send_error_packet(node, EIO, "%s", errmsg);
    SOL_WRN("%s reader_cb=%p", errmsg, type->reader_cb);

    return -EIO;
}

static int
iio_get_info(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    static const char *errmsg = "Could not read configuration attribute";
    struct iio_device_config *device_config = data;
    struct sol_iio_device *device = device_config->device;
    struct iio_node_type *type = (struct iio_node_type *)sol_flow_node_get_type(node);
    char *sampling_frequency_name = device_config->config.sampling_frequency_name;
    int frequency, r;

    if (device_config->data_type == DOUBLE) {
        struct iio_double_data *mdata = data;
        const char *name;
        double value;

        name = sol_iio_channel_get_name(mdata->channel_val);
        SOL_NULL_CHECK_GOTO(name, error);

        r = sol_iio_device_get_scale(device, name, &value);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        r = sol_flow_send_drange_value_packet(node, type->scale_port, value);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        r = sol_iio_device_get_offset(device, name, &value);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        r = sol_flow_send_drange_value_packet(node, type->offset_port, value);
        SOL_INT_CHECK_GOTO(r, < 0, error);
    } else if (device_config->data_type == DIRECTION_VECTOR) {
        struct iio_direction_vector_data *mdata = data;
        const char *name_x, *name_y, *name_z;
        double value_x, value_y, value_z;

        name_x = sol_iio_channel_get_name(mdata->channel_x);
        SOL_NULL_CHECK_GOTO(name_x, error);
        name_y = sol_iio_channel_get_name(mdata->channel_y);
        SOL_NULL_CHECK_GOTO(name_y, error);
        name_z = sol_iio_channel_get_name(mdata->channel_z);
        SOL_NULL_CHECK_GOTO(name_z, error);

        r = sol_iio_device_get_scale(device, name_x, &value_x);
        SOL_INT_CHECK_GOTO(r, < 0, error);
        r = sol_iio_device_get_scale(device, name_y, &value_y);
        SOL_INT_CHECK_GOTO(r, < 0, error);
        r = sol_iio_device_get_scale(device, name_z, &value_z);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        r = sol_flow_send_direction_vector_components_packet(node,
            type->scale_port, value_x, value_y, value_z);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        r = sol_iio_device_get_offset(device, name_x, &value_x);
        SOL_INT_CHECK_GOTO(r, < 0, error);
        r = sol_iio_device_get_offset(device, name_y, &value_y);
        SOL_INT_CHECK_GOTO(r, < 0, error);
        r = sol_iio_device_get_offset(device, name_z, &value_z);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        r = sol_flow_send_direction_vector_components_packet(node,
            type->offset_port, value_x, value_y, value_z);
        SOL_INT_CHECK_GOTO(r, < 0, error);
    } else if (device_config->data_type == COLOR) {
        struct iio_color_data *mdata = data;
        const char *name_red, *name_green, *name_blue;
        double value_red, value_green, value_blue;

        name_red = sol_iio_channel_get_name(mdata->channel_red);
        SOL_NULL_CHECK_GOTO(name_red, error);
        name_green = sol_iio_channel_get_name(mdata->channel_green);
        SOL_NULL_CHECK_GOTO(name_green, error);
        name_blue = sol_iio_channel_get_name(mdata->channel_blue);
        SOL_NULL_CHECK_GOTO(name_blue, error);

        r = sol_iio_device_get_scale(device, name_red, &value_red);
        SOL_INT_CHECK_GOTO(r, < 0, error);
        r = sol_flow_send_drange_value_packet(node, type->scale_red_port, value_red);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        r = sol_iio_device_get_scale(device, name_green, &value_green);
        SOL_INT_CHECK_GOTO(r, < 0, error);
        r = sol_flow_send_drange_value_packet(node, type->scale_green_port, value_green);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        r = sol_iio_device_get_scale(device, name_blue, &value_blue);
        SOL_INT_CHECK_GOTO(r, < 0, error);
        r = sol_flow_send_drange_value_packet(node, type->scale_blue_port, value_blue);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        r = sol_iio_device_get_offset(device, name_red, &value_red);
        SOL_INT_CHECK_GOTO(r, < 0, error);
        r = sol_flow_send_drange_value_packet(node, type->offset_red_port, value_red);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        r = sol_iio_device_get_offset(device, name_green, &value_green);
        SOL_INT_CHECK_GOTO(r, < 0, error);
        r = sol_flow_send_drange_value_packet(node, type->offset_green_port, value_green);
        SOL_INT_CHECK_GOTO(r, < 0, error);

        r = sol_iio_device_get_offset(device, name_blue, &value_blue);
        SOL_INT_CHECK_GOTO(r, < 0, error);
        r = sol_flow_send_drange_value_packet(node, type->offset_blue_port, value_blue);
        SOL_INT_CHECK_GOTO(r, < 0, error);
    }

    r = sol_iio_device_get_sampling_frequency(device, sampling_frequency_name, &frequency);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    r = sol_flow_send_irange_value_packet(node, type->sampling_frequency_port, frequency);
    SOL_INT_CHECK_GOTO(r, < 0, error);

error:
    sol_flow_send_error_packet(node, EIO, "%s", errmsg);
    return -EIO;
}

static void
iio_direction_vector_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct iio_direction_vector_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_direction_vector out = {
        .min = mdata->iio_base.out_range.min,
        .max = mdata->iio_base.out_range.max
    };
    int r;
    struct iio_node_type *type;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    r = sol_iio_read_channel_value(mdata->channel_x, &out.x);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    r = sol_iio_read_channel_value(mdata->channel_y, &out.y);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    r = sol_iio_read_channel_value(mdata->channel_z, &out.z);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    SOL_DBG("Before mount_calibration: %f-%f-%f", out.x, out.y, out.z);

    // mount correction
    sol_iio_mount_calibration(device, &out);

    sol_flow_send_direction_vector_packet(node, type->out_port, &out);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static void
iio_double_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct iio_double_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_drange out = {
        .min = mdata->iio_base.out_range.min,
        .max = mdata->iio_base.out_range.max,
        .step = mdata->iio_base.out_range.step
    };

    int r;
    struct iio_node_type *type;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    r = sol_iio_read_channel_value(mdata->channel_val, &out.val);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    sol_flow_send_drange_value_packet(node, type->out_port, out.val);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static void
iio_color_reader_cb(void *data, struct sol_iio_device *device)
{
    static const char *errmsg = "Could not read channel buffer values";
    struct sol_flow_node *node = data;
    struct iio_color_data *mdata = sol_flow_node_get_private_data(node);
    struct sol_rgb out = {
        .red_max = mdata->iio_base.out_range.max,
        .green_max = mdata->iio_base.out_range.max,
        .blue_max = mdata->iio_base.out_range.max
    };
    double tmp;
    int r;
    struct iio_node_type *type;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    r = sol_iio_read_channel_value(mdata->channel_red, &tmp);
    if (r < 0 || tmp < 0 || tmp > UINT32_MAX) goto error;
    out.red = tmp;

    r = sol_iio_read_channel_value(mdata->channel_green, &tmp);
    if (r < 0 || tmp < 0 || tmp > UINT32_MAX) goto error;
    out.green = tmp;

    r = sol_iio_read_channel_value(mdata->channel_blue, &tmp);
    if (r < 0 || tmp < 0 || tmp > UINT32_MAX) goto error;
    out.blue = tmp;

    sol_flow_send_rgb_packet(node, type->out_port, &out);

    return;

error:
    sol_flow_send_error_packet_str(node, EIO, errmsg);
    SOL_WRN("%s", errmsg);
}

static bool
gyroscope_create_channels(struct iio_direction_vector_data *mdata, int device_id)
{
    mdata->iio_base.device = sol_iio_open(device_id, &mdata->iio_base.config);
    SOL_NULL_CHECK(mdata->iio_base.device, false);

    mdata->channel_x = iio_add_channel(mdata->scale.x, mdata->offset.x, "in_anglvel_x", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_x, error);

    mdata->channel_y = iio_add_channel(mdata->scale.y, mdata->offset.y, "in_anglvel_y", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_y, error);

    mdata->channel_z = iio_add_channel(mdata->scale.z, mdata->offset.z, "in_anglvel_z", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_z, error);

    sol_iio_device_start_buffer(mdata->iio_base.device);

    return true;

error:
    SOL_WRN("Could not create iio/gyroscope node. Failed to open IIO device %d",
        device_id);
    sol_iio_close(mdata->iio_base.device);
    return false;
}

static int
gyroscope_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct iio_direction_vector_data *mdata = data;
    const struct sol_flow_node_type_iio_gyroscope_options *opts;
    int device_id, ret;
    struct iio_node_type *type;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_GYROSCOPE_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_gyroscope_options *)options;

    mdata->iio_base.buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->iio_base.config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->iio_base.config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->iio_base.config.trigger_name, -ENOMEM);
    }

    mdata->iio_base.data_type = DIRECTION_VECTOR;
    mdata->iio_base.config.buffer_size = opts->buffer_size;
    mdata->iio_base.config.sampling_frequency = opts->sampling_frequency;
    ret = snprintf(mdata->iio_base.config.sampling_frequency_name,
        sizeof(mdata->iio_base.config.sampling_frequency_name), "%s", "in_anglvel_");
    SOL_EXP_CHECK_GOTO(ret < 0 || ret >= (int)sizeof(mdata->iio_base.config.sampling_frequency_name), err);

    if (mdata->iio_base.buffer_enabled) {
        mdata->iio_base.config.sol_iio_reader_cb = type->reader_cb;
        mdata->iio_base.config.data = node;
    }
    mdata->iio_base.use_device_default_scale = opts->use_device_default_scale;
    mdata->iio_base.use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->iio_base.out_range = opts->out_range;

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/gyroscope node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    if (!gyroscope_create_channels(mdata, device_id))
        goto err;

    return 0;

err:
    free((char *)mdata->iio_base.config.trigger_name);
    return -EINVAL;
}

static bool
magnet_create_channels(struct iio_direction_vector_data *mdata, int device_id)
{
    mdata->iio_base.device = sol_iio_open(device_id, &mdata->iio_base.config);
    SOL_NULL_CHECK(mdata->iio_base.device, false);

    mdata->channel_x = iio_add_channel(mdata->scale.x, mdata->offset.x, "in_magn_x", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_x, error);

    mdata->channel_y = iio_add_channel(mdata->scale.y, mdata->offset.y, "in_magn_y", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_y, error);

    mdata->channel_z = iio_add_channel(mdata->scale.z, mdata->offset.z, "in_magn_z", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_z, error);

    sol_iio_device_start_buffer(mdata->iio_base.device);

    return true;

error:
    SOL_WRN("Could not create iio/magnet node. Failed to open IIO device %d",
        device_id);
    sol_iio_close(mdata->iio_base.device);
    return false;
}

static int
magnet_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct iio_direction_vector_data *mdata = data;
    const struct sol_flow_node_type_iio_magnetometer_options *opts;
    int device_id, ret;
    struct iio_node_type *type;
    struct sol_str_table *table;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_MAGNETOMETER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_magnetometer_options *)options;

    mdata->iio_base.buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->iio_base.config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->iio_base.config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->iio_base.config.trigger_name, -ENOMEM);
    }

    mdata->iio_base.data_type = DIRECTION_VECTOR;
    mdata->iio_base.config.buffer_size = opts->buffer_size;
    mdata->iio_base.config.sampling_frequency = opts->sampling_frequency;
    ret = snprintf(mdata->iio_base.config.sampling_frequency_name,
        sizeof(mdata->iio_base.config.sampling_frequency_name), "%s", "in_magn_");
    SOL_EXP_CHECK_GOTO(ret < 0 || ret >= (int)sizeof(mdata->iio_base.config.sampling_frequency_name), err);

    mdata->iio_base.config.oversampling_ratio_table = calloc(4, sizeof(struct sol_str_table));
    SOL_NULL_CHECK(mdata->iio_base.config.oversampling_ratio_table, -ENOMEM);

    table = mdata->iio_base.config.oversampling_ratio_table;
    GEN_SOL_STR_TABLE(table->key, table->len, table->val,
        "in_magn_x_", (int16_t)opts->oversampling_ratio.x);
    table++;
    GEN_SOL_STR_TABLE(table->key, table->len, table->val,
        "in_magn_y_", (int16_t)opts->oversampling_ratio.y);
    table++;
    GEN_SOL_STR_TABLE(table->key, table->len, table->val,
        "in_magn_z_", (int16_t)opts->oversampling_ratio.z);

    if (mdata->iio_base.buffer_enabled) {
        mdata->iio_base.config.sol_iio_reader_cb = type->reader_cb;
        mdata->iio_base.config.data = node;
    }
    mdata->iio_base.use_device_default_scale = opts->use_device_default_scale;
    mdata->iio_base.use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->iio_base.out_range = opts->out_range;

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
    free((char *)mdata->iio_base.config.trigger_name);
    return -EINVAL;
}

static bool
temp_create_channels(struct iio_double_data *mdata, int device_id)
{
    mdata->iio_base.device = sol_iio_open(device_id, &mdata->iio_base.config);
    SOL_NULL_CHECK(mdata->iio_base.device, false);

    mdata->channel_val = iio_add_channel(mdata->scale, mdata->offset, "in_temp", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_val, error);

    sol_iio_device_start_buffer(mdata->iio_base.device);

    return true;

error:
    SOL_WRN("Could not create iio/thermometer node. Failed to open"
        " IIO device %d", device_id);

    sol_iio_close(mdata->iio_base.device);
    return false;
}

static int
temperature_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct iio_double_data *mdata = data;
    const struct sol_flow_node_type_iio_thermometer_options *opts;
    int device_id, ret;
    struct iio_node_type *type;
    struct sol_str_table *table;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_THERMOMETER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_thermometer_options *)options;

    mdata->iio_base.buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->iio_base.config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->iio_base.config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->iio_base.config.trigger_name, -ENOMEM);
    }

    mdata->iio_base.data_type = DOUBLE;
    mdata->iio_base.config.buffer_size = opts->buffer_size;
    mdata->iio_base.config.sampling_frequency = opts->sampling_frequency;
    ret = snprintf(mdata->iio_base.config.sampling_frequency_name,
        sizeof(mdata->iio_base.config.sampling_frequency_name), "%s", "in_temp_");
    SOL_EXP_CHECK_GOTO(ret < 0 || ret >= (int)sizeof(mdata->iio_base.config.sampling_frequency_name), err);

    mdata->iio_base.config.oversampling_ratio_table = calloc(2, sizeof(struct sol_str_table));
    SOL_NULL_CHECK(mdata->iio_base.config.oversampling_ratio_table, -ENOMEM);

    table = mdata->iio_base.config.oversampling_ratio_table;
    GEN_SOL_STR_TABLE(table->key, table->len, table->val,
        "in_temp_", opts->oversampling_ratio);

    if (mdata->iio_base.buffer_enabled) {
        mdata->iio_base.config.sol_iio_reader_cb = type->reader_cb;
        mdata->iio_base.config.data = node;
    }
    mdata->iio_base.use_device_default_scale = opts->use_device_default_scale;
    mdata->iio_base.use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->iio_base.out_range = opts->out_range;

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
    free((char *)mdata->iio_base.config.trigger_name);
    return -EINVAL;

}

static bool
pressure_create_channels(struct iio_double_data *mdata, int device_id)
{
    mdata->iio_base.device = sol_iio_open(device_id, &mdata->iio_base.config);
    SOL_NULL_CHECK(mdata->iio_base.device, false);

    mdata->channel_val = iio_add_channel(mdata->scale, mdata->offset, "in_pressure", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_val, error);

    sol_iio_device_start_buffer(mdata->iio_base.device);

    return true;

error:
    SOL_WRN("Could not create iio/pressure node. Failed to open IIO device %d",
        device_id);

    sol_iio_close(mdata->iio_base.device);
    return false;
}

static int
pressure_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct iio_double_data *mdata = data;
    const struct sol_flow_node_type_iio_pressure_sensor_options *opts;
    int device_id, ret;
    struct iio_node_type *type;
    struct sol_str_table *table;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_PRESSURE_SENSOR_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_pressure_sensor_options *)options;

    mdata->iio_base.buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->iio_base.config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->iio_base.config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->iio_base.config.trigger_name, -ENOMEM);
    }

    mdata->iio_base.data_type = DOUBLE;
    mdata->iio_base.config.buffer_size = opts->buffer_size;
    mdata->iio_base.config.sampling_frequency = opts->sampling_frequency;
    ret = snprintf(mdata->iio_base.config.sampling_frequency_name,
        sizeof(mdata->iio_base.config.sampling_frequency_name), "%s", "in_pressure_");
    SOL_EXP_CHECK_GOTO(ret < 0 || ret >= (int)sizeof(mdata->iio_base.config.sampling_frequency_name), err);

    mdata->iio_base.config.oversampling_ratio_table = calloc(2, sizeof(struct sol_str_table));
    SOL_NULL_CHECK(mdata->iio_base.config.oversampling_ratio_table, -ENOMEM);

    table = mdata->iio_base.config.oversampling_ratio_table;
    GEN_SOL_STR_TABLE(table->key, table->len, table->val,
        "in_pressure_", opts->oversampling_ratio);

    if (mdata->iio_base.buffer_enabled) {
        mdata->iio_base.config.sol_iio_reader_cb = type->reader_cb;
        mdata->iio_base.config.data = node;
    }
    mdata->iio_base.use_device_default_scale = opts->use_device_default_scale;
    mdata->iio_base.use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->iio_base.out_range = opts->out_range;

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
    free((char *)mdata->iio_base.config.trigger_name);
    return -EINVAL;

}

static bool
color_create_channels(struct iio_color_data *mdata, int device_id)
{
    mdata->iio_base.device = sol_iio_open(device_id, &mdata->iio_base.config);
    SOL_NULL_CHECK(mdata->iio_base.device, false);

    mdata->channel_red = iio_add_channel(mdata->scale_red, mdata->offset_red, "in_intensity_red", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_red, error);

    mdata->channel_green = iio_add_channel(mdata->scale_green, mdata->offset_green, "in_intensity_green", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_green, error);

    mdata->channel_blue = iio_add_channel(mdata->scale_blue, mdata->offset_blue, "in_intensity_blue", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_blue, error);

    sol_iio_device_start_buffer(mdata->iio_base.device);

    return true;

error:
    SOL_WRN("Could not create iio/color-sensor node. Failed to open IIO"
        " device %d", device_id);
    sol_iio_close(mdata->iio_base.device);
    return false;
}

static int
color_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct iio_color_data *mdata = data;
    const struct sol_flow_node_type_iio_color_sensor_options *opts;
    int device_id;
    struct iio_node_type *type;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_COLOR_SENSOR_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_color_sensor_options *)options;

    mdata->iio_base.buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->iio_base.config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->iio_base.config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->iio_base.config.trigger_name, -ENOMEM);
    }

    mdata->iio_base.data_type = COLOR;
    mdata->iio_base.config.buffer_size = opts->buffer_size;
    mdata->iio_base.config.sampling_frequency = opts->sampling_frequency;
    if (mdata->iio_base.buffer_enabled) {
        mdata->iio_base.config.sol_iio_reader_cb = type->reader_cb;
        mdata->iio_base.config.data = node;
    }
    mdata->iio_base.use_device_default_scale = opts->use_device_default_scale;
    mdata->iio_base.use_device_default_offset = opts->use_device_default_offset;

    mdata->scale_red = opts->scale_red;
    mdata->offset_red = opts->offset_red;

    mdata->scale_green = opts->scale_green;
    mdata->offset_green = opts->offset_green;

    mdata->scale_blue = opts->scale_blue;
    mdata->offset_blue = opts->offset_blue;

    mdata->iio_base.out_range = opts->out_range;

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
    free((char *)mdata->iio_base.config.trigger_name);
    return -EINVAL;
}

static bool
accelerate_create_channels(struct iio_direction_vector_data *mdata, int device_id)
{
    mdata->iio_base.device = sol_iio_open(device_id, &mdata->iio_base.config);
    SOL_NULL_CHECK(mdata->iio_base.device, false);

    mdata->channel_x = iio_add_channel(mdata->scale.x, mdata->offset.x, "in_accel_x", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_x, error);

    mdata->channel_y = iio_add_channel(mdata->scale.y, mdata->offset.y, "in_accel_y", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_y, error);

    mdata->channel_z = iio_add_channel(mdata->scale.z, mdata->offset.z, "in_accel_z", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_z, error);

    sol_iio_device_start_buffer(mdata->iio_base.device);

    return true;

error:
    SOL_WRN("Could not create iio/accelerate node. Failed to open IIO device %d",
        device_id);
    sol_iio_close(mdata->iio_base.device);
    return false;
}

static int
accelerate_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct iio_direction_vector_data *mdata = data;
    const struct sol_flow_node_type_iio_accelerometer_options *opts;
    int device_id, ret;
    struct iio_node_type *type;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_ACCELEROMETER_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_accelerometer_options *)options;

    mdata->iio_base.buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->iio_base.config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->iio_base.config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->iio_base.config.trigger_name, -ENOMEM);
    }

    mdata->iio_base.data_type = DIRECTION_VECTOR;
    mdata->iio_base.config.buffer_size = opts->buffer_size;
    mdata->iio_base.config.sampling_frequency = opts->sampling_frequency;
    ret = snprintf(mdata->iio_base.config.sampling_frequency_name,
        sizeof(mdata->iio_base.config.sampling_frequency_name), "%s", "in_accel_");
    SOL_EXP_CHECK_GOTO(ret < 0 || ret >= (int)sizeof(mdata->iio_base.config.sampling_frequency_name), err);

    if (mdata->iio_base.buffer_enabled) {
        mdata->iio_base.config.sol_iio_reader_cb = type->reader_cb;
        mdata->iio_base.config.data = node;
    }
    mdata->iio_base.use_device_default_scale = opts->use_device_default_scale;
    mdata->iio_base.use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->iio_base.out_range = opts->out_range;

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
    free((char *)mdata->iio_base.config.trigger_name);
    return -EINVAL;
}

static bool
humidity_create_channels(struct iio_double_data *mdata, int device_id)
{
    mdata->iio_base.device = sol_iio_open(device_id, &mdata->iio_base.config);
    SOL_NULL_CHECK(mdata->iio_base.device, false);

    mdata->channel_val = iio_add_channel(mdata->scale, mdata->offset, "in_humidityrelative", &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_val, error);

    sol_iio_device_start_buffer(mdata->iio_base.device);

    return true;

error:
    SOL_WRN("Could not create iio/humidity node. Failed to open IIO device %d",
        device_id);

    sol_iio_close(mdata->iio_base.device);
    return false;
}

static int
humidity_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct iio_double_data *mdata = data;
    const struct sol_flow_node_type_iio_humidity_sensor_options *opts;
    int device_id;
    struct iio_node_type *type;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_HUMIDITY_SENSOR_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_humidity_sensor_options *)options;

    mdata->iio_base.buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->iio_base.config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->iio_base.config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->iio_base.config.trigger_name, -ENOMEM);
    }

    mdata->iio_base.data_type = DOUBLE;
    mdata->iio_base.config.buffer_size = opts->buffer_size;
    mdata->iio_base.config.sampling_frequency = opts->sampling_frequency;
    if (mdata->iio_base.buffer_enabled) {
        mdata->iio_base.config.sol_iio_reader_cb = type->reader_cb;
        mdata->iio_base.config.data = node;
    }
    mdata->iio_base.use_device_default_scale = opts->use_device_default_scale;
    mdata->iio_base.use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->iio_base.out_range = opts->out_range;

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
    free((char *)mdata->iio_base.config.trigger_name);
    return -EINVAL;

}

static bool
adc_create_channels(struct iio_double_data *mdata, int device_id, const int channel_id)
{
    char channel_name[NAME_MAX];

    mdata->iio_base.device = sol_iio_open(device_id, &mdata->iio_base.config);
    SOL_NULL_CHECK(mdata->iio_base.device, false);

    GEN_CHANNEL_NAME(channel_name, "in_voltage", error, channel_id);

    mdata->channel_val = iio_add_channel(mdata->scale, mdata->offset, channel_name, &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_val, error);

    sol_iio_device_start_buffer(mdata->iio_base.device);

    return true;

error:
    SOL_WRN("Could not create iio/adc node. Failed to open IIO device %d",
        device_id);

    sol_iio_close(mdata->iio_base.device);
    return false;
}

static int
adc_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct iio_double_data *mdata = data;
    const struct sol_flow_node_type_iio_adc_options *opts;
    int device_id;
    struct iio_node_type *type;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_ADC_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_adc_options *)options;

    mdata->iio_base.buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->iio_base.config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->iio_base.config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->iio_base.config.trigger_name, -ENOMEM);
    }

    mdata->iio_base.data_type = DOUBLE;
    mdata->iio_base.config.buffer_size = opts->buffer_size;
    mdata->iio_base.config.sampling_frequency = opts->sampling_frequency;
    if (mdata->iio_base.buffer_enabled) {
        mdata->iio_base.config.sol_iio_reader_cb = type->reader_cb;
        mdata->iio_base.config.data = node;
    }
    mdata->iio_base.use_device_default_scale = opts->use_device_default_scale;
    mdata->iio_base.use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->iio_base.out_range = opts->out_range;

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/adc node. Failed to open IIO device %s",
            opts->iio_device);
        goto err;
    }

    if (!adc_create_channels(mdata, device_id, opts->channel_id))
        goto err;

    return 0;

err:
    free((char *)mdata->iio_base.config.trigger_name);
    return -EINVAL;

}

static bool
light_create_channels(struct iio_double_data *mdata, int device_id, const int channel_id)
{
    char channel_name[NAME_MAX];

    mdata->iio_base.device = sol_iio_open(device_id, &mdata->iio_base.config);
    SOL_NULL_CHECK(mdata->iio_base.device, false);

    GEN_CHANNEL_NAME(channel_name, "in_illuminance", error, channel_id);

    mdata->channel_val = iio_add_channel(mdata->scale, mdata->offset, channel_name, &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_val, error);

    sol_iio_device_start_buffer(mdata->iio_base.device);

    return true;

error:
    SOL_WRN("Could not create iio/light-sensor node. Failed to open IIO"
        " device %d", device_id);

    sol_iio_close(mdata->iio_base.device);
    return false;
}

static int
light_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct iio_double_data *mdata = data;
    const struct sol_flow_node_type_iio_light_sensor_options *opts;
    int device_id, ret;
    struct iio_node_type *type;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_LIGHT_SENSOR_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_light_sensor_options *)options;

    mdata->iio_base.buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->iio_base.config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->iio_base.config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->iio_base.config.trigger_name, -ENOMEM);
    }

    mdata->iio_base.data_type = DOUBLE;
    mdata->iio_base.config.buffer_size = opts->buffer_size;
    mdata->iio_base.config.sampling_frequency = opts->sampling_frequency;
    ret = snprintf(mdata->iio_base.config.sampling_frequency_name,
        sizeof(mdata->iio_base.config.sampling_frequency_name), "%s", "in_illuminance_");
    SOL_EXP_CHECK_GOTO(ret < 0 || ret >= (int)sizeof(mdata->iio_base.config.sampling_frequency_name), err);

    if (mdata->iio_base.buffer_enabled) {
        mdata->iio_base.config.sol_iio_reader_cb = type->reader_cb;
        mdata->iio_base.config.data = node;
    }
    mdata->iio_base.use_device_default_scale = opts->use_device_default_scale;
    mdata->iio_base.use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->iio_base.out_range = opts->out_range;

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/light-sensor node. Failed to open"
            " IIO device %s", opts->iio_device);
        goto err;
    }

    if (!light_create_channels(mdata, device_id, opts->channel_id))
        goto err;

    return 0;

err:
    free((char *)mdata->iio_base.config.trigger_name);
    return -EINVAL;

}

static bool
intensity_both_create_channels(struct iio_double_data *mdata, int device_id, const int channel_id)
{
    char channel_name[NAME_MAX];
    int ret = 0;

    mdata->iio_base.device = sol_iio_open(device_id, &mdata->iio_base.config);
    SOL_NULL_CHECK(mdata->iio_base.device, false);

    if (channel_id >= 0) {
        ret = snprintf(channel_name, sizeof(channel_name), "in_intensity%d_both", channel_id);
        SOL_EXP_CHECK_GOTO(ret < 0 || ret >= (int)sizeof(channel_name), error);
    } else {
        strncpy(channel_name, "in_intensity_both", sizeof(channel_name) - 1);
        channel_name[sizeof(channel_name) - 1] = '\0';
    }

    mdata->channel_val = iio_add_channel(mdata->scale, mdata->offset, channel_name, &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_val, error);

    sol_iio_device_start_buffer(mdata->iio_base.device);

    return true;

error:
    SOL_WRN("Could not create iio/intensity-both-sensor node. Failed to open IIO"
        " device %d", device_id);

    sol_iio_close(mdata->iio_base.device);
    return false;
}

static int
intensity_both_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct iio_double_data *mdata = data;
    const struct sol_flow_node_type_iio_intensity_both_sensor_options *opts;
    int device_id;
    struct iio_node_type *type;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_INTENSITY_BOTH_SENSOR_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_intensity_both_sensor_options *)options;

    mdata->iio_base.buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->iio_base.config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->iio_base.config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->iio_base.config.trigger_name, -ENOMEM);
    }

    mdata->iio_base.data_type = DOUBLE;
    mdata->iio_base.config.buffer_size = opts->buffer_size;
    mdata->iio_base.config.sampling_frequency = opts->sampling_frequency;
    if (mdata->iio_base.buffer_enabled) {
        mdata->iio_base.config.sol_iio_reader_cb = type->reader_cb;
        mdata->iio_base.config.data = node;
    }
    mdata->iio_base.use_device_default_scale = opts->use_device_default_scale;
    mdata->iio_base.use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->iio_base.out_range = opts->out_range;

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/intensity-both-sensor node. Failed to open"
            " IIO device %s", opts->iio_device);
        goto err;
    }

    if (!intensity_both_create_channels(mdata, device_id, opts->channel_id))
        goto err;

    return 0;

err:
    free((char *)mdata->iio_base.config.trigger_name);
    return -EINVAL;

}

static bool
intensity_ir_create_channels(struct iio_double_data *mdata, int device_id, const int channel_id)
{
    char channel_name[NAME_MAX];
    int ret = 0;

    mdata->iio_base.device = sol_iio_open(device_id, &mdata->iio_base.config);
    SOL_NULL_CHECK(mdata->iio_base.device, false);

    if (channel_id >= 0) {
        ret = snprintf(channel_name, sizeof(channel_name), "in_intensity%d_ir", channel_id);
        SOL_EXP_CHECK_GOTO(ret < 0 || ret >= (int)sizeof(channel_name), error);
    } else {
        strncpy(channel_name, "in_intensity_ir", sizeof(channel_name) - 1);
        channel_name[sizeof(channel_name) - 1] = '\0';
    }

    mdata->channel_val = iio_add_channel(mdata->scale, mdata->offset, channel_name, &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_val, error);

    sol_iio_device_start_buffer(mdata->iio_base.device);

    return true;

error:
    SOL_WRN("Could not create iio/intensity-ir-sensor node. Failed to open IIO"
        " device %d", device_id);

    sol_iio_close(mdata->iio_base.device);
    return false;
}

static int
intensity_ir_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct iio_double_data *mdata = data;
    const struct sol_flow_node_type_iio_intensity_ir_sensor_options *opts;
    int device_id;
    struct iio_node_type *type;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_INTENSITY_IR_SENSOR_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_intensity_ir_sensor_options *)options;

    mdata->iio_base.buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->iio_base.config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->iio_base.config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->iio_base.config.trigger_name, -ENOMEM);
    }

    mdata->iio_base.data_type = DOUBLE;
    mdata->iio_base.config.buffer_size = opts->buffer_size;
    mdata->iio_base.config.sampling_frequency = opts->sampling_frequency;
    if (mdata->iio_base.buffer_enabled) {
        mdata->iio_base.config.sol_iio_reader_cb = type->reader_cb;
        mdata->iio_base.config.data = node;
    }
    mdata->iio_base.use_device_default_scale = opts->use_device_default_scale;
    mdata->iio_base.use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->iio_base.out_range = opts->out_range;

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/intensity-ir-sensor node. Failed to open"
            " IIO device %s", opts->iio_device);
        goto err;
    }

    if (!intensity_ir_create_channels(mdata, device_id, opts->channel_id))
        goto err;

    return 0;

err:
    free((char *)mdata->iio_base.config.trigger_name);
    return -EINVAL;

}

static bool
proximity_create_channels(struct iio_double_data *mdata, int device_id, const int channel_id)
{
    char channel_name[NAME_MAX];

    mdata->iio_base.device = sol_iio_open(device_id, &mdata->iio_base.config);
    SOL_NULL_CHECK(mdata->iio_base.device, false);

    GEN_CHANNEL_NAME(channel_name, "in_proximity", error, channel_id);

    mdata->channel_val = iio_add_channel(mdata->scale, mdata->offset, channel_name, &mdata->iio_base);
    SOL_NULL_CHECK_GOTO(mdata->channel_val, error);

    sol_iio_device_start_buffer(mdata->iio_base.device);

    return true;

error:
    SOL_WRN("Could not create iio/proximity-sensor node. Failed to open"
        " IIO device %d", device_id);

    sol_iio_close(mdata->iio_base.device);
    return false;
}

static int
proximity_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct iio_double_data *mdata = data;
    const struct sol_flow_node_type_iio_proximity_sensor_options *opts;
    int device_id;
    struct iio_node_type *type;

    type = (struct iio_node_type *)sol_flow_node_get_type(node);

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_IIO_PROXIMITY_SENSOR_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_iio_proximity_sensor_options *)options;

    mdata->iio_base.buffer_enabled = opts->buffer_size > -1;

    SOL_SET_API_VERSION(mdata->iio_base.config.api_version = SOL_IIO_CONFIG_API_VERSION; )

    if (opts->iio_trigger_name) {
        mdata->iio_base.config.trigger_name = strdup(opts->iio_trigger_name);
        SOL_NULL_CHECK(mdata->iio_base.config.trigger_name, -ENOMEM);
    }

    mdata->iio_base.data_type = DOUBLE;
    mdata->iio_base.config.buffer_size = opts->buffer_size;
    mdata->iio_base.config.sampling_frequency = opts->sampling_frequency;
    if (mdata->iio_base.buffer_enabled) {
        mdata->iio_base.config.sol_iio_reader_cb = type->reader_cb;
        mdata->iio_base.config.data = node;
    }
    mdata->iio_base.use_device_default_scale = opts->use_device_default_scale;
    mdata->iio_base.use_device_default_offset = opts->use_device_default_offset;
    mdata->scale = opts->scale;
    mdata->offset = opts->offset;
    mdata->iio_base.out_range = opts->out_range;

    device_id = sol_iio_address_device(opts->iio_device);
    if (device_id < 0) {
        SOL_WRN("Could not create iio/proximity-sensor node. Failed to open"
            " IIO device %s", opts->iio_device);
        goto err;
    }

    if (!proximity_create_channels(mdata, device_id, opts->channel_id))
        goto err;

    return 0;

err:
    free((char *)mdata->iio_base.config.trigger_name);
    return -EINVAL;

}

#include "iio-gen.c"
