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

#include <sol-i2c.h>
#include <sol-log.h>
#include <sol-mainloop.h>
#include <sol-vector.h>

#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "am2315.h"

/* AM2315 humidity/temperature sensor.
 * http://www.adafruit.com/datasheets/AM2315.pdf
 */

#define AM2315_INTERVAL_BETWEEN_READINGS 2
#define AM2315_READ_REG 0x03
#define AM2315_HUMIDITY_HIGH 0x00
#define AM2315_READ_LEN 4
#define AM2315_BUFFER_LEN 8
#define AM2315_WRITE_MSG_LEN 3
#define STEP_TIME 1

#define READING_SCALE 10.0

struct am2315 {
    uint8_t buffer[AM2315_BUFFER_LEN];
    struct sol_i2c *i2c;
    void (*humidity_callback)(float humidity, bool success, void *data);
    void (*temperature_callback)(float temperature, bool success, void *data);
    void *humidity_callback_data;
    void *temperature_callback_data;
    struct sol_i2c_pending *i2c_pending;
    struct sol_timeout *timer;
    unsigned pending_temperature;
    unsigned pending_humidity;
    time_t last_reading;
    uint16_t temperature;
    uint16_t humidity;
    uint8_t slave;
    uint8_t refcount : 7;
    uint8_t success : 1;
};

static struct sol_ptr_vector devices = SOL_PTR_VECTOR_INIT;

static int
timer_sched(struct am2315 *device, unsigned int timeout_ms, bool (*cb)(void *data))
{
    device->timer = sol_timeout_add(timeout_ms, cb, device);
    SOL_NULL_CHECK(device->timer, -ENOMEM);

    return 0;
}

struct am2315 *
am2315_open(uint8_t bus, uint8_t slave)
{
    struct am2315 *device;
    struct sol_i2c *i2c;
    int i;

    /* Is the requested device already open? */
    SOL_PTR_VECTOR_FOREACH_IDX (&devices, device, i) {
        if (sol_i2c_bus_get(device->i2c) == bus && device->slave == slave) {
            device->refcount++;
            return device;
        }
    }

    i2c = sol_i2c_open(bus, SOL_I2C_SPEED_10KBIT);
    SOL_NULL_CHECK_MSG(i2c, NULL, "Failed to open i2c bus");

    device = calloc(1, sizeof(struct am2315));
    if (!device)
        goto fail;

    device->i2c = i2c;
    device->slave = slave;
    device->refcount++;

    sol_ptr_vector_append(&devices, device);

    return device;

fail:
    sol_i2c_close(i2c);

    return NULL;
}

void
am2315_close(struct am2315 *device)
{
    struct am2315 *itr;
    int i;

    device->refcount--;
    if (device->refcount) return;

    if (device->timer)
        sol_timeout_del(device->timer);

    if (device->i2c_pending)
        sol_i2c_pending_cancel(device->i2c, device->i2c_pending);

    sol_i2c_close(device->i2c);

    SOL_PTR_VECTOR_FOREACH_IDX (&devices, itr, i) {
        if (itr == device) {
            sol_ptr_vector_del(&devices, i);
            break;
        }
    }

    free(device);
}

