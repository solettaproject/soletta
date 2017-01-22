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

#pragma once

#include <sol-buffer.h>
#include <sol-common-buildopts.h>
#include <sol-str-table.h>

#include <linux/limits.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to access Linux Industrial I/O (iio) devices under Soletta
 */

/**
 * @defgroup IIO IIO
 * @ingroup IO
 *
 * @brief IIO (Linux Industrial I/O) API for Soletta.
 *
 * @{
 */

/**
 * @typedef sol_iio_device
 * @brief An IIO device handle
 * @see sol_iio_open()
 * @see sol_iio_close()
 * @see sol_iio_device_trigger()
 * @see sol_iio_device_start_buffer()
 */
struct sol_iio_device;
typedef struct sol_iio_device sol_iio_device;

/**
 * @typedef sol_iio_channel
 * @brief An IIO channel handle
 * @see sol_iio_add_channel()
 * @see sol_iio_read_channel_value()
 * @see sol_iio_read_channel_raw_buffer()
 * @see sol_iio_read_channel_value()
 * @see SOL_IIO_CHANNEL_CONFIG_INIT()
 */
struct sol_iio_channel;
typedef struct sol_iio_channel sol_iio_channel;

/**
 * @brief A configuration struct for an IIO device
 *
 * @see sol_iio_open()
 */
typedef struct sol_iio_config {
#ifndef SOL_NO_API_VERSION
#define SOL_IIO_CONFIG_API_VERSION (2)
    uint16_t api_version; /**< The API version */
#endif
    const char *trigger_name; /**< Name of IIO trigger to be used on this device. Set to hrtimer:trigger name if want to use hrtimer trigger. If NULL or empty, will try to use device current trigger. If device has no current trigger, will create a sysfs or hrtimer trigger and use it. */
    void (*sol_iio_reader_cb)(void *data, struct sol_iio_device *device); /**< Callback to be called when get new device readings on buffer */
    const void *data; /**< User defined data to be sent to sol_iio_reader_cb */
    int buffer_size; /**< The size of reading buffer. 0: use device default; -1: disable buffer and readings will be performed on channel files on sysfs. */
    int sampling_frequency; /**< Device sampling frequency. -1 uses device default */
    char sampling_frequency_name[NAME_MAX]; /**< Sampling frequency sysfs node name. Some drivers expose the sampling frequency that is shared by channel type. Such as in_magn_sampling_frequency, in_accel_sampling_frequency. */
    struct sol_str_table *oversampling_ratio_table; /**< Hardware applied number of measurements for acquiring one data point. The HW will do [_name]_oversampling_ratio measurements and return the average value as output data. */

} sol_iio_config;

/**
 * @brief A configuration struct for an IIO channel
 *
 * @see sol_iio_add_channel()
 */
typedef struct sol_iio_channel_config {
#ifndef SOL_NO_API_VERSION
#define SOL_IIO_CHANNEL_CONFIG_API_VERSION (1)
    uint16_t api_version; /**< The API version */
#endif
    double scale; /**< Channel scale, to be applied to raw readings. -1 uses device default. Some devices share scale among all channels, so changing one will change all. If, in this case, different channels set different scales the result is unknown. */
    int offset; /**< Channel offset, to be added to raw readings. Some devices share offset among all channels, so changing one will change all. If, in this case, different channels set different offsets the result is unknown. */
    bool use_custom_offset; /**< If true, will use user defined offset on member #offset of this struct */
} sol_iio_channel_config;

/**
 * @brief Macro that may be used for initialized a @ref sol_iio_channel_config
 *
 * This macro will init a with default values a @ref sol_iio_channel_config. It'll start with scale @c -1.0 and
 * without custom offset.
 */
#define SOL_IIO_CHANNEL_CONFIG_INIT { \
        SOL_SET_API_VERSION(.api_version = SOL_IIO_CHANNEL_CONFIG_API_VERSION, ) \
        .scale = -1.0, .offset = 0, .use_custom_offset = false }

/**
 * @brief Open an IIO device
 *
 * Using different channel_id and the same configuration, one device can be opened
 * multiple times
 * @param id Id of iio device. It's the number which identifies device
 * on file system. Can be found at '/sys/bus/iio/devices/iio:deviceX'.
 * @param config IIO config.
 *
 * @return A new IIO handle
 */
struct sol_iio_device *sol_iio_open(int id, const struct sol_iio_config *config);

/**
 * @brief Close an IIO device
 *
 * @param device IIO device handle to close
 */
void sol_iio_close(struct sol_iio_device *device);

/**
 * @brief Add reading channel.
 *
 * @param device IIO handle of device to which channel is being add
 * @param name Name of channel. Eg 'in_anglvel_x'.
 * @param config Channel config.
 *
 * @return A new IIO channel handle
 */
struct sol_iio_channel *sol_iio_add_channel(struct sol_iio_device *device, const char *name, const struct sol_iio_channel_config *config);

/**
 * @brief Read channel value.
 *
 * If buffer is enabled, it will read from last buffer data. Callback
 * 'sol_iio_reader_cb' is called when there are new data on buffer.
 * If buffer is disabled, will read from channel file on sysfs.
 *
 * @param channel IIO channel handle to be read
 * @param value Where read value will be stored
 *
 * @return 0 if reading was performed correctly, if fail return error code always negative.
 */
int sol_iio_read_channel_value(struct sol_iio_channel *channel, double *value);

