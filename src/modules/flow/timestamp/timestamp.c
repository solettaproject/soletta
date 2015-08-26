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

#include "timestamp-gen.h"
#include "sol-flow-internal.h"

#include <sol-util.h>
#include <math.h>
#include <time.h>


static int
time_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct timespec current_time;
    int r;

    r = sol_util_timespec_get_realtime(&current_time);
    if (r < 0) {
        sol_flow_send_error_packet(node, r,
            "Could not fetch current time: %s", sol_util_strerrora(r));
        return 0;
    }

    return sol_flow_send_timestamp_packet(node,
        SOL_FLOW_NODE_TYPE_TIMESTAMP_TIME__OUT__OUT, &current_time);
}

struct make_time_data {
    struct tm received_time;
    time_t nsec;
    int16_t initialized;
};

#define ALL_PORTS_INITIALIZED ((int)pow(2, SOL_FLOW_NODE_TYPE_TIMESTAMP_MAKE_TIME__IN_LAST + 1) - 1)

static int
send_timestamp(struct sol_flow_node *node, uint16_t port, struct make_time_data *mdata)
{
    struct timespec timestamp;
    time_t timestamp_sec;

    mdata->initialized |= 1 << port;

    if (mdata->initialized != ALL_PORTS_INITIALIZED)
        return 0;

    timestamp_sec = mktime(&mdata->received_time);
    if (timestamp_sec < 0) {
        sol_flow_send_error_packet(node, EINVAL,
            "Failed to convert to timestamp");
        return 0;
    }

    timestamp.tv_sec = timestamp_sec;
    timestamp.tv_nsec = mdata->nsec;

    return sol_flow_send_timestamp_packet(node,
        SOL_FLOW_NODE_TYPE_TIMESTAMP_MAKE_TIME__OUT__OUT, &timestamp);
}

#undef ALL_PORTS_INITIALIZED

static int
make_year(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct make_time_data *mdata = data;
    int32_t value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    if (value < 1970) {
        sol_flow_send_error_packet(node, EINVAL,
            "Year (%" PRId32 ") out of range. Can't be less than 1970.",
            value);
        return 0;
    }

    mdata->received_time.tm_year = value - 1900;

    return send_timestamp(node, port, mdata);
}

static int
make_month(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct make_time_data *mdata = data;
    int32_t value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    if (value < 1 || value > 12) {
        sol_flow_send_error_packet(node, EINVAL,
            "Month (%" PRId32 ") out of range. Must be from 1 to 12.",
            value);
        return 0;
    }

    mdata->received_time.tm_mon = value - 1;

    return send_timestamp(node, port, mdata);
}

static int
make_day(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct make_time_data *mdata = data;
    int32_t value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    if (value < 1 || value > 31) {
        sol_flow_send_error_packet(node, EINVAL,
            "Day (%" PRId32 ") out of range. Must be from 1 to 31.",
            value);
        return 0;
    }

    mdata->received_time.tm_mday = value;

    return send_timestamp(node, port, mdata);
}

static int
make_hour(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct make_time_data *mdata = data;
    int32_t value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    if (value < 0 || value > 23) {
        sol_flow_send_error_packet(node, EINVAL,
            "Hour (%" PRId32 ") out of range. Must be from 0 to 23.",
            value);
        return 0;
    }

    mdata->received_time.tm_hour = value;

    return send_timestamp(node, port, mdata);
}

static int
make_minute(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct make_time_data *mdata = data;
    int32_t value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    if (value < 0 || value > 59) {
        sol_flow_send_error_packet(node, EINVAL,
            "Minute (%" PRId32 ") out of range. Must be from 0 to 59.",
            value);
        return 0;
    }

    mdata->received_time.tm_min = value;

    return send_timestamp(node, port, mdata);
}

