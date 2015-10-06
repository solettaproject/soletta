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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-pwm.h"
#include "sol-util.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "pwm");

#define PWM_BASE "/sys/class/pwm"

#define EXPORT_STAT_RETRIES 10

#define PWM_PATH(dst, pwm, action) \
    snprintf(dst, sizeof(dst), PWM_BASE "/pwmchip%d/pwm%d/%s", pwm->device, pwm->channel, action);

struct sol_pwm {
    int device;
    int channel;

    FILE *period;
    FILE *duty_cycle;

    bool owned;
};

static bool
_pwm_export(int device, int channel, bool export)
{
    int npwm, len, retries = 0;
    char path[PATH_MAX];
    const char *what = export ? "export" : "unexport";
    bool ret = false;

    if (export) {
        snprintf(path, sizeof(path), PWM_BASE "/pwmchip%d/npwm", device);
        if (sol_util_read_file(path, "%d", &npwm) < 1) {
            SOL_WRN("pwm #%d: could not read number of PWM channels available", device);
            return false;
        }

        if (channel >= npwm) {
            SOL_WRN("pwm #%d: requested channel '%d' is beyond the number of available PWM channels (%d)", device, channel, npwm);
            return false;
        }
    }

    snprintf(path, sizeof(path), PWM_BASE "/pwmchip%d/%s", device, what);
    if (sol_util_write_file(path, "%d", channel) < 0) {
        SOL_WRN("Failed writing to PWM export file");
        return false;
    }

    if (!export)
        return true;

    len = snprintf(path, sizeof(path), PWM_BASE "/pwmchip%d/pwm%d", device, channel);
    if (len < 0 || len > (int)sizeof(path))
        return false;

    /* busywait for the exported pwm's sysfs entry to be created. It's
     * usually instantaneous, but on some slow systems it takes long enough
     * that we fail the rest of the gpio_open() if we don't wait
     */
    while (retries < EXPORT_STAT_RETRIES) {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000 };
        struct stat st;
        if (!stat(path, &st)) {
            ret = true;
            break;
        }
        nanosleep(&ts, NULL);
        retries++;
    }

    return ret;
}

static int
_pwm_read(const struct sol_pwm *pwm, const char *file, const char *fmt, ...)
{
    char path[PATH_MAX];
    va_list ap;
    int ret;

    PWM_PATH(path, pwm, file);
    va_start(ap, fmt);
    ret = sol_util_vread_file(path, fmt, ap);
    va_end(ap);

    return ret;
}

static int
_pwm_write(struct sol_pwm *pwm, const char *file, const char *fmt, ...)
{
    char path[PATH_MAX];
    va_list ap;
    int ret;

    PWM_PATH(path, pwm, file);
    va_start(ap, fmt);
    ret = sol_util_vwrite_file(path, fmt, ap);
    va_end(ap);

    return ret;
}

static FILE *
_pwm_fopen(const char *path)
{
    FILE *fp;

    fp = fopen(path, "w+e");
    if (!fp)
        return NULL;
    setvbuf(fp, NULL, _IONBF, 0);
    return fp;
}

static bool
_pwm_open_period(struct sol_pwm *pwm)
{
    char path[PATH_MAX];
    struct stat st;

    /* try 2 different paths to set periods */
    snprintf(path, sizeof(path), PWM_BASE "/pwmchip%d/device/pwm_period",
        pwm->device);
    if (!stat(path, &st)) {
        pwm->period = _pwm_fopen(path);
        if (!pwm->period)
            SOL_WRN("pwm #%d,%d: could not open period file %s", pwm->device,
                pwm->channel, path);
    }

    if (!pwm->period) {
        PWM_PATH(path, pwm, "period");
        pwm->period = _pwm_fopen(path);
        if (!pwm->period) {
            SOL_WRN("pwm #%d,%d: could not open period file %s", pwm->device,
                pwm->channel, path);
            return false;
        }
    }

    return true;
}

static int
_pwm_config(struct sol_pwm *pwm, const struct sol_pwm_config *config)
{
    const char *pol_str;
    char path[PATH_MAX];
    char pol_value[10];
    int r;

    sol_pwm_set_enabled(pwm, false);

    switch (config->polarity) {
    case SOL_PWM_POLARITY_NORMAL:
        pol_str = "normal";
        break;
    case SOL_PWM_POLARITY_INVERSED:
        pol_str = "inversed";
        break;
    default:
        SOL_WRN("pwm #%d,%d: invalid polarity value '%d'", pwm->device, pwm->channel, config->polarity);
        return -EINVAL;
    }

    r = _pwm_read(pwm, "polarity", "%9[^\n]", pol_value);
    if (r < 1) {
        SOL_WRN("pwm #%d,%d: could not get polarity value", pwm->device,
            pwm->channel);
        return r < 0 ? r : -EIO;
    }

    if (strcmp(pol_str, pol_value) != 0) {
        r = _pwm_write(pwm, "polarity", "%s", pol_str);
        if (r < 0) {
            // TODO: Check if we get a meaningful error when it fails due to
            // lack of support to change polarity
            SOL_WRN("pwm #%d,%d: could not change polarity", pwm->device,
                pwm->channel);
            return r;
        }
    }

    if (!_pwm_open_period(pwm)) {
        fclose(pwm->period);
        return -ENOENT;
    }

    PWM_PATH(path, pwm, "duty_cycle");
    pwm->duty_cycle = _pwm_fopen(path);
    if (!pwm->duty_cycle) {
        SOL_WRN("pwm #%d,%d: could not open duty_cycle file", pwm->device,
            pwm->channel);
        fclose(pwm->period);
        return -EIO;
    }

    if (config->period_ns != -1) {
        /* We'll assume that if we have an initial period, the most likely
         * case is that it will remain constant, so we set it here and
         * close the file.
         */
        sol_pwm_set_duty_cycle(pwm, 0);
        sol_pwm_set_period(pwm, config->period_ns);
        fclose(pwm->period);
        pwm->period = NULL;
    }

    if (config->duty_cycle_ns != -1)
        sol_pwm_set_duty_cycle(pwm, config->duty_cycle_ns);

    sol_pwm_set_enabled(pwm, config->enabled);

    return 0;
}

