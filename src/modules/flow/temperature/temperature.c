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

#include "temperature-gen.h"
#include "sol-flow-internal.h"

#include <sol-util.h>
#include <errno.h>


#define CELSIUS_TO_FAHRENHEIT(_var) \
    do { \
        _var = _var * 9.0 / 5 + 32; \
    } while (0)

#define CELSIUS_TO_FAHRENHEIT_INTERVAL(_var) \
    do { \
        _var *= 9.0 / 5; \
    } while (0)

#define CELSIUS_TO_KELVIN(_var) \
    do { \
        _var += 273.15; \
    } while (0)

#define FAHRENHEIT_TO_CELSIUS(_var) \
    do { \
        _var = (_var - 32) * 5.0 / 9; \
    } while (0)

#define FAHRENHEIT_TO_CELSIUS_INTERVAL(_var) \
    do { \
        _var *= 5.0 / 9; \
    } while (0)

#define FAHRENHEIT_TO_RANKINE(_var) \
    do { \
        _var += 459.67; \
    } while (0)

#define KELVIN_TO_CELSIUS(_var) \
    do { \
        _var -= 273.15; \
    } while (0)

#define KELVIN_TO_RANKINE(_var) \
    do { \
        _var *= 9.0 / 5; \
    } while (0)

#define RANKINE_TO_CELSIUS(_var) \
    do { \
        _var = (_var - 491.67) * 5.0 / 9; \
    } while (0)

#define RANKINE_TO_CELSIUS_INTERVAL(_var) \
    do { \
        _var *= 5.0 / 9; \
    } while (0)

#define RANKINE_TO_FAHRENHEIT(_var) \
    do { \
        _var -= 459.67; \
    } while (0)

#define RANKINE_TO_KELVIN(_var) \
    do { \
        _var *= 5.0 / 9; \
    } while (0)


static int
fahrenheit_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_drange in_value, out_value;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__FAHRENHEIT, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value = in_value;
    FAHRENHEIT_TO_RANKINE(out_value.val);
    FAHRENHEIT_TO_RANKINE(out_value.min);
    FAHRENHEIT_TO_RANKINE(out_value.max);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__RANKINE, &out_value);
    SOL_INT_CHECK(r, < 0, r);

    RANKINE_TO_KELVIN(out_value.val);
    RANKINE_TO_KELVIN(out_value.min);
    RANKINE_TO_KELVIN(out_value.max);
    RANKINE_TO_KELVIN(out_value.step);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__KELVIN, &out_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value = in_value;
    FAHRENHEIT_TO_CELSIUS(out_value.val);
    FAHRENHEIT_TO_CELSIUS(out_value.min);
    FAHRENHEIT_TO_CELSIUS(out_value.max);
    FAHRENHEIT_TO_CELSIUS_INTERVAL(out_value.step);

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__CELSIUS, &out_value);
}

static int
celsius_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_drange in_value, out_value;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__CELSIUS, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value = in_value;
    CELSIUS_TO_KELVIN(out_value.val);
    CELSIUS_TO_KELVIN(out_value.min);
    CELSIUS_TO_KELVIN(out_value.max);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__KELVIN, &out_value);
    SOL_INT_CHECK(r, < 0, r);

    KELVIN_TO_RANKINE(out_value.val);
    KELVIN_TO_RANKINE(out_value.min);
    KELVIN_TO_RANKINE(out_value.max);
    KELVIN_TO_RANKINE(out_value.step);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__RANKINE, &out_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value = in_value;
    CELSIUS_TO_FAHRENHEIT(out_value.val);
    CELSIUS_TO_FAHRENHEIT(out_value.min);
    CELSIUS_TO_FAHRENHEIT(out_value.max);
    CELSIUS_TO_FAHRENHEIT_INTERVAL(out_value.step);

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__FAHRENHEIT, &out_value);
}

static int
kelvin_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_drange in_value, out_value;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__KELVIN, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value = in_value;
    KELVIN_TO_CELSIUS(out_value.val);
    KELVIN_TO_CELSIUS(out_value.min);
    KELVIN_TO_CELSIUS(out_value.max);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__CELSIUS, &out_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value = in_value;
    KELVIN_TO_RANKINE(out_value.val);
    KELVIN_TO_RANKINE(out_value.min);
    KELVIN_TO_RANKINE(out_value.max);
    KELVIN_TO_RANKINE(out_value.step);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__RANKINE, &out_value);
    SOL_INT_CHECK(r, < 0, r);

    RANKINE_TO_FAHRENHEIT(out_value.val);
    RANKINE_TO_FAHRENHEIT(out_value.min);
    RANKINE_TO_FAHRENHEIT(out_value.max);

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__FAHRENHEIT, &out_value);
}

static int
rankine_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    int r;
    struct sol_drange in_value, out_value;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__RANKINE, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value = in_value;
    RANKINE_TO_KELVIN(out_value.val);
    RANKINE_TO_KELVIN(out_value.min);
    RANKINE_TO_KELVIN(out_value.max);
    RANKINE_TO_KELVIN(out_value.step);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__KELVIN, &out_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value = in_value;
    RANKINE_TO_FAHRENHEIT(out_value.val);
    RANKINE_TO_FAHRENHEIT(out_value.min);
    RANKINE_TO_FAHRENHEIT(out_value.max);

    r = sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__FAHRENHEIT, &out_value);
    SOL_INT_CHECK(r, < 0, r);

    out_value = in_value;
    RANKINE_TO_CELSIUS(out_value.val);
    RANKINE_TO_CELSIUS(out_value.min);
    RANKINE_TO_CELSIUS(out_value.max);
    RANKINE_TO_CELSIUS_INTERVAL(out_value.step);

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_TEMPERATURE_CONVERTER__OUT__CELSIUS, &out_value);
}

#include "temperature-gen.c"
