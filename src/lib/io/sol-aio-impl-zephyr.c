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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Zephyr includes */
#include "adc.h"

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "aio");

#include "sol-aio.h"
#include "sol-mainloop.h"
#include "sol-vector.h"

struct aio_dev {
    const char *name;
    int refcnt;
};

static struct aio_dev aio_0_dev = {
    .name = "ADC",
    .refcnt = 0
};

static struct aio_dev *devs[1] = {
    &aio_0_dev
};

struct sol_aio {
    struct device *dev;
    struct aio_dev *dev_ref;

    struct adc_seq_table table;
    struct adc_seq_entry sample;
    struct {
        const void *cb_data;
        void (*read_cb)(void *cb_data, struct sol_aio *aio, int32_t ret);
        struct sol_timeout *timeout;
        int32_t value;
    } async;
};

static void
aio_read_dispatch(struct sol_aio *aio)
{
    if (!aio->async.read_cb)
        return;

    aio->async.read_cb((void *)aio->async.cb_data, aio, aio->async.value);
}

/* On Zephyr, the precision (sample width) is set at build time, for
 * the ADC controller -- CONFIG_ADC_DW_SAMPLE_WIDTH kernel option */

SOL_API struct sol_aio *
sol_aio_open_raw(int device, int pin, unsigned int precision)
{
    struct sol_aio *aio = NULL;
    struct device *dev = NULL;

    SOL_LOG_INTERNAL_INIT_ONCE;

    if (device != 0) {
        SOL_WRN("Unsupported AIO device %d", device);
        goto err;
    }

    dev = device_get_binding((char *)devs[device]->name);
    if (!dev) {
        SOL_WRN("Failed to open AIO device %s", devs[device]->name);
        return NULL;
    }

    aio = calloc(1, sizeof(struct sol_aio));
    SOL_NULL_CHECK(aio, NULL);

    aio->sample.sampling_delay = 12;
    aio->sample.channel_id = pin;
    aio->sample.buffer = (uint8_t *)&aio->async.value;
    aio->sample.buffer_length = sizeof(aio->async.value);
    aio->table.entries = &aio->sample;
    aio->table.num_entries = 1;

    devs[device]->refcnt++;
    if (devs[device]->refcnt == 1)
        adc_enable(dev);
    else {
        SOL_WRN("No support for more than 1 AIO user yet");
        return NULL;
    }

    aio->dev = dev;
    aio->dev_ref = devs[device];

err:
    return aio;
}

SOL_API void
sol_aio_close(struct sol_aio *aio)
{
    SOL_NULL_CHECK(aio);

    if (aio->async.timeout) {
        aio->async.timeout = NULL;
        aio_read_dispatch(aio);
    }

    if (!--(aio->dev_ref->refcnt))
        adc_disable(aio->dev);

    free(aio);
}

static bool
aio_read_timeout_cb(void *data)
{
    struct sol_aio *aio = data;
    int r;

    r = adc_read(aio->dev, &aio->table);
    if (r != 0)
        aio->async.value = -EIO;

    aio->async.timeout = NULL;
    aio_read_dispatch(aio);

    return false;
}

SOL_API struct sol_aio_pending *
sol_aio_get_value(struct sol_aio *aio,
    void (*read_cb)(void *cb_data,
    struct sol_aio *aio,
    int32_t ret),
    const void *cb_data)
{
    errno = EINVAL;
    SOL_NULL_CHECK(aio, NULL);

    errno = EBUSY;
    SOL_EXP_CHECK(aio->async.timeout, NULL);

    aio->async.cb_data = cb_data;
    aio->async.read_cb = read_cb;
    aio->async.value = 0;

    aio->async.timeout = sol_timeout_add(0, aio_read_timeout_cb, aio);
    errno = ENOMEM;
    SOL_NULL_CHECK(aio->async.timeout, NULL);

    errno = 0;
    return (struct sol_aio_pending *)aio->async.timeout;
}

SOL_API void
sol_aio_pending_cancel(struct sol_aio *aio, struct sol_aio_pending *pending)
{
    SOL_NULL_CHECK(aio);
    SOL_NULL_CHECK(pending);

    /* if true, the due callback was not issued yet */
    if (aio->async.timeout == (struct sol_timeout *)pending) {
        sol_timeout_del(aio->async.timeout);
        aio->async.timeout = NULL;
    } else
        SOL_WRN("Invalid AIO pending handle.");
}
