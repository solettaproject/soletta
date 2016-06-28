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

#include "sol-flow/temperature.h"
#include "sol-flow-internal.h"

#include <sol-util-internal.h>
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
