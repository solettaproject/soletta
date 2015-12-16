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
#include <sol-common-buildopts.h>
#include <sol-vector.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Soletta types' manipulation.
 */

/**
 * @defgroup Type_Checking Type Checking
 *
 * @{
 */

/**
 * @def _SOL_TYPE_CHECK(type, var, value)
 *
 * @brief Internal macro to check type of given value.
 *
 * It does so by creating a block with a new variable called @a var of the given @a type,
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
 * @brief Macro to check @a type of given @a value.
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

/**
 * @brief Data type to describe a direction vector.
 */
struct sol_direction_vector {
    double x; /**< @brief X coordinate */
    double y; /**< @brief Y coordinate */
    double z; /**< @brief Z coordinate */
    double min; /**< @brief Minimum value of a coordinate for all axis */
    double max; /**< @brief Maximum value of a coordinate for all axis */
};

/**
 * @brief Data type to describe a location.
 */
struct sol_location {
    double lat; /**< @brief Latitude */
    double lon; /**< @brief Longitude */
    double alt; /**< @brief Altitude */
};

/**
 * @brief Data type to describe a RGB color.
 */
struct sol_rgb {
    uint32_t red; /**< @brief Red component */
    uint32_t green; /**< @brief Green component */
    uint32_t blue; /**< @brief Blue component */
    uint32_t red_max; /**< @brief Red component maximum value */
    uint32_t green_max; /**< @brief Green component maximum value */
    uint32_t blue_max; /**< @brief Blue component maximum value */
};

/**
 * @brief Set a maximum value for all components of a RGB color
 *
 * @param color The RGB color
 * @param max_value Maximum value to set
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_rgb_set_max(struct sol_rgb *color, uint32_t max_value);

/**
 * @brief Data type describing a Double range.
 */
struct sol_drange {
    double val; /**< @brief Current value */
    double min; /**< @brief Range minimum value */
    double max; /**< @brief Range maximum value */
    double step; /**< @brief Range step */
};

/**
 * @brief Data type describing a spec for Double ranges.
 *
 * A range spec is composed by the range limits and step.
 */
struct sol_drange_spec {
    double min; /**< @brief Range minimum value */
    double max; /**< @brief Range maximum value */
    double step; /**< @brief Range step */
};

/**
 * @brief Helper macro to initialize a double range with default values.
 */
#define SOL_DRANGE_INIT() \
    { \
        .min = -DBL_MAX, \
        .max = DBL_MAX, \
        .step = DBL_MIN, \
        .val = 0 \
    }

/**
 * @brief Helper macro to initialize a double range with default spec and a given value.
 *
 * @param value_ Initial value
 */
#define SOL_DRANGE_INIT_VALUE(value_) \
    { \
        .min = -DBL_MAX, \
        .max = DBL_MAX, \
        .step = DBL_MIN, \
        .val = value_ \
    }

/**
 * @brief Adds the double ranges @c var0 and @c var1 and stores the result in @c result.
 *
 * This function takes into consideration the range spec of the arguments
 * and set the appropriated spec in the result.
 *
 * @param var0 First double range
 * @param var1 Second double range
 * @param result Resulting range
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_drange_addition(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);

/**
 * @brief Divides the double range @c var0 by @c var1 and stores the result in @c result.
 *
 * This function takes into consideration the range spec of the arguments
 * and set the appropriated spec in the result.
 *
 * @param var0 First double range
 * @param var1 Second double range
 * @param result Resulting range
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_drange_division(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);

/**
 * @brief Calculates the module of the double range @c var0 by @c var1
 * and stores the result in @c result.
 *
 * This function takes into consideration the range spec of the arguments
 * and set the appropriated spec in the result.
 *
 * @param var0 First double range
 * @param var1 Second double range
 * @param result Resulting range
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_drange_modulo(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);

/**
 * @brief Multiplies the double ranges @c var0 and @c var1 and stores the result in @c result.
 *
 * This function takes into consideration the range spec of the arguments
 * and set the appropriated spec in the result.
 *
 * @param var0 First double range
 * @param var1 Second double range
 * @param result Resulting range
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_drange_multiplication(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);

/**
 * @brief Subtracts the double range @c var1 from @c var0 and stores the result in @c result.
 *
 * This function takes into consideration the range spec of the arguments
 * and set the appropriated spec in the result.
 *
 * @param var0 First double range
 * @param var1 Second double range
 * @param result Resulting range
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_drange_subtraction(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);

/**
 * @brief Checks @c var0 and @c var1 for equality.
 *
 * It uses relative comparison to account for impressions caused by floating point arithmetics,
 * so give preference to use this function instead of comparing the numbers directly.
 *
 * @param var0 First argument
 * @param var1 Second argument
 *
 * @return @c true if both values are equal, @c false otherwise.
 */
