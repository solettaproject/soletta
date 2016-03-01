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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "sol-flow-single.h"
#include "sol-log.h"
#include "sol-util.h"
#include "soletta.h"

/**
 * @file single-node.c
 *
 * Example how to create and use a single node without an associated
 * flow. This is useful when you need to access a component, send
 * packets to its input ports manually and be notified when it's
 * sending packets on its output ports.
 *
 * To showcase it we use "wallclock/minute" and "wallclock/second"
 * node types, they use both input and output ports as well as
 * options. Although simple, this is a realistic example since to
 * properly tick at every minute one needs to calculate the expire
 * time to the next minute (not just start a 60s timer) and handle
 * monitoring the system clock for changes.
 */

static struct sol_flow_node *minutes, *seconds;

static uint16_t minutes_port_out, minutes_port_enabled;
static uint16_t seconds_port_out, seconds_port_enabled;

static int32_t
get_int32_packet(const struct sol_flow_node *n, uint16_t port, const struct sol_flow_packet *packet)
{
    const struct sol_flow_node_type *type;
    const struct sol_flow_port_description *port_desc;
    int32_t value;
    int err;

    err = sol_flow_packet_get_irange_value(packet, &value);
    if (err < 0) {
        fprintf(stderr, "ERROR: could not get irange packet value: %p %s\n",
            packet, sol_util_strerrora(-err));
        return err;
    }

    type = sol_flow_node_get_type(n);
    port_desc = sol_flow_node_get_port_out_description(type, port);
    if (!port_desc) {
        fprintf(stderr, "ERROR: no output port description for index %" PRIu16
            " of node %p\n",
            port, n);
        return -ENOENT;
    }

    printf("node type %s port #%" PRIu16 " '%s' (%s): %" PRId32 "\n",
        type->description->name, port, port_desc->name,
        port_desc->data_type, value);
    return value;
}

static void
on_minutes_packet(void *data, struct sol_flow_node *n, uint16_t port, const struct sol_flow_packet *packet)
{
    int32_t value = get_int32_packet(n, port, packet);
    static int i = 0;

    if (value < 0)
        return;

    i++;
    if (i == 1)
        return; /* first time let it go */
    if (i % 2 == 0) {
        puts("stop seconds and disconnect output port, will change in 1 minute");
        sol_flow_single_port_out_disconnect(seconds, seconds_port_out);
        sol_flow_send_boolean_packet(seconds, seconds_port_enabled, false);
    } else {
        puts("start seconds and connect output port, will change in 1 minute");
        sol_flow_single_port_out_connect(seconds, seconds_port_out);
        sol_flow_send_boolean_packet(seconds, seconds_port_enabled, true);
    }
}

static void
on_seconds_packet(void *data, struct sol_flow_node *n, uint16_t port, const struct sol_flow_packet *packet)
{
    get_int32_packet(n, port, packet);
}