static uint16_t
_crc16(uint8_t *ptr, uint8_t len)
{
    uint16_t crc = 0xffff;
    uint8_t i;

    while (len--) {
        crc ^= *ptr++;
        for (i = 0; i < 8; i++) {
            if (crc & 0x01) {
                crc >>= 1;
                crc ^= 0xa001;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

static bool
_send_readings(void *data)
{
    struct am2315 *device = data;
    float temperature, humidity;

    temperature = device->temperature / READING_SCALE;
    humidity = device->humidity / READING_SCALE;

    if (device->temperature_callback) {
        while (device->pending_temperature--) {
            device->temperature_callback(temperature, device->success,
                device->temperature_callback_data);
        }
    }

    if (device->humidity_callback) {
        while (device->pending_humidity--) {
            device->humidity_callback(humidity, device->success,
                device->humidity_callback_data);
        }
    }

    return false;
}

static void
read_data_cb(void *cb_data, struct sol_i2c *i2c, uint8_t *data, ssize_t status)
{
    struct am2315 *device = cb_data;
    uint16_t crc;

    device->i2c_pending = NULL;
    if (status != AM2315_BUFFER_LEN) {
        SOL_WRN("Could not read sensor data");
        device->success = false;
        goto end;
    }

    /* CRC from first byte to 6th. iow, do not calculate CRC of CRC bytes */
    crc = _crc16(device->buffer, 6);
    if (device->buffer[6] != (crc & 0xff) || device->buffer[7] != (crc >> 8)) {
        SOL_WRN("Invalid sensor readings: CRC mismatch");
        device->success = false;
        goto end;
    }

    if (device->buffer[0] != AM2315_READ_REG || device->buffer[1] != AM2315_READ_LEN) {
        SOL_WRN("Invalid sensor readings: unexpected data");
        device->success = false;
        goto end;
    }

    /* TODO datasheet is ambiguous about temperature format: it appears to not be
     * complement of 2, but signal and magnitude. Indeed,
     * https://github.com/adafruit/Adafruit_AM2315/blob/master/Adafruit_AM2315.cpp
     * treats as so - but I couldn't confirm that. All other codes on internet
     * do not seem to care about it. */
    device->humidity = (device->buffer[2] << 8) | (device->buffer[3] & 0xff);
    device->temperature = (device->buffer[4] << 8) | (device->buffer[5] & 0xff);

    device->success = true;

end:
    _send_readings(device);
}

static bool
_read_data(void *data)
{
    struct am2315 *device = data;

    device->timer = NULL;
    if (sol_i2c_busy(device->i2c)) {
        timer_sched(device, STEP_TIME, _read_data);
        return false;
    }

    if (!sol_i2c_set_slave_address(device->i2c, device->slave)) {
        SOL_WRN("Failed to set slave at address 0x%02x", device->slave);
        goto error;
    }

    /* Read 8 bytes: 1st is the function code, 2nd is data lenght,
     * 3rd and 4th are humidity hi/lo, 5th and 6th are temperature
     * hi/lo, 7th and 8th are CRC code to validade data. */
    device->i2c_pending = sol_i2c_read(device->i2c, device->buffer,
        sizeof(device->buffer), read_data_cb, device);
    SOL_NULL_CHECK_MSG_GOTO(device->i2c_pending, error, "Could not read sensor data");

    return false;

error:
    device->success = false;
    _send_readings(device);

    return false;
}

static void
update_readings_cb(void *cb_data, struct sol_i2c *i2c, uint8_t *data, ssize_t status)
{
    struct am2315 *device = cb_data;

    device->i2c_pending = NULL;
    if (status < 0) {
        SOL_WRN("Could not read sensor");
        device->success = false;
        _send_readings(device);
        return;
    }

    /* Datasheet asks for some delay */
    timer_sched(device, 2, _read_data);
}

static bool
_update_readings(void *data)
{
    struct am2315 *device = data;
    time_t current_time;

    device->timer = NULL;
    if (sol_i2c_busy(device->i2c)) {
        timer_sched(device, STEP_TIME, _update_readings);
        return false;
    }

    device->buffer[0] = AM2315_READ_REG;
    device->buffer[1] = AM2315_HUMIDITY_HIGH;
    device->buffer[2] = AM2315_READ_LEN;

    current_time = time(NULL);
    if (current_time == -1) {
        SOL_WRN("Could not get current time");
        return false;
    }

    // TODO Should we care if time is changed backwards?
    if (current_time - device->last_reading <= AM2315_INTERVAL_BETWEEN_READINGS) {
        return false;
    }

    device->last_reading = current_time;

    if (!sol_i2c_set_slave_address(device->i2c, device->slave)) {
        SOL_WRN("Failed to set slave at address 0x%02x", device->slave);
        device->success = false;
        _send_readings(device);
        return false;
    }

    /* Write a message to read data */
    device->i2c_pending = sol_i2c_write(device->i2c, device->buffer,
        AM2315_WRITE_MSG_LEN, update_readings_cb, device);
    if (!device->i2c_pending) {
        SOL_WRN("Could not read sensor");
        device->success = false;
        _send_readings(device);
    }

    return false;
}

void
am2315_temperature_callback_set(struct am2315 *device, void (*cb)(float temperature, bool success, void *data), void *data)
{
    device->temperature_callback = cb;
    device->temperature_callback_data = data;
}

void
am2315_read_temperature(struct am2315 *device)
{
    device->pending_temperature++;
    _update_readings(device);
}

void
am2315_humidity_callback_set(struct am2315 *device, void (*cb)(float temperature, bool success, void *data), void *data)
{
    device->humidity_callback = cb;
    device->humidity_callback_data = data;
}

void
am2315_read_humidity(struct am2315 *device)
{
    device->pending_humidity++;
    _update_readings(device);
}
