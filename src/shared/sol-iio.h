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

/**
 * @file
 * @brief Routines to access Linux Industrial I/O (iio) devices under Solleta
 */

struct sol_iio_device;
struct sol_iio_channel;
struct sol_iio_buffer;

/**
 * Callback to read data from buffer
 */
typedef void (*sol_iio_reader_cb)(struct sol_iio_device *device, struct sol_iio_buffer *buffer, void *data);

/**
 * Open an IIO device
 *
 * @param device_id Id of iio device. It's the number which identifies device
 * on file system. Can be found at '/sys/bus/iio/devices/iio:deviceX'
 * @param buffer_size size of reading buffer. If 0, will use device default;
 * if -1 will disable buffer - and all reads will be performed on channel files
 * on sysfs.
 * @param trigger_name Name of IIO trigger that will be used on this device. If NULL
 * or empty, will try to use device current trigger. If device has no current trigger,
 * will create a 'sysfs_trigger' and use it.
 *
 * @return A new IIO handle
 */
struct sol_iio_device *sol_iio_open(int device_id, int buffer_size, const char *trigger_name);

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
 *
 * @return A new IIO channel handle
 */
struct sol_iio_channel *sol_iio_add_channel(struct sol_iio_device *device, const char *name);

/**
 * Read channel value.
 *
 * This method should be used if buffer is disabled,
 * and readings will be performed on sysfs. When buffer is enabled, use
 * sol_iio_read_buffer_channel_value() instead.
 *
 * @param channel IIO channel handle to be read
 * @param value Where read value will be stored
 *
 * @return true if reading was performed correctly
 */
bool sol_iio_read_channel_value(struct sol_iio_channel *channel, double *value);

/**
 * Set callback for read operations.
 *
 * When buffer is enabled, reading can only
 * be performed if there's data on buffer. A trigger is used to send values
 * to buffer. When buffer is ready, the callback set here will be called
 * and data can be read using sol_iio_read_buffer_channel_value().
 *
 * @param device IIO handle of device
 * @param reader_cb sol_iio_reader_cb that will be called when there's data on
 * buffer.
 * @param data User data to be sent to reader callback.
 */
void sol_iio_set_reader_cb(struct sol_iio_device *device, sol_iio_reader_cb reader_cb, void *data);

/**
 * Read channel value on buffer.
 *
 * Reader callback contains a sol_iio_buffer
 * containing the data read. To read channels value on it, this function shall
 * be used
 *
 * @param channel IIO channel handle to be read
 * @param buffer sol_iio_buffer containing the data read from device
 * @param value Where read value will be stored
 *
 * @return true if reading was performed correctly
 */
bool sol_iio_read_buffer_channel_value(struct sol_iio_channel *channel, struct sol_iio_buffer *buffer, double *value);

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
 * Reading here means that the buffer will be opened, when real readings
 * happens, the callback set using sol_iio_set_reader_cb will be called.
 *
 * @param device IIO handler of device on which reading will be performed
 *
 * @return true if reading started successfully
 */
bool sol_iio_device_start_buffer(struct sol_iio_device *device);

/**
 * Set device sampling frequency.
 *
 * Some devices have a sampling frequency that can be adjusted.
 *
 * @param device IIO handler of device
 * @param frequency new frequency value
 *
 * @return true if set is done successfully
 */
bool sol_iio_set_sampling_frequency(struct sol_iio_device *device, int frequency);


/**
 * Set channel scale.
 *
 * Scale is applied to raw device readings. Some devices accept changes of
 * this value, in order to fine tune readings on a certain environment.
 *
 * @param channel IIO handle of channel to be changed.
 * @param scale new scale value
 *
 * @return true if set is done successfully
 *
 * @note Some devices can share scale among all data channels - so changing
 * one will change all channels.
 */
bool sol_iio_set_channel_scale(struct sol_iio_channel *channel, double scale);

/**
 * Set channel offset.
 *
 * Offset is added to raw device readings. Some devices accept changes of
 * this value, in order to fine tune readings on a certain environment.
 *
 * @param channel IIO handle of channel to be changed.
 * @param offset new offset value
 *
 * @return true if set is done successfully
 *
 * @note Some devices can share offset among all data channels - so changing
 * one will change all channels.
 */
bool sol_iio_set_channel_offset(struct sol_iio_channel *channel, int offset);
