/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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
#include <limits.h>
#include <errno.h>

#include <sol-iio.h>
#include <sol-log.h>
#include <sol-mainloop.h>
#include <sol-mqtt.h>

/* comment out following if you don't plan to use calibration and denoise */
#define GYRO_CALIBRATE
#define DENOISE_MEDIAN

#ifdef GYRO_CALIBRATE
#include <math.h>
#define GYRO_MAX_ERR 0.05
#define GYRO_DS_SIZE 100
#endif

#ifdef DENOISE_MEDIAN
#define GYRO_DENOISE_MAX_SAMPLES 5
#define GYRO_DENOISE_NUM_FIELDS 3
#endif

#define GYRO_DROP_SAMPLES 5 /* Drop first few gyro samples for noisy sensor */

#ifdef GYRO_CALIBRATE
struct gyro_cal {
    bool calibrated;
    double bias_x, bias_y, bias_z;
    int count;
    double min_x;
    double min_y;
    double min_z;
    double max_x;
    double max_y;
    double max_z;
};
#endif

#ifdef DENOISE_MEDIAN
struct filter_median {
    double *buff;
    unsigned int idx;
    unsigned int count;
    unsigned int sample_size;
};
#endif

struct iio_gyroscope_data {
    struct sol_iio_channel *channel_x;
    struct sol_iio_channel *channel_y;
    struct sol_iio_channel *channel_z;
    struct sol_mqtt *mqtt;
    char *mqtt_topic;
#ifdef GYRO_CALIBRATE
    struct gyro_cal cal_data;
#endif
#ifdef DENOISE_MEDIAN
    struct filter_median filter_data;
#endif
    unsigned int drop_samples_count;
};

static struct sol_timeout *timeout;

static bool
try_reconnect(void *data)
{
    SOL_INF("Try reconnect...");
    return sol_mqtt_reconnect((struct sol_mqtt *)data) != 0;
}

static void
on_connect(void *data, struct sol_mqtt *mqtt)
{
    if (sol_mqtt_get_connection_status(mqtt) == SOL_MQTT_CONNECTED) {
        SOL_INF("Connected...");
    } else {
        SOL_WRN("Unable to connect, retrying...");
        timeout = sol_timeout_add(1000, try_reconnect, mqtt);
        return;
    }
}

static void
on_disconnect(void *data, struct sol_mqtt *mqtt)
{
    SOL_INF("Disconnect...");
    sol_timeout_add(1000, try_reconnect, mqtt);
}

#ifdef GYRO_CALIBRATE
static void
reset_calibrate(struct gyro_cal *cal_data)
{
    cal_data->calibrated = false;
    cal_data->count = 0;
    cal_data->bias_x = cal_data->bias_y = cal_data->bias_z = 0;
    cal_data->min_x = cal_data->min_y = cal_data->min_z = 1.0;
    cal_data->max_x = cal_data->max_y = cal_data->max_z = -1.0;
}

static bool
gyro_collect(struct gyro_cal *cal_data, double x, double y, double z)
{
    /* Analyze gyroscope data */

    if (fabs(x) >= 1 || fabs(y) >= 1 || fabs(z) >= 1) {
        /* We're supposed to be standing still ; start over */
        reset_calibrate(cal_data);

        return false; /* Uncalibrated */
    }

    /* Thanks to https://github.com/01org/android-iio-sensors-hal
     * for calibration algorithm
     */
    if (cal_data->count < GYRO_DS_SIZE) {
        if (x < cal_data->min_x)
            cal_data->min_x = x;

        if (y < cal_data->min_y)
            cal_data->min_y = y;

        if (z < cal_data->min_z)
            cal_data->min_z = z;

        if (x > cal_data->max_x)
            cal_data->max_x = x;

        if (y > cal_data->max_y)
            cal_data->max_y = y;

        if (z > cal_data->max_z)
            cal_data->max_z = z;

        if (fabs(cal_data->max_x - cal_data->min_x) <= GYRO_MAX_ERR &&
            fabs(cal_data->max_y - cal_data->min_y) <= GYRO_MAX_ERR &&
            fabs(cal_data->max_z - cal_data->min_z) <= GYRO_MAX_ERR)
            cal_data->count++; /* One more conformant sample */
        else {
            /* Out of spec sample ; start over */
            reset_calibrate(cal_data);
        }

        return false; /* Still uncalibrated */
    }

    /* We got enough stable samples to estimate gyroscope bias */
    cal_data->bias_x = (cal_data->max_x + cal_data->min_x) / 2;
    cal_data->bias_y = (cal_data->max_y + cal_data->min_y) / 2;
    cal_data->bias_z = (cal_data->max_z + cal_data->min_z) / 2;

    return true; /* Calibrated! */
}

