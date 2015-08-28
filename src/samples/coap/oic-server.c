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


#define DEFAULT_UDP_PORT 5683

static int console_fd;

static bool
get_scrolllock_led(void)
{
    char value;

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
    uint8_t *payload, uint16_t *payload_len)
{
    static const uint8_t response_on[] = "{\"oc\":[{\"rep\":{\"power\":13,\"state\":true}}]}";
    static const uint8_t response_off[] = "{\"oc\":[{\"rep\":{\"power\":13,\"state\":false}}]}";

    if (get_scrolllock_led()) {
        if ((sizeof(response_on) - 1) > *payload_len)
            return SOL_COAP_RSPCODE_UNAUTHORIZED;

        memcpy(payload, response_on, sizeof(response_on) - 1);
        *payload_len = sizeof(response_on) - 1;
    } else {
        if ((sizeof(response_off) - 1) > *payload_len)
            return SOL_COAP_RSPCODE_UNAUTHORIZED;

        memcpy(payload, response_off, sizeof(response_off) - 1);
        *payload_len = sizeof(response_off) - 1;
    }

    return SOL_COAP_RSPCODE_CONTENT;
}

static sol_coap_responsecode_t
user_handle_put(const struct sol_network_link_addr *cliaddr, const void *data,
    uint8_t *payload, uint16_t *payload_len)
{
    static const char on_state[] = "\"state\":true";
    bool new_state = memmem(payload, *payload_len, on_state, sizeof(on_state) - 1);

    return set_scrolllock_led(new_state) ? SOL_COAP_RSPCODE_OK : SOL_COAP_RSPCODE_INTERNAL_ERROR;
}

static bool
register_light_resource_type(
    sol_coap_responsecode_t (*handle_get)(const struct sol_network_link_addr *cliaddr, const void *data, uint8_t *payload, uint16_t *payload_len),
    sol_coap_responsecode_t (*handle_put)(const struct sol_network_link_addr *cliaddr, const void *data, uint8_t *payload, uint16_t *payload_len))
{
    /* This function will be auto-generated from the RAML definitions. */

    struct sol_oic_resource_type resource_type = {
        .api_version = SOL_OIC_RESOURCE_TYPE_API_VERSION,
        .endpoint = SOL_STR_SLICE_LITERAL("/a/light"),
        .resource_type = SOL_STR_SLICE_LITERAL("core.light"),
        .iface = SOL_STR_SLICE_LITERAL("oc.mi.def"),
        .get = {
            .handle = handle_get    /* User-provided. */
        },
        .put = {
            .handle = handle_put    /* User-provided. */
        }
    };
    struct sol_oic_device_definition *def;

    def = sol_oic_server_register_definition((struct sol_str_slice)SOL_STR_SLICE_LITERAL("/l"),
        (struct sol_str_slice)SOL_STR_SLICE_LITERAL("oic.light"),
        SOL_COAP_FLAGS_OC_CORE | SOL_COAP_FLAGS_WELL_KNOWN);
    SOL_NULL_CHECK(def, false);

    return sol_oic_device_definition_register_resource_type(def, &resource_type, NULL, SOL_COAP_FLAGS_OC_CORE);
}

int
main(int argc, char *argv[])
{
    char old_led_state;

    sol_init();

    if (sol_oic_server_init(DEFAULT_UDP_PORT) != 0) {
        SOL_WRN("Could not create OIC server.");
        return -1;
    }

    if (!register_light_resource_type(user_handle_get, user_handle_put)) {
        SOL_WRN("Could not register light resource type.");
        return -1;
    }

    console_fd = open("/dev/console", O_RDWR);
    if (console_fd < 0) {
        SOL_ERR("Could not open '/dev/console'");
        return -1;
    }

    if (ioctl(console_fd, KDGETLED, (char *)&old_led_state)) {
        SOL_ERR("Could not get the keyboard leds state");
        return -1;
    }

    sol_run();

    sol_oic_server_release();

    if (ioctl(console_fd, KDSETLED, old_led_state)) {
        SOL_ERR("Could not return the leds to the old state");
        return -1;
    }

    return 0;
}
