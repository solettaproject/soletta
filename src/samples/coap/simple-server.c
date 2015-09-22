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
#include "sol-network.h"
#include "sol-str-slice.h"
#include "sol-util.h"
#include "sol-vector.h"

#include "sol-coap.h"

#define DEFAULT_UDP_PORT 5683

#define OC_CORE_JSON_SEPARATOR ","
#define OC_CORE_ELEM_JSON_START "{\"oc\":[{\"href\":\"%s\",\"rep\":{"
#define OC_CORE_PROP_JSON_NUMBER "\"%s\":%d"
#define OC_CORE_PROP_JSON_STRING "\"%s\":\"%s\""
#define OC_CORE_PROP_JSON_BOOLEAN "\"%s\":%s"
#define OC_CORE_ELEM_JSON_END "}}]}"


struct light_context {
    struct sol_coap_server *server;
    struct sol_coap_resource *resource;
};

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

static void
set_scrolllock_led(bool on)
{
    char old;

    if (ioctl(console_fd, KDGETLED, (char *)&old)) {
        perror("Could not get led state");
        return;
    }

    if (ioctl(console_fd, KDSETLED, on ? (old | LED_SCR) : (old & ~LED_SCR))) {
        perror("Could not set led state");
        return;
    }
}

static int
light_resource_to_rep(const struct sol_coap_resource *resource,
    bool state, char *buf, int buflen)
{
    uint8_t path[64];
    int len = 0;

    memset(&path, 0, sizeof(path));
    sol_coap_uri_path_to_buf(resource->path, path, sizeof(path));

    len += snprintf(buf + len, buflen - len, OC_CORE_ELEM_JSON_START, path);

    /* FIXME */
    len += snprintf(buf + len, buflen - len, OC_CORE_PROP_JSON_NUMBER, "power", 100);
    len += snprintf(buf + len, buflen - len, OC_CORE_JSON_SEPARATOR);
    len += snprintf(buf + len, buflen - len, OC_CORE_PROP_JSON_STRING, "name", "Soletta LAMP!");
    len += snprintf(buf + len, buflen - len, OC_CORE_JSON_SEPARATOR);

    len += snprintf(buf + len, buflen - len, OC_CORE_PROP_JSON_BOOLEAN, "state",
        state ? "true" : "false" );

    len += snprintf(buf + len, buflen - len, OC_CORE_ELEM_JSON_END);

    return len;
}

static int
light_method_put(const struct sol_coap_resource *resource, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr, void *data)
{
    struct sol_coap_server *server = (void *)data;
    struct sol_coap_packet *resp;
    char *sub = NULL;
    uint8_t *p;
    uint16_t len;
    bool value;
    sol_coap_responsecode_t code = SOL_COAP_RSPCODE_CONTENT;

    sol_coap_packet_get_payload(req, &p, &len);

    if (p)
        sub = strstr((char *)p, "state\":");
    if (!sub) {
        code = SOL_COAP_RSPCODE_BAD_REQUEST;
        goto done;
    }

    value = !memcmp(sub + strlen("state\":"), "true", sizeof("true") - 1);

    SOL_INF("Changing light state to %s", value ? "on" : "off");

    set_scrolllock_led(value);

done:
    resp = sol_coap_packet_new(req);
    if (!resp) {
        SOL_WRN("Could not build response packet");
        return -1;
    }
    sol_coap_header_set_type(resp, SOL_COAP_TYPE_ACK);
    sol_coap_header_set_code(resp, code);

    return sol_coap_send_packet(server, resp, cliaddr);
}

static bool
update_light(void *data)
{
    struct light_context *context = data;
    struct sol_coap_server *server = context->server;
    struct sol_coap_resource *resource = context->resource;
    struct sol_coap_packet *pkt;
    uint8_t *payload;
    uint16_t len;

    SOL_INF("Emitting notification");

    pkt = sol_coap_packet_notification_new(server, resource);
    SOL_NULL_CHECK(pkt, false);

    sol_coap_header_set_code(pkt, SOL_COAP_RSPCODE_CONTENT);

    sol_coap_packet_get_payload(pkt, &payload, &len);
    len = light_resource_to_rep(resource, get_scrolllock_led(), (char *)payload, len);
    sol_coap_packet_set_payload_used(pkt, len);

    return !sol_coap_packet_send_notification(server, resource, pkt);
}

static int
light_method_get(const struct sol_coap_resource *resource, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr, void *data)
{
    struct sol_coap_server *server = (void *)data;
    struct sol_coap_packet *resp;
    uint8_t *payload;
    uint16_t len;

    resp = sol_coap_packet_new(req);
    if (!resp) {
        SOL_WRN("Could not build response packet");
        return -1;
    }
    sol_coap_header_set_type(resp, SOL_COAP_TYPE_ACK);
    sol_coap_header_set_code(resp, SOL_COAP_RSPCODE_CONTENT);

    sol_coap_packet_get_payload(resp, &payload, &len);
    len = light_resource_to_rep(resource, get_scrolllock_led(), (char *)payload, len);
    sol_coap_packet_set_payload_used(resp, len);

    return sol_coap_send_packet(server, resp, cliaddr);
}

static struct sol_coap_resource light = {
    .api_version = SOL_COAP_RESOURCE_API_VERSION,
    .get = light_method_get,
    .put = light_method_put,
    .iface = SOL_STR_SLICE_LITERAL("oc.mi.def"),
    .resource_type = SOL_STR_SLICE_LITERAL("core.light"),
    .flags = SOL_COAP_FLAGS_WELL_KNOWN,
    .path = {
        SOL_STR_SLICE_LITERAL("a"),
        SOL_STR_SLICE_LITERAL("light"),
        SOL_STR_SLICE_EMPTY,
    }
};

int
main(int argc, char *argv[])
{
    struct light_context context = { .resource = &light };
    struct sol_coap_server *server;
    char old_led_state;

    sol_init();

    server = sol_coap_server_new(DEFAULT_UDP_PORT);
    if (!server) {
        SOL_WRN("Could not create a coap server using port %d.", DEFAULT_UDP_PORT);
        return -1;
    }

    if (!sol_coap_server_register_resource(server, &light, server)) {
        SOL_WRN("Could not register resource for the light");
        return -1;
    }

    console_fd = open("/dev/console", O_RDWR);
    if (console_fd < 0) {
        perror("Could not open '/dev/console'");
        return -1;
    }

    if (ioctl(console_fd, KDGETLED, (char *)&old_led_state)) {
        perror("Could not get the keyboard leds state");
        return -1;
    }

    context.server = server;
    sol_timeout_add(5000, update_light, &context);

    sol_run();

    sol_coap_server_unref(server);

    if (ioctl(console_fd, KDSETLED, old_led_state)) {
        perror("Could not return the leds to the old state");
        return -1;
    }

    return 0;
}