static void
clamp_gyro_readings_to_zero(struct gyro_cal *cal_data, double *x, double *y, double *z)
{
    double near_zero;

    /* If we're calibrated, don't filter out as much */
    if (cal_data->calibrated)
        near_zero = 0.02; /* rad/s */
    else
        near_zero = 0.1;

    /* If motion on all axes is small enough */
    if (fabs(*x) < near_zero && fabs(*y) < near_zero && fabs(*z) < near_zero) {
        /*
         * Report that we're not moving at all... but not exactly zero
         * as composite sensors(orientation, rotation vector) don't seem
         * to react very well to it.
         */

        *x *= 0.000001;
        *y *= 0.000001;
        *z *= 0.000001;
    }
}
#endif

#ifdef DENOISE_MEDIAN
static unsigned int
partition(double *list, unsigned int left, unsigned int right, unsigned int pivot_index)
{
    unsigned int i;
    unsigned int store_index = left;
    double aux;
    double pivot_value = list[pivot_index];

    /* Swap list[pivotIndex] and list[right] */
    aux = list[pivot_index];
    list[pivot_index] = list[right];
    list[right] = aux;

    for (i = left; i < right; i++) {
        if (list[i] < pivot_value) {
            /* Swap list[store_index] and list[i] */
            aux = list[store_index];
            list[store_index] = list[i];
            list[i] = aux;
            store_index++;
        }
    }

    /* Swap list[right] and list[store_index] */
    aux = list[right];
    list[right] = list[store_index];
    list[store_index] = aux;
    return store_index;
}

static double
median(double *queue, unsigned int size)
{
    /* http://en.wikipedia.org/wiki/Quickselect */

    unsigned int left = 0;
    unsigned int right = size - 1;
    unsigned int pivot_index;
    unsigned int median_index = (right / 2);
    double temp[size];

    memcpy(temp, queue, size * sizeof(double));

    /* If the list has only one element return it */
    if (left == right)
        return temp[left];

    while (left < right) {
        pivot_index = (left + right) / 2;
        pivot_index = partition(temp, left, right, pivot_index);
        if (pivot_index == median_index)
            return temp[median_index];
        else if (pivot_index > median_index)
            right = pivot_index - 1;
        else
            left = pivot_index + 1;
    }

    return temp[left];
}

static void
denoise_median(struct filter_median *filter_data, double *x, double *y, double *z)
{
    /* Thanks to https://github.com/01org/android-iio-sensors-hal
     * for denoise algorithm
     */
    unsigned int offset;

    if (filter_data->count < filter_data->sample_size)
        filter_data->count++;

    offset = 0;
    filter_data->buff[offset + filter_data->idx] = *x;
    *x = median(filter_data->buff + offset, filter_data->count);

    offset = filter_data->sample_size * 1;
    filter_data->buff[offset + filter_data->idx] = *y;
    *y = median(filter_data->buff + offset, filter_data->count);

    offset = filter_data->sample_size * 2;
    filter_data->buff[offset + filter_data->idx] = *z;
    *z = median(filter_data->buff + offset, filter_data->count);

    filter_data->idx = (filter_data->idx + 1) % filter_data->sample_size;
}

#endif