bool sol_drange_val_equal(double var0, double var1);

/**
 * @brief Checks the double ranges @c var0 and @c var1 for equality.
 *
 * This function takes into consideration the range spec of the arguments.
 *
 * @param var0 First double range
 * @param var1 Second double range
 *
 * @return @c true if both are equal, @c false otherwise.
 */
bool sol_drange_equal(const struct sol_drange *var0, const struct sol_drange *var1);

/**
 * @brief Initializes @c result with the given @c spec and @c value.
 *
 * @param spec Double range spec
 * @param value Desired value
 * @param result Double range to be initialized
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_drange_compose(const struct sol_drange_spec *spec, double value, struct sol_drange *result);

/**
 * @brief Data type describing Integer ranges.
 */
struct sol_irange {
    int32_t val; /**< @brief Current value */
    int32_t min; /**< @brief Range minimum value */
    int32_t max; /**< @brief Range maximum value */
    int32_t step; /**< @brief Range step */
};

/**
 * @brief Data type describing a spec for Integer ranges.
 *
 * A range spec is composed by the range limits and step.
 */
struct sol_irange_spec {
    int32_t min; /**< @brief Range minimum value */
    int32_t max; /**< @brief Range maximum value */
    int32_t step; /**< @brief Range step */
};

/**
 * @brief Helper macro to initialize an integer range with default values.
 */
#define SOL_IRANGE_INIT() \
    { \
        .min = INT32_MIN, \
        .max = INT32_MAX, \
        .step = 1, \
        .val = 0 \
    }

/**
 * @brief Helper macro to initialize a integer range with default spec and a given value.
 *
 * @param value_ Initial value
 */
#define SOL_IRANGE_INIT_VALUE(value_) \
    { \
        .min = INT32_MIN, \
        .max = INT32_MAX, \
        .step = 1, \
        .val = value_ \
    }

/**
 * @brief Adds the integer ranges @c var0 and @c var1 and stores the result in @c result.
 *
 * This function takes into consideration the range spec of the arguments
 * and set the appropriated spec in the result.
 *
 * @param var0 First integer range
 * @param var1 Second integer range
 * @param result Resulting range
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_irange_addition(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

/**
 * @brief Divides the integer range @c var0 by @c var1 and stores the result in @c result.
 *
 * This function takes into consideration the range spec of the arguments
 * and set the appropriated spec in the result.
 *
 * @param var0 First integer range
 * @param var1 Second integer range
 * @param result Resulting range
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_irange_division(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

/**
 * @brief Calculates the module of the integer range @c var0 by @c var1
 * and stores the result in @c result.
 *
 * This function takes into consideration the range spec of the arguments
 * and set the appropriated spec in the result.
 *
 * @param var0 First integer range
 * @param var1 Second integer range
 * @param result Resulting range
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_irange_modulo(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

/**
 * @brief Multiplies the integer ranges @c var0 and @c var1 and stores the result in @c result.
 *
 * This function takes into consideration the range spec of the arguments
 * and set the appropriated spec in the result.
 *
 * @param var0 First integer range
 * @param var1 Second integer range
 * @param result Resulting range
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_irange_multiplication(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

/**
 * @brief Subtracts the integer range @c var1 from @c var0 and stores the result in @c result.
 *
 * This function takes into consideration the range spec of the arguments
 * and set the appropriated spec in the result.
 *
 * @param var0 First integer range
 * @param var1 Second integer range
 * @param result Resulting range
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_irange_subtraction(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

/**
 * @brief Checks the integer ranges @c var0 and @c var1 for equality.
 *
 * This function takes into consideration the range spec of the arguments.
 *
 * @param var0 First integer range
 * @param var1 Second integer range
 *
 * @return @c true if both are equal, @c false otherwise.
 */
