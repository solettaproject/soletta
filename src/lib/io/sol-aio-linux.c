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

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "aio");

#include "sol-aio.h"

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
};

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
sol_aio_open_raw(const int device, const int pin, const unsigned int precision)
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

SOL_API int32_t
sol_aio_get_value(const struct sol_aio *aio)
{
    unsigned int val;

    SOL_NULL_CHECK(aio, -1);
    SOL_NULL_CHECK(aio->fp, -1);

    rewind(aio->fp);

    if (fscanf(aio->fp, "%u", &val) < 1) {
        SOL_WRN("aio #%d,%d: Could not read value.", aio->device, aio->pin);
        return -1;
    }

    return (int32_t)(val & aio->mask);
}