static void
iio_gyroscope_reader_cb(void *data, struct sol_iio_device *device)
{
    int r;
    char buffer[128];
    struct iio_gyroscope_data *gyro_data = data;
    struct sol_direction_vector out;
    struct sol_buffer payload;
    struct sol_mqtt_message mqtt_message;

#ifdef GYRO_CALIBRATE
    struct gyro_cal *cal_data = &(gyro_data->cal_data);
#endif
#ifdef DENOISE_MEDIAN
    struct filter_median *filter_data = &(gyro_data->filter_data);
#endif
    r = sol_iio_read_channel_value(gyro_data->channel_x, &out.x);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    r = sol_iio_read_channel_value(gyro_data->channel_y, &out.y);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    r = sol_iio_read_channel_value(gyro_data->channel_z, &out.z);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    /*
     * For noisy sensors drop a few samples to make sure we have at least
     * GYRO_DROP_SAMPLES events in the filtering queue.
     * This improves mean and std dev.
     */
    if (gyro_data->drop_samples_count < GYRO_DROP_SAMPLES) {
        gyro_data->drop_samples_count++;
        return;
    }

    sol_iio_mount_calibration(device, &out);

#ifdef GYRO_CALIBRATE
    if (cal_data->calibrated == false)
        cal_data->calibrated = gyro_collect(cal_data, out.x, out.y, out.z);

    out.x = out.x - cal_data->bias_x;
    out.y = out.y - cal_data->bias_y;
    out.z = out.z - cal_data->bias_z;
#endif

#ifdef DENOISE_MEDIAN
    denoise_median(filter_data, &out.x, &out.y, &out.z);
#endif

#ifdef GYRO_CALIBRATE
    clamp_gyro_readings_to_zero(cal_data, &out.x, &out.y, &out.z);
#endif

#ifdef GYRO_CALIBRATE
    snprintf(buffer, sizeof(buffer) - 1, "%f\t%f\t%f\t[rad/sec]\t(%s)", out.x, out.y, out.z,
        (cal_data->calibrated ? "Calibrated" : "Not calibrated"));
#else
    snprintf(buffer, sizeof(buffer) - 1, "%f\t%f\t%f\t[rad/sec]\t(Calibration disabled)",
        out.x, out.y, out.z);
#endif
    printf("%s\n", buffer);

    if (gyro_data->mqtt == NULL) {
        SOL_WRN("mqtt is null");
        return;
    }
    if (sol_mqtt_get_connection_status(gyro_data->mqtt) == SOL_MQTT_CONNECTED) {

        payload = SOL_BUFFER_INIT_CONST(buffer, strlen(buffer));

        mqtt_message = (struct sol_mqtt_message){
            SOL_SET_API_VERSION(.api_version = SOL_MQTT_MESSAGE_API_VERSION, )
            .topic = gyro_data->mqtt_topic,
            .payload = &payload,
            .qos = SOL_MQTT_QOS_EXACTLY_ONCE,
            .retain = false,
        };

        if (sol_mqtt_publish(gyro_data->mqtt, &mqtt_message)) {
            SOL_WRN("Unable to publish message");
        }
    }
    return;

error:
    SOL_WRN("Could not read channel buffer values");
}

