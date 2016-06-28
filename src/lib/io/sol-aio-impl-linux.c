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

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <sol-mainloop.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "aio");

#include "sol-aio.h"
#ifdef WORKER_THREAD
#include "sol-worker-thread.h"
#endif

#define AIO_BASE_PATH "/sys/bus/iio/devices"

#define AIO_PATH(dst, device, pin) \
    ({ \
        int _tmp = snprintf(dst, sizeof(dst), AIO_BASE_PATH "/iio:device%d/in_voltage%d_raw", \
            device, pin); \
        (_tmp > 0 && _tmp < PATH_MAX); \
    })

#define AIO_DEV_PATH(dst, device) \
    ({ \
        int _tmp = snprintf(dst, sizeof(dst), AIO_BASE_PATH "/iio:device%d", device); \
        (_tmp > 0 && _tmp < PATH_MAX); \
    })

struct sol_aio {
    FILE *fp;
    int device;
    int pin;
    unsigned int mask;
    struct {
        const void *cb_data;
#ifdef WORKER_THREAD
        struct sol_worker_thread *worker;
#else
        struct sol_timeout *timeout;
#endif
        unsigned int value;
        void (*dispatch)(struct sol_aio *aio);

        union {
            struct {
                void (*cb)(void *cb_data, struct sol_aio *aio, int32_t ret);
            } read_cb;
        };
    } async;
};

#ifdef WORKER_THREAD
#define BUSY_CHECK(aio, ret) SOL_EXP_CHECK(aio->async.worker, ret);
#else
#define BUSY_CHECK(aio, ret) SOL_EXP_CHECK(aio->async.timeout, ret);
#endif

static bool
_aio_open_fp(struct sol_aio *aio)
{
    char path[PATH_MAX];

    if (!AIO_PATH(path, aio->device, aio->pin))
        return false;

    aio->fp = fopen(path, "re");
    if (!aio->fp)
        return false;
    setvbuf(aio->fp, NULL, _IONBF, 0);

    return true;
}

SOL_API struct sol_aio *
sol_aio_open_raw(int device, int pin, unsigned int precision)
{
    char path[PATH_MAX];
    struct stat st;
    struct sol_aio *aio;

    SOL_LOG_INTERNAL_INIT_ONCE;

    if (!precision) {
        SOL_WRN("aio #%d,%d: Invalid precision value=%d. Precision needs to be different of zero.",
            device, pin, precision);
        return NULL;
    }

    aio = calloc(1, sizeof(*aio));
    if (!aio) {
        SOL_WRN("aio #%d,%d: could not allocate aio context", device, pin);
        return NULL;
    }

    aio->device = device;
    aio->pin = pin;
    aio->mask = (0x01 << precision) - 1;

    if (!_aio_open_fp((struct sol_aio *)aio)) {
        if (!AIO_DEV_PATH(path, device) || stat(path, &st))
            SOL_WRN("aio #%d,%d: aio device %d does not exist", device, pin, device);
        else
            SOL_WRN("aio #%d,%d: Couldn't open pin %d on device %d", device, pin, pin, device);

        free(aio);
        return NULL;
    }

    return aio;
}

SOL_API void
sol_aio_close(struct sol_aio *aio)
{
    SOL_NULL_CHECK(aio);

    if (aio->fp)
        fclose(aio->fp);

    free(aio);
}

static void
_aio_get_value(struct sol_aio *aio, unsigned int *val)
{
    rewind(aio->fp);

    if (fscanf(aio->fp, "%u", val) < 1) {
        SOL_WRN("AIO #%d,%d: Could not read value.", aio->device, aio->pin);
        *val = -EIO;
    }
}

#ifdef WORKER_THREAD
static bool
aio_get_value_worker_thread_iterate(void *data)
{
    struct sol_aio *aio = data;

    _aio_get_value(aio, &aio->async.value);
    return false;
}

static void
aio_worker_thread_finished(void *data)
{
    struct sol_aio *aio = data;

    aio->async.worker = NULL;
    aio->async.dispatch(aio);
}
#else
static bool
aio_get_value_timeout_cb(void *data)
{
    struct sol_aio *aio = data;

    _aio_get_value(aio, &aio->async.value);
    aio->async.timeout = NULL;
    aio->async.dispatch(aio);
    return false;
}
#endif

static void
_aio_read_dispatch(struct sol_aio *aio)
{
    int32_t ret = aio->async.value;

    if (ret >= 0)
        ret = (int32_t)(aio->async.value & aio->mask);

    if (!aio->async.read_cb.cb) return;

    aio->async.read_cb.cb((void *)aio->async.cb_data, aio, ret);
}

SOL_API struct sol_aio_pending *
sol_aio_get_value(struct sol_aio *aio,
    void (*read_cb)(void *cb_data,
    struct sol_aio *aio,
    int32_t ret),
    const void *cb_data)
{
    struct sol_aio_pending *pending;

#ifdef WORKER_THREAD
    struct sol_worker_thread_config config = {
        SOL_SET_API_VERSION(.api_version = SOL_WORKER_THREAD_CONFIG_API_VERSION, )
        .setup = NULL,
        .cleanup = NULL,
        .iterate = aio_get_value_worker_thread_iterate,
        .finished = aio_worker_thread_finished,
        .feedback = NULL,
        .data = aio
    };
#endif

    errno = EINVAL;
    SOL_NULL_CHECK(aio, NULL);
    SOL_NULL_CHECK(aio->fp, NULL);

    errno = EBUSY;
    BUSY_CHECK(aio, NULL);

    aio->async.value = 0;
    aio->async.read_cb.cb = read_cb;
    aio->async.dispatch = _aio_read_dispatch;
    aio->async.cb_data = cb_data;

#ifdef WORKER_THREAD
    aio->async.worker = sol_worker_thread_new(&config);
    SOL_NULL_CHECK_GOTO(aio->async.worker, err_no_mem);

    pending = (struct sol_aio_pending *)aio->async.worker;
#else
    aio->async.timeout = sol_timeout_add(0, aio_get_value_timeout_cb, aio);
    SOL_NULL_CHECK_GOTO(aio->async.timeout, err_no_mem);

    pending = (struct sol_aio_pending *)aio->async.timeout;
#endif

    errno = 0;
    return pending;

err_no_mem:
    errno = ENOMEM;
    return NULL;
}

SOL_API void
sol_aio_pending_cancel(struct sol_aio *aio, struct sol_aio_pending *pending)
{
    SOL_NULL_CHECK(aio);
    SOL_NULL_CHECK(pending);

#ifdef WORKER_THREAD
    if (aio->async.worker == (struct sol_worker_thread *)pending) {
        sol_worker_thread_cancel(aio->async.worker);
        aio->async.worker = NULL;
    } else {
#else
    if (aio->async.timeout == (struct sol_timeout *)pending) {
        sol_timeout_del(aio->async.timeout);
        aio->async.dispatch(aio);
        aio->async.timeout = NULL;
    } else {
#endif
        SOL_WRN("Invalid AIO pending handle.");
    }
}
