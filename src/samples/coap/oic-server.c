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
#include <fcntl.h>
#include <linux/kd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

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

static bool
set_scrolllock_led(bool on)
{
    char old;

    if (console_fd < 0) {
        printf("setting LED to %s\n", on ? "true" : "false");
        led_state = on;
        return true;
    }

    if (ioctl(console_fd, KDGETLED, (char *)&old)) {
        perror("Could not get led state");
        return false;
    }

    if (ioctl(console_fd, KDSETLED, on ? (old | LED_SCR) : (old & ~LED_SCR))) {
        perror("Could not set led state");
        return false;
    }

    return true;
}

static int
user_handle_get(void *data, struct sol_oic_request *request)
{
    int r;
    struct sol_oic_repr_field field;
    struct sol_oic_response *response = NULL;
    struct sol_oic_map_writer *output;

    response = sol_oic_server_response_new(request);
    SOL_NULL_CHECK_GOTO(response, error);
    output = sol_oic_server_response_get_writer(response);
    field = SOL_OIC_REPR_BOOL("state", get_scrolllock_led());
    r = sol_oic_map_append(output, &field);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    field = SOL_OIC_REPR_INT("power", 13);
    r = sol_oic_map_append(output, &field);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    return sol_oic_server_send_response(request, response,
        SOL_COAP_RESPONSE_CODE_CONTENT);

error:
    if (response)
        sol_oic_server_response_free(response);
    return sol_oic_server_send_response(request, NULL,
        SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR);
}

static int
user_handle_put(void *data, struct sol_oic_request *request)
{
    enum sol_coap_response_code code = SOL_COAP_RESPONSE_CODE_BAD_REQUEST;
    enum sol_oic_map_loop_reason reason;
    struct sol_oic_repr_field field;
    struct sol_oic_map_reader iter;
    struct sol_oic_map_reader *input;

    input = sol_oic_server_request_get_reader(request);
    SOL_OIC_MAP_LOOP(input, &field, &iter, reason) {
        if (!strcmp(field.key, "state") && field.type == SOL_OIC_REPR_TYPE_BOOL) {
            if (set_scrolllock_led(field.v_boolean))
                code = SOL_COAP_RESPONSE_CODE_OK;
            else
                code = SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR;

            sol_oic_repr_field_clear(&field);
            goto end;
        }
    }

end:
    return sol_oic_server_send_response(request, NULL, code);
}

static struct sol_oic_server_resource *
register_light_resource_type(
    int (*handle_get)(void *data, struct sol_oic_request *request),
    int (*handle_put)(void *data, struct sol_oic_request *request),
    const char *resource_type)
{
    /* This function will be auto-generated from the RAML definitions. */

    struct sol_oic_resource_type rt = {
        SOL_SET_API_VERSION(.api_version = SOL_OIC_RESOURCE_TYPE_API_VERSION, )
        .resource_type = sol_str_slice_from_str(resource_type),
        .interface = SOL_STR_SLICE_LITERAL("oc.mi.def"),
        .get = {
            .handle = handle_get    /* User-provided. */
        },
        .put = {
            .handle = handle_put    /* User-provided. */
        }
    };

    return sol_oic_server_register_resource(&rt, NULL,
        SOL_OIC_FLAG_DISCOVERABLE | SOL_OIC_FLAG_OBSERVABLE | SOL_OIC_FLAG_ACTIVE);
}

int
main(int argc, char *argv[])
{
    struct sol_oic_server_resource *res;
    char old_led_state;
    const char *resource_type = "core.light";

    if (argc < 2) {
        printf("No resource type specified, assuming core.light\n");
    } else {
        printf("Resource type specified: %s\n", argv[1]);
        resource_type = argv[1];
    }

    sol_init();

    res = register_light_resource_type(user_handle_get, user_handle_put,
        resource_type);
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

    sol_oic_server_unregister_resource(res);

    if (console_fd >= 0 && ioctl(console_fd, KDSETLED, old_led_state)) {
        SOL_ERR("Could not return the leds to the old state");
        return -1;
    }

    return 0;
}
