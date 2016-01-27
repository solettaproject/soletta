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

#include <inttypes.h>
#include <stdint.h>

#include "sol-flow/platform.h"
#include "sol-platform.h"
#include "sol-flow-internal.h"

// =============================================================================
// PLATFORM
// =============================================================================

struct platform_data {
    struct sol_flow_node *node;
    enum sol_platform_state state;
};

struct monitor_data {
    uint16_t connections;
};

struct monitor_node_type {
    struct sol_flow_node_type base;
    int (*send_packet)(const void *, struct sol_flow_node *);
    int (*monitor_register)(struct sol_flow_node *);
    int (*monitor_unregister)(struct sol_flow_node *);
};

static int locale_monitor_unregister(struct sol_flow_node *node);

static int
state_dispatch_ready(struct platform_data *mdata)
{
    return sol_flow_send_boolean_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_PLATFORM__OUT__READY,
        mdata->state == SOL_PLATFORM_STATE_RUNNING);
}

static int
state_dispatch(struct platform_data *mdata)
{
    return state_dispatch_ready(mdata);
    /* TODO dispatch irange packet */
}

static void
on_state_changed(void *data, enum sol_platform_state state)
{
    struct platform_data *mdata = data;

    SOL_DBG("state changed %d -> %d", mdata->state, state);
    mdata->state = state;
    state_dispatch(mdata);
}

static int
platform_trigger_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct platform_data *mdata = data;

    return state_dispatch(mdata);
}

static int
platform_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct platform_data *mdata = data;

    mdata->node = node;
    sol_platform_add_state_monitor(on_state_changed, mdata);
    mdata->state = sol_platform_get_state();

    return state_dispatch_ready(mdata);
}

static void
platform_close(struct sol_flow_node *node, void *data)
{
    struct platform_data *mdata = data;

    sol_platform_del_state_monitor(on_state_changed, mdata);
}

// =============================================================================
// PLATFORM SERVICE
// =============================================================================

struct platform_service_data {
    struct sol_flow_node *node;
    char *service_name;
    enum sol_platform_service_state state;
};

static void
service_state_dispatch_active(struct platform_service_data *mdata)
{
    sol_flow_send_boolean_packet(mdata->node,
        SOL_FLOW_NODE_TYPE_PLATFORM_SERVICE__OUT__ACTIVE,
        mdata->state == SOL_PLATFORM_SERVICE_STATE_ACTIVE);
}

static void
service_state_dispatch(struct platform_service_data *mdata)
{
    service_state_dispatch_active(mdata);
    /* TODO dispatch irange packet */
}

static void
on_service_state_changed(void *data, const char *service, enum sol_platform_service_state state)
{
    struct platform_service_data *mdata = data;

    SOL_DBG("service %s state changed %d -> %d", service, mdata->state, state);
    mdata->state = state;
    service_state_dispatch(mdata);
}

static int
platform_service_trigger_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct platform_service_data *mdata = data;

    service_state_dispatch(mdata);
    return 0;
}

static int
platform_service_start_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct platform_service_data *mdata = data;

    return sol_platform_start_service(mdata->service_name);
}

static int
platform_service_stop_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct platform_service_data *mdata = data;

    return sol_platform_stop_service(mdata->service_name);
}

static int
platform_service_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct platform_service_data *mdata = data;
    const struct sol_flow_node_type_platform_service_options *opts;

    SOL_NULL_CHECK_MSG(options, -1, "Platform Service Node: Options not found.");
    opts = (const struct sol_flow_node_type_platform_service_options *)options;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_PLATFORM_SERVICE_OPTIONS_API_VERSION, -EINVAL);

    mdata->service_name = strdup(opts->service_name);
    SOL_NULL_CHECK(mdata->service_name, -ENOMEM);

    mdata->node = node;

    sol_platform_add_service_monitor(on_service_state_changed, mdata->service_name, mdata);
    mdata->state = sol_platform_get_service_state(mdata->service_name);

    return sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_PLATFORM_SERVICE__OUT__ACTIVE,
        (mdata->state == SOL_PLATFORM_SERVICE_STATE_ACTIVE));
}

static void
platform_service_close(struct sol_flow_node *node, void *data)
{
    struct platform_service_data *mdata = data;

    sol_platform_del_service_monitor(on_service_state_changed, mdata->service_name, mdata);

    free(mdata->service_name);
}

static int
platform_machine_id_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    const char *id;

    id = sol_platform_get_machine_id();
    if (!id) {
        sol_flow_send_error_packet(node, ENOSYS,
            "Fail on retrieving machine id -- not available");
        return 0; /* do not fail to create node */
    }

    return sol_flow_send_string_packet(node,
        SOL_FLOW_NODE_TYPE_PLATFORM_MACHINE_ID__OUT__OUT, id);
}

