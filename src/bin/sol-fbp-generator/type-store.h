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

#include "sol-json.h"
#include "sol-str-slice.h"
#include "sol-vector.h"

struct type_store;

struct port_description {
    char *name;
    char *data_type;
};

enum option_value_type {
    OPTION_VALUE_TYPE_NONE = 0,

    OPTION_VALUE_TYPE_UNPARSED_JSON,

    OPTION_VALUE_TYPE_STRING,
    OPTION_VALUE_TYPE_RANGE,
    OPTION_VALUE_TYPE_RGB,
};

/* Options are stored as strings because that's convenient for the
 * generator. */
struct option_range_value {
    char *val;
    char *min;
    char *max;
    char *step;
};

struct option_rgb_value {
    char *red;
    char *red_max;
    char *green;
    char *green_max;
    char *blue;
    char *blue_max;
};

struct option_description {
    char *name;
    char *data_type;

    enum option_value_type default_value_type;
    union {
        char *string;
        struct option_range_value range;
        struct option_rgb_value rgb;
        struct sol_json_token token;
    } default_value;
};

struct type_description {
    char *name;
    char *symbol;
    char *options_symbol;
    struct sol_vector in_ports;
    struct sol_vector out_ports;
    struct sol_vector options;
};

struct type_store *type_store_new(void);

bool type_store_read_from_json(struct type_store *store, struct sol_str_slice input);

/* Call after all the types are read. */
struct type_description *type_store_find(struct type_store *store, const char *name);

void type_store_del(struct type_store *store);

void type_store_print(struct type_store *store);
