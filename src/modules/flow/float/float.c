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

#include "float-gen.h"
#include "sol-flow-internal.h"
#include "sol-mainloop.h"

#include <sol-util.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

// =============================================================================
// DRANGE ARITHMETIC
// =============================================================================

struct drange_arithmetic_data {
    struct sol_drange var0;
    struct sol_drange var1;
    int (*func) (const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);
    uint16_t port;
    bool var0_initialized : 1;
    bool var1_initialized : 1;
};

static int
addition_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_arithmetic_data *mdata = data;

    mdata->port = SOL_FLOW_NODE_TYPE_FLOAT_ADDITION__OUT__OUT;
    mdata->func = sol_drange_addition;

    return 0;
}

static int
division_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_arithmetic_data *mdata = data;

    mdata->port = SOL_FLOW_NODE_TYPE_FLOAT_DIVISION__OUT__OUT;
    mdata->func = sol_drange_division;

    return 0;
}

static int
modulo_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_arithmetic_data *mdata = data;

    mdata->port = SOL_FLOW_NODE_TYPE_FLOAT_MODULO__OUT__OUT;
    mdata->func = sol_drange_modulo;

    return 0;
}

static int
multiplication_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_arithmetic_data *mdata = data;

    mdata->port = SOL_FLOW_NODE_TYPE_FLOAT_MULTIPLICATION__OUT__OUT;
    mdata->func = sol_drange_multiplication;

    return 0;
}

static int
subtraction_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_arithmetic_data *mdata = data;

    mdata->port = SOL_FLOW_NODE_TYPE_FLOAT_SUBTRACTION__OUT__OUT;
    mdata->func = sol_drange_subtraction;

    return 0;
}

static int
operator_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct drange_arithmetic_data *mdata = data;
    struct sol_drange value;
    int r;

    r = sol_flow_packet_get_drange(packet, &value);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    if (port == 0) {
        mdata->var0 = value;
        mdata->var0_initialized = true;
    } else {
        mdata->var1 = value;
        mdata->var1_initialized = true;
    }

    /* wait until have both variables */
    if (!(mdata->var0_initialized && mdata->var1_initialized))
        return 0;

    r = mdata->func(&mdata->var0, &mdata->var1, &value);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    sol_flow_send_drange_packet(node, mdata->port, &value);

    return 0;

error:
    sol_flow_send_error_packet(node, -r, sol_util_strerrora(-r));
    return 0;
}

// MATH

struct drange_pow_data {
    double var0;
    double var1;
    bool var0_initialized : 1;
    bool var1_initialized : 1;
};

static int
pow_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct drange_pow_data *mdata = data;
    double value, result;
    int r;

    r = sol_flow_packet_get_drange_value(packet, &value);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    if (port == 0) {
        if (mdata->var0_initialized &&
            sol_drange_val_equal(mdata->var0, value))
            return 0;
        mdata->var0 = value;
        mdata->var0_initialized = true;
    } else {
        if (mdata->var1_initialized &&
            sol_drange_val_equal(mdata->var1, value))
            return 0;
        mdata->var1 = value;
        mdata->var1_initialized = true;
    }

    if (!(mdata->var0_initialized && mdata->var1_initialized))
        return 0;

    errno = 0;
    result = pow(mdata->var0, mdata->var1);
    if (errno != 0) {
        r = -errno;
        goto error;
    }

    sol_flow_send_drange_value_packet(node, SOL_FLOW_NODE_TYPE_FLOAT_POW__OUT__OUT, result);

    return 0;

error:
    sol_flow_send_error_packet(node, -r, sol_util_strerrora(-r));
    return 0;
}

static int
ln_process(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    double value, result;
    int r;
    char *errmsg;

    r = sol_flow_packet_get_drange_value(packet, &value);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    if (fpclassify(value) == FP_ZERO || islessequal(value, 0.0)) {
        asprintf(&errmsg, "Number can't be negative or too close to zero. %s", sol_util_strerrora(EDOM));
        r = -EDOM;
        goto error;
    }

    result = log(value);

    sol_flow_send_drange_value_packet(node, SOL_FLOW_NODE_TYPE_FLOAT_LN__OUT__OUT, result);

    return 0;

error:
    if (errmsg) {
        sol_flow_send_error_packet(node, -r, errmsg);
        free(errmsg);
    } else {
        sol_flow_send_error_packet(node, -r, sol_util_strerrora(-r));
    }
    return 0;
}

