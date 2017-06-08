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
#include <math.h>

#include <sol-iio.h>
#include <sol-log.h>
#include <sol-mainloop.h>
#include <sol-mqtt.h>

/* comment out following if you don't plan to use calibration and denoise */
#define MAGN_CALIBRATE
#define DENOISE_AVERAGE

#ifdef MAGN_CALIBRATE
#include "matrix-ops.h"
#define MAGN_DS_SIZE 32
#define CONVERT_GAUSS_TO_MICROTESLA(x) ((x) * 100)
#define EPSILON 0.000000001
#define MAGNETIC_LOW 960 /* 31 micro tesla squared */
#define CAL_STEPS 5
#endif

#ifdef DENOISE_AVERAGE
/* Filter defines */
#define FILTER_MAX_SAMPLE 20
#define FILTER_NUM_FIELD 3
#endif

#ifdef MAGN_CALIBRATE
struct compass_cal {
    int cal_level;
    /* hard iron offsets */
    double offset[3][1];

    /* soft iron matrix */
    double w_invert[3][3];

    /* geomagnetic strength */
    double bfield;

    /* selection data */
    double sample[MAGN_DS_SIZE][3];
    unsigned int sample_count;
    double average[3];
};
#endif

#ifdef DENOISE_AVERAGE
struct filter_average {
    int max_samples;        /* Maximum averaging window size */
    int num_fields;         /* Number of fields per sample (usually 3) */
    double *history;        /* Working buffer containing recorded samples */
    double *history_sum;    /* The current sum of the history elements */
    int history_size;       /* Number of recorded samples */
    int history_entries;    /* How many of these are initialized */
    int history_index;      /* Index of sample to evict next time */
};
#endif

struct iio_magnetometer_data {
    struct sol_iio_channel *channel_x;
    struct sol_iio_channel *channel_y;
    struct sol_iio_channel *channel_z;
    struct sol_mqtt *mqtt;
    char *mqtt_topic;
#ifdef MAGN_CALIBRATE
    struct compass_cal cal_data;
#endif
#ifdef DENOISE_AVERAGE
    struct filter_average filter;
#endif
    int sampling_frequency;
};

static struct sol_timeout *timeout;

#ifdef MAGN_CALIBRATE
/* We'll have multiple calibration levels so that we can provide an estimation
 * as fast as possible
 */
static const double min_diffs[CAL_STEPS] = { 0.2, 0.25, 0.4, 0.6, 1.0 };
static const double max_sqr_errs[CAL_STEPS] = { 10.0, 10.0, 8.0, 5.0, 3.5 };
static const unsigned int lookback_counts[CAL_STEPS] = { 2, 3, 4, 5, 6 };
typedef double mat_input_t[MAGN_DS_SIZE][3];
#endif

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
    }
}

static void
on_disconnect(void *data, struct sol_mqtt *mqtt)
{
    SOL_INF("Disconnect...");
    sol_timeout_add(1000, try_reconnect, mqtt);
}

#ifdef MAGN_CALIBRATE
/* Reset calibration algorithm */
static void
reset_sample(struct compass_cal *cal_data)
{
    int i, j;

    cal_data->sample_count = 0;
    for (i = 0; i < MAGN_DS_SIZE; i++)
        for (j = 0; j < 3; j++)
            cal_data->sample[i][j] = 0;

    cal_data->average[0] = cal_data->average[1] = cal_data->average[2] = 0;
}

static double
calc_square_err(struct compass_cal *data)
{
    double err = 0;
    double raw[3][1], result[3][1], mat_diff[3][1];
    int i;
    float stdev[3] = { 0, 0, 0 };
    double diff;

    for (i = 0; i < MAGN_DS_SIZE; i++) {
        raw[0][0] = data->sample[i][0];
        raw[1][0] = data->sample[i][1];
        raw[2][0] = data->sample[i][2];

        stdev[0] += (raw[0][0] - data->average[0]) * (raw[0][0] - data->average[0]);
        stdev[1] += (raw[1][0] - data->average[1]) * (raw[1][0] - data->average[1]);
        stdev[2] += (raw[2][0] - data->average[2]) * (raw[2][0] - data->average[2]);

        substract(3, 1, raw, data->offset, mat_diff);
        multiply(3, 3, 1, data->w_invert, mat_diff, result);

        diff = sqrt(result[0][0] * result[0][0] + result[1][0] * result[1][0] + result[2][0] * result[2][0]) - data->bfield;

        err += diff * diff;
    }

    stdev[0] = sqrt(stdev[0] / MAGN_DS_SIZE);
    stdev[1] = sqrt(stdev[1] / MAGN_DS_SIZE);
    stdev[2] = sqrt(stdev[2] / MAGN_DS_SIZE);

    /* A sanity check - if we have too little variation for an axis it's best
     * to reject the calibration than risking a wrong calibration
     */
    if (stdev[0] <= 1 || stdev[1] <= 1 || stdev[2] <= 1)
        return max_sqr_errs[0];

    err /= MAGN_DS_SIZE;
    return err;
}

