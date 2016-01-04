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

#include "sol-io-common-zephyr.h"

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
 * the ADC controller -- CONFIG_ADC_DW_WIDTH kernel option */

SOL_API struct sol_aio *
sol_aio_open_raw(const int device, const int pin, const unsigned int precision)
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

    aio->sample.sampling_delay = 1;
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
    int ret;

    ret = adc_read(aio->dev, &aio->table);
    if (ret != DEV_OK)
        aio->async.value = zephyr_err_to_errno(ret);

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
    SOL_NULL_CHECK(aio, NULL);

    aio->async.cb_data = cb_data;
    aio->async.read_cb = read_cb;
    aio->async.value = 0;

    aio->async.timeout = sol_timeout_add(0, aio_read_timeout_cb, aio);
    SOL_NULL_CHECK(aio->async.timeout, NULL);

    return (struct sol_aio_pending *)aio->async.timeout;
}

SOL_API bool
sol_aio_busy(struct sol_aio *aio)
{
    SOL_NULL_CHECK(aio, true);

    return aio->async.timeout != NULL;
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
