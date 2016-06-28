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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "aio");

#include "sol-aio.h"
#include "sol-vector.h"

#include "periph/adc.h"

struct dev_ref {
    uint16_t device;
    uint16_t ref;
};

static struct sol_vector _dev_ref = SOL_VECTOR_INIT(struct dev_ref);

struct sol_aio {
    int device;
    int pin;

    struct {
        const void *cb_data;
        struct sol_timeout *timeout;
        void (*read_cb)(void *cb_data, struct sol_aio *aio, int32_t ret);
        int32_t value;
    } async;
};

static bool
_check_precision(unsigned int precision, adc_precision_t *output)
{
    switch (precision) {
    case 6:
        *output = ADC_RES_6BIT;
        break;
    case 8:
        *output = ADC_RES_8BIT;
        break;
    case 10:
        *output = ADC_RES_10BIT;
        break;
    case 12:
        *output = ADC_RES_12BIT;
        break;
    case 14:
        *output = ADC_RES_14BIT;
        break;
    case 16:
        *output = ADC_RES_16BIT;
        break;
    default:
        return false;
    }

    return true;
}

static void
_power_on(int device)
{
    uint16_t i;
    struct dev_ref *ref;

    SOL_VECTOR_FOREACH_IDX (&_dev_ref, ref, i) {
        if (ref->device == device) {
            ref->ref++;
            return;
        }
    }

    ref = sol_vector_append(&_dev_ref);
    ref->device = device;
    ref->ref = 1;
    adc_poweron(device);
}

static void
_power_off(int device)
{
    uint16_t i;
    struct dev_ref *ref;

    SOL_VECTOR_FOREACH_IDX (&_dev_ref, ref, i) {
        if (ref->device == device) {
            if (!--ref->ref) {
                sol_vector_del(&_dev_ref, i);
                adc_poweroff(device);
            }
            return;
        }
    }

    SOL_DBG("aio: Trying to power off device %d, but reference was not found.", device);
}

struct sol_aio *
sol_aio_open_raw(int device, int pin, unsigned int precision)
{
    struct sol_aio *aio;
    adc_precision_t prec;

    SOL_LOG_INTERNAL_INIT_ONCE;

    if (!_check_precision(precision, &prec)) {
        SOL_WRN("aio #%d,%d: Invalid precision=%d. \
            See 'adc_precision_t' for valid values on riot.",
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

    _power_on(device);

    if (adc_init(device, prec)) {
        SOL_WRN("aio #%d,%d: Couldn't initialize aio device with given precision=%d.",
            device, pin, precision);
        goto error;
    }

    return aio;

error:
    _power_off(device);
    free(aio);
    return NULL;
}

void
sol_aio_close(struct sol_aio *aio)
{
    SOL_NULL_CHECK(aio);
    _power_off(aio->device);
    free(aio);
}

static void
aio_read_dispatch(struct sol_aio *aio)
{
    if (!aio->async.read_cb)
        return;

    aio->async.read_cb((void *)aio->async.cb_data, aio, aio->async.value);
}

static bool
aio_read_timeout_cb(void *data)
{
    struct sol_aio *aio = data;

    aio->async.value = (int32_t)adc_sample(aio->device, aio->pin);
    aio->async.timeout = NULL;
    aio_read_dispatch(aio);
    return false;
}

struct sol_aio_pending *
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

    aio->async.value = 0;
    aio->async.read_cb = read_cb;
    aio->async.cb_data = cb_data;

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

    if (aio->async.timeout == (struct sol_timeout *)pending) {
        sol_timeout_del(aio->async.timeout);
        aio_read_dispatch(aio);
        aio->async.timeout = NULL;
    } else
        SOL_WRN("Invalid AIO pending handle.");
}
