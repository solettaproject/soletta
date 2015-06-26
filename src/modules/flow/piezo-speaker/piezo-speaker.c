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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "flow-piezo-speaker");

#include "sol-flow.h"
#include "sol-mainloop.h"
#include "sol-pwm.h"
#include "sol-str-table.h"
#include "sol-util.h"
#include "sol-worker-thread.h"

#include "piezo-speaker-gen.h"

static bool be_quiet(void *data);

enum piezo_speaker_note_periods_us {
    SPEAKER_NOTE_DO = 3830,
    SPEAKER_NOTE_RE = 3400,
    SPEAKER_NOTE_MI = 3038,
    SPEAKER_NOTE_FA = 2864,
    SPEAKER_NOTE_SOL = 2550,
    SPEAKER_NOTE_LA = 2272,
    SPEAKER_NOTE_SI = 2028,
    SPEAKER_NOTE_DO_HIGH = 1912,
    SPEAKER_NOTE_SENTINEL = 0
};

enum tune_iteration_state {
    ITER_NEXT = 0,
    ITER_LAST,
    ITER_ERROR
};

struct piezo_speaker_data {
    struct sol_pwm *pwm;
    struct sol_timeout *timer;
    uint32_t *periods_us, *delays_us, num_entries, tempo_ms, curr_idx;
    enum tune_iteration_state curr_state;
    bool loop : 1;
};

//FIXME: consider changing the tune syntax to
//http://en.wikipedia.org/wiki/Music_Macro_Language#Modern_MML

static const struct sol_str_table note_to_period_table[] = {
    SOL_STR_TABLE_ITEM("C", SPEAKER_NOTE_DO_HIGH),
    SOL_STR_TABLE_ITEM("a", SPEAKER_NOTE_LA),
    SOL_STR_TABLE_ITEM("b", SPEAKER_NOTE_SI),
    SOL_STR_TABLE_ITEM("c", SPEAKER_NOTE_DO),
    SOL_STR_TABLE_ITEM("d", SPEAKER_NOTE_RE),
    SOL_STR_TABLE_ITEM("e", SPEAKER_NOTE_MI),
    SOL_STR_TABLE_ITEM("f", SPEAKER_NOTE_FA),
    SOL_STR_TABLE_ITEM("g", SPEAKER_NOTE_SOL),
    { }
};

static int
byte_to_note_period_us(const char value)
{
    struct sol_str_slice note;
    int period;

    note.data = &value;
    note.len = 1;
    period = sol_str_table_lookup_fallback(note_to_period_table, note,
        SPEAKER_NOTE_SENTINEL);

    if (period == SPEAKER_NOTE_SENTINEL) {
        SOL_WRN("unhandled note '%c'\n", value);
        return -EINVAL;
    }

    return period;
}

