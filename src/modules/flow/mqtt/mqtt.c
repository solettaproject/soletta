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

#include "sol-flow/mqtt.h"
#include "sol-flow-internal.h"

#include <sol-certificate.h>
#include <sol-mainloop.h>
#include <sol-mqtt.h>
#include <sol-util.h>
#include <errno.h>


struct client_data {
    struct sol_flow_node *node;

    char *host;
    int port;

    char *user;
    char *pass;

    char *id;

    int keepalive;

    bool clean_session;
    sol_mqtt_qos qos;

    struct sol_cert *ca_cert;
    struct sol_cert *client_cert;
    struct sol_cert *private_key;

    char *topic;
    struct sol_blob *payload;

    struct sol_mqtt *mqtt;

    bool pending_publish;
    bool pending_subscribe;
};

static void
publish(struct client_data *mdata)
{
    struct sol_mqtt_message message;
    struct sol_buffer payload_buffer;
    int r;

    if (!mdata->payload)
        return;

    payload_buffer = SOL_BUFFER_INIT_CONST(mdata->payload->mem, mdata->payload->size);

    message = (struct sol_mqtt_message){
        .topic = mdata->topic,
        .payload = &payload_buffer,
        .qos = mdata->qos,
        .retain = false,
    };

    r = sol_mqtt_publish(mdata->mqtt, &message);

    if (r != 0)
        sol_flow_send_error_packet(mdata->node, ENOTCONN, "Disconnected from MQTT broker");
}

static void
subscribe(struct client_data *mdata)
{
    int r;

    r = sol_mqtt_subscribe(mdata->mqtt, mdata->topic, mdata->qos);

    if (r != 0)
        sol_flow_send_error_packet(mdata->node, ENOTCONN, "Disconnected from MQTT broker");
}

static void
on_connect(void *data, struct sol_mqtt *mqtt)
{
    struct client_data *mdata = data;

    if (sol_mqtt_get_connection_status(mqtt) != SOL_MQTT_CONNECTED) {
        sol_flow_send_error_packet(mdata->node, ENOTCONN, "Unable to connect to MQTT broker");
        return;
    }

    if (mdata->pending_publish) {
        mdata->pending_publish = false;
        publish(mdata);
    }

    if (mdata->pending_subscribe) {
        mdata->pending_subscribe = false;
        subscribe(mdata);
    }
}

static void
on_disconnect(void *data, struct sol_mqtt *mqtt)
{
    struct client_data *mdata = data;

    sol_mqtt_disconnect(mdata->mqtt);
    mdata->mqtt = NULL;
}

static void
on_message(void *data, struct sol_mqtt *mqtt, const struct sol_mqtt_message *message)
{
    struct client_data *mdata = data;
    struct sol_blob *blob;
    char *payload;

    payload = sol_util_memdup(message->payload->data, message->payload->used);
    SOL_NULL_CHECK(payload);

    blob = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL, payload, message->payload->used);
    SOL_NULL_CHECK_GOTO(blob, error);

    sol_flow_send_blob_packet(mdata->node, SOL_FLOW_NODE_TYPE_MQTT_CLIENT__OUT__OUTDATA, blob);
    sol_blob_unref(blob);

    return;

error:
    free(payload);
}

static void
mqtt_init(struct client_data *mdata)
{
    struct sol_mqtt_config config = {
        .api_version = SOL_MQTT_CONFIG_API_VERSION,
        .clean_session = mdata->clean_session,
        .keepalive = mdata->keepalive,
        .username = mdata->user,
        .client_id = mdata->id,
        .password = mdata->pass,
        .ca_cert = mdata->ca_cert,
        .client_cert = mdata->client_cert,
        .private_key = mdata->private_key,
        .handlers = {
            .connect = on_connect,
            .disconnect = on_disconnect,
            .message = on_message,
        },
    };

    mdata->mqtt = sol_mqtt_connect(mdata->host, mdata->port, &config, mdata);

    if (!mdata->mqtt) {
        sol_flow_send_error_packet(mdata->node, ENOMEM,
            "Unable to create MQTT session. Retrying...");
    }
}