int
main(int argc, char *argv[])
{
    struct sol_iio_device *device = NULL;
    struct sol_iio_config iio_config = {};
    struct sol_iio_channel_config channel_config = SOL_IIO_CHANNEL_CONFIG_INIT;
    struct iio_gyroscope_data gyro_data = {};
    struct sol_mqtt_config mqtt_config = {
        SOL_SET_API_VERSION(.api_version = SOL_MQTT_CONFIG_API_VERSION, )
        .clean_session = true,
        .keep_alive = 60,
        .handlers = {
            SOL_SET_API_VERSION(.api_version = SOL_MQTT_HANDLERS_API_VERSION, )
            .connect = on_connect,
            .disconnect = on_disconnect,
        },
    };
    int device_id;

    if (argc < 11) {
        fprintf(stderr, "\nUsage: %s <device name> <trigger name> <buffer size> " \
            "<sampling frequency> <scale> <custom offset> <offset> " \
            "<MQTT broker ip> <MQTT broker port> <MQTT topic>\n" \
            "\t<buffer size>:\t\t0=default\n" \
            "\t<sampling frequency>:\t-1=default\n" \
            "\t<scale>:\t\t<-1=default\n" \
            "\t<custom offset>:\ty or n\n" \
            "\t<offset>:\t\tonly take effect if custom offset is \"y\"\n" \
            "Press CTRL + C to quit\n" \
            , argv[0]);
        return 0;
    }

    sol_init();

    device_id = sol_iio_address_device(argv[1]);
    SOL_INT_CHECK_GOTO(device_id, < 0, error_iio);

    SOL_SET_API_VERSION(iio_config.api_version = SOL_IIO_CONFIG_API_VERSION; )
    iio_config.trigger_name = strdup(argv[2]);
    SOL_NULL_CHECK_GOTO(iio_config.trigger_name, error_iio);

    iio_config.buffer_size = atoi(argv[3]);
    iio_config.sampling_frequency = atoi(argv[4]);
    channel_config.scale = atof(argv[5]);
    if (strncmp(argv[6], "y", 1) == 0) {
        channel_config.use_custom_offset = true;
        channel_config.offset = atoi(argv[7]);
    } else
        channel_config.use_custom_offset = false;

    iio_config.data = &gyro_data;
    iio_config.sol_iio_reader_cb = iio_gyroscope_reader_cb;

    mqtt_config.host = argv[8];
    mqtt_config.port = atoi(argv[9]);
    gyro_data.mqtt_topic = argv[10];

    device = sol_iio_open(device_id, &iio_config);
    SOL_NULL_CHECK_GOTO(device, error_iio);

    gyro_data.channel_x = sol_iio_add_channel(device, "in_anglvel_x",
        &channel_config);
    SOL_NULL_CHECK_GOTO(gyro_data.channel_x, error_iio);

    gyro_data.channel_y = sol_iio_add_channel(device, "in_anglvel_y",
        &channel_config);
    SOL_NULL_CHECK_GOTO(gyro_data.channel_y, error_iio);

    gyro_data.channel_z = sol_iio_add_channel(device, "in_anglvel_z",
        &channel_config);
    SOL_NULL_CHECK_GOTO(gyro_data.channel_z, error_iio);

#ifdef GYRO_CALIBRATE
    reset_calibrate(&(gyro_data.cal_data));
#endif
    gyro_data.drop_samples_count = 0;

#ifdef DENOISE_MEDIAN
    gyro_data.filter_data.buff = (double *)calloc(GYRO_DENOISE_MAX_SAMPLES,
        sizeof(double) * GYRO_DENOISE_NUM_FIELDS);
    SOL_NULL_CHECK_GOTO(gyro_data.filter_data.buff, error_iio);

    gyro_data.filter_data.sample_size = GYRO_DENOISE_MAX_SAMPLES;
    gyro_data.filter_data.count = 0;
    gyro_data.filter_data.idx = 0;
#endif

    sol_iio_device_start_buffer(device);

    gyro_data.mqtt = sol_mqtt_connect(&mqtt_config);
    SOL_NULL_CHECK_GOTO(gyro_data.mqtt, error_iio);

    sol_run();

    free((char *)iio_config.trigger_name);
    iio_config.trigger_name = NULL;

#ifdef DENOISE_MEDIAN
    free(gyro_data.filter_data.buff);
    gyro_data.filter_data.buff = NULL;
#endif

    /* Must do sol_iio_close(device), it will disable IIO buffer.
     * If not, the trigger, buffer size and buffer enable cannot be set
     * on the next launch.
     */
    sol_iio_close(device);

    if (timeout)
        sol_timeout_del(timeout);

    sol_mqtt_disconnect(gyro_data.mqtt);

    sol_shutdown();
    return 0;

error_iio:
    if (device) {
        /* Must do sol_iio_close(device), it will disable IIO buffer.
         * If not, the trigger, buffer size and buffer enable cannot be set
         * on the next launch.
         */
        sol_iio_close(device);
    }

    if (timeout)
        sol_timeout_del(timeout);

    if (iio_config.trigger_name) {
        free((char *)iio_config.trigger_name);
        iio_config.trigger_name = NULL;
    }

#ifdef DENOISE_MEDIAN
    if (gyro_data.filter_data.buff) {
        free(gyro_data.filter_data.buff);
        gyro_data.filter_data.buff = NULL;
    }
#endif

    sol_shutdown();
    return -1;
}
