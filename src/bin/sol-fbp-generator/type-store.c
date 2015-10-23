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

#include <errno.h>
#include <stdio.h>

#include "sol-json.h"
#include "sol-log.h"
#include "sol-missing.h"
#include "sol-str-slice.h"
#include "sol-util.h"
#include "sol-vector.h"

#include "type-store.h"

struct decoder {
    struct sol_json_scanner scanner;
    struct sol_json_token next;
    bool pending;
    bool end;
};

struct type_store {
    struct sol_vector types;
};

#define CONST_SLICE(NAME, VALUE) \
    static const struct sol_str_slice NAME = SOL_STR_SLICE_LITERAL(VALUE)

CONST_SLICE(NAME_SLICE, "name");
CONST_SLICE(SYMBOL_SLICE, "symbol");
CONST_SLICE(OPTIONS_SYMBOL_SLICE, "options_symbol");
CONST_SLICE(IN_PORTS_SLICE, "in_ports");
CONST_SLICE(OUT_PORTS_SLICE, "out_ports");
CONST_SLICE(DATA_TYPE_SLICE, "data_type");
CONST_SLICE(ARRAY_SIZE_SLICE, "array_size");
CONST_SLICE(BASE_PORT_IDX_SLICE, "base_port_idx");
CONST_SLICE(OPTIONS_SLICE, "options");
CONST_SLICE(MEMBERS_SLICE, "members");
CONST_SLICE(DEFAULT_SLICE, "default");
CONST_SLICE(VAL_SLICE, "val");
CONST_SLICE(MIN_SLICE, "min");
CONST_SLICE(MAX_SLICE, "max");
CONST_SLICE(STEP_SLICE, "step");
CONST_SLICE(RED_SLICE, "red");
CONST_SLICE(RED_MAX_SLICE, "red_max");
CONST_SLICE(GREEN_SLICE, "green");
CONST_SLICE(GREEN_MAX_SLICE, "green_max");
CONST_SLICE(BLUE_SLICE, "blue");
CONST_SLICE(BLUE_MAX_SLICE, "blue_max");
CONST_SLICE(X_SLICE, "x");
CONST_SLICE(Y_SLICE, "y");
CONST_SLICE(Z_SLICE, "z");

static void
decoder_init(struct decoder *d, struct sol_str_slice input)
{
    sol_json_scanner_init(&d->scanner, input.data, input.len);
    d->pending = false;
    d->end = false;
}

static enum sol_json_type
next(struct decoder *d, struct sol_json_token *token)
{
    struct sol_json_token tmp;

    if (d->end)
        return SOL_JSON_TYPE_UNKNOWN;

    if (d->pending) {
        if (token)
            *token = d->next;
        d->pending = false;
        return sol_json_token_get_type(&d->next);
    }

    if (!sol_json_scanner_next(&d->scanner, &tmp)) {
        if (!errno)
            d->end = true;
        return SOL_JSON_TYPE_UNKNOWN;
    }

    if (token)
        *token = tmp;
    return sol_json_token_get_type(&tmp);
}

static bool
accept(struct decoder *d, enum sol_json_type expected)
{
    enum sol_json_type type;

    type = next(d, NULL);
    if (type != expected) {
        fprintf(stderr, "Error while parsing JSON file: Expected '%c' and got '%c'\n", expected, type);
        return false;
    }
    return true;
}

static void
skip(struct decoder *d)
{
    next(d, NULL);
}

static enum sol_json_type
peek(struct decoder *d)
{
    if (d->end)
        return SOL_JSON_TYPE_UNKNOWN;

    if (!d->pending) {
        if (!sol_json_scanner_next(&d->scanner, &d->next))
            return SOL_JSON_TYPE_UNKNOWN;
        d->pending = true;
    }

    return sol_json_token_get_type(&d->next);
}

static struct sol_str_slice
get_slice(struct sol_json_token *token)
{
    return SOL_STR_SLICE_STR(token->start + 1, token->end - token->start - 2);
}

static char *
get_string(struct sol_json_token *token)
{
    return strndup(token->start + 1, token->end - token->start - 2);
}