SOL_API struct sol_pwm *
sol_pwm_open_raw(int device, int channel, const struct sol_pwm_config *config)
{
    char path[PATH_MAX];
    struct sol_pwm *pwm;
    struct stat st;

    SOL_LOG_INTERNAL_INIT_ONCE;

    if (unlikely(config->api_version != SOL_PWM_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open pwm that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_PWM_CONFIG_API_VERSION);
        return NULL;
    }

    pwm = calloc(1, sizeof(*pwm));
    if (!pwm) {
        SOL_WRN("pwm #%d,%d: could not allocate pwm context", device, channel);
        return NULL;
    }

    snprintf(path, sizeof(path), PWM_BASE "/pwmchip%d", device);
    if (stat(path, &st)) {
        SOL_WRN("pwm #%d,%d: pwm device %d does not exist", device, channel,
            device);
        goto open_error;
    }

    snprintf(path, sizeof(path), PWM_BASE "/pwmchip%d/pwm%d", device, channel);
    if (stat(path, &st)) {
        if (!_pwm_export(device, channel, true)) {
            SOL_WRN("pwm #%d,%d: could not export", device, channel);
            goto open_error;
        }

        pwm->owned = true;
    }

    pwm->device = device;
    pwm->channel = channel;

    if (_pwm_config(pwm, config) < 0)
        goto config_error;

    return pwm;
config_error:
    if (pwm->owned)
        _pwm_export(device, channel, false);
open_error:
    free(pwm);
    return NULL;
}

SOL_API void
sol_pwm_close(struct sol_pwm *pwm)
{
    SOL_NULL_CHECK(pwm);

    sol_pwm_set_enabled(pwm, false);

    sol_pwm_set_duty_cycle(pwm, 0);
    if (pwm->duty_cycle)
        fclose(pwm->duty_cycle);

    sol_pwm_set_period(pwm, 0);
    if (pwm->period)
        fclose(pwm->period);

    if (pwm->owned)
        _pwm_export(pwm->device, pwm->channel, false);
    free(pwm);
}

SOL_API bool
sol_pwm_set_enabled(struct sol_pwm *pwm, bool enable)
{
    SOL_NULL_CHECK(pwm, false);

    if (_pwm_write(pwm, "enable", "%d", enable) < 0) {
        SOL_WRN("pwm #%d,%d: could not %s", pwm->device, pwm->channel, enable ? "enable" : "disable");
        return false;
    }

    return true;
}

SOL_API bool
sol_pwm_get_enabled(const struct sol_pwm *pwm)
{
    int value;

    SOL_NULL_CHECK(pwm, false);

    if (_pwm_read(pwm, "enable", "%d", &value) < 1) {
        SOL_WRN("pwm #%d,%d: could not get enable value", pwm->device, pwm->channel);
        return false;
    }

    return value;
}

SOL_API bool
sol_pwm_set_period(struct sol_pwm *pwm, uint32_t period_ns)
{
    SOL_NULL_CHECK(pwm, false);

    if (!pwm->period) {
        if (!_pwm_open_period(pwm))
            return false;
    }
    if (fprintf(pwm->period, "%u", period_ns) < 0) {
        SOL_WRN("pwm #%d,%d: could not set period", pwm->device, pwm->channel);
        return false;
    }

    return true;
}

SOL_API int32_t
sol_pwm_get_period(const struct sol_pwm *pwm)
{
    int value, r;

    SOL_NULL_CHECK(pwm, -EINVAL);

    if (!pwm->period) {
        r = _pwm_read(pwm, "period", "%d", &value);
    } else {
        rewind(pwm->period);
        r = fscanf(pwm->period, "%d", &value);
    }

    if (r < 1) {
        SOL_WRN("pwm #%d,%d: could not read period", pwm->device, pwm->channel);
        return -errno;
    }

    return value;
}

SOL_API bool
sol_pwm_set_duty_cycle(struct sol_pwm *pwm, uint32_t duty_cycle_ns)
{
    SOL_NULL_CHECK(pwm, false);

    if (fprintf(pwm->duty_cycle, "%u", duty_cycle_ns) < 0) {
        SOL_WRN("pwm #%d,%d: could not set duty_cycle", pwm->device, pwm->channel);
        return false;
    }

    return true;
}

SOL_API int32_t
sol_pwm_get_duty_cycle(const struct sol_pwm *pwm)
{
    int value;

    SOL_NULL_CHECK(pwm, -EINVAL);

    rewind(pwm->duty_cycle);
    if (fscanf(pwm->duty_cycle, "%d", &value) < 1) {
        SOL_WRN("pwm #%d,%d: could not read duty_cycle", pwm->device, pwm->channel);
        return -errno;
    }

    return value;
}