static int
sqrt_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    double value, result;
    int r;
    char *errmsg;

    r = sol_flow_packet_get_drange_value(packet, &value);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    if (isless(value, 0)) {
        asprintf(&errmsg, "Number can't be negative. %s", sol_util_strerrora(EDOM));
        r = -EDOM;
        goto error;
    }

    result = sqrt(value);

    sol_flow_send_drange_value_packet(node, SOL_FLOW_NODE_TYPE_FLOAT_SQRT__OUT__OUT, result);

    return 0;

error:
    if (errmsg) {
        sol_flow_send_error_packet(node, -r, errmsg);
        free(errmsg);
    } else {
        sol_flow_send_error_packet(node, -r, sol_util_strerrora(-r));
    }
    return 0;
}

static int
abs_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    double value, result;
    int r;

    r = sol_flow_packet_get_drange_value(packet, &value);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    result = fabs(value);

    sol_flow_send_drange_value_packet(node, SOL_FLOW_NODE_TYPE_FLOAT_ABS__OUT__OUT, result);

    return 0;

error:
    sol_flow_send_error_packet(node, -r, sol_util_strerrora(-r));
    return 0;
}

struct drange_map_data {
    struct sol_drange input;
    struct sol_drange output;
    struct sol_drange output_value;
    bool use_input_range : 1;
};

static int
map_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_map_data *mdata = data;
    const struct sol_flow_node_type_float_map_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_FLOAT_MAP_OPTIONS_API_VERSION, -EINVAL);

    opts = (const struct sol_flow_node_type_float_map_options *)options;

    mdata->use_input_range = opts->use_input_range;
    mdata->input = opts->input_range;

    if (!mdata->use_input_range &&
        isgreaterequal(mdata->input.min, mdata->input.max)) {
        SOL_WRN("Invalid range: input max must to be bigger than min");
        return -EINVAL;
    }

    mdata->output = opts->output_range;

    if (isless(mdata->output.min, mdata->output.max)) {
        mdata->output_value.min = mdata->output.min;
        mdata->output_value.max = mdata->output.max;
    } else {
        mdata->output_value.max = mdata->output.min;
        mdata->output_value.min = mdata->output.max;
    }

    mdata->output_value.step = opts->output_range.step;

    return 0;
}

static int
_map(long double in_value, long double in_min, long double in_max, long double out_min, long double out_max, long double out_step, double *out_value)
{
    long double result;

    result = (in_value - in_min) * (out_max - out_min) /
             (in_max - in_min) + out_min;

    errno = 0;
    result -= fmodl((result - out_min), out_step);
    if (errno > 0)
        return -errno;

    *out_value = result;

    return 0;
}

static int
map_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct drange_map_data *mdata = data;
    struct sol_drange in_value;
    double out_value;
    int r;
    char *errmsg;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    if (mdata->use_input_range) {
        if (isgreaterequal(in_value.min, in_value.max)) {
            asprintf(&errmsg, "Invalid range: input max must be bigger than min. %s", sol_util_strerrora(EINVAL));
            r = -EINVAL;
            goto error;
        }
        r = _map(in_value.val, in_value.min, in_value.max, mdata->output.min,
            mdata->output.max, mdata->output_value.step, &out_value);
    } else
        r = _map(in_value.val, mdata->input.min, mdata->input.max,
            mdata->output.min, mdata->output.max, mdata->output_value.step,
            &out_value);

    SOL_INT_CHECK_GOTO(r, < 0, error);

    mdata->output_value.val = out_value;

    sol_flow_send_drange_packet(node, SOL_FLOW_NODE_TYPE_FLOAT_MAP__OUT__OUT,
        &mdata->output_value);

    return 0;

error:
    if (errmsg) {
        sol_flow_send_error_packet(node, -r, errmsg);
        free(errmsg);
    } else {
        sol_flow_send_error_packet(node, -r, sol_util_strerrora(-r));
    }
    return 0;
}

struct drange_constrain_data {
    struct sol_drange val;
    bool use_input_range : 1;
};