static int
get_int(struct sol_json_token *token, int *value)
{
    int v;
    char *str, *endptr = NULL;

    str = strndupa(token->start, token->end - token->start);

    errno = 0;
    v = strtol(str, &endptr, 0);

    if (errno)
        return -errno;

    if (*endptr != 0)
        return -EINVAL;

    if ((long)(int)v != v)
        return -ERANGE;

    *value = v;
    return 0;
}

static bool
read_json_property_value(struct decoder *d, struct sol_json_token *value)
{
    const char *start;

    if (!accept(d, SOL_JSON_TYPE_PAIR_SEP))
        return false;
    if (!sol_json_scanner_next(&d->scanner, value))
        return false;
    start = value->start;
    if (!sol_json_scanner_skip_over(&d->scanner, value))
        return false;
    value->start = start;
    return true;
}

static bool
skip_property_value(struct decoder *d)
{
    struct sol_json_token ignored;

    return read_json_property_value(d, &ignored);
}

static void
type_description_init(struct type_description *desc)
{
    desc->name = NULL;
    desc->symbol = NULL;
    desc->options_symbol = NULL;
    sol_vector_init(&desc->in_ports, sizeof(struct port_description));
    sol_vector_init(&desc->out_ports, sizeof(struct port_description));
    sol_vector_init(&desc->options, sizeof(struct option_description));
}

static void
option_description_fini(struct option_description *o)
{
    struct option_direction_vector_value *direction_vector;
    struct option_range_value *range;
    struct option_spec_range_value *spec_range;
    struct option_rgb_value *rgb;

    free(o->name);
    free(o->data_type);

    switch (o->default_value_type) {
    case OPTION_VALUE_TYPE_STRING:
        free(o->default_value.string);
        break;
    case OPTION_VALUE_TYPE_RANGE:
        range = &o->default_value.range;
        free(range->val);
        free(range->min);
        free(range->max);
        free(range->step);
        break;
    case OPTION_VALUE_TYPE_SPEC_RANGE:
        spec_range = &o->default_value.spec_range;
        free(spec_range->min);
        free(spec_range->max);
        free(spec_range->step);
        break;
    case OPTION_VALUE_TYPE_RGB:
        rgb = &o->default_value.rgb;
        free(rgb->red);
        free(rgb->green);
        free(rgb->blue);
        free(rgb->red_max);
        free(rgb->green_max);
        free(rgb->blue_max);
        break;
    case OPTION_VALUE_TYPE_DIRECTION_VECTOR:
        direction_vector = &o->default_value.direction_vector;
        free(direction_vector->x);
        free(direction_vector->y);
        free(direction_vector->z);
        free(direction_vector->min);
        free(direction_vector->max);
        break;
    default:
        /* Nothing to free. */
        break;
    }
}

static void
type_description_fini(struct type_description *desc)
{
    struct port_description *p;
    struct option_description *o;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&desc->in_ports, p, i) {
        free(p->name);
        free(p->data_type);
    }

    SOL_VECTOR_FOREACH_IDX (&desc->out_ports, p, i) {
        free(p->name);
        free(p->data_type);
    }

    SOL_VECTOR_FOREACH_IDX (&desc->options, o, i) {
        option_description_fini(o);
    }

    sol_vector_clear(&desc->in_ports);
    sol_vector_clear(&desc->out_ports);
    sol_vector_clear(&desc->options);

    free(desc->name);
    free(desc->symbol);
    free(desc->options_symbol);
}

static bool
read_string_property_value(struct decoder *d, struct sol_json_token *value)
{
    if (!accept(d, SOL_JSON_TYPE_PAIR_SEP))
        return false;
    if (next(d, value) != SOL_JSON_TYPE_STRING)
        return false;
    return true;
}

static bool
read_int_property_value(struct decoder *d, struct sol_json_token *value)
{
    if (!accept(d, SOL_JSON_TYPE_PAIR_SEP))
        return false;
    if (next(d, value) != SOL_JSON_TYPE_NUMBER)
        return false;
    return true;
}

