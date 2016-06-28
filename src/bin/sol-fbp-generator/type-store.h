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

#include "sol-json.h"
#include "sol-str-slice.h"
#include "sol-vector.h"

struct type_store;

struct port_description {
    char *name;
    char *data_type;
    int array_size;
    int base_port_idx;
};

enum option_value_type {
    OPTION_VALUE_TYPE_NONE = 0,

    OPTION_VALUE_TYPE_UNPARSED_JSON,

    OPTION_VALUE_TYPE_STRING,
    OPTION_VALUE_TYPE_SPEC_RANGE,
    OPTION_VALUE_TYPE_RGB,
    OPTION_VALUE_TYPE_DIRECTION_VECTOR
};

/* Options are stored as strings because that's convenient for the
 * generator. */
struct option_spec_range_value {
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

struct option_direction_vector_value {
    char *x;
    char *y;
    char *z;
    char *min;
    char *max;
};

struct option_description {
    char *name;
    char *data_type;

    enum option_value_type default_value_type;
    union {
        char *string;
        struct option_spec_range_value spec_range;
        struct option_rgb_value rgb;
        struct option_direction_vector_value direction_vector;
        struct sol_json_token token;
    } default_value;
};

struct type_description {
    char *name;
    char *symbol;
    char *options_symbol;
    char *header_file;
    bool generated_options;
    struct sol_vector in_ports;
    struct sol_vector out_ports;
    struct sol_vector options;
};

struct type_store *type_store_new(void);

bool type_store_read_from_json(struct type_store *store, struct sol_str_slice input);

/* Call after all the types are read. */
struct type_description *type_store_find(struct type_store *store, const char *name);

/* All the information of this type description will be copied. */
bool type_store_add_type(struct type_store *store, const struct type_description *type);

void type_store_del(struct type_store *store);

void type_store_print(struct type_store *store);

bool type_store_copy_option_description(struct option_description *dst, const struct option_description *src, const struct sol_str_slice opt_name);