static int
constrain_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_constrain_data *mdata = data;
    const struct sol_flow_node_type_float_constrain_options *opts;

    opts = (const struct sol_flow_node_type_float_constrain_options *)options;
    mdata->val = opts->range;
    mdata->use_input_range = opts->use_input_range;

    if (isgreater(mdata->val.min, mdata->val.max)) {
        double min = mdata->val.min;
        mdata->val.min = mdata->val.max;
        mdata->val.max = min;
    }

    return 0;
}

static int
_constrain(struct sol_drange *value)
{
    double val = value->val;

    errno = 0;
    val -= fmod(value->val - value->min, value->step);
    if (errno) {
        SOL_WRN("Modulo failed: %f, %f", value->val - value->min, value->step);
        return -errno;
    }

    if (isless(val, value->min)) val = value->min;
    if (isgreater(val, value->max)) val = value->max;

    value->val = val;

    return 0;
}

static int
constrain_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct drange_constrain_data *mdata = data;
    int r;
    struct sol_drange value;

    r = sol_flow_packet_get_drange(packet, &value);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    if (!mdata->use_input_range) {
        value.min = mdata->val.min;
        value.max = mdata->val.max;
        value.step = mdata->val.step;
    }

    r = _constrain(&value);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    mdata->val = value;

    sol_flow_send_drange_packet(node, SOL_FLOW_NODE_TYPE_FLOAT_CONSTRAIN__OUT__OUT,
        &mdata->val);

    return 0;

error:
    sol_flow_send_error_packet(node, -r, sol_util_strerrora(-r));
    return 0;
}

struct drange_min_max_data {
    double val[2];
    int (*func) (double var0, double var1);
    uint16_t port;
    bool val_initialized[2];
};

static int
drange_val_isless(double var0, double var1)
{
    return isless(var0, var1);
}

static int
min_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_min_max_data *mdata = data;

    mdata->port = SOL_FLOW_NODE_TYPE_FLOAT_MIN__OUT__OUT;
    mdata->func = drange_val_isless;

    return 0;
}

static int
drange_val_isgreater(double var0, double var1)
{
    return isgreater(var0, var1);
}

static int
max_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_min_max_data *mdata = data;

    mdata->port = SOL_FLOW_NODE_TYPE_FLOAT_MAX__OUT__OUT;
    mdata->func = drange_val_isgreater;

    return 0;
}

static int
min_max_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct drange_min_max_data *mdata = data;
    double *result;
    int r;

    r = sol_flow_packet_get_drange_value(packet, &mdata->val[port]);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    mdata->val_initialized[port] = true;
    if (!(mdata->val_initialized[0] && mdata->val_initialized[1]))
        return 0;

    if (mdata->func(mdata->val[0], mdata->val[1]))
        result = &mdata->val[0];
    else
        result = &mdata->val[1];

    sol_flow_send_drange_value_packet(node, mdata->port, *result);

    return 0;

error:
    sol_flow_send_error_packet(node, -r, sol_util_strerrora(-r));
    return 0;
}

// =============================================================================
// DRANGE COMPARISON
// =============================================================================

struct drange_comparison_data {
    bool (*func) (double var0, double var1);
    double var0;
    double var1;
    uint16_t port;
    bool var0_initialized : 1;
    bool var1_initialized : 1;
};

static int
equal_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_comparison_data *mdata = data;

    mdata->port = SOL_FLOW_NODE_TYPE_FLOAT_EQUAL__OUT__OUT;
    mdata->func = sol_drange_val_equal;

    return 0;
}

static bool
drange_val_less(double var0, double var1)
{
    return isless(var0, var1);
}

static int
less_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_comparison_data *mdata = data;

    mdata->port = SOL_FLOW_NODE_TYPE_FLOAT_LESS__OUT__OUT;
    mdata->func = drange_val_less;

    return 0;
}

static bool
drange_val_less_or_equal(double var0, double var1)
{
    return islessequal(var0, var1);
}

static int
less_or_equal_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_comparison_data *mdata = data;

    mdata->port = SOL_FLOW_NODE_TYPE_FLOAT_LESS_OR_EQUAL__OUT__OUT;
    mdata->func = drange_val_less_or_equal;

    return 0;
}

static bool
drange_val_greater(double var0, double var1)
{
    return isgreater(var0, var1);
}