/* Given an real symmetric 3x3 matrix A, compute the eigenvalues */
static void
compute_eigenvalues(double mat[3][3], double *eig1, double *eig2, double *eig3)
{
    double phi;
    double p = mat[0][1] * mat[0][1] + mat[0][2] * mat[0][2] + mat[1][2] * mat[1][2];
    double q = (mat[0][0] + mat[1][1] + mat[2][2]) / 3;
    double temp1;
    double temp2;
    double temp3;
    double mat2[3][3];
    double r;

    if (p < EPSILON) {
        *eig1 = mat[0][0];
        *eig2 = mat[1][1];
        *eig3 = mat[2][2];
        return;
    }

    q = (mat[0][0] + mat[1][1] + mat[2][2]) / 3;
    temp1 = mat[0][0] - q;
    temp2 = mat[1][1] - q;
    temp3 = mat[2][2] - q;

    p = temp1 * temp1 + temp2 * temp2 + temp3 * temp3 + 2 * p;
    p = sqrt(p / 6);

    assign(3, 3, mat, mat2);
    mat2[0][0] -= q;
    mat2[1][1] -= q;
    mat2[2][2] -= q;
    multiply_scalar_inplace(3, 3, mat2, 1 / p);

    r = (mat2[0][0] * mat2[1][1] * mat2[2][2] + mat2[0][1] * mat2[1][2] * mat2[2][0]
        + mat2[0][2] * mat2[1][0] * mat2[2][1] - mat2[0][2] * mat2[1][1] * mat2[2][0]
        - mat2[0][0] * mat2[1][2] * mat2[2][1] - mat2[0][1] * mat2[1][0] * mat2[2][2]) / 2;

    if (r <= -1.0)
        phi = M_PI / 3;
    else if (r >= 1.0)
        phi = 0;
    else
        phi = acos(r) / 3;

    *eig3 = q + 2 *p *cos(phi);
    *eig1 = q + 2 *p *cos(phi + 2 *M_PI / 3);
    *eig2 = 3 * q - *eig1 - *eig3;
}

static void
calc_evector(double mat[3][3], double eig, double vec[3][1])
{
    double h[3][3];
    double x_tmp[2][2];
    double x[2][2];
    double temp1;
    double temp2;
    double norm;

    assign(3, 3, mat, h);
    h[0][0] -= eig;
    h[1][1] -= eig;
    h[2][2] -= eig;

    x[0][0] = h[1][1];
    x[0][1] = h[1][2];
    x[1][0] = h[2][1];
    x[1][1] = h[2][2];
    invert(2, x, x_tmp);
    assign(2, 2, x_tmp, x);

    temp1 = x[0][0] * (-h[1][0]) + x[0][1] * (-h[2][0]);
    temp2 = x[1][0] * (-h[1][0]) + x[1][1] * (-h[2][0]);
    norm = sqrt(1 + temp1 * temp1 + temp2 * temp2);

    vec[0][0] = 1.0 / norm;
    vec[1][0] = temp1 / norm;
    vec[2][0] = temp2 / norm;
}

