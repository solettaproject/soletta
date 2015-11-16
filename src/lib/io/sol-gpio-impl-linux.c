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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-gpio.h"
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-util.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "gpio");

#define GPIO_BASE "/sys/class/gpio"

#define EXPORT_STAT_RETRIES 10

struct sol_gpio {
    uint32_t pin;

    FILE *fp;
    struct {
        struct sol_fd *fd_watch;
        struct sol_timeout *timer;
        void (*cb)(void *data, struct sol_gpio *gpio, bool value);
        const void *data;
        bool last_value : 1;
        bool on_raise : 1;
        bool on_fall : 1;
    } irq;

    bool owned;
};

static bool
_gpio_export(uint32_t gpio, bool unexport)
{
    char gpio_dir[PATH_MAX];
    int len, retries = 0;
    const char *export_path = GPIO_BASE "/export";
    const char *unexport_path = GPIO_BASE "/unexport";
    const char *action = export_path;
    bool ret = false;

    if (unexport)
        action = unexport_path;

    if (sol_util_write_file(action, "%u", gpio) < 0) {
        SOL_WRN("Failed writing to GPIO export file");
        return false;
    }

    if (unexport)
        return true;

    len = snprintf(gpio_dir, sizeof(gpio_dir), GPIO_BASE "/gpio%u", gpio);
    if (len < 0 || len > PATH_MAX)
        return false;

    /* busywait for the exported gpio's sysfs entry to be created. It's
     * usually instantaneous, but on some slow systems it takes long enough
     * that we fail the rest of the gpio_open() if we don't wait
     */
    while (retries < EXPORT_STAT_RETRIES) {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000 };
        struct stat st;
        if (!stat(gpio_dir, &st)) {
            ret = true;
            break;
        }
        nanosleep(&ts, NULL);
        retries++;
    }

    return ret;
}

static int
_gpio_open_fd(struct sol_gpio *gpio, enum sol_gpio_direction dir)
{
    const char *mode;
    char path[PATH_MAX];
    int len;

    switch (dir) {
    case SOL_GPIO_DIR_OUT:
        mode = "we";
        break;
    case SOL_GPIO_DIR_IN:
        mode = "re";
        break;
    default:
        mode = "w+e";
    }

    len = snprintf(path, sizeof(path), GPIO_BASE "/gpio%u/value", gpio->pin);
    if (len < 0 || len > PATH_MAX)
        return -ENAMETOOLONG;

    gpio->fp = fopen(path, mode);
    if (!gpio->fp)
        return -errno;
    setvbuf(gpio->fp, NULL, _IONBF, 0);

    return fileno(gpio->fp);
}

static bool
_gpio_on_event(void *userdata, int fd, uint32_t cond)
{
    struct sol_gpio *gpio = userdata;

    if (cond & SOL_FD_FLAGS_PRI) {
        bool val = sol_gpio_read(gpio);
        gpio->irq.cb((void *)gpio->irq.data, gpio, val);
    }

    /*
       No error checking because sysfs files will always have POLLERR set,
       so they can be used in the exceptfd set for select().
     */

    return true;
}

static bool
_gpio_on_timeout(void *userdata)
{
    struct sol_gpio *gpio = userdata;
    int val;

    val = sol_gpio_read(gpio);
    if (gpio->irq.last_value != val) {
        gpio->irq.last_value = val;
        if ((val && gpio->irq.on_raise)
            || (!val && gpio->irq.on_fall))
            gpio->irq.cb((void *)gpio->irq.data, gpio, val);
    }
    return true;
}

static int
_gpio_in_config(struct sol_gpio *gpio, const struct sol_gpio_config *config, int fd)
{
    const char *mode_str;
    char gpio_dir[PATH_MAX];
    struct stat st;
    enum sol_gpio_edge trig = config->in.trigger_mode;

    switch (trig) {
    case SOL_GPIO_EDGE_RISING:
        mode_str = "rising";
        break;
    case SOL_GPIO_EDGE_FALLING:
        mode_str = "falling";
        break;
    case SOL_GPIO_EDGE_BOTH:
        mode_str = "both";
        break;
    case SOL_GPIO_EDGE_NONE:
        return 0;
    default:
        SOL_WRN("gpio #%u: Unsupported edge mode '%d'", gpio->pin, trig);
        return -EINVAL;
    }

    // clear any pending interrupts
    gpio->irq.last_value = sol_gpio_read(gpio);
    gpio->irq.cb = config->in.cb;
    gpio->irq.data = config->in.user_data;

    snprintf(gpio_dir, sizeof(gpio_dir), GPIO_BASE "/gpio%u/edge", gpio->pin);
    if (!stat(gpio_dir, &st)) {
        if (sol_util_write_file(gpio_dir, "%s", mode_str) < 0) {
            SOL_WRN("gpio #%u: could not set requested edge mode, falling back to timeout mode",
                gpio->pin);
            goto timeout_mode;
        }
        gpio->irq.fd_watch = sol_fd_add(fd, SOL_FD_FLAGS_PRI, _gpio_on_event,
            gpio);

        return 0;
    }

timeout_mode:
    if (config->in.poll_timeout == 0) {
        SOL_WRN("gpio #%u: Timeout value '%" PRIu32 "' is invalid, must be a positive number of milliseconds.",
            gpio->pin, config->in.poll_timeout);
        return -EINVAL;
    }

    gpio->irq.on_raise = (trig == SOL_GPIO_EDGE_BOTH || trig == SOL_GPIO_EDGE_RISING);
    gpio->irq.on_fall = (trig == SOL_GPIO_EDGE_BOTH || trig == SOL_GPIO_EDGE_FALLING);
    gpio->irq.timer = sol_timeout_add(config->in.poll_timeout, _gpio_on_timeout, gpio);

    return 0;
}