static bool
read_port(struct decoder *d, struct port_description *p)
{
    while (true) {
        struct sol_json_token key, value;
        struct sol_str_slice key_slice;

        if (next(d, &key) != SOL_JSON_TYPE_STRING)
            return false;

        key_slice = get_slice(&key);

        if (sol_str_slice_eq(key_slice, NAME_SLICE)) {
            if (!read_string_property_value(d, &value))
                return false;
            p->name = get_string(&value);

        } else if (sol_str_slice_eq(key_slice, DATA_TYPE_SLICE)) {
            if (!read_string_property_value(d, &value))
                return false;
            p->data_type = get_string(&value);

        } else if (sol_str_slice_eq(key_slice, ARRAY_SIZE_SLICE)) {
            if (!read_int_property_value(d, &value))
                return false;
            get_int(&value, &p->array_size);

        } else if (sol_str_slice_eq(key_slice, BASE_PORT_IDX_SLICE)) {
            if (!read_int_property_value(d, &value))
                return false;
            get_int(&value, &p->base_port_idx);

        } else if (!skip_property_value(d)) {
            return false;
        }

        if (peek(d) != SOL_JSON_TYPE_ELEMENT_SEP)
            break;
        accept(d, SOL_JSON_TYPE_ELEMENT_SEP);
    }

    if (!p->name || !p->data_type || p->array_size == -1 || p->base_port_idx == -1)
        return false;

    return accept(d, SOL_JSON_TYPE_OBJECT_END);
}

static bool
read_ports_array(struct decoder *d, struct sol_vector *ports)
{
    if (!accept(d, SOL_JSON_TYPE_PAIR_SEP))
        return false;

    if (!accept(d, SOL_JSON_TYPE_ARRAY_START))
        return false;

    while (true) {
        struct port_description *p;
        if (!accept(d, SOL_JSON_TYPE_OBJECT_START))
            return false;

        p = sol_vector_append(ports);
        if (!p)
            return false;
        p->name = NULL;
        p->data_type = NULL;
        p->array_size = -1;
        p->base_port_idx = -1;

        if (!read_port(d, p))
            return false;

        if (peek(d) != SOL_JSON_TYPE_ELEMENT_SEP)
            break;
        skip(d);
    }

    return accept(d, SOL_JSON_TYPE_ARRAY_END);
}

static bool
read_default_value(struct decoder *d, struct option_description *o)
{
    o->default_value_type = OPTION_VALUE_TYPE_UNPARSED_JSON;
    return read_json_property_value(d, &o->default_value.token);
}

static bool
get_value(struct sol_json_token *value, char **value_data, struct sol_json_token *key, struct sol_str_slice *key_slice)
{
    enum sol_json_type type;

    type = sol_json_token_get_type(value);
    if (type != SOL_JSON_TYPE_NUMBER && type != SOL_JSON_TYPE_STRING)
        return false;

    *key_slice = get_slice(key);

    if (!value_data)
        return false;
    *value_data = strndup(value->start, value->end - value->start);
    if (!*value_data)
        return false;

    return true;
}

static bool
parse_range_default_value(struct sol_json_scanner *s, struct option_description *o)
{
    struct option_range_value *range;
    struct sol_json_token tmp, key, value;
    struct sol_str_slice key_slice;
    enum sol_json_loop_reason reason;
    char *value_data;

    range = &o->default_value.range;
    memset(range, 0, sizeof(struct option_range_value));

    SOL_JSON_SCANNER_OBJECT_LOOP (s, &tmp, &key, &value, reason) {
        if (!get_value(&value, &value_data, &key, &key_slice))
            return false;

        if (sol_str_slice_eq(key_slice, VAL_SLICE))
            range->val = value_data;
        else if (sol_str_slice_eq(key_slice, MIN_SLICE))
            range->min = value_data;
        else if (sol_str_slice_eq(key_slice, MAX_SLICE))
            range->max = value_data;
        else if (sol_str_slice_eq(key_slice, STEP_SLICE))
            range->step = value_data;
        else
            free(value_data);

        if (reason != SOL_JSON_LOOP_REASON_OK)
            return false;
    }

    return true;
}