static int
hostname_send(const void *hostname, struct sol_flow_node *node)
{
    int r;

    if (!hostname) {
        hostname = sol_platform_get_hostname();
        SOL_NULL_CHECK(hostname, -ECANCELED);
    }

    r = sol_flow_send_string_packet(node, 0, hostname);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
system_clock_send(const void *timestamp, struct sol_flow_node *node)
{
    struct sol_irange irange = SOL_IRANGE_INIT();
    int r;

    if (!timestamp) {
        int64_t ts;
        ts = sol_platform_get_system_clock();
        if (ts > INT32_MAX) {
            sol_flow_send_error_packet(node, EOVERFLOW,
                "The timestamp %" PRId64 " can not be expressed using 32 bits", ts);
            return -EOVERFLOW;
        }
        irange.val = ts;
    } else
        irange.val = (*((int64_t *)timestamp));

    r = sol_flow_send_irange_packet(node, 0, &irange);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
monitor_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct sol_flow_node_type_platform_hostname_options *opts;
    const struct monitor_node_type *monitor_type =
        (const struct monitor_node_type *)sol_flow_node_get_type(node);

    opts = (const struct sol_flow_node_type_platform_hostname_options *)options;

    if (opts->send_initial_packet)
        return monitor_type->send_packet(NULL, node);
    return 0;
}

static int
hostname_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    const char *name;

    r = sol_flow_packet_get_string(packet, &name);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_platform_set_hostname(name);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static void
hostname_changed(void *data, const char *hostname)
{
    hostname_send(hostname, data);
}

static int
hostname_monitor_register(struct sol_flow_node *node)
{
    return sol_platform_add_hostname_monitor(hostname_changed, node);
}

static int
hostname_monitor_unregister(struct sol_flow_node *node)
{
    return sol_platform_del_hostname_monitor(hostname_changed, node);
}

static void
system_clock_changed(void *data, int64_t timestamp)
{
    system_clock_send(&timestamp, data);
}

static int
system_clock_monitor_register(struct sol_flow_node *node)
{
    return sol_platform_add_system_clock_monitor(system_clock_changed, node);
}

static int
system_clock_monitor_unregister(struct sol_flow_node *node)
{
    return sol_platform_del_system_clock_monitor(system_clock_changed, node);
}

static int
monitor_out_connect(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id)
{
    struct monitor_data *mdata = data;
    const struct monitor_node_type *monitor_type =
        (const struct monitor_node_type *)sol_flow_node_get_type(node);


    mdata->connections++;
    if (mdata->connections == 1)
        return monitor_type->monitor_register(node);
    return 0;
}

static int
monitor_out_disconnect(struct sol_flow_node *node, void *data,
    uint16_t port, uint16_t conn_id)
{
    struct monitor_data *mdata = data;
    const struct monitor_node_type *monitor_type =
        (const struct monitor_node_type *)sol_flow_node_get_type(node);

    if (!mdata->connections)
        return 0;

    if (!--mdata->connections)
        return monitor_type->monitor_unregister(node);
    return 0;
}

static int
system_clock_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange irange;
    int r;

    r = sol_flow_packet_get_irange(packet, &irange);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_platform_set_system_clock(irange.val);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
timezone_send(const void *tzone, struct sol_flow_node *node)
{
    int r;

    if (!tzone) {
        tzone = sol_platform_get_timezone();
        SOL_NULL_CHECK(tzone, -ECANCELED);
    }

    r = sol_flow_send_string_packet(node, 0, tzone);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static void
timezone_changed(void *data, const char *tzone)
{
    timezone_send(tzone, data);
}

static int
timezone_monitor_register(struct sol_flow_node *node)
{
    return sol_platform_add_timezone_monitor(timezone_changed, node);
}

static int
timezone_monitor_unregister(struct sol_flow_node *node)
{
    return sol_platform_del_timezone_monitor(timezone_changed, node);
}

static int
timezone_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    const char *tz;

    r = sol_flow_packet_get_string(packet, &tz);
    SOL_INT_CHECK(r, < 0, r);
    r = sol_platform_set_timezone(tz);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
locale_send(struct sol_flow_node *node, enum sol_platform_locale_category category, const char *locale)
{
    if (category == SOL_PLATFORM_LOCALE_UNKNOWN && !locale) {
        locale_monitor_unregister(node);
        return sol_flow_send_error_packet(node, ECANCELED,
            "Something wrong happened with the locale monitor,"
            "stoping to monitor locale changes");
    }

    if (!locale) {
        locale = sol_platform_get_locale(category);
        SOL_NULL_CHECK(locale, -EINVAL);
    }
    return sol_flow_send_string_packet(node, (int)category, locale);
}

static int
locale_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    int r;
    const struct sol_flow_node_type_platform_hostname_options *opts;
    enum sol_platform_locale_category i;

    opts = (const struct sol_flow_node_type_platform_hostname_options *)options;

    if (!opts->send_initial_packet)
        return 0;

    for (i = SOL_PLATFORM_LOCALE_LANGUAGE; i <= SOL_PLATFORM_LOCALE_TIME; i++) {
        r = locale_send(node, i, NULL);
        SOL_INT_CHECK(r, < 0, r);
    }
    return 0;
}

static void
locale_changed(void *data, enum sol_platform_locale_category category, const char *locale)
{
    locale_send(data, category, locale);
}

static int
locale_monitor_register(struct sol_flow_node *node)
{
    return sol_platform_add_locale_monitor(locale_changed, node);
}

static int
locale_monitor_unregister(struct sol_flow_node *node)
{
    return sol_platform_del_locale_monitor(locale_changed, node);
}

static int
set_locale(enum sol_platform_locale_category category, const struct sol_flow_packet *packet)
{
    int r;
    const char *locale;

    r = sol_flow_packet_get_string(packet, &locale);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_platform_set_locale(category, locale);
    SOL_INT_CHECK(r, < 0, r);
    return 0;
}

static int
locale_all_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_locale(SOL_PLATFORM_LOCALE_LANGUAGE, packet);
}