static int
ellipsoid_fit(mat_input_t m, double offset[3][1], double w_invert[3][3], double *bfield)
{
    int i;
    double h[MAGN_DS_SIZE][9];
    double w[MAGN_DS_SIZE][1];
    double h_trans[9][MAGN_DS_SIZE];
    double p_temp1[9][9];
    double p_temp2[9][MAGN_DS_SIZE];
    double temp1[3][3], temp[3][3];
    double temp1_inv[3][3];
    double temp2[3][1];
    double result[9][9];
    double p[9][1];
    double a[3][3], sqrt_evals[3][3], evecs[3][3], evecs_trans[3][3];
    double evec1[3][1], evec2[3][1], evec3[3][1];
    double off_x;
    double off_y;
    double off_z;
    double eig1 = 0, eig2 = 0, eig3 = 0;

    for (i = 0; i < MAGN_DS_SIZE; i++) {
        w[i][0] = m[i][0] * m[i][0];
        h[i][0] = m[i][0];
        h[i][1] = m[i][1];
        h[i][2] = m[i][2];
        h[i][3] = -1 * m[i][0] * m[i][1];
        h[i][4] = -1 * m[i][0] * m[i][2];
        h[i][5] = -1 * m[i][1] * m[i][2];
        h[i][6] = -1 * m[i][1] * m[i][1];
        h[i][7] = -1 * m[i][2] * m[i][2];
        h[i][8] = 1;
    }

    transpose(MAGN_DS_SIZE, 9, h, h_trans);
    multiply(9, MAGN_DS_SIZE, 9, h_trans, h, result);
    invert(9, result, p_temp1);
    multiply(9, 9, MAGN_DS_SIZE, p_temp1, h_trans, p_temp2);
    multiply(9, MAGN_DS_SIZE, 1, p_temp2, w, p);

    temp1[0][0] = 2;
    temp1[0][1] = p[3][0];
    temp1[0][2] = p[4][0];
    temp1[1][0] = p[3][0];
    temp1[1][1] = 2 * p[6][0];
    temp1[1][2] = p[5][0];
    temp1[2][0] = p[4][0];
    temp1[2][1] = p[5][0];
    temp1[2][2] = 2 * p[7][0];

    temp2[0][0] = p[0][0];
    temp2[1][0] = p[1][0];
    temp2[2][0] = p[2][0];

    invert(3, temp1, temp1_inv);
    multiply(3, 3, 1, temp1_inv, temp2, offset);
    off_x = offset[0][0];
    off_y = offset[1][0];
    off_z = offset[2][0];

    a[0][0] = 1.0 / (p[8][0] + off_x * off_x + p[6][0] * off_y * off_y
        + p[7][0] * off_z * off_z + p[3][0] * off_x * off_y
        + p[4][0] * off_x * off_z + p[5][0] * off_y * off_z);

    a[0][1] = p[3][0] * a[0][0] / 2;
    a[0][2] = p[4][0] * a[0][0] / 2;
    a[1][2] = p[5][0] * a[0][0] / 2;
    a[1][1] = p[6][0] * a[0][0];
    a[2][2] = p[7][0] * a[0][0];
    a[2][1] = a[1][2];
    a[1][0] = a[0][1];
    a[2][0] = a[0][2];

    compute_eigenvalues(a, &eig1, &eig2, &eig3);

    if (eig1 <= 0 || eig2 <= 0 || eig3 <= 0)
        return 0;

    sqrt_evals[0][0] = sqrt(eig1);
    sqrt_evals[1][0] = 0;
    sqrt_evals[2][0] = 0;
    sqrt_evals[0][1] = 0;
    sqrt_evals[1][1] = sqrt(eig2);
    sqrt_evals[2][1] = 0;
    sqrt_evals[0][2] = 0;
    sqrt_evals[1][2] = 0;
    sqrt_evals[2][2] = sqrt(eig3);

    calc_evector(a, eig1, evec1);
    calc_evector(a, eig2, evec2);
    calc_evector(a, eig3, evec3);

    evecs[0][0] = evec1[0][0];
    evecs[1][0] = evec1[1][0];
    evecs[2][0] = evec1[2][0];
    evecs[0][1] = evec2[0][0];
    evecs[1][1] = evec2[1][0];
    evecs[2][1] = evec2[2][0];
    evecs[0][2] = evec3[0][0];
    evecs[1][2] = evec3[1][0];
    evecs[2][2] = evec3[2][0];

    multiply(3, 3, 3, evecs, sqrt_evals, temp1);
    transpose(3, 3, evecs, evecs_trans);
    multiply(3, 3, 3, temp1, evecs_trans, temp);
    transpose(3, 3, temp, w_invert);

    *bfield = pow(sqrt(1 / eig1) * sqrt(1 / eig2) * sqrt(1 / eig3), 1.0 / 3.0);

    if (*bfield < 0)
        return 0;

    multiply_scalar_inplace(3, 3, w_invert, *bfield);

    return 1;
}
static void
compass_cal_init(struct compass_cal *cal_data)
{
    cal_data->cal_level = 0;
    reset_sample(cal_data);

    cal_data->offset[0][0] = 0;
    cal_data->offset[1][0] = 0;
    cal_data->offset[2][0] = 0;
    cal_data->w_invert[0][0] = 1;
    cal_data->w_invert[1][0] = 0;
    cal_data->w_invert[2][0] = 0;
    cal_data->w_invert[0][1] = 0;
    cal_data->w_invert[1][1] = 1;
    cal_data->w_invert[2][1] = 0;
    cal_data->w_invert[0][2] = 0;
    cal_data->w_invert[1][2] = 0;
    cal_data->w_invert[2][2] = 1;
    cal_data->bfield = 0;
}