static bool
parse_spec_range_default_value(struct sol_json_scanner *s, struct option_description *o)
{
    struct option_spec_range_value *spec_range;
    struct sol_json_token tmp, key, value;
    struct sol_str_slice key_slice;
    enum sol_json_loop_reason reason;
    char *value_data;

    spec_range = &o->default_value.spec_range;
    memset(spec_range, 0, sizeof(struct option_spec_range_value));

    SOL_JSON_SCANNER_OBJECT_LOOP (s, &tmp, &key, &value, reason) {
        if (!get_value(&value, &value_data, &key, &key_slice))
            return false;

        if (sol_str_slice_eq(key_slice, MIN_SLICE))
            spec_range->min = value_data;
        else if (sol_str_slice_eq(key_slice, MAX_SLICE))
            spec_range->max = value_data;
        else if (sol_str_slice_eq(key_slice, STEP_SLICE))
            spec_range->step = value_data;
        else
            free(value_data);

        if (reason != SOL_JSON_LOOP_REASON_OK)
            return false;
    }

    return true;
}

static bool
parse_rgb_default_value(struct sol_json_scanner *s, struct option_description *o)
{
    struct option_rgb_value *rgb;
    struct sol_json_token tmp, key, value;
    struct sol_str_slice key_slice;
    enum sol_json_loop_reason reason;
    char *value_data;

    rgb = &o->default_value.rgb;
    memset(rgb, 0, sizeof(struct option_rgb_value));

    SOL_JSON_SCANNER_OBJECT_LOOP (s, &tmp, &key, &value, reason) {
        if (!get_value(&value, &value_data, &key, &key_slice))
            return false;

        if (sol_str_slice_eq(key_slice, RED_SLICE))
            rgb->red = value_data;
        else if (sol_str_slice_eq(key_slice, GREEN_SLICE))
            rgb->green = value_data;
        else if (sol_str_slice_eq(key_slice, BLUE_SLICE))
            rgb->blue = value_data;
        else if (sol_str_slice_eq(key_slice, RED_MAX_SLICE))
            rgb->red_max = value_data;
        else if (sol_str_slice_eq(key_slice, GREEN_MAX_SLICE))
            rgb->green_max = value_data;
        else if (sol_str_slice_eq(key_slice, BLUE_MAX_SLICE))
            rgb->blue_max = value_data;
        else
            free(value_data);

        if (reason != SOL_JSON_LOOP_REASON_OK)
            return false;
    }

    return true;
}

static bool
parse_direction_vector_default_value(struct sol_json_scanner *s, struct option_description *o)
{
    struct option_direction_vector_value *direction_vector;
    struct sol_json_token tmp, key, value;
    enum sol_json_loop_reason reason;
    struct sol_str_slice key_slice;
    char *value_data;

    direction_vector = &o->default_value.direction_vector;
    memset(direction_vector, 0, sizeof(struct option_direction_vector_value));

    SOL_JSON_SCANNER_OBJECT_LOOP (s, &tmp, &key, &value, reason) {
        if (!get_value(&value, &value_data, &key, &key_slice))
            return false;

        if (sol_str_slice_eq(key_slice, X_SLICE))
            direction_vector->x = value_data;
        else if (sol_str_slice_eq(key_slice, Y_SLICE))
            direction_vector->y = value_data;
        else if (sol_str_slice_eq(key_slice, Z_SLICE))
            direction_vector->z = value_data;
        else if (sol_str_slice_eq(key_slice, MIN_SLICE))
            direction_vector->min = value_data;
        else if (sol_str_slice_eq(key_slice, MAX_SLICE))
            direction_vector->max = value_data;
        else
            free(value_data);

        if (reason != SOL_JSON_LOOP_REASON_OK)
            return false;
    }

    return true;
}

static bool
parse_string_default_value(struct sol_json_scanner *s, struct option_description *o)
{
    struct sol_json_token token;
    enum sol_json_type type;
    char *value;

    if (!sol_json_scanner_next(s, &token))
        return false;

    type = sol_json_token_get_type(&token);
    switch (type) {
    case SOL_JSON_TYPE_TRUE:
    case SOL_JSON_TYPE_FALSE:
    case SOL_JSON_TYPE_STRING:
    case SOL_JSON_TYPE_NUMBER:
        value = strndup(token.start, token.end - token.start);
        break;

    case SOL_JSON_TYPE_NULL:
        value = NULL;
        break;

    default:
        return false;
    }

    o->default_value.string = value;
    return true;
}

