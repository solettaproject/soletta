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

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "aio");

#include "sol-aio.h"

#include "periph/adc.h"

struct sol_aio {
    int device;
    int pin;
};

bool
_check_precision(const unsigned int precision, enum adc_precision_t *output)
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

struct sol_aio *
sol_aio_open_raw(const int device, const int pin, const unsigned int precision)
{
    struct sol_aio *aio;
    enum adc_precision_t prec;

    SOL_LOG_INTERNAL_INIT_ONCE;

    if (!_check_precision(precision, &prec)) {
        SOL_WRN("aio #%d,%d: Invalid precision=%d. \
            See 'enum adc_precision_t' for valid values on riot.",
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

    adc_poweron(device);

    if (adc_init(device, prec)) {
        SOL_WRN("aio #%d,%d: Couldn't initialize aio device with given precision=%d.",
            device, pin, precision);
        goto error;
    }

    return aio;

error:
    poweroff(device);
    free(aio);
    return NULL;
}

void
sol_aio_close(struct sol_aio *aio)
{
    SOL_NULL_CHECK(aio);
    adc_poweroff(aio->device);
    free(aio);
}

int
sol_aio_get_value(const struct sol_aio *aio)
{
    SOL_NULL_CHECK(aio, -1);

    return adc_sample(aio->device, aio->pin);
}
