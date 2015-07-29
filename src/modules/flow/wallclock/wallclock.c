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

#include "wallclock-gen.h"
#include "sol-flow-internal.h"
#include "sol-mainloop.h"
#include "sol-vector.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum sol_flow_node_wallclock_type {
    TIMEOUT_SECOND,
    TIMEOUT_MINUTE,
    TIMEOUT_HOUR,
    TIMEOUT_WEEKDAY,
    TIMEOUT_MONTHDAY
};

struct wallclock_data {
    enum sol_flow_node_wallclock_type type;
};

struct wallclock_timer {
    struct sol_ptr_vector clients;
    struct sol_timeout *timer;
    int pending_deletion;
    int walking;
    struct sol_irange val;
};

static struct wallclock_timer timers[] = {
    [TIMEOUT_SECOND] = { .clients = SOL_PTR_VECTOR_INIT,
                         .val.step = 1,
                         .val.min = 0,
                         .val.max = 59, },
    [TIMEOUT_MINUTE] = { .clients = SOL_PTR_VECTOR_INIT,
                         .val.step = 1,
                         .val.min = 0,
                         .val.max = 59, },
    [TIMEOUT_HOUR] = { .clients = SOL_PTR_VECTOR_INIT,
                       .val.step = 1,
                       .val.min = 0,
                       .val.max = 23, },
    [TIMEOUT_WEEKDAY] = { .clients = SOL_PTR_VECTOR_INIT,
                          .val.step = 1,
                          .val.min = 0,
                          .val.max = 6, },
    [TIMEOUT_MONTHDAY] = { .clients = SOL_PTR_VECTOR_INIT,
                           .val.step = 1,
                           .val.min = 1,
                           .val.max = 31, },
};

static void
clients_cleanup(struct wallclock_timer *timer)
{
    uint16_t i;
    struct sol_flow_node *n;

    if (timer->walking > 0)
        return;

    if (timer->pending_deletion > 0) {
        SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&(timer->clients), n, i) {
            if (n) continue;
            timer->pending_deletion--;
            sol_ptr_vector_del(&(timer->clients), i);
            if (timer->pending_deletion == 0)
                break;
        }
    }

    if (sol_ptr_vector_get_len(&(timer->clients)) == 0) {
        if (timer->timer) {
            sol_timeout_del(timer->timer);
            timer->timer = NULL;
        }
    }
}

#define CLOCK_GETTIME_DO(_store_var, _store_val)        \
    do {                                                \
        if (sol_util_timespec_get_realtime(&ts) < 0) {   \
            SOL_WRN("could not fetch current time: %s",  \
                sol_util_strerrora(errno));           \
            time_fail = true;                           \
            _store_var = 0;                             \
        } else                                          \
            _store_var = _store_val;                    \
    } while (0)

static bool wallclock_do(void *data);

static bool
wallclock_schedule_next(struct sol_flow_node *node)
{
    struct wallclock_data *mdata = sol_flow_node_get_private_data(node);
    struct wallclock_timer *timer = &timers[mdata->type];
    bool time_fail = false;
    struct tm local_time;
    time_t current_time;
    struct timespec ts;
    uint32_t timeout;

    if (mdata->type == TIMEOUT_SECOND) {
        CLOCK_GETTIME_DO(timeout,
            1000 - ((ts.tv_sec * 1000)
            + (ts.tv_nsec / 1000000)) % 1000);
        if (time_fail)
            goto err;
    } else {
        time_t seconds;
        time_t minutes;

        if (time(&current_time) == -1) {
            SOL_WRN("could not fetch current time: %s",
                sol_util_strerrora(errno));
            goto err;
        }

        if (!localtime_r(&current_time, &local_time)) {
            SOL_WRN("could not convert time: %s",
                sol_util_strerrora(errno));
            goto err;
        }

#define SECONDS_IN_MINUTE (60)
#define SECONDS_IN_HOUR (3600)
#define MINUTES_IN_HOUR (60)
#define HOURS_IN_DAY (24)

        seconds = (local_time.tm_sec);
        minutes = (local_time.tm_min) * SECONDS_IN_MINUTE;

        if (mdata->type == TIMEOUT_MINUTE) {
            timeout = (SECONDS_IN_MINUTE - local_time.tm_sec);
        } else if (mdata->type == TIMEOUT_HOUR) {
            timeout = (MINUTES_IN_HOUR - local_time.tm_min) * SECONDS_IN_MINUTE
                - seconds;
        } else { /* TIMEOUT_MONTHDAY or TIMEOUT_WEEKDAY */
            timeout = (HOURS_IN_DAY - local_time.tm_hour) * SECONDS_IN_HOUR
                - minutes - seconds;
        }
    }

#undef SECONDS_IN_MINUTE
#undef SECONDS_IN_HOUR
#undef MINUTES_IN_HOUR
#undef HOURS_IN_DAY

    timer->timer = sol_timeout_add(
        mdata->type == TIMEOUT_SECOND ? timeout : timeout * 1000,
        wallclock_do, node);

    return 0;

err:
    timer->timer = NULL;
    return -errno;
}