static int
locale_address_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_locale(SOL_PLATFORM_LOCALE_ADDRESS, packet);
}

static int
locale_collate_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_locale(SOL_PLATFORM_LOCALE_COLLATE, packet);
}

static int
locale_ctype_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_locale(SOL_PLATFORM_LOCALE_CTYPE, packet);
}

static int
locale_identification_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_locale(SOL_PLATFORM_LOCALE_IDENTIFICATION, packet);
}

static int
locale_measurement_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_locale(SOL_PLATFORM_LOCALE_MEASUREMENT, packet);
}

static int
locale_messages_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_locale(SOL_PLATFORM_LOCALE_MESSAGES, packet);
}

static int
locale_monetary_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_locale(SOL_PLATFORM_LOCALE_MONETARY, packet);
}

static int
locale_name_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_locale(SOL_PLATFORM_LOCALE_NAME, packet);
}

static int
locale_numeric_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_locale(SOL_PLATFORM_LOCALE_NUMERIC, packet);
}

static int
locale_paper_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_locale(SOL_PLATFORM_LOCALE_PAPER, packet);
}

static int
locale_telephone_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_locale(SOL_PLATFORM_LOCALE_TELEPHONE, packet);
}

static int
locale_time_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return set_locale(SOL_PLATFORM_LOCALE_TIME, packet);
}

static int
locale_apply_lang_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_platform_apply_locale(SOL_PLATFORM_LOCALE_LANGUAGE);
}

static int
locale_apply_address_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_platform_apply_locale(SOL_PLATFORM_LOCALE_ADDRESS);
}

static int
locale_apply_collate_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_platform_apply_locale(SOL_PLATFORM_LOCALE_COLLATE);
}

static int
locale_apply_ctype_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_platform_apply_locale(SOL_PLATFORM_LOCALE_CTYPE);
}
static int
locale_apply_identification_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_platform_apply_locale(SOL_PLATFORM_LOCALE_IDENTIFICATION);
}

static int
locale_apply_measurement_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_platform_apply_locale(SOL_PLATFORM_LOCALE_MEASUREMENT);
}

static int
locale_apply_messages_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_platform_apply_locale(SOL_PLATFORM_LOCALE_MESSAGES);
}

static int
locale_apply_monetary_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_platform_apply_locale(SOL_PLATFORM_LOCALE_MONETARY);
}

static int
locale_apply_name_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_platform_apply_locale(SOL_PLATFORM_LOCALE_NAME);
}

static int
locale_apply_numeric_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_platform_apply_locale(SOL_PLATFORM_LOCALE_NUMERIC);
}

static int
locale_apply_paper_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_platform_apply_locale(SOL_PLATFORM_LOCALE_PAPER);
}

static int
locale_apply_telephone_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_platform_apply_locale(SOL_PLATFORM_LOCALE_TELEPHONE);
}

static int
locale_apply_time_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    return sol_platform_apply_locale(SOL_PLATFORM_LOCALE_TIME);
}

#include "platform-gen.c"