static void
create_minutes(void)
{
    const struct sol_flow_node_type *type;
    struct sol_flow_node_named_options named_opts = {};
    const char *strv_opts[] = {
        "send_initial_packet=1",
        NULL
    };
    struct sol_flow_node_options *opts;
    int err;

    /* Resolves the type based on its name. This will take care of
     * built-in modules and external modules, loading on demand as
     * required. This macro also handles static-compiles, so the
     * second parameter is the C symbol to be used in such case.
     */
    err = sol_flow_get_node_type("wallclock/minute",
        SOL_FLOW_NODE_TYPE_WALLCLOCK_MINUTE, &type);
    if (err < 0) {
        fputs("could not find type: wallclock/minute\n", stderr);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    /* For efficiency matters Soletta doesn't work with port names,
     * rather using port indexes. If
     * SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED, then we can resolve
     * strings to numbers, otherwise it is required to check the port
     * numbers, which are often available in the header file such as
     * sol-flow/wallclock.h such as
     * SOL_FLOW_NODE_TYPE_WALLCLOCK_MINUTE__OUT__OUT.
     */
    minutes_port_enabled = sol_flow_node_find_port_in(type, "ENABLED");
    if (minutes_port_enabled == UINT16_MAX) {
        fputs("ERROR: couldn't find ouput port by name: ENABLED\n", stderr);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }
    minutes_port_out = sol_flow_node_find_port_out(type, "OUT");
    if (minutes_port_out == UINT16_MAX) {
        fputs("ERROR: couldn't find ouput port by name: OUT\n", stderr);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    /* wallclock/minute takes a boolean option send_initial_packet.
     * We have couple of options to create it:
     *
     * 1 - include sol-flow/wallclock.h, declare a variable of type
     *     struct sol_flow_node_type_wallclock_minute_options and fill
     *     its members. This requires sol-flow/wallclock.h to be
     *     avaiable, but is more efficient since goes straight to the
     *     point, no parsing or memory allocations. It would look
     *     like:
     *
     *        #include <sol-flow/wallclock.h>
     *
     *        struct sol_flow_node_type_wallclock_minute_options opts =
     *           SOL_FLOW_NODE_TYPE_WALLCLOCK_MINUTE_OPTIONS_DEFAULTS(
     *              .send_initial_packet = true);
     *
     * 2 - access type->options_size,
     *     type->default_options and
     *     type->description->options and manually
     *     create the structure in runtime. This demands
     *     SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED, but may be useful
     *     when converting from a different representation, like a
     *     language binding such as JavaScript or Python where one can
     *     get an object, hashmap or dictionary and convert straight
     *     to the C structure needed by the flow node type.
     *
     * 3 - use helper sol_flow_node_named_options_init_from_strv() and
     *     sol_flow_node_options_new(), giving it an array of
     *     "key=value" strings. It will do the work described in #2
     *     for us, thus also depends on
     *     SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED.
     *
     * We'll use approach #3 since it is simpler. Language bindings
     * should go with option #2 and those that want to squeeze the
     * size and get performance should consider #1.
     */
    err = sol_flow_node_named_options_init_from_strv(&named_opts, type, strv_opts);
    if (err < 0) {
        fputs("could not parse options for wallclock/minute\n", stderr);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    /* convert the named options in the actual options structure */
    err = sol_flow_node_options_new(type, &named_opts, &opts);
    sol_flow_node_named_options_fini(&named_opts);
    if (err < 0) {
        fputs("could not create options for wallclock/minute\n", stderr);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    /* Build the single node wrapping the wanted 'wallclock/minute'.
     * For most matters the single node behaves like the inner node,
     * it will copy the descriptions and options.
     *
     * The difference is that if you call sol_flow_send_packet() on
     * its input ports, it will forward the packet to the inner
     * node. Likewise, packets originated at the outgoing ports of the
     * inner node will be delivered through the process callback
     * (on_packet()) provided to the single node.
     *
     * Note that ports you want to send (in) or receive (out) packets
     * must be connected with the connected_ports_in and
     * connected_ports_out parameters, or later with
     * sol_flow_single_port_in_connect() and
     * sol_flow_single_port_out_connect().
     */
    minutes = sol_flow_single_new("minutes", type, opts,
        SOL_FLOW_SINGLE_CONNECTIONS(minutes_port_enabled),
        SOL_FLOW_SINGLE_CONNECTIONS(minutes_port_out),
        on_minutes_packet, NULL);
    sol_flow_node_options_del(type, opts);
}


static void
create_seconds(void)
{
    const struct sol_flow_node_type *type;
    struct sol_flow_node_named_options named_opts = {};
    const char *strv_opts[] = {
        "send_initial_packet=1",
        NULL
    };
    struct sol_flow_node_options *opts;
    int err;

    err = sol_flow_get_node_type("wallclock/second",
        SOL_FLOW_NODE_TYPE_WALLCLOCK_SECOND, &type);
    if (err < 0) {
        fputs("could not find type: wallclock/second\n", stderr);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    seconds_port_enabled = sol_flow_node_find_port_in(type, "ENABLED");
    if (seconds_port_enabled == UINT16_MAX) {
        fputs("ERROR: couldn't find ouput port by name: ENABLED\n", stderr);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }
    seconds_port_out = sol_flow_node_find_port_out(type, "OUT");
    if (seconds_port_out == UINT16_MAX) {
        fputs("ERROR: couldn't find ouput port by name: OUT\n", stderr);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    err = sol_flow_node_named_options_init_from_strv(&named_opts, type, strv_opts);
    if (err < 0) {
        fputs("could not parse options for wallclock/second\n", stderr);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    err = sol_flow_node_options_new(type, &named_opts, &opts);
    sol_flow_node_named_options_fini(&named_opts);
    if (err < 0) {
        fputs("could not create options for wallclock/second\n", stderr);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    seconds = sol_flow_single_new("seconds", type, opts,
        SOL_FLOW_SINGLE_CONNECTIONS(seconds_port_enabled),
        SOL_FLOW_SINGLE_CONNECTIONS(seconds_port_out),
        on_seconds_packet, NULL);
    sol_flow_node_options_del(type, opts);
}

static void
startup(void)
{
    create_minutes();
    create_seconds();
}

static void
shutdown(void)
{
    sol_flow_node_del(minutes);
    sol_flow_node_del(seconds);
}

SOL_MAIN_DEFAULT(startup, shutdown);