static int
stop_sound(struct piezo_speaker_data *mdata)
{
    int r;

    r = sol_pwm_set_duty_cycle(mdata->pwm, 0);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
start_sound(struct piezo_speaker_data *mdata,
    uint32_t period_us)
{
    int r;

    r = sol_pwm_set_duty_cycle(mdata->pwm, 0);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_pwm_set_period(mdata->pwm, period_us * 1000);
    SOL_INT_CHECK(r, < 0, r);

    //we want a perfect square signal, thus half the period. it seems
    //that < half period would affect the final volume, we can expose
    //that later
    r = sol_pwm_set_duty_cycle(mdata->pwm, (period_us * 1000) / 2);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static enum tune_iteration_state
tune_iterate(struct piezo_speaker_data *mdata)
{
    if (mdata->periods_us[mdata->curr_idx] == SPEAKER_NOTE_SENTINEL) {
        goto _end;
    } else {
        int r = start_sound(mdata, mdata->periods_us[mdata->curr_idx]);
        SOL_INT_CHECK(r, < 0, ITER_ERROR);
    }

_end:
    if (mdata->curr_idx == mdata->num_entries - 1 && !mdata->loop)
        return ITER_LAST;

    return ITER_NEXT;
}

static int
tune_stop(struct piezo_speaker_data *mdata)
{
    if (!mdata->timer)
        return 0;

    sol_timeout_del(mdata->timer);
    mdata->timer = NULL;

    return sol_pwm_set_enabled(mdata->pwm, false);
}

static bool
timeout_do(void *data)
{
    struct piezo_speaker_data *mdata = data;

    //activate a note (or not, if curr_idx is for a delay) and return
    //the state
    mdata->curr_state = tune_iterate(mdata);
    if (mdata->curr_state == ITER_ERROR) {
        tune_stop(mdata);
        return false;
    }

    //hold that note for the given delay
    mdata->timer = sol_timeout_add(mdata->delays_us[mdata->curr_idx] / 1000,
        be_quiet, mdata);

    return false;
}

static int
tune_start(struct piezo_speaker_data *mdata)
{
    int r;

    mdata->curr_idx = 0;
    r = sol_pwm_set_enabled(mdata->pwm, true);
    SOL_INT_CHECK(r, < 0, r);
    mdata->timer = sol_timeout_add(0, timeout_do, mdata);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

//pause between notes for half the tempo
static bool
be_quiet(void *data)
{
    struct piezo_speaker_data *mdata = data;
    int r;

    r = stop_sound(mdata);

    if (r < 0 || mdata->curr_state == ITER_LAST) {
        tune_stop(mdata);
        return false;
    }

    mdata->curr_idx = (mdata->curr_idx + 1) % mdata->num_entries;

    mdata->timer = sol_timeout_add(mdata->tempo_ms / 2, timeout_do, mdata);

    return false;
}

static int
enabled_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct piezo_speaker_data *mdata = data;
    bool in_value;
    int r;

    r = sol_flow_packet_get_boolean(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (!in_value)
        return tune_stop(mdata);

    if (in_value && mdata->periods_us && !mdata->timer) {
        r = tune_start(mdata);
        SOL_INT_CHECK(r, < 0, r);
    }


    return 0;
}

static const char TUNE_FIELD_SEPARATOR = '|';

static int
tune_parse(struct piezo_speaker_data *mdata, const char *tune)
{
    const char *pos = tune;
    unsigned i;

    mdata->num_entries = 0;

    //two passes to avoid reallocs
    while (pos && *pos) {
        pos++;
        mdata->num_entries++;
        if (pos && *pos == TUNE_FIELD_SEPARATOR) break;
    }
    pos = tune;

    free(mdata->periods_us);
    free(mdata->delays_us);
    mdata->periods_us = calloc(mdata->num_entries, sizeof(*mdata->periods_us));
    if (!mdata->periods_us)
        return -ENOMEM;
    mdata->delays_us = calloc(mdata->num_entries, sizeof(*mdata->delays_us));
    if (!mdata->delays_us) {
        free(mdata->periods_us);
        return -ENOMEM;
    }

    for (i = 0; pos && *pos != TUNE_FIELD_SEPARATOR; pos++, i++) {
        if (isspace(*pos))
            mdata->periods_us[i] = SPEAKER_NOTE_SENTINEL;
        else {
            mdata->periods_us[i] = byte_to_note_period_us(*pos);
            SOL_INT_CHECK(mdata->periods_us[i],
                == SPEAKER_NOTE_SENTINEL, -EINVAL);
        }
    }
    if (!pos || *pos != TUNE_FIELD_SEPARATOR)
        goto _format_err;
    pos++;

    for (i = 0; pos && *pos != TUNE_FIELD_SEPARATOR; pos++, i++) {
        uint32_t beat = *pos - '0';

        if (beat <= 0 || beat > 9) {
            SOL_WRN("Bad format for speaker tune string (%s)"
                " -- beat %c not supported --  we can't apply a new tune",
                tune, *pos);
            return -EINVAL;
        }

        mdata->delays_us[i] = beat;
    }
    if (!pos) goto _format_err;

    if (*pos != TUNE_FIELD_SEPARATOR || i != mdata->num_entries) {
        if (!i) goto _format_err;
        SOL_WRN("Bad format for speaker tune string (%s)"
            " -- less beat (%d) than note (%d) entries."
            " The notes array length is being shrunk to match the beats",
            tune, i, mdata->num_entries);
        mdata->num_entries = i;
        while (pos && *pos != TUNE_FIELD_SEPARATOR) pos++;
    }
    if (!pos) goto _format_err;
    pos++;

    errno = 0;
    mdata->tempo_ms = strtol(pos, NULL, 10);
    if (errno != 0)
        goto _format_err;
    if (mdata->tempo_ms * 1000 > INT32_MAX / 9) {
        SOL_WRN("Bad format for speaker tune string (%s)"
            " -- base tempo too high %" PRId32 " ms (max is %d ms)"
            " -- we can't apply a new tune",
            tune, mdata->tempo_ms, INT32_MAX / 9000);
        return -EINVAL;
    }

    for (i = 0; i < mdata->num_entries; i++)
        mdata->delays_us[i] *= mdata->tempo_ms * 1000;

    return 0;

_format_err:
    free(mdata->periods_us);
    free(mdata->delays_us);
    mdata->num_entries = 0;
    mdata->periods_us = mdata->delays_us = NULL;
    SOL_WRN("Bad format for speaker tune string (%s),"
        " we can't apply a new tune", tune);
    return -EINVAL;
}

static int
tune_set(struct sol_flow_node *node,
    void *data,
    uint16_t port,
    uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    struct piezo_speaker_data *mdata = data;
    const char *in_value;
    bool last_state = false;
    int r;

    r = sol_flow_packet_get_string(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (mdata->timer) {
        last_state = true;
        r = tune_stop(mdata);
        SOL_INT_CHECK(r, < 0, r);
    }

    r = tune_parse(mdata, in_value);
    SOL_INT_CHECK(r, < 0, r);

    if (last_state) {
        r = tune_start(mdata);
        SOL_INT_CHECK(r, < 0, r);
    }

    return 0;
}

static int
piezo_speaker_open(struct sol_flow_node *node,
    void *data,
    const struct sol_flow_node_options *options)
{
    int r;
    struct piezo_speaker_data *mdata = data;
    const struct sol_flow_node_type_piezo_speaker_sound_options *opts =
        (const struct sol_flow_node_type_piezo_speaker_sound_options *)options;
    struct sol_pwm_config pwm_config = { 0 };

    pwm_config.period_ns = -1;
    pwm_config.duty_cycle_ns = 0;

    mdata->pwm = sol_pwm_open(opts->chip.val, opts->pin.val, &pwm_config);
    if (!mdata->pwm) {
        SOL_WRN("could not open pwm (chip=%" PRId32 ", pin=%" PRId32 ")\n",
            opts->chip.val, opts->pin.val);
        goto _error;
    }

    mdata->loop = opts->loop;

    if (strlen(opts->tune)) {
        r = tune_parse(mdata, opts->tune);
        SOL_INT_CHECK(r, < 0, r);
    } else
        SOL_WRN("No tune in opts, awaiting string package\n");

    SOL_DBG("Piezo open ok (chip=%" PRId32 ", pin=%" PRId32 ")\n",
        opts->chip.val, opts->pin.val);

    return 0;

_error:
    return -EINVAL;
}

static void
piezo_speaker_close(struct sol_flow_node *node, void *data)
{
    struct piezo_speaker_data *mdata = data;

    SOL_DBG("Piezo close\n");

    tune_stop(mdata);

    sol_pwm_close(mdata->pwm);

    free(mdata->periods_us);
    free(mdata->delays_us);
}


#include "piezo-speaker-gen.c"