static int
greater_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_comparison_data *mdata = data;

    mdata->port = SOL_FLOW_NODE_TYPE_FLOAT_GREATER__OUT__OUT;
    mdata->func = drange_val_greater;

    return 0;
}

static bool
drange_val_greater_or_equal(double var0, double var1)
{
    return isgreaterequal(var0, var1);
}

static int
greater_or_equal_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_comparison_data *mdata = data;

    mdata->port = SOL_FLOW_NODE_TYPE_FLOAT_GREATER_OR_EQUAL__OUT__OUT;
    mdata->func = drange_val_greater_or_equal;

    return 0;
}

static bool
drange_val_not_equal(double var0, double var1)
{
    return !sol_drange_val_equal(var0, var1);
}

static int
not_equal_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct drange_comparison_data *mdata = data;

    mdata->port = SOL_FLOW_NODE_TYPE_FLOAT_NOT_EQUAL__OUT__OUT;
    mdata->func = drange_val_not_equal;

    return 0;
}

static int
comparison_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct drange_comparison_data *mdata = data;
    int r;
    struct sol_drange in_value;
    bool output;

    r = sol_flow_packet_get_drange(packet, &in_value);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    if (port == 0) {
        mdata->var0_initialized = true;
        mdata->var0 = in_value.val;
    } else if (port == 1) {
        mdata->var1_initialized = true;
        mdata->var1 = in_value.val;
    }

    /* only send something after both variables values are received */
    if (!(mdata->var0_initialized && mdata->var1_initialized))
        return 0;

    output = mdata->func(mdata->var0, mdata->var1);

    sol_flow_send_boolean_packet(node, mdata->port, output);

    return 0;

error:
    sol_flow_send_error_packet(node, -r, sol_util_strerrora(-r));
    return 0;
}

// =============================================================================
// DRANGE FILTER
// =============================================================================

struct float_filter_data {
    double max;
    double min;
    bool range_override;
};

static int
float_filter_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct float_filter_data *mdata = data;
    const struct sol_flow_node_type_float_filter_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_FLOAT_FILTER_OPTIONS_API_VERSION, -EINVAL);

    opts = (const struct sol_flow_node_type_float_filter_options *)options;
    if (isgreater(opts->max.val, opts->min.val)) {
        mdata->min = opts->min.val;
        mdata->max = opts->max.val;
    } else {
        SOL_DBG("min (%f) should be smaller than max (%f).",
            opts->min.val, opts->max.val);
        mdata->min = opts->max.val;
        mdata->max = opts->min.val;
    }
    mdata->range_override = opts->range_override;
    return 0;
}


static int
float_filter_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct  sol_flow_packet *packet)
{
    struct sol_drange value;
    int r;
    struct float_filter_data *mdata = data;

    r = sol_flow_packet_get_drange(packet, &value);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    if (isgreaterequal(value.val, mdata->min) && islessequal(value.val, mdata->max)) {
        if (mdata->range_override) {
            value.min = mdata->min;
            value.max = mdata->max;
            value.step = 1;
        }
        sol_flow_send_drange_packet(node, SOL_FLOW_NODE_TYPE_FLOAT_FILTER__OUT__OUT, &value);
    }

    return 0;

error:
    sol_flow_send_error_packet(node, -r, sol_util_strerrora(-r));
    return 0;
}


// =============================================================================
// DRANGE WAVE GENERATOR (TRAPEZOIDAL/SINUSOIDAL)
// =============================================================================

struct drange_wave_generator_trapezoidal_data {
    struct t_state {
        struct sol_drange val;
        unsigned min_tick_cnt, max_tick_cnt, curr_period_tick;
        bool increasing : 1;
        bool did_first : 1;
    } t_state;
    double inc_step, dec_step;
    uint32_t ticks_inc, ticks_dec, ticks_at_min, ticks_at_max, period_in_ticks;
};

static bool
tick_process(struct drange_wave_generator_trapezoidal_data *mdata)
{
    struct t_state *t_state = &mdata->t_state;

    t_state->curr_period_tick++;

    if (t_state->max_tick_cnt) {
        t_state->max_tick_cnt--;
        return true;
    }
    if (t_state->min_tick_cnt) {
        t_state->min_tick_cnt--;
        return true;
    }

    return false;
}