static void
mqtt_client_close(struct sol_flow_node *node, void *data)
{
    struct client_data *mdata = data;

    if (mdata->mqtt)
        sol_mqtt_disconnect(mdata->mqtt);

    if (mdata->payload)
        sol_blob_unref(mdata->payload);

    sol_cert_unref(mdata->ca_cert);
    sol_cert_unref(mdata->client_cert);
    sol_cert_unref(mdata->ca_cert);

    free(mdata->host);
    free(mdata->user);
    free(mdata->pass);
    free(mdata->id);
    free(mdata->topic);
}

static int
mqtt_client_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct client_data *mdata = data;
    const struct sol_flow_node_type_mqtt_client_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_MQTT_CLIENT_OPTIONS_API_VERSION, -EINVAL);
    opts = (const struct sol_flow_node_type_mqtt_client_options *)options;

    mdata->node = node;

#define ALLOC_AND_CHECK(src, dst, alloc) \
    do { \
        if (src) { \
            dst = alloc(src); \
            SOL_NULL_CHECK_GOTO(dst, error); \
        } \
    } while (0) \

    ALLOC_AND_CHECK(opts->host, mdata->host, strdup);
    ALLOC_AND_CHECK(opts->username, mdata->user, strdup);
    ALLOC_AND_CHECK(opts->password, mdata->pass, strdup);
    ALLOC_AND_CHECK(opts->client_id, mdata->id, strdup);
    ALLOC_AND_CHECK(opts->ca_cert, mdata->ca_cert, sol_cert_load_from_file);
    ALLOC_AND_CHECK(opts->client_cert, mdata->client_cert, sol_cert_load_from_file);
    ALLOC_AND_CHECK(opts->private_key, mdata->private_key, sol_cert_load_from_file);
    ALLOC_AND_CHECK(opts->topic, mdata->topic, strdup);

#undef ALLOC_AND_CHECK

    mdata->port = opts->port;
    mdata->keepalive = opts->keepalive;
    mdata->qos = (sol_mqtt_qos)opts->qos;
    mdata->clean_session = opts->clean_session;

    return 0;

error:
    mqtt_client_close(node, data);
    return -ENOMEM;
}

static int
clean_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct client_data *mdata = data;
    int r;
    bool in_value;

    r = sol_flow_packet_get_boolean(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->clean_session = in_value;

    return 0;
}

static int
keepalive_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct client_data *mdata = data;
    int r;
    struct sol_irange in_value;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->keepalive = in_value.val;

    return 0;
}

static int
host_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct client_data *mdata = data;
    int r;
    const char *in_value;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_if_changed(&mdata->host, in_value);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
publish_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct client_data *mdata = data;

    if (!mdata->mqtt) {
        mdata->pending_publish = true;
        mqtt_init(mdata);
    } else {
        publish(mdata);
    }

    return 0;
}

static int
subscribe_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct client_data *mdata = data;

    if (!mdata->mqtt) {
        mdata->pending_subscribe = true;
        mqtt_init(mdata);
    } else {
        subscribe(mdata);
    }

    return 0;
}

static int
user_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct client_data *mdata = data;
    int r;
    const char *in_value;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_if_changed(&mdata->user, in_value);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
qos_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct client_data *mdata = data;
    int r;
    struct sol_irange in_value;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->qos = (sol_mqtt_qos)in_value.val;

    return 0;
}

static int
data_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct client_data *mdata = data;
    int r;
    struct sol_blob *in_value;

    r = sol_flow_packet_get_blob(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    sol_blob_ref(in_value);

    if (mdata->payload)
        sol_blob_unref(mdata->payload);

    mdata->payload = in_value;

    return 0;
}

static int
topic_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct client_data *mdata = data;
    int r;
    const char *in_value;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_if_changed(&mdata->topic, in_value);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
id_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct client_data *mdata = data;
    int r;
    const char *in_value;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_if_changed(&mdata->id, in_value);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
port_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct client_data *mdata = data;
    int r;
    struct sol_irange in_value;

    r = sol_flow_packet_get_irange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    mdata->port = in_value.val;

    return 0;
}

static int
pass_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct client_data *mdata = data;
    int r;
    const char *in_value;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_replace_str_if_changed(&mdata->pass, in_value);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

#include "mqtt-gen.c"