static bool
parse_default_value(struct option_description *o)
{
    struct sol_json_scanner value_scanner;

    if (o->default_value_type != OPTION_VALUE_TYPE_UNPARSED_JSON)
        return true;

    /* Use a new scanner to parse the value, to not disrupt our main
     * scanner state. */
    sol_json_scanner_init_from_token(&value_scanner, &o->default_value.token);

    if (streq(o->data_type, "int") || streq(o->data_type, "drange")) {
        o->default_value_type = OPTION_VALUE_TYPE_RANGE;
        return parse_range_default_value(&value_scanner, o);
    }

    if (streq(o->data_type, "drange-spec")) {
        o->default_value_type = OPTION_VALUE_TYPE_SPEC_RANGE;
        return parse_spec_range_default_value(&value_scanner, o);
    }

    if (streq(o->data_type, "rgb")) {
        o->default_value_type = OPTION_VALUE_TYPE_RGB;
        return parse_rgb_default_value(&value_scanner, o);
    }

    if (streq(o->data_type, "direction-vector")) {
        o->default_value_type = OPTION_VALUE_TYPE_DIRECTION_VECTOR;
        return parse_direction_vector_default_value(&value_scanner, o);
    }

    o->default_value_type = OPTION_VALUE_TYPE_STRING;
    return parse_string_default_value(&value_scanner, o);
}

static bool
read_option(struct decoder *d, struct option_description *o)
{
    while (true) {
        struct sol_json_token key, value;
        struct sol_str_slice key_slice;

        if (next(d, &key) != SOL_JSON_TYPE_STRING)
            return false;

        key_slice = get_slice(&key);

        if (sol_str_slice_eq(key_slice, NAME_SLICE)) {
            if (!read_string_property_value(d, &value))
                return false;
            o->name = get_string(&value);

        } else if (sol_str_slice_eq(key_slice, DATA_TYPE_SLICE)) {
            if (!read_string_property_value(d, &value))
                return false;
            o->data_type = get_string(&value);

        } else if (sol_str_slice_eq(key_slice, DEFAULT_SLICE)) {
            if (!read_default_value(d, o))
                return false;

        } else if (!skip_property_value(d)) {
            return false;
        }

        if (peek(d) != SOL_JSON_TYPE_ELEMENT_SEP)
            break;
        skip(d);
    }

    if (!o->name || !o->data_type)
        return false;

    /* The default_value might be read before the data_type, so we
     * delay parsing it until we finish reading all properties for an
     * option. */
    if (!parse_default_value(o))
        return false;

    return accept(d, SOL_JSON_TYPE_OBJECT_END);
}

static bool
read_members_array(struct decoder *d, struct sol_vector *options)
{
    if (!accept(d, SOL_JSON_TYPE_PAIR_SEP))
        return false;

    if (!accept(d, SOL_JSON_TYPE_ARRAY_START))
        return false;

    if (peek(d) == SOL_JSON_TYPE_ARRAY_END) {
        accept(d, SOL_JSON_TYPE_ARRAY_END);
        return true;
    }

    while (true) {
        struct option_description *o;
        if (!accept(d, SOL_JSON_TYPE_OBJECT_START))
            return false;

        o = sol_vector_append(options);
        if (!o)
            return false;
        o->name = NULL;
        o->data_type = NULL;
        o->default_value_type = OPTION_VALUE_TYPE_NONE;

        if (!read_option(d, o))
            return false;

        if (peek(d) != SOL_JSON_TYPE_ELEMENT_SEP)
            break;
        skip(d);
    }

    return accept(d, SOL_JSON_TYPE_ARRAY_END);
}

