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

#pragma once

#define SOL_LOG_DOMAIN &_converter_log_domain
#include "sol-log-internal.h"
extern struct sol_log_domain _converter_log_domain;

#include "sol-buffer.h"
#include "sol-flow/converter.h"
#include "sol-mainloop.h"
#include "sol-flow-internal.h"

enum auto_number_state {
    ANS_INIT,
    ANS_AUTO,
    ANS_MANUAL
}; /* Keeps track if we're auto-numbering fields */

/* Keeps track of our auto-numbering state, and which number field
 * we're on */
struct auto_number {
    enum auto_number_state an_state;
    int an_field_number;
};

struct string_converter {
    struct sol_flow_node *node;
    char *format;
};

void auto_number_init(struct auto_number *auto_number);
int do_integer_markup(struct string_converter *mdata, const char *format, struct sol_irange *args, struct auto_number *auto_number, struct sol_buffer *out) SOL_ATTR_WARN_UNUSED_RESULT;
int do_float_markup(struct string_converter *mdata, const char *format, struct sol_drange *args, struct auto_number *auto_number, struct sol_buffer *out) SOL_ATTR_WARN_UNUSED_RESULT;