static int
make_second(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct make_time_data *mdata = data;
    int32_t value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    if (value < 0 || value > 59) {
        sol_flow_send_error_packet(node, EINVAL,
            "Second (%" PRId32 ") out of range. Must be from 0 to 59.",
            value);
        return 0;
    }

    mdata->received_time.tm_sec = value;

    return send_timestamp(node, port, mdata);
}

static int
make_nano_second(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct make_time_data *mdata = data;
    int32_t value;
    int r;

    r = sol_flow_packet_get_irange_value(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    if (value < 0 || value > 999999999) {
        sol_flow_send_error_packet(node, EINVAL,
            "Nano second (%" PRId32 ") out of range. Must be from "
            "0 to 999999999.", value);
        return 0;
    }

    mdata->nsec = value;

    return send_timestamp(node, port, mdata);
}

static int
localtime_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct timespec value;
    struct tm split_time;
    struct sol_irange year = { 0, 0, INT32_MAX, 1 };
    struct sol_irange mon = { 0, 0, 11, 1 };
    struct sol_irange mday = { 0, 1, 31, 1 };
    struct sol_irange hour = { 0, 0, 23, 1 };
    struct sol_irange min = { 0, 0, 59, 1 };
    struct sol_irange sec = { 0, 0, 59, 1 };
    struct sol_irange nsec = { 0, 0, 999999999, 1 };
    struct sol_irange wday = { 0, 0, 6, 1 };
    struct sol_irange yday = { 0, 0, 365, 1 };
    int r;

    r = sol_flow_packet_get_timestamp(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    tzset();
    if (!localtime_r(&value.tv_sec, &split_time)) {
        sol_flow_send_error_packet(node, EINVAL,
            "Could not convert time.");
        return 0;
    }

    year.val = split_time.tm_year + 1900;
    r = sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_TIMESTAMP_SPLIT_TIME__OUT__YEAR,
        &year);
    SOL_INT_CHECK(r, < 0, r);

    mon.val = split_time.tm_mon + 1;
    r = sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_TIMESTAMP_SPLIT_TIME__OUT__MONTH,
        &mon);
    SOL_INT_CHECK(r, < 0, r);

    mday.val = split_time.tm_mday;
    r = sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_TIMESTAMP_SPLIT_TIME__OUT__MONTH_DAY,
        &mday);
    SOL_INT_CHECK(r, < 0, r);

    hour.val = split_time.tm_hour;
    r = sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_TIMESTAMP_SPLIT_TIME__OUT__HOUR,
        &hour);
    SOL_INT_CHECK(r, < 0, r);

    min.val = split_time.tm_min;
    r = sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_TIMESTAMP_SPLIT_TIME__OUT__MINUTE,
        &min);
    SOL_INT_CHECK(r, < 0, r);

    sec.val = split_time.tm_sec;
    r = sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_TIMESTAMP_SPLIT_TIME__OUT__SECOND,
        &sec);
    SOL_INT_CHECK(r, < 0, r);

    nsec.val = value.tv_nsec;
    r = sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_TIMESTAMP_SPLIT_TIME__OUT__NANO_SECOND,
        &nsec);
    SOL_INT_CHECK(r, < 0, r);

    wday.val = split_time.tm_wday;
    r = sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_TIMESTAMP_SPLIT_TIME__OUT__WEEK_DAY,
        &wday);
    SOL_INT_CHECK(r, < 0, r);

    yday.val = split_time.tm_yday;
    r = sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_TIMESTAMP_SPLIT_TIME__OUT__YEAR_DAY,
        &yday);
    SOL_INT_CHECK(r, < 0, r);

    if (split_time.tm_isdst < 0)
        SOL_DBG("Daylight saving time information not available.");
    else {
        r = sol_flow_send_boolean_packet(node,
            SOL_FLOW_NODE_TYPE_TIMESTAMP_SPLIT_TIME__OUT__DAYLIGHT_SAVING_TIME,
            !!split_time.tm_isdst);
        SOL_INT_CHECK(r, < 0, r);
    }

    return 0;
}