static bool
read_options(struct decoder *d, struct sol_vector *options)
{
    if (!accept(d, SOL_JSON_TYPE_PAIR_SEP))
        return false;

    if (!accept(d, SOL_JSON_TYPE_OBJECT_START))
        return false;

    if (peek(d) == SOL_JSON_TYPE_OBJECT_END)
        return false;

    while (true) {
        struct sol_json_token key;
        struct sol_str_slice key_slice;

        if (next(d, &key) != SOL_JSON_TYPE_STRING)
            return false;

        key_slice = get_slice(&key);

        if (sol_str_slice_eq(key_slice, MEMBERS_SLICE)) {
            if (!read_members_array(d, options))
                return false;

        } else if (!skip_property_value(d)) {
            return false;
        }

        if (peek(d) != SOL_JSON_TYPE_ELEMENT_SEP)
            break;
        accept(d, SOL_JSON_TYPE_ELEMENT_SEP);
    }

    return accept(d, SOL_JSON_TYPE_OBJECT_END);
}

static bool
read_type(struct decoder *d, struct type_description *desc)
{
    if (!accept(d, SOL_JSON_TYPE_OBJECT_START))
        return false;

    if (peek(d) == SOL_JSON_TYPE_OBJECT_END)
        return false;

    while (true) {
        struct sol_json_token key, value;
        struct sol_str_slice key_slice;

        if (next(d, &key) != SOL_JSON_TYPE_STRING)
            return false;

        key_slice = get_slice(&key);

        if (sol_str_slice_eq(key_slice, NAME_SLICE)) {
            if (!read_string_property_value(d, &value))
                return false;
            desc->name = get_string(&value);

        } else if (sol_str_slice_eq(key_slice, SYMBOL_SLICE)) {
            if (!read_string_property_value(d, &value))
                return false;
            desc->symbol = get_string(&value);

        } else if (sol_str_slice_eq(key_slice, OPTIONS_SYMBOL_SLICE)) {
            if (!read_string_property_value(d, &value))
                return false;
            desc->options_symbol = get_string(&value);

        } else if (sol_str_slice_eq(key_slice, IN_PORTS_SLICE)) {
            if (!read_ports_array(d, &desc->in_ports))
                return false;

        } else if (sol_str_slice_eq(key_slice, OUT_PORTS_SLICE)) {
            if (!read_ports_array(d, &desc->out_ports))
                return false;

        } else if (sol_str_slice_eq(key_slice, OPTIONS_SLICE)) {
            if (!read_options(d, &desc->options))
                return false;

        } else if (!skip_property_value(d)) {
            return false;
        }

        if (peek(d) != SOL_JSON_TYPE_ELEMENT_SEP)
            break;
        accept(d, SOL_JSON_TYPE_ELEMENT_SEP);
    }

    if (!desc->name || !desc->symbol)
        return false;

    if (desc->options.len > 0 && !desc->options_symbol)
        return false;

    return accept(d, SOL_JSON_TYPE_OBJECT_END);
}

bool
type_store_read_from_json(struct type_store *store, struct sol_str_slice input)
{
    struct decoder decoder, *d = &decoder;

    decoder_init(&decoder, input);

    if (!accept(d, SOL_JSON_TYPE_OBJECT_START))
        return false;

    if (!accept(d, SOL_JSON_TYPE_STRING))
        return false;

    if (!accept(d, SOL_JSON_TYPE_PAIR_SEP))
        return false;

    if (!accept(d, SOL_JSON_TYPE_ARRAY_START))
        return false;

    while (true) {
        struct type_description *desc;

        desc = sol_vector_append(&store->types);
        if (!desc)
            return false;

        type_description_init(desc);
        if (!read_type(&decoder, desc)) {
            type_description_fini(desc);
            sol_vector_del(&store->types, store->types.len - 1);
        }

        if (peek(d) != SOL_JSON_TYPE_ELEMENT_SEP)
            break;

        accept(d, SOL_JSON_TYPE_ELEMENT_SEP);
    }

    if (!accept(d, SOL_JSON_TYPE_ARRAY_END))
        return false;

    if (!accept(d, SOL_JSON_TYPE_OBJECT_END))
        return false;

    return true;
}