static void
scale(double *x, double *y, double *z)
{
    double sqr_norm = 0;
    double sanity_norm = 0;
    double scale = 1;

    sqr_norm = (*x * *x + *y * *y + *z * *z);

    if (sqr_norm < MAGNETIC_LOW)
        sanity_norm = MAGNETIC_LOW;

    if (fpclassify(sanity_norm) != FP_ZERO && fpclassify(sqr_norm) != FP_ZERO) {
        scale = sanity_norm / sqr_norm;
        scale = sqrt(scale);
        *x = *x * scale;
        *y = *y * scale;
        *z = *z * scale;
    }
}

static int
compass_ready(struct compass_cal *cal_data)
{
    mat_input_t mat;
    int i;
    float max_sqr_err;

    struct compass_cal new_cal_data;

    /*
     * Some sensors take unrealistically long to calibrate at higher levels.
     * We'll use a max_cal_level if we have such a property setup, or go with
     * the default settings if not.
     */
    int cal_steps = CAL_STEPS;

    if (cal_data->sample_count < MAGN_DS_SIZE)
        return cal_data->cal_level;

    max_sqr_err = max_sqr_errs[cal_data->cal_level];

    /* Enough points have been collected, do the ellipsoid calibration */

    /* Compute average per axis */
    cal_data->average[0] /= MAGN_DS_SIZE;
    cal_data->average[1] /= MAGN_DS_SIZE;
    cal_data->average[2] /= MAGN_DS_SIZE;

    for (i = 0; i < MAGN_DS_SIZE; i++) {
        mat[i][0] = cal_data->sample[i][0];
        mat[i][1] = cal_data->sample[i][1];
        mat[i][2] = cal_data->sample[i][2];
    }

    /* Check if result is good. The sample data must remain the same */
    new_cal_data = *cal_data;

    if (ellipsoid_fit(mat, new_cal_data.offset, new_cal_data.w_invert, &new_cal_data.bfield)) {
        double new_err = calc_square_err(&new_cal_data);
#ifdef DEBUG
        printf("new err is %f, max sqr err id %f\n", new_err, max_sqr_err);
#endif
        if (new_err < max_sqr_err) {
            double err = calc_square_err(cal_data);
            if (new_err < err) {
                /* New cal data is better, so we switch to the new */
                memcpy(cal_data->offset, new_cal_data.offset, sizeof(cal_data->offset));
                memcpy(cal_data->w_invert, new_cal_data.w_invert, sizeof(cal_data->w_invert));
                cal_data->bfield = new_cal_data.bfield;
                if (cal_data->cal_level < (cal_steps - 1))
                    cal_data->cal_level++;
#ifdef DEBUG
                printf("CompassCalibration: ready check success, caldata: %f %f %f %f %f %f %f %f "
                    "%f %f %f %f %f, err %f\n",
                    cal_data->offset[0][0],
                    cal_data->offset[1][0],
                    cal_data->offset[2][0],
                    cal_data->w_invert[0][0],
                    cal_data->w_invert[0][1],
                    cal_data->w_invert[0][2],
                    cal_data->w_invert[1][0],
                    cal_data->w_invert[1][1],
                    cal_data->w_invert[1][2],
                    cal_data->w_invert[2][0],
                    cal_data->w_invert[2][1],
                    cal_data->w_invert[2][2],
                    cal_data->bfield,
                    new_err);
#endif
            }
        }
    }
    reset_sample(cal_data);
    return cal_data->cal_level;
}

