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
 * This performs a compile-time type check on a const string, returning it.
 *
 * It is often used with macros that ends into types such as 'void*'
 * or even variable-arguments (varargs).
 *
 * Example:
 * @code
 * #define mystruct_set_string(st, x) \
 *    do { \
 *       st->type = mystruct_type_string; \
 *       st->value = sol_check_const_str(x); \
 *    } while (0)
 * @endcode
 *
 * @see sol_check_str()
 */
static inline const char *sol_check_const_str(const char *s) { return s; }

/**
 * This performs a compile-time type check on a string, returning it.
 *
 * It is often used with macros that ends into types such as 'void*'
 * or even variable-arguments (varargs).
 *
 * Example:
 * @code
 * // free() that checks if the given argument is a string.
 * #define free_str(x) free(sol_check_str(x))
 * @endcode
 *
 * @see sol_check_const_str()
 */
static inline char *sol_check_str(char *s) { return s; }

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