struct type_store *
type_store_new(void)
{
    struct type_store *store;

    store = calloc(1, sizeof(struct type_store));
    if (!store)
        return NULL;

    sol_vector_init(&store->types, sizeof(struct type_description));
    return store;
}

struct type_description *
type_store_find(struct type_store *store, const char *name)
{
    struct type_description *desc;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&store->types, desc, i) {
        if (streq(name, desc->name))
            return desc;
    }

    return NULL;
}

bool
type_store_add_type(struct type_store *store, const struct type_description *type)
{
    struct option_description *o, *option;
    struct port_description *p, *port;
    struct type_description *t;
    uint16_t i;

    SOL_NULL_CHECK(store, false);
    SOL_NULL_CHECK(type, false);

    t = sol_vector_append(&store->types);
    SOL_NULL_CHECK(t, false);

    t->name = strdup(type->name);
    SOL_NULL_CHECK_GOTO(t->name, fail_name);

    t->symbol = strdup(type->symbol);
    SOL_NULL_CHECK_GOTO(t->symbol, fail_symbol);

    t->options_symbol = strdup(type->options_symbol);
    SOL_NULL_CHECK_GOTO(t->options_symbol, fail_options_symbol);

    t->generated_options = type->generated_options;
    sol_vector_init(&t->in_ports, sizeof(struct port_description));
    SOL_VECTOR_FOREACH_IDX (&type->in_ports, p, i) {
        port = sol_vector_append(&t->in_ports);
        SOL_NULL_CHECK_GOTO(port, fail_in_ports);

        port->name = strdup(p->name);
        SOL_NULL_CHECK_GOTO(port->name, fail_in_ports);

        port->data_type = strdup(p->data_type);
        SOL_NULL_CHECK_GOTO(port->data_type, fail_in_ports);

        port->array_size = p->array_size;
        port->base_port_idx = p->base_port_idx;
    }

    sol_vector_init(&t->out_ports, sizeof(struct port_description));
    SOL_VECTOR_FOREACH_IDX (&type->out_ports, p, i) {
        port = sol_vector_append(&t->out_ports);
        SOL_NULL_CHECK_GOTO(port, fail_out_ports);

        port->name = strdup(p->name);
        SOL_NULL_CHECK_GOTO(port->name, fail_out_ports);

        port->data_type = strdup(p->data_type);
        SOL_NULL_CHECK_GOTO(port->data_type, fail_out_ports);

        port->array_size = p->array_size;
        port->base_port_idx = p->base_port_idx;
    }

    sol_vector_init(&t->options, sizeof(struct option_description));
    SOL_VECTOR_FOREACH_IDX (&type->options, o, i) {
        option = sol_vector_append(&t->options);
        SOL_NULL_CHECK_GOTO(option, fail_options);
        if (!type_store_copy_option_description(option, o,
            sol_str_slice_from_str(o->name)))
            goto fail_options;
    }

    return true;

fail_options:
    SOL_VECTOR_FOREACH_IDX (&t->options, o, i) {
        free(o->name);
        free(o->data_type);

        if (o->default_value_type == OPTION_VALUE_TYPE_STRING)
            free(o->default_value.string);
    }
    sol_vector_clear(&t->options);
fail_out_ports:
    SOL_VECTOR_FOREACH_IDX (&t->out_ports, p, i) {
        free(p->name);
        free(p->data_type);
    }
    sol_vector_clear(&t->out_ports);
fail_in_ports:
    SOL_VECTOR_FOREACH_IDX (&t->in_ports, p, i) {
        free(p->name);
        free(p->data_type);
    }
    sol_vector_clear(&t->in_ports);
fail_options_symbol:
    free(t->symbol);
fail_symbol:
    free(t->name);
fail_name:
    sol_vector_del(&store->types, store->types.len - 1);

    return false;
}

void
type_store_del(struct type_store *store)
{
    struct type_description *desc;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&store->types, desc, i) {
        type_description_fini(desc);
    }

    sol_vector_clear(&store->types);
    free(store);
}