static int
_gpio_config(struct sol_gpio *gpio, const struct sol_gpio_config *config)
{
    char gpio_dir[PATH_MAX];
    const char *dir_val;
    struct stat st;
    int fd;
    bool no_dir = false;

    if (config->dir == SOL_GPIO_DIR_OUT) {
        if (config->out.value)
            dir_val = "high";
        else
            dir_val = "low";
    } else
        dir_val = "in";

    /* Set the GPIO direction if it's supported. If the file does not exist,
     * we have no way of knowing if the requested mode will work, so we can
     * do nothing but trust the user
     */
    snprintf(gpio_dir, sizeof(gpio_dir), GPIO_BASE "/gpio%u/direction", gpio->pin);
    if (!stat(gpio_dir, &st)) {
        if (sol_util_write_file(gpio_dir, "%s", dir_val) < 0) {
            SOL_WRN("gpio #%u: could not set direction to '%s'", gpio->pin, dir_val);
            return -EIO;
        }
    } else
        no_dir = true;

    snprintf(gpio_dir, sizeof(gpio_dir), GPIO_BASE "/gpio%u/active_low", gpio->pin);

    if (sol_util_write_file(gpio_dir, "%d", config->active_low) < 0) {
        SOL_WRN("gpio #%u: could not set requested active_low", config->active_low);
        return -EINVAL;
    }

    fd = _gpio_open_fd(gpio, config->dir);
    if (fd < 0)
        return fd;

    if (config->dir == SOL_GPIO_DIR_IN)
        return _gpio_in_config(gpio, config, fd);
    else if (no_dir) {
        /* if there's no direction file, we need to set the requested value
         * by normal writing
         */
        sol_gpio_write(gpio, config->out.value);
    }

    return 0;
}

SOL_API struct sol_gpio *
sol_gpio_open_raw(uint32_t pin, const struct sol_gpio_config *config)
{
    struct sol_gpio *gpio;
    char gpio_dir[PATH_MAX];
    struct stat st;

    SOL_LOG_INTERNAL_INIT_ONCE;

#ifndef SOL_NO_API_VERSION
    if (unlikely(config->api_version != SOL_GPIO_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open gpio that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_GPIO_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    gpio = calloc(1, sizeof(*gpio));
    if (!gpio) {
        SOL_WRN("gpio #%u: could not allocate gpio context", pin);
        return NULL;
    }

    snprintf(gpio_dir, sizeof(gpio_dir), GPIO_BASE "/gpio%u", pin);
    if (stat(gpio_dir, &st)) {
        if (!_gpio_export(pin, false)) {
            SOL_WRN("gpio #%u: could not export", pin);
            goto export_error;
        }
        gpio->owned = true;
    }

    gpio->pin = pin;

    if (_gpio_config(gpio, config) < 0)
        goto open_error;

    return gpio;
open_error:
    if (gpio->owned)
        _gpio_export(pin, true);
export_error:
    sol_gpio_close(gpio);
    return NULL;
}

SOL_API void
sol_gpio_close(struct sol_gpio *gpio)
{
    SOL_NULL_CHECK(gpio);

    if (gpio->irq.fd_watch)
        sol_fd_del(gpio->irq.fd_watch);
    if (gpio->irq.timer)
        sol_timeout_del(gpio->irq.timer);

    if (gpio->fp)
        fclose(gpio->fp);

    if (gpio->owned)
        _gpio_export(gpio->pin, true);
    free(gpio);
}

SOL_API bool
sol_gpio_write(struct sol_gpio *gpio, bool val)
{
    SOL_NULL_CHECK(gpio, false);

    return fprintf(gpio->fp, "%d", val) > 0;
}

SOL_API int
sol_gpio_read(struct sol_gpio *gpio)
{
    int val;

    SOL_NULL_CHECK(gpio, -EINVAL);

    rewind(gpio->fp);
    if (fscanf(gpio->fp, "%d", &val) < 1) {
        SOL_WRN("gpio #%u: could not read value", gpio->pin);
        return -errno;
    }

    return val;
}
