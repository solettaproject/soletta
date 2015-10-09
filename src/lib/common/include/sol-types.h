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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <float.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Solleta types' manipulation.
 */

/**
 * @defgroup Type_Checking Type Checking
 *
 * @{
 */

/**
 * @def _SOL_TYPE_CHECK(type, var, value)
 *
 * Internal macro to check type of given value. It does so by creating
 * a block with a new variable called @a var of the given @a type,
 * assigning the @a value to it and then returning the variable.
 *
 * The compiler should issue compile-time type errors if the value
 * doesn't match the type of variable.
 *
 * @see SOL_TYPE_CHECK()
 * @internal
 */
#define _SOL_TYPE_CHECK(type, var, value) \
    ({ type var = (value); var; })

/**
 * @def SOL_TYPE_CHECK(type, value)
 *
 * Macro to check @a type of given @a value.
 *
 * The compiler should issue compile-time type errors if the value
 * doesn't match the type.
 *
 * It is often used with macros that ends into types such as 'void*'
 * or even variable-arguments (varargs).
 *
 * Example: set mystruct to hold a string, checking type:
 * @code
 * struct mystruct {
 *    int type;
 *    void *value;
 * };
 *
 * #define mystruct_set_string(st, x) \
 *    do { \
 *       st->type = mystruct_type_string; \
 *       st->value = SOL_TYPE_CHECK(const char *, x); \
 *    } while (0)
 * @endcode
 *
 * Example: a free that checks if argument is a string
 * @code
 * #define free_str(x) free(SOL_TYPE_CHECK(char *, x))
 * @endcode
 *
 * @param type the desired type to check.
 * @param value the value to check, it's returned by the macro.
 * @return the given value
 */
#define SOL_TYPE_CHECK(type, value) \
    _SOL_TYPE_CHECK(type, __dummy_ ## __COUNTER__, (value))

/**
 * @}
 */

/**
 * @defgroup Types Types
 *
 * @{
 */

/*** SOL_DIRECTION_VECTOR ***/

struct sol_direction_vector {
    double x;
    double y;
    double z;
    double min;
    double max;
};

/*** SOL_LOCATION ***/

struct sol_location {
    double lat;
    double lon;
    double alt;
};


/*** SOL_RGB ***/

struct sol_rgb {
    uint32_t red;
    uint32_t green;
    uint32_t blue;
    uint32_t red_max;
    uint32_t green_max;
    uint32_t blue_max;
};

int sol_rgb_set_max(struct sol_rgb *color, uint32_t max_value);

/*** SOL_DRANGE ***/

struct sol_drange {
    double val;
    double min;
    double max;
    double step;
};

#define SOL_DRANGE_INIT() \
    { \
        .min = -DBL_MAX, \
        .max = DBL_MAX, \
        .step = DBL_MIN, \
        .val = 0 \
    }

#define SOL_DRANGE_INIT_VALUE(value_) \
    { \
        .min = -DBL_MAX, \
        .max = DBL_MAX, \
        .step = DBL_MIN, \
        .val = value_ \
    }

int sol_drange_addition(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);

int sol_drange_division(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);

int sol_drange_modulo(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);

int sol_drange_multiplication(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);

int sol_drange_subtraction(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);

bool sol_drange_val_equal(double var0, double var1);
bool sol_drange_equal(const struct sol_drange *var0, const struct sol_drange *var1);

/*** SOL_IRANGE ***/

struct sol_irange {
    int32_t val;
    int32_t min;
    int32_t max;
    int32_t step;
};

#define SOL_IRANGE_INIT() \
    { \
        .min = INT32_MIN, \
        .max = INT32_MAX, \
        .step = 1, \
        .val = 0 \
    }

#define SOL_IRANGE_INIT_VALUE(value_) \
    { \
        .min = INT32_MIN, \
        .max = INT32_MAX, \
        .step = 1, \
        .val = value_ \
    }

int sol_irange_addition(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

int sol_irange_division(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

int sol_irange_modulo(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

int sol_irange_multiplication(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

int sol_irange_subtraction(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

bool sol_irange_equal(const struct sol_irange *var0, const struct sol_irange *var1);

struct sol_blob_type;
struct sol_blob {
    const struct sol_blob_type *type;
    struct sol_blob *parent;
    void *mem;
    size_t size;
    uint16_t refcnt;
};

struct sol_blob_type {
#define SOL_BLOB_TYPE_API_VERSION (1)
    uint16_t api_version;
    uint16_t sub_api;
    void (*free)(struct sol_blob *blob);
};

/*
 * The default type uses free() to release the blob's memory
 */
extern const struct sol_blob_type *SOL_BLOB_TYPE_DEFAULT;

/*
 * The no-free type doesn't free blob's data memory. Used when pointing to inner
 * position of a pre existing blob or any other case when blob's data memory
 * shouldn't be freed
 * Note that blob's struct memory will be freed.
 */
extern const struct sol_blob_type *SOL_BLOB_TYPE_NOFREE;

struct sol_blob *sol_blob_new(const struct sol_blob_type *type, struct sol_blob *parent, const void *mem, size_t size);
int sol_blob_setup(struct sol_blob *blob, const struct sol_blob_type *type, const void *mem, size_t size);
struct sol_blob *sol_blob_ref(struct sol_blob *blob);
void sol_blob_unref(struct sol_blob *blob);
void sol_blob_set_parent(struct sol_blob *blob, struct sol_blob *parent);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