bool sol_irange_equal(const struct sol_irange *var0, const struct sol_irange *var1);

/**
 * @brief Initializes @c result with the given @c spec and @c value.
 *
 * @param spec Integer range spec
 * @param value Desired value
 * @param result Integer range to be initialized
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_irange_compose(const struct sol_irange_spec *spec, int32_t value, struct sol_irange *result);

struct sol_blob_type;

/**
 * @brief Data type describing the default blob implementation.
 */
struct sol_blob {
    const struct sol_blob_type *type; /**< @brief Blob type */
    struct sol_blob *parent; /**< @brief Pointer to the parent Blob */
    void *mem; /**< @brief Blob data */
    size_t size; /**< @brief Blob size */
    uint16_t refcnt; /**< @brief Blob reference counter */
};

/**
 * @brief Data type describing a blob type.
 *
 * This should be used by different kinds of Blob implementation to make than
 * compatible to our blob API.
 */
struct sol_blob_type {
#ifndef SOL_NO_API_VERSION
#define SOL_BLOB_TYPE_API_VERSION (1)
    uint16_t api_version; /**< @brief API version */
    uint16_t sub_api; /**< @brief Type API version */
#endif
    void (*free)(struct sol_blob *blob); /**< @brief Callback to free an instance */
};

/**
 * @brief Blob type object for the default implementation.
 *
 * The default type uses free() to release the blob's memory
 */
extern const struct sol_blob_type *SOL_BLOB_TYPE_DEFAULT;

/**
 * @brief Blob type object for the @c nofree implementation.
 *
 * The no-free type doesn't free blob's data memory. Used when pointing to inner
 * position of a pre existing blob or any other case when blob's data memory
 * shouldn't be freed.
 *
 * @note Blob's struct memory will be freed.
 */
extern const struct sol_blob_type *SOL_BLOB_TYPE_NOFREE;

/**
 * @brief Creates a new blob instance of the given type @c type.
 *
 * @param type Blob type of the new instance
 * @param parent Parent blob
 * @param mem Blob's data
 * @param size Blob's data size
 *
 * @return A new blob instance
 */
struct sol_blob *sol_blob_new(const struct sol_blob_type *type, struct sol_blob *parent, const void *mem, size_t size);

/**
 * @brief Setup a blob structure with the given parameters.
 *
 * @param blob Blob to setup
 * @param type Blob's type
 * @param mem Blob's data
 * @param size Blob's data size
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 */
int sol_blob_setup(struct sol_blob *blob, const struct sol_blob_type *type, const void *mem, size_t size);

/**
 * @brief Increments the reference counter of the given blob.
 *
 * @param blob The Blob increase the references
 *
 * @return Pointer to the referenced blob
 */
struct sol_blob *sol_blob_ref(struct sol_blob *blob);

/**
 * @brief Decreases the reference counter of the given blob.
 *
 * When the reference counter reaches @c 0, the blob is freed.
 *
 * @param blob The Blob to decrease the references
 */
void sol_blob_unref(struct sol_blob *blob);

/**
 * @brief Set the blob's parent.
 *
 * @param blob The blob
 * @param parent New parent
 */
void sol_blob_set_parent(struct sol_blob *blob, struct sol_blob *parent);

/**
 * @brief Data type to describe <key, value> pairs of strings.
 */
struct sol_key_value {
    const char *key; /**< @brief Pair's key */
    const char *value; /**< @brief Pair's value */
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