static void
direction_switch(struct drange_wave_generator_trapezoidal_data *mdata,
    bool inc_to_dec)
{
    if (inc_to_dec) {
        mdata->t_state.increasing = true;
        mdata->t_state.val.step = mdata->dec_step;
    } else {
        mdata->t_state.increasing = false;
        mdata->t_state.val.step = mdata->inc_step;
    }
}

static void
direction_check(struct drange_wave_generator_trapezoidal_data *mdata,
    bool reset_min_cnt,
    bool reset_max_cnt)
{
    struct sol_drange *v = &mdata->t_state.val;
    struct t_state *t_state = &mdata->t_state;

    if (sol_drange_val_equal(v->val, v->max)) {
        if (reset_max_cnt)
            t_state->max_tick_cnt = mdata->ticks_at_max;
        direction_switch(mdata, true);
    } else if (sol_drange_val_equal(v->val, v->min)) {
        if (reset_min_cnt)
            t_state->min_tick_cnt = mdata->ticks_at_min;
        direction_switch(mdata, false);
    }

    t_state->curr_period_tick %= mdata->period_in_ticks;
}

static bool
trapezoidal_iterate_do(struct drange_wave_generator_trapezoidal_data *mdata)
{
    struct sol_drange *v = &mdata->t_state.val;

    if (tick_process(mdata))
        return false;

    v->val += v->step;

    return true;
}

static void
trapezoidal_iterate(struct drange_wave_generator_trapezoidal_data *mdata)
{
    bool ret;

    if (!mdata->t_state.did_first) {
        mdata->t_state.did_first = true;
        direction_check(mdata, false, false);
        return;
    }

    ret = trapezoidal_iterate_do(mdata);
    direction_check(mdata, ret, ret);

    return;
}

static int
wave_generator_trapezoidal_process(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct drange_wave_generator_trapezoidal_data *mdata = data;

    trapezoidal_iterate(mdata);

    sol_flow_send_drange_packet(node, SOL_FLOW_NODE_TYPE_WAVE_GENERATOR_TRAPEZOIDAL__OUT__OUT,
        &mdata->t_state.val);

    return 0;
}

static int
wave_generator_trapezoidal_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct drange_wave_generator_trapezoidal_data *mdata = data;
    const struct sol_flow_node_type_wave_generator_trapezoidal_options *opts = (const struct sol_flow_node_type_wave_generator_trapezoidal_options *)options;
    uint32_t tick_start;
    struct t_state *t_state;
    struct sol_drange *val;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(opts, SOL_FLOW_NODE_TYPE_WAVE_GENERATOR_TRAPEZOIDAL_OPTIONS_API_VERSION, -EINVAL);

    if (isgreaterequal(opts->min.val, opts->max.val)) {
        SOL_ERR("Trapezoidal wave generator's min must be less than its max");
        return -EDOM;
    }
    if (opts->ticks_inc.val < 1) {
        SOL_ERR("Trapezoidal wave generator's ticks_inc value (%d) cannot"
            " be less than 1)", opts->ticks_inc.val);
        return -EDOM;
    }
    if (opts->ticks_dec.val < 1) {
        SOL_ERR("Trapezoidal wave generator's ticks_dec value (%d) cannot"
            " be less than 1)", opts->ticks_dec.val);
        return -EDOM;
    }
    if (opts->tick_start.val < 0) {
        SOL_ERR("Trapezoidal wave generator's tick_start value (%d) cannot"
            " be less than 0)", opts->tick_start.val);
        return -EDOM;
    }
    if (opts->ticks_at_max.val < 0) {
        SOL_ERR("Trapezoidal wave generator's ticks_at_max value (%d) cannot"
            " be less than 0)", opts->ticks_at_max.val);
        return -EDOM;
    }
    if (opts->ticks_at_min.val < 0) {
        SOL_ERR("Trapezoidal wave generator's ticks_at_min value (%d) cannot"
            " be less than 0)", opts->ticks_at_min.val);
        return -EDOM;
    }

    mdata->ticks_at_min = opts->ticks_at_min.val;
    mdata->ticks_at_max = opts->ticks_at_max.val;

    t_state = &mdata->t_state;
    val = &t_state->val;

    t_state->did_first = false;

    val->min = opts->min.val;
    val->max = opts->max.val;

    mdata->ticks_inc = opts->ticks_inc.val;
    mdata->ticks_dec = opts->ticks_dec.val;
    mdata->inc_step = (val->max - val->min) / mdata->ticks_inc;
    mdata->dec_step = (val->min - val->max) / mdata->ticks_dec;

    /* calculating starting val from tick_start */
    t_state->increasing = true;
    mdata->period_in_ticks = mdata->ticks_at_min + mdata->ticks_inc
                             + mdata->ticks_at_max + mdata->ticks_dec;

    tick_start = opts->tick_start.val % mdata->period_in_ticks;

    t_state->max_tick_cnt = 0;
    t_state->min_tick_cnt = mdata->ticks_at_min;

    val->val = val->min;
    val->step = mdata->inc_step;

    while (t_state->curr_period_tick != tick_start) {
        trapezoidal_iterate_do(mdata);
        /* we set min_tick_cnt manually, but max_tick_cnt has be
         * updated on the run */
        direction_check(mdata, false, true);
    }

    return 0;
}

