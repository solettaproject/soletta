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
#include <fcntl.h>
#include <linux/kd.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-str-slice.h"
#include "sol-util.h"
#include "sol-vector.h"

#include "sol-coap.h"
#include "sol-oic-server.h"

static int console_fd;
static bool led_state;

static bool
get_scrolllock_led(void)
{
    char value;

    if (console_fd < 0)
        return led_state;

    if (ioctl(console_fd, KDGETLED, (char *)&value)) {
        perror("Could not get led state");
        return false;
    }

    return value & LED_SCR;
}

static int
set_scrolllock_led(bool on)
{
    char old;

    if (console_fd < 0) {
        printf("setting LED to %s\n", on ? "true" : "false");
        led_state = on;
        return 0;
    }

    if (ioctl(console_fd, KDGETLED, (char *)&old)) {
        perror("Could not get led state");
        return -1;
    }

    if (ioctl(console_fd, KDSETLED, on ? (old | LED_SCR) : (old & ~LED_SCR))) {
        perror("Could not set led state");
        return -1;
    }

    return 0;
}

static sol_coap_responsecode_t
user_handle_get(const struct sol_network_link_addr *cliaddr, const void *data,
    const struct sol_vector *input, struct sol_vector *output)
{
    struct sol_oic_repr_field *field;

    field = sol_vector_append(output);
    SOL_NULL_CHECK(field, SOL_COAP_RSPCODE_INTERNAL_ERROR);

    *field = SOL_OIC_REPR_BOOLEAN("state", get_scrolllock_led());

    field = sol_vector_append(output);
    SOL_NULL_CHECK(field, SOL_COAP_RSPCODE_INTERNAL_ERROR);

    *field = SOL_OIC_REPR_INT("power", 13);

    return SOL_COAP_RSPCODE_CONTENT;
}

static sol_coap_responsecode_t
user_handle_put(const struct sol_network_link_addr *cliaddr, const void *data,
    const struct sol_vector *input, struct sol_vector *output)
{
    struct sol_oic_repr_field *iter;
    uint16_t idx;

    SOL_VECTOR_FOREACH_IDX (input, iter, idx) {
        if (streq(iter->key, "state") && iter->type == SOL_OIC_REPR_TYPE_BOOLEAN) {
            if (set_scrolllock_led(iter->v_boolean))
                return SOL_COAP_RSPCODE_OK;

            return SOL_COAP_RSPCODE_INTERNAL_ERROR;
        }
    }

    return SOL_COAP_RSPCODE_BAD_REQUEST;
}

static struct sol_oic_server_resource *
register_light_resource_type(
    sol_coap_responsecode_t (*handle_get)(const struct sol_network_link_addr *cliaddr, const void *data, const struct sol_vector *input, struct sol_vector *output),
    sol_coap_responsecode_t (*handle_put)(const struct sol_network_link_addr *cliaddr, const void *data, const struct sol_vector *input, struct sol_vector *output))
{
    /* This function will be auto-generated from the RAML definitions. */

    struct sol_oic_resource_type rt = {
        .api_version = SOL_OIC_RESOURCE_TYPE_API_VERSION,
        .resource_type = SOL_STR_SLICE_LITERAL("core.light"),
        .interface = SOL_STR_SLICE_LITERAL("oc.mi.def"),
        .get = {
            .handle = handle_get    /* User-provided. */
        },
        .put = {
            .handle = handle_put    /* User-provided. */
        }
    };

    return sol_oic_server_add_resource(&rt, NULL,
        SOL_OIC_FLAG_DISCOVERABLE | SOL_OIC_FLAG_OBSERVABLE | SOL_OIC_FLAG_ACTIVE);
}

int
main(int argc, char *argv[])
{
    struct sol_oic_server_resource *res;
    char old_led_state;

    sol_init();

    if (sol_oic_server_init() != 0) {
        SOL_WRN("Could not create OIC server.");
        return -1;
    }

    res = register_light_resource_type(user_handle_get, user_handle_put);
    if (!res) {
        SOL_WRN("Could not register light resource type.");
        return -1;
    }

    console_fd = open("/dev/console", O_RDWR);
    if (console_fd < 0) {
        SOL_WRN("Could not open '/dev/console', printing to stdout");
    } else if (ioctl(console_fd, KDGETLED, (char *)&old_led_state)) {
        SOL_ERR("Could not get the keyboard leds state");
        return -1;
    }

    sol_run();

    sol_oic_server_del_resource(res);
    sol_oic_server_release();

    if (console_fd >= 0 && ioctl(console_fd, KDSETLED, old_led_state)) {
        SOL_ERR("Could not return the leds to the old state");
        return -1;
    }

    return 0;
}