static void
type_description_print(struct type_description *desc)
{
    struct port_description *p;
    struct option_description *o;
    uint16_t i;

    printf("name=%s\n", desc->name);
    printf("symbol=%s\n", desc->symbol);
    printf("options_symbol=%s\n", desc->options_symbol);
    printf("in_ports\n");
    SOL_VECTOR_FOREACH_IDX (&desc->in_ports, p, i) {
        printf("  %s (%s)\n", p->name, p->data_type);
    }
    printf("out_ports\n");
    SOL_VECTOR_FOREACH_IDX (&desc->out_ports, p, i) {
        printf("  %s (%s)\n", p->name, p->data_type);
    }

    if (desc->options.len > 0)
        printf("options\n");
    SOL_VECTOR_FOREACH_IDX (&desc->options, o, i) {
        struct option_range_value *range;
        struct option_spec_range_value *spec_range;
        struct option_rgb_value *rgb;
        struct option_direction_vector_value *direction_vector;

        printf("  %s (%s", o->name, o->data_type);

        switch (o->default_value_type) {
        case OPTION_VALUE_TYPE_STRING:
            printf(", default=%s)\n", o->default_value.string);
            break;
        case OPTION_VALUE_TYPE_RANGE:
            range = &o->default_value.range;
            printf(", default val=%s  min=%s  max=%s  step=%s)\n",
                range->val, range->min, range->max, range->step);
            break;
        case OPTION_VALUE_TYPE_SPEC_RANGE:
            spec_range = &o->default_value.spec_range;
            printf(", default min=%s  max=%s  step=%s)\n",
                spec_range->min, spec_range->max, spec_range->step);
            break;
        case OPTION_VALUE_TYPE_RGB:
            rgb = &o->default_value.rgb;
            printf(", default red=%s  green=%s  blue=%s  red_max=%s  "
                "green_max=%s  blue_max=%s)\n",
                rgb->red, rgb->green, rgb->blue, rgb->red_max, rgb->green_max,
                rgb->blue_max);
            break;
        case OPTION_VALUE_TYPE_DIRECTION_VECTOR:
            direction_vector = &o->default_value.direction_vector;
            printf(", default x=%s  y=%s  z=%s  min=%s  "
                "max=%s)\n",
                direction_vector->x, direction_vector->y, direction_vector->z, direction_vector->min,
                direction_vector->max);
            break;
        default:
            printf(")\n");
            break;
        }
    }
}

void
type_store_print(struct type_store *store)
{
    struct type_description *desc;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&store->types, desc, i) {
        type_description_print(desc);
        printf("\n");
    }
}

bool
type_store_copy_option_description(struct option_description *dst,
    const struct option_description *src,
    const struct sol_str_slice opt_name)
{
    dst->name = strndup(opt_name.data, opt_name.len);
    SOL_NULL_CHECK_GOTO(dst->name, fail_name);

    dst->data_type = strdup(src->data_type);
    SOL_NULL_CHECK_GOTO(dst->data_type, fail_type);

    dst->default_value_type = src->default_value_type;

    switch (src->default_value_type) {
    case OPTION_VALUE_TYPE_STRING:
        if (src->default_value.string)
            dst->default_value.string = strdup(src->default_value.string);
        else
            dst->default_value.string = strdup("NULL");
        SOL_NULL_CHECK_GOTO(dst->default_value.string, fail_string);
        break;
    case OPTION_VALUE_TYPE_RANGE:
        dst->default_value.range = src->default_value.range;
        break;
    case OPTION_VALUE_TYPE_SPEC_RANGE:
        dst->default_value.spec_range = src->default_value.spec_range;
        break;
    case OPTION_VALUE_TYPE_RGB:
        dst->default_value.rgb = src->default_value.rgb;
        break;
    case OPTION_VALUE_TYPE_DIRECTION_VECTOR:
        dst->default_value.direction_vector = src->default_value.direction_vector;
        break;
    case OPTION_VALUE_TYPE_UNPARSED_JSON:
        dst->default_value.token = src->default_value.token;
        break;
    case OPTION_VALUE_TYPE_NONE:
        break;
    }
    return true;

fail_string:
    free(dst->data_type);
    dst->data_type = NULL;
fail_type:
    free(dst->name);
    dst->name = NULL;
fail_name:
    return false;
}