static int
compass_collect(struct compass_cal *cal_data, double *x, double *y, double *z)
{
    double data[FILTER_NUM_FIELD] = { *x, *y, *z };
    unsigned int index, j;
    unsigned int lookback_count;
    double min_diff;

    /* Discard the point if not valid */
    if (fpclassify(data[0]) == FP_ZERO || fpclassify(data[1]) == FP_ZERO || fpclassify(data[2]) == FP_ZERO)
        return -1;

    lookback_count = lookback_counts[cal_data->cal_level];
    min_diff = min_diffs[cal_data->cal_level];

    /* For the current point to be accepted, each x/y/z value must be different
     * enough to the last several collected points
     */
    if (cal_data->sample_count > 0 && cal_data->sample_count < MAGN_DS_SIZE) {
        unsigned int lookback =
            lookback_count < cal_data->sample_count ? lookback_count : cal_data->sample_count;
        for (index = 0; index < lookback; index++)
            for (j = 0; j < FILTER_NUM_FIELD; j++)
                if (fabsf(data[j] - cal_data->sample[cal_data->sample_count - 1 - index][j]) <
                    min_diff) {
#ifdef DEBUG
                    printf("CompassCalibration:point reject: [%f,%f,%f], selected_count=%d\n",
                        data[0],
                        data[1],
                        data[2],
                        cal_data->sample_count);
#endif
                    return 0;
                }
    }

    if (cal_data->sample_count < MAGN_DS_SIZE) {
        memcpy(cal_data->sample[cal_data->sample_count], data, sizeof(double) * 3);
        cal_data->sample_count++;
        cal_data->average[0] += data[0];
        cal_data->average[1] += data[1];
        cal_data->average[2] += data[2];
#ifdef DEBUG
        printf("CompassCalibration:point collected [%f,%f,%f], selected_count=%d\n",
            data[0],
            data[1],
            data[2],
            cal_data->sample_count);
#endif
    }
    return 1;
}

static void
compass_compute_cal(struct compass_cal *cal_data, double *x, double *y, double *z)
{
    double result[3][1], raw[3][1], diff[3][1];

    if (!cal_data->cal_level)
        return;

    raw[0][0] = *x;
    raw[1][0] = *y;
    raw[2][0] = *z;

    substract(3, 1, raw, cal_data->offset, diff);
    multiply(3, 3, 1, cal_data->w_invert, diff, result);

    *x = result[0][0];
    *y = result[1][0];
    *z = result[2][0];

    scale(x, y, z);
}

static void
calibrate_compass(struct compass_cal *cal_data, double *x, double *y, double *z)
{
    /* Thanks to https://github.com/01org/android-iio-sensors-hal for calibration algorithm */

    /* Calibration is continuous */
    compass_collect(cal_data, x, y, z);

    compass_ready(cal_data);

    if (cal_data->cal_level == 0)
        scale(x, y, z);
    else
        compass_compute_cal(cal_data, x, y, z);
}
#endif

#ifdef DENOISE_AVERAGE
static void
denoise_average(int sampling_frequency, struct filter_average *filter, double *x, double *y, double *z)
{
    /* Thanks to https://github.com/01org/android-iio-sensors-hal for denoise algorithm */
    /*
     * Smooth out incoming data using a moving average over a number of
     * samples. We accumulate one second worth of samples, or max_samples,
     * depending on which is lower.
     */
    double *data[3];
    int f;
    int history_size;
    int history_full = 0;

    data[0] = x;
    data[1] = y;
    data[2] = z;

    /* Don't denoise anything if we have less than two samples per second */
    if (sampling_frequency < 2)
        return;

    if (!filter)
        return;

    /* Restrict window size to the min of sampling_rate and max_samples */
    if (sampling_frequency > filter->max_samples)
        history_size = filter->max_samples;
    else
        history_size = sampling_frequency;

    /* Reset history if we're operating on an incorrect window size */
    if (filter->history_size != history_size) {
        filter->history_size = history_size;
        filter->history_entries = 0;
        filter->history_index = 0;
        filter->history =
            (double *)realloc(filter->history, filter->history_size * filter->num_fields * sizeof(double));
        if (filter->history) {
            filter->history_sum =
                (double *)realloc(filter->history_sum, filter->num_fields * sizeof(double));
            if (filter->history_sum)
                memset(filter->history_sum, 0, filter->num_fields * sizeof(double));
        }
    }

    if (!filter->history || !filter->history_sum) {
        SOL_ERR("Failed to allocate memory for history or history sum");
        return; /* Unlikely, but still... */
    }

    /* Update initialized samples count */
    if (filter->history_entries < filter->history_size)
        filter->history_entries++;
    else
        history_full = 1;

    /* Record new sample and calculate the moving sum */
    for (f = 0; f < filter->num_fields; f++) {
        /** A field is going to be overwritten if history is full, so decrease the history sum */
        if (history_full)
            filter->history_sum[f] -=
                filter->history[filter->history_index * filter->num_fields + f];

        filter->history[filter->history_index * filter->num_fields + f] = *data[f];
        filter->history_sum[f] += *data[f];

        /* For now simply compute a mobile mean for each field and output filtered data */
        *data[f] = filter->history_sum[f] / filter->history_entries;
    }

    /* Update our rolling index (next evicted cell) */
    filter->history_index = (filter->history_index + 1) % filter->history_size;
}
#endif

