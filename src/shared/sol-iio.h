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

#pragma once

#include <sol-buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to access Linux Industrial I/O (iio) devices under Solleta
 */

struct sol_iio_device;
struct sol_iio_channel;

struct sol_iio_config {
#define SOL_IIO_CONFIG_API_VERSION (1)
    uint16_t api_version;
    const char *trigger_name; /**< Name of IIO trigger to be used on this device. If NULL or empty, will try to use device current trigger. If device has no current trigger, will create a 'sysfs_trigger' and use it. */
    void (*sol_iio_reader_cb)(void *data, struct sol_iio_device *device); /**< Callback to be called when get new device readings on buffer */
    const void *data; /**< User defined data to be sent to sol_iio_reader_cb */
    int buffer_size; /**< size of reading buffer. 0: use device default; -1: disable buffer and readings will be performed on channel files on sysfs. */
    int sampling_frequency; /**< Device sampling frequency. -1 uses device default */
};

struct sol_iio_channel_config {
#define SOL_IIO_CHANNEL_CONFIG_API_VERSION (1)
    uint16_t api_version;
    double scale; /**< Channel scale, to be applied to raw readings. -1 uses device default. Some devices share scale among all channels, so changing one will change all. If, in this case, different channels set different scales the result is unknown. */
    int offset; /**< Channel offset, to be added to raw readings. Some devices share offset among all channels, so changing one will change all. If, in this case, different channels set different offsets the result is unknown. */
    bool use_custom_offset; /**< If true, will use user defined offset on member #offset of this struct */
};

#define SOL_IIO_CHANNEL_CONFIG_INIT { .api_version = SOL_IIO_CHANNEL_CONFIG_API_VERSION, .scale = -1.0, .use_custom_offset = false }

/**
 * Open an IIO device
 *
 * @param device_id Id of iio device. It's the number which identifies device
 * on file system. Can be found at '/sys/bus/iio/devices/iio:deviceX'.
 * @param config IIO config.
 *
 * @return A new IIO handle
 */
struct sol_iio_device *sol_iio_open(int id, const struct sol_iio_config *config);

/**
 * Close an IIO device
 *
 * @param device IIO device handle to close
 */
void sol_iio_close(struct sol_iio_device *device);

/**
 * Add reading channel.
 *
 * @param device IIO handle of device to which channel is being add
 * @param name Name of channel. Eg 'in_anglvel_x'.
 * @param config Channel config.
 *
 * @return A new IIO channel handle
 */
struct sol_iio_channel *sol_iio_add_channel(struct sol_iio_device *device, const char *name, const struct sol_iio_channel_config *config);

/**
 * Read channel value.
 *
 * If buffer is enabled, it will read from last buffer data. Callback
 * 'sol_iio_reader_cb' is called when there are new data on buffer.
 * If buffer is disabled, will read from channel file on sysfs.
 *
 * @param channel IIO channel handle to be read
 * @param value Where read value will be stored
 *
 * @return true if reading was performed correctly
 */
bool sol_iio_read_channel_value(struct sol_iio_channel *channel, double *value);

/**
 * Manually 'pull' device current trigger.
 *
 * If device current trigger has a 'trigger_now' file that start
 * a reading on device, writes to it to produce a new reading
 *
 * @param device IIO handler of device to have its trigger manually 'pulled'.
 *
 * @return true if writing to file is successful
 */
bool sol_iio_device_trigger_now(struct sol_iio_device *device);

/**
 * Start reading device buffer.
 *
 * Reading on buffer should start after all channels were enabled
 * (which is done when a channel is added using sol_iio_add_channel()).
 * So, call this function after having added all channels.
 * Reading here means that the buffer will be opened; when real readings
 * happens the callback set on config will be called.
 *
 * @param device IIO handler of device on which reading will be performed
 *
 * @return true if reading started successfully
 */
bool sol_iio_device_start_buffer(struct sol_iio_device *device);

/**
 * Address an IIO device from a list of commands to find them.
 *
 * IIO devices may exist on sysfs after being plugged, or need to be
 * explicitly created if, for instance, they use I2C or SPI interfaces.
 * This function provides a way of addressing an IIO device to get its IIO
 * id from a series of space separated @a commands. Commands are processed
 * from left to right and processing stops on first command that worked.
 * A provided callback will be called with IIO device id or -1 if no command
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
 * <tt> create,<bus_type>,<rel_path>,<devnumber>,<devname> </tt>
 *
 * @arg @a bus_type is the bus type, supported values are: i2c
 * @arg @a rel_path is the relative path for device on '/sys/devices',
 * like 'platform/80860F41:05'
 * @arg @a devnumber is device number on bus, like 0xA4
 * @arg @a devname is device name, the one recognized by its driver
 *
 * If device already exists, will just return its IIO id on @a cb.
 *
 * @param commands space separated commands on format specified above. e.g. <tt>
 * l3g4200d create,i2c,platform/80860F41:05,0x69,l3g4200d </tt>
 * @param cb callback to be called after device resolution. It contains IIO
 * device id, or -1 if unsuccessful
 * @param data user data to be passed to callback
 *
 * @return @a true if attempt to resolve device was successful, @a false
 * othewise. Note that to really know if device was resolved, besides return,
 * one needs to check callback device_id.
 */
bool sol_iio_address_device(const char *commands, void (*cb)(void *data, int device_id), const void *data);

#ifdef __cplusplus
}
#endif