struct drange_wave_generator_sinusoidal_data {
    struct s_state {
        struct sol_drange val;
        double rad_val;
        bool did_first : 1;
    } s_state;
    double rad_step;
    double amplitude;
};

static void
sinusoidal_calc_next(struct drange_wave_generator_sinusoidal_data *mdata)
{
    struct s_state *s_state;
    struct sol_drange *val;

    s_state = &mdata->s_state;
    val = &s_state->val;

    s_state->rad_val += mdata->rad_step;
    s_state->rad_val = fmod(s_state->rad_val, 2 * M_PI);

    val->val = sin(s_state->rad_val) * mdata->amplitude;
    val->step = (sin(s_state->rad_val + mdata->rad_step)
                 * mdata->amplitude) - val->val;
}

static void
sinusoidal_iterate(struct drange_wave_generator_sinusoidal_data *mdata)
{
    if (!mdata->s_state.did_first) {
        mdata->s_state.did_first = true;
        return;
    }

    sinusoidal_calc_next(mdata);

    return;
}

static int
wave_generator_sinusoidal_process(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct drange_wave_generator_sinusoidal_data *mdata = data;

    sinusoidal_iterate(mdata);

    sol_flow_send_drange_packet(node, SOL_FLOW_NODE_TYPE_WAVE_GENERATOR_SINUSOIDAL__OUT__OUT,
        &mdata->s_state.val);

    return 0;
}

static int
wave_generator_sinusoidal_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct drange_wave_generator_sinusoidal_data *mdata = data;
    const struct sol_flow_node_type_wave_generator_sinusoidal_options *opts = (const struct sol_flow_node_type_wave_generator_sinusoidal_options *)options;
    uint32_t tick_start;
    struct s_state *s_state;
    struct sol_drange *val;
    unsigned i;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(opts, SOL_FLOW_NODE_TYPE_WAVE_GENERATOR_SINUSOIDAL_OPTIONS_API_VERSION, -EINVAL);

    if (islessequal(opts->amplitude.val, 0)) {
        SOL_ERR("Sinusoidal wave generator's multiplier must be greater "
            "than zero");
        return -EDOM;
    }
    if (opts->ticks_per_period.val < 1) {
        SOL_ERR("Sinusoidal wave generator's ticks_per_period value (%d) cannot"
            " be less than 1)", opts->ticks_per_period.val);
        return -EDOM;
    }
    if (opts->tick_start.val < 0) {
        SOL_ERR("Sinusoidal wave generator's tick_start value (%d) cannot"
            " be less than 0)", opts->tick_start.val);
        return -EDOM;
    }

    mdata->amplitude = opts->amplitude.val;
    s_state = &mdata->s_state;
    val = &s_state->val;

    s_state->did_first = false;

    val->min = mdata->amplitude * -1.0;
    val->max = mdata->amplitude;

    mdata->rad_step = 2 * M_PI / opts->ticks_per_period.val;

    /* calculating starting val from tick_start */
    val->val = 0;
    tick_start = opts->tick_start.val % opts->ticks_per_period.val;

    for (i = 0; i < tick_start; i++)
        sinusoidal_calc_next(mdata);

    return 0;
}

#include "float-gen.c"