struct timestamp_comparison_node_type {
    struct sol_flow_node_type base;
    bool (*func) (struct timespec *var0, struct timespec *var1);
};

struct timestamp_comparison_data {
    struct timespec val[2];
    bool val_initialized[2];
};

static bool
timestamp_val_equal(struct timespec *var0, struct timespec *var1)
{
    if (var0->tv_sec != var1->tv_sec)
        return false;
    return var0->tv_nsec == var1->tv_nsec;
}

static bool
timestamp_val_less(struct timespec *var0, struct timespec *var1)
{
    if (var0->tv_sec == var1->tv_sec)
        return var0->tv_nsec < var1->tv_nsec;
    return var0->tv_sec < var1->tv_sec;
}

static bool
timestamp_val_less_or_equal(struct timespec *var0, struct timespec *var1)
{
    if (var0->tv_sec == var1->tv_sec)
        return var0->tv_nsec <= var1->tv_nsec;
    return var0->tv_sec <= var1->tv_sec;
}

static bool
timestamp_val_greater(struct timespec *var0, struct timespec *var1)
{
    if (var0->tv_sec == var1->tv_sec)
        return var0->tv_nsec > var1->tv_nsec;
    return var0->tv_sec > var1->tv_sec;
}

static bool
timestamp_val_greater_or_equal(struct timespec *var0, struct timespec *var1)
{
    if (var0->tv_sec == var1->tv_sec)
        return var0->tv_nsec >= var1->tv_nsec;
    return var0->tv_sec >= var1->tv_sec;
}

static bool
timestamp_val_not_equal(struct timespec *var0, struct timespec *var1)
{
    if (var0->tv_sec != var1->tv_sec)
        return true;
    return var0->tv_nsec != var1->tv_nsec;
}

static bool
two_vars_get_value(struct timestamp_comparison_data *mdata, uint16_t port, const struct sol_flow_packet *packet)
{
    int r;

    r = sol_flow_packet_get_timestamp(packet, &mdata->val[port]);
    SOL_INT_CHECK(r, < 0, r);

    mdata->val_initialized[port] = true;
    if (!(mdata->val_initialized[0] && mdata->val_initialized[1]))
        return false;

    return true;
}

static int
comparison_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct timestamp_comparison_data *mdata = data;
    const struct timestamp_comparison_node_type *type;
    bool output;

    if (!two_vars_get_value(mdata, port, packet))
        return 0;

    type = (const struct timestamp_comparison_node_type *)
        sol_flow_node_get_type(node);

    output = type->func(&mdata->val[0], &mdata->val[1]);

    return sol_flow_send_boolean_packet(node, 0, output);
}

static int
delta_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct timestamp_comparison_data *mdata = data;
    struct timespec sub_result;
    time_t result;
    int r;
    int32_t output;

    if (!two_vars_get_value(mdata, port, packet))
        return 0;

    result = mdata->val[1].tv_sec - mdata->val[0].tv_sec;
    if (result > INT32_MAX) {
        sol_flow_send_error_packet(node, ERANGE,
            "Delta is too big for seconds: %s", sol_util_strerrora(ERANGE));
        return 0;
    }
    output = result;

    r = sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_TIMESTAMP_DELTA__OUT__SECONDS, output);
    SOL_INT_CHECK(r, < 0, r);

    sol_util_timespec_sub(&mdata->val[0], &mdata->val[1], &sub_result);
    result = sub_result.tv_sec * NSEC_PER_SEC + sub_result.tv_nsec;

    if (result > INT32_MAX) {
        SOL_DBG("Delta is too big for nanoseconds: %s",
            sol_util_strerrora(ERANGE));
        return 0;
    }
    output = result;

    return sol_flow_send_irange_value_packet(node,
        SOL_FLOW_NODE_TYPE_TIMESTAMP_DELTA__OUT__NANO_SECONDS, output);
}

#include "timestamp-gen.c"