static void
iio_magnetometer_reader_cb(void *data, struct sol_iio_device *device)
{
    int r;
    char buffer[128];
    struct iio_magnetometer_data *magn_data = data;
    struct sol_direction_vector out;
    double azimuth;
    struct sol_buffer payload;
    struct sol_mqtt_message mqtt_message;

#ifdef MAGN_CALIBRATE
    struct compass_cal *cal_data = &(magn_data->cal_data);
#endif
#ifdef DENOISE_AVERAGE
    struct filter_average *filter = &(magn_data->filter);
#endif
    r = sol_iio_read_channel_value(magn_data->channel_x, &out.x);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    r = sol_iio_read_channel_value(magn_data->channel_y, &out.y);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    r = sol_iio_read_channel_value(magn_data->channel_z, &out.z);
    SOL_INT_CHECK_GOTO(r, < 0, error);

#ifdef MAGN_CALIBRATE
    out.x = CONVERT_GAUSS_TO_MICROTESLA(out.x);
    out.y = CONVERT_GAUSS_TO_MICROTESLA(out.y);
    out.z = CONVERT_GAUSS_TO_MICROTESLA(out.z);

    sol_iio_mount_calibration(device, &out);

    calibrate_compass(cal_data, &out.x, &out.y, &out.z);
#endif

#ifdef DENOISE_AVERAGE
    denoise_average(magn_data->sampling_frequency, filter, &out.x, &out.y, &out.z);
#endif

    if ((fpclassify(out.x) == FP_ZERO) && (fpclassify(out.y) == FP_ZERO)) {
        SOL_ERR("Point (0, 0) is invalid!\n");
        return;
    }

    if (fpclassify(out.x) == FP_ZERO) {
        if (out.y > 0)
            azimuth = 0;
        else
            azimuth = 180;
    } else if (fpclassify(out.y) == FP_ZERO) {
        if (out.x > 0)
            azimuth = 90;
        else
            azimuth = 270;
    } else {
        if (out.x > 0)
            azimuth = 90 - atan(out.y / out.x) * 180 / M_PI;
        else
            azimuth = 270 - atan(out.y / out.x) * 180 / M_PI;
    }

#ifdef MAGN_CALIBRATE
    snprintf(buffer, sizeof(buffer) - 1, "%d\t%d\t%d\t[uT]\t\t(Azimuth:%d)(Calibrated Level:%d)"
        , (int)out.x, (int)out.y, (int)out.z, (int)(360 - azimuth), cal_data->cal_level);
#else
    snprintf(buffer, sizeof(buffer) - 1, "%d\t%d\t%d\t[uT]\t(Azimuth:%d)\t(Calibration disabled)"
        , (int)out.x, (int)out.y, (int)out.z, (int)(360 - azimuth));
#endif
    printf("%s\n", buffer);
    if (magn_data->mqtt == NULL) {
        SOL_WRN("mqtt is null");
        return;
    }
    if (sol_mqtt_get_connection_status(magn_data->mqtt) == SOL_MQTT_CONNECTED) {

        payload = SOL_BUFFER_INIT_CONST(buffer, strlen(buffer));

        mqtt_message = (struct sol_mqtt_message){
            SOL_SET_API_VERSION(.api_version = SOL_MQTT_MESSAGE_API_VERSION, )
            .topic = magn_data->mqtt_topic,
            .payload = &payload,
            .qos = SOL_MQTT_QOS_EXACTLY_ONCE,
            .retain = false,
        };

        if (sol_mqtt_publish(magn_data->mqtt, &mqtt_message)) {
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
    struct iio_magnetometer_data magn_data = {};
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
    int ret;

    if (argc < 11) {
        fprintf(stderr, "\nUsage: %s <device name> <trigger name> <buffer size> " \
            "<sampling frequency> <scale> <custom offset> <offset> " \
            "<MQTT broker ip> <MQTT broker port> <MQTT topic>\n" \
            "\t<buffer size>:\t\t0=default\n" \
            "\t<sampling frequency>:\tMust be >=1 \n" \
            "\t<scale>:\t\t-1=default\n" \
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
    ret = snprintf(iio_config.sampling_frequency_name,
        sizeof(iio_config.sampling_frequency_name), "%s", "in_magn_");
    SOL_INT_CHECK_GOTO(ret, >= (int)sizeof(iio_config.sampling_frequency_name), error_iio);
    SOL_INT_CHECK_GOTO(ret, < 0, error_iio);

    magn_data.sampling_frequency = iio_config.sampling_frequency;

    channel_config.scale = atof(argv[5]);
    if (strncmp(argv[6], "y", 1) == 0) {
        channel_config.use_custom_offset = true;
        channel_config.offset = atoi(argv[7]);
    } else
        channel_config.use_custom_offset = false;

    iio_config.data = &magn_data;
    iio_config.sol_iio_reader_cb = iio_magnetometer_reader_cb;

    mqtt_config.host = argv[8];
    mqtt_config.port = atoi(argv[9]);
    magn_data.mqtt_topic = argv[10];

    device = sol_iio_open(device_id, &iio_config);
    SOL_NULL_CHECK_GOTO(device, error_iio);
    magn_data.channel_x = sol_iio_add_channel(device, "in_magn_x",
        &channel_config);
    SOL_NULL_CHECK_GOTO(magn_data.channel_x, error_iio);

    magn_data.channel_y = sol_iio_add_channel(device, "in_magn_y",
        &channel_config);
    SOL_NULL_CHECK_GOTO(magn_data.channel_y, error_iio);

    magn_data.channel_z = sol_iio_add_channel(device, "in_magn_z",
        &channel_config);
    SOL_NULL_CHECK_GOTO(magn_data.channel_z, error_iio);

#ifdef MAGN_CALIBRATE
    compass_cal_init(&(magn_data.cal_data));
#endif

#ifdef DENOISE_AVERAGE
    memset(&(magn_data.filter), 0, sizeof(struct filter_average));
    magn_data.filter.max_samples = FILTER_MAX_SAMPLE;
    magn_data.filter.num_fields = FILTER_NUM_FIELD;
#endif

    sol_iio_device_start_buffer(device);

    magn_data.mqtt = sol_mqtt_connect(&mqtt_config);
    SOL_NULL_CHECK_GOTO(magn_data.mqtt, error_iio);

    sol_run();

    free((char *)iio_config.trigger_name);
    iio_config.trigger_name = NULL;

#ifdef DENOISE_AVERAGE
    if (magn_data.filter.history) {
        free(magn_data.filter.history);
        magn_data.filter.history = NULL;
    }
    if (magn_data.filter.history_sum) {
        free(magn_data.filter.history);
        magn_data.filter.history = NULL;
    }
#endif
    /* Must do sol_iio_close(device), it will disable IIO buffer.
     * If not, the trigger, buffer size and buffer enable cannot
     * be set on the next launch.
     */
    sol_iio_close(device);

    if (timeout)
        sol_timeout_del(timeout);

    sol_mqtt_disconnect(magn_data.mqtt);

    sol_shutdown();
    return 0;

error_iio:
    if (device) {
        /* Must do sol_iio_close(device), it will disable IIO buffer.
         * If not, the trigger, buffer size and buffer enable cannot
         * be set on the next launch.
         */
        sol_iio_close(device);
    }

    if (timeout)
        sol_timeout_del(timeout);

    if (iio_config.trigger_name) {
        free((char *)iio_config.trigger_name);
        iio_config.trigger_name = NULL;
    }

#ifdef DENOISE_AVERAGE
    if (magn_data.filter.history) {
        free(magn_data.filter.history);
        magn_data.filter.history = NULL;
    }
    if (magn_data.filter.history_sum) {
        free(magn_data.filter.history);
        magn_data.filter.history = NULL;
    }
#endif

    sol_shutdown();
    return -1;
}