/**
 * @brief Manually 'pull' device current trigger.
 *
 * If device current trigger has a 'trigger_now' file that start
 * a reading on device, writes to it to produce a new reading
 *
 * @param device IIO handler of device to have its trigger manually 'pulled'.
 *
 * @return 0 if writing to file is successful, if fail return error code always negative.
 */
int sol_iio_device_trigger(struct sol_iio_device *device);

/**
 * @brief Start reading device buffer.
 *
 * Reading on buffer should start after all channels were enabled
 * (which is done when a channel is added using sol_iio_add_channel()).
 * So, call this function after having added all channels.
 * Reading here means that the buffer will be opened; when real readings
 * happens the callback set on config will be called.
 *
 * @param device IIO handler of device on which reading will be performed
 *
 * @return 0 if reading started successfully, if fail return error code always negative.
 */
int sol_iio_device_start_buffer(struct sol_iio_device *device);

/**
 * @brief Address an IIO device from a list of commands to find them.
 *
 * IIO devices may exist on sysfs after being plugged, or need to be
 * explicitly created if, for instance, they use I2C or SPI interfaces.
 * This function provides a way of addressing an IIO device to get its IIO
 * id from a series of space separated @a commands. Commands are processed
 * from left to right and processing stops on first command that worked.
 * IIO device id will be returned, or a negative number if no command
 * resolved to an IIO device.
 *
 * There are essentially five commands. It can be an absolute path
 * (starting with '/') pointing to sysfs dir of device. Alternatively,
 * it can be @c i2c/X-YYYY, for i2c device, where @a X is the bus number and
 * @a YYYY is the device number, eg, @c 7-0069 for device 0x69 on bus 7.
 * If its a raw number, will be interpreted as IIO device id and this function
 * will only check the id. It can also be device name, as it appears on
 * 'name' file on sysfs.
 * Finally, it can describe a command to @a create an IIO device. In this case,
 * command is a combination on the form
 * <tt> create,\<bus_type\>,\<rel_path\>,\<devnumber\>,\<devname\> </tt>
 *
 * @arg @a bus_type is the bus type, supported values are: i2c
 * @arg @a rel_path is the relative path for device on '/sys/devices',
 * like 'platform/80860F41:05'
 * @arg @a devnumber is device number on bus, like 0xA4
 * @arg @a devname is device name, the one recognized by its driver
 *
 * If device already exists, will just return its IIO id.
 *
 * @param commands space separated commands on format specified above. e.g. <tt>
 * l3g4200d create,i2c,platform/80860F41:05,0x69,l3g4200d </tt>
 *
 * @return IIO device id on success. A negative number means failure.
 */
int sol_iio_address_device(const char *commands);

/**
 * @brief Returns raw buffer with channel sample.
 *
 * This function is meaningful only when buffer is enabled. Useful for reading
 * samples bigger than 64 bits. For channels with 64 or less bits, prefer
 * @c sol_iio_read_channel_value, with return a more meaningful value, adjusted
 * by channel offset and scale.
 *
 * @param channel channel to get raw buffer
 *
 * @return a sol_str_slice containing channel raw readings. Slice will
 * be empty if buffer is not enabled or if there are no readings yet.
 *
 * @note Buffer size is the same as storage bits (aligned to byte boundary).
 */
struct sol_str_slice
sol_iio_read_channel_raw_buffer(struct sol_iio_channel *channel);

/**
 * @brief Perform the mount calibration.
 *
 * This function is meaningful only when mount_matrix is supported.
 *
 * @param device device to get mount matrix
 * @param value data to be calibrated
 *
 * @return 0 if mount_matrix is exits and perform the calibration, -1 means does not support mount_matrix.
 *
 */
int
sol_iio_mount_calibration(struct sol_iio_device *device, sol_direction_vector *value);

/**
 * @brief Gets the configuration attribute name of a channel.
 *
 * @param channel sol_iio_channel structure which the name is desired.
 * @return The configuration attribute name of the channel on success, @c NULL on error.
 *
 */
const char *sol_iio_channel_get_name(const struct sol_iio_channel *channel);


/**
 * @brief Gets the configuration scale attribute from one device
 *
 * @param device The sol_iio_device structure which the scale is desired
 * @param prefix_name pointer to the attribute name Eg: "in_anglvel_x"
 * @param scale data to be get from attribute name
 *
 * @return 0 on success, -errno on failure.
 */
int sol_iio_device_get_scale(const struct sol_iio_device *device, const char *prefix_name, double *scale);

/**
 * @brief Gets the configuration offset attribute from one device
 *
 * @param device The sol_iio_device structure which the offset is desired
 * @param prefix_name pointer to the attribute name Eg: "in_anglvel_x"
 * @param offset data to be get from attribute name
 *
 * @return 0 on success, -errno on failure.
 */
int sol_iio_device_get_offset(const struct sol_iio_device *device, const char *prefix_name, double *offset);

/**
 * @brief Gets the configuration sampling_frequency attribute from one device
 *
 * @param device The sol_iio_device structure which the sampling_frequency is desired
 * @param prefix_name pointer to the attribute name Eg: "in_anglvel"
 * @param sampling_frequency data to be get from attribute name
 *
 * @return 0 on success, -errno on failure.
 */
int sol_iio_device_get_sampling_frequency(const struct sol_iio_device *device, const char *prefix_name, int *sampling_frequency);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