static bool
wallclock_do(void *data)
{
    struct sol_flow_node *node = data, *n;
    struct wallclock_data *mdata = sol_flow_node_get_private_data(node);
    enum sol_flow_node_wallclock_type type = mdata->type;
    struct wallclock_timer *timer = &timers[type];
    struct tm local_time;
    time_t current_time;
    struct timespec ts;
    bool time_fail;
    uint16_t i;

    time_fail = false;
    if (type == TIMEOUT_SECOND) {
        CLOCK_GETTIME_DO(timer->val.val, ts.tv_sec % 60);
    } else {
        if (time(&current_time) == -1) {
            SOL_WRN("could not fetch current time: %s",
                sol_util_strerrora(errno));
            time_fail = true;
        } else if (!localtime_r(&current_time, &local_time)) {
            SOL_WRN("could not convert time: %s",
                sol_util_strerrora(errno));
            time_fail = true;
        }

        if (time_fail) {
            timer->val.val = 0;
        } else if (type == TIMEOUT_MINUTE) {
            timer->val.val = local_time.tm_min;
        } else if (type == TIMEOUT_HOUR) {
            timer->val.val = local_time.tm_hour;
        } else if (type == TIMEOUT_WEEKDAY) {
            timer->val.val = local_time.tm_wday;
        } else { /* TIMEOUT_MONTHDAY */
            timer->val.val = local_time.tm_mday;
        }
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&(timer->clients), n, i)
        sol_flow_send_irange_packet(n, 0, &timer->val);

    clients_cleanup(timer);
    if (sol_ptr_vector_get_len(&(timer->clients)) == 0) return false;

    wallclock_schedule_next(node);

    return false;
}

#undef CLOCK_GETTIME_DO

static int
wallclock_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct wallclock_data *mdata = data;
    struct wallclock_timer *timer;

    (void)options;
    timer = &timers[mdata->type];
    sol_ptr_vector_append(&(timer->clients), node);

    if (!timer->timer)
        return wallclock_schedule_next(node);

    return 0;
}

static void
wallclock_close(struct sol_flow_node *node, void *data)
{
    struct wallclock_data *mdata = data;
    struct wallclock_timer *timer = &timers[mdata->type];
    struct sol_flow_node *n;
    uint16_t i;

    timer->walking++;
    SOL_PTR_VECTOR_FOREACH_IDX (&(timer->clients), n, i) {
        if (n != node) continue;
        if (timer->walking > 1) {
            timer->pending_deletion++;
            sol_ptr_vector_set(&(timer->clients), i, NULL);
        } else {
            sol_ptr_vector_del(&(timer->clients), i);
        }
        break;
    }
    timer->walking--;

    clients_cleanup(timer);

    if (sol_ptr_vector_get_len(&(timer->clients)) == 0) {
        if (timer->timer) {
            sol_timeout_del(timer->timer);
            timer->timer = NULL;
        }
    }
}

static int
wallclock_second_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct wallclock_data *mdata = data;

    mdata->type = TIMEOUT_SECOND;
    return wallclock_open(node, data, options);
}

static void
wallclock_second_close(struct sol_flow_node *node, void *data)
{
    return wallclock_close(node, data);
}

static int
wallclock_minute_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct wallclock_data *mdata = data;

    mdata->type = TIMEOUT_MINUTE;
    return wallclock_open(node, data, options);
}

static void
wallclock_minute_close(struct sol_flow_node *node, void *data)
{
    return wallclock_close(node, data);
}

static int
wallclock_hour_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct wallclock_data *mdata = data;

    mdata->type = TIMEOUT_HOUR;
    return wallclock_open(node, data, options);
}

static void
wallclock_hour_close(struct sol_flow_node *node, void *data)
{
    return wallclock_close(node, data);
}

static int
wallclock_weekday_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct wallclock_data *mdata = data;

    mdata->type = TIMEOUT_WEEKDAY;
    return wallclock_open(node, data, options);
}

static void
wallclock_weekday_close(struct sol_flow_node *node, void *data)
{
    return wallclock_close(node, data);
}

static int
wallclock_monthday_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    struct wallclock_data *mdata = data;

    mdata->type = TIMEOUT_MONTHDAY;
    return wallclock_open(node, data, options);
}

static void
wallclock_monthday_close(struct sol_flow_node *node, void *data)
{
    return wallclock_close(node, data);
}

#include "wallclock-gen.c"
