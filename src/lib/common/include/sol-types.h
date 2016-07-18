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

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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

#ifndef SSIZE_MAX
/**
 * @brief Maximum value of a ssize variable.
 */
#define SSIZE_MAX LONG_MAX
#endif

#ifndef SSIZE_MIN
/**
 * @brief Minimum value of a ssize variable.
 */
#define SSIZE_MIN LONG_MIN
#endif

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
typedef struct sol_direction_vector {
    double x; /**< @brief X coordinate */
    double y; /**< @brief Y coordinate */
    double z; /**< @brief Z coordinate */
    double min; /**< @brief Minimum value of a coordinate for all axis */
    double max; /**< @brief Maximum value of a coordinate for all axis */
} sol_direction_vector;

/**
 * @brief Checks the ranges of @c var0 and @c var1 for equality.
 *
 * @param var0 The first direction vector
 * @param var1 The second direction vector
 *
 * @return @c true if both are equal, @c false otherwise.
 */
bool sol_direction_vector_eq(const struct sol_direction_vector *var0, const struct sol_direction_vector *var1);

/**
 * @brief Data type to describe a location.
 */
typedef struct sol_location {
    double lat; /**< @brief Latitude */
    double lon; /**< @brief Longitude */
    double alt; /**< @brief Altitude */
} sol_location;

/**
 * @brief Data type to describe a RGB color.
 */
typedef struct sol_rgb {
    uint32_t red; /**< @brief Red component */
    uint32_t green; /**< @brief Green component */
    uint32_t blue; /**< @brief Blue component */
    uint32_t red_max; /**< @brief Red component maximum value */
    uint32_t green_max; /**< @brief Green component maximum value */
    uint32_t blue_max; /**< @brief Blue component maximum value */
} sol_rgb;

/**
 * @brief Checks the ranges of @c var0 and @c var1 for equality.
 *
 * @param var0 The first RGB
 * @param var1 The second RGB
 *
 * @return @c true if both are equal, @c false otherwise.
 */
bool sol_rgb_eq(const struct sol_rgb *var0, const struct sol_rgb *var1);

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
typedef struct sol_drange {
    double val; /**< @brief Current value */
    double min; /**< @brief Range minimum value */
    double max; /**< @brief Range maximum value */
    double step; /**< @brief Range step */
} sol_drange;

/**
 * @brief Data type describing a spec for Double ranges.
 *
 * A range spec is composed by the range limits and step.
 */
typedef struct sol_drange_spec {
    double min; /**< @brief Range minimum value */
    double max; /**< @brief Range maximum value */
    double step; /**< @brief Range step */
} sol_drange_spec;

/**
 * @brief Helper macro to initialize a double range with default values.
 */
#define SOL_DRANGE_INIT() \
    { \
        .val = 0, \
        .min = -DBL_MAX, \
        .max = DBL_MAX, \
        .step = DBL_MIN \
    }

/**
 * @brief Helper macro to initialize a double range with default spec and a given value.
 *
 * @param value_ Initial value
 */
#define SOL_DRANGE_INIT_VALUE(value_) \
    { \
        .val = value_, \
        .min = -DBL_MAX, \
        .max = DBL_MAX, \
        .step = DBL_MIN \
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
int sol_drange_add(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);

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
int sol_drange_div(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);

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
int sol_drange_mod(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);

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
int sol_drange_mul(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);

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
int sol_drange_sub(const struct sol_drange *var0, const struct sol_drange *var1, struct sol_drange *result);


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
bool sol_drange_eq(const struct sol_drange *var0, const struct sol_drange *var1);

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
typedef struct sol_irange {
    int32_t val; /**< @brief Current value */
    int32_t min; /**< @brief Range minimum value */
    int32_t max; /**< @brief Range maximum value */
    int32_t step; /**< @brief Range step */
} sol_irange;

/**
 * @brief Data type describing a spec for Integer ranges.
 *
 * A range spec is composed by the range limits and step.
 */
typedef struct sol_irange_spec {
    int32_t min; /**< @brief Range minimum value */
    int32_t max; /**< @brief Range maximum value */
    int32_t step; /**< @brief Range step */
} sol_irange_spec;

/**
 * @brief Helper macro to initialize an integer range with default values.
 */
#define SOL_IRANGE_INIT() \
    { \
        .val = 0, \
        .min = INT32_MIN, \
        .max = INT32_MAX, \
        .step = 1 \
    }

/**
 * @brief Helper macro to initialize a integer range with default spec and a given value.
 *
 * @param value_ Initial value
 */
#define SOL_IRANGE_INIT_VALUE(value_) \
    { \
        .val = value_, \
        .min = INT32_MIN, \
        .max = INT32_MAX, \
        .step = 1 \
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
int sol_irange_add(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

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
int sol_irange_div(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

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
int sol_irange_mod(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

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
int sol_irange_mul(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

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
int sol_irange_sub(const struct sol_irange *var0, const struct sol_irange *var1, struct sol_irange *result);

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
bool sol_irange_eq(const struct sol_irange *var0, const struct sol_irange *var1);

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
typedef struct sol_blob {
    const struct sol_blob_type *type; /**< @brief Blob type */
    struct sol_blob *parent; /**< @brief Pointer to the parent Blob */
    void *mem; /**< @brief Blob data */
    size_t size; /**< @brief Blob size */
    uint16_t refcnt; /**< @brief Blob reference counter */
} sol_blob;

/**
 * @brief Data type describing a blob type.
 *
 * This should be used by different kinds of Blob implementation to make than
 * compatible to our blob API.
 */
typedef struct sol_blob_type {
#ifndef SOL_NO_API_VERSION
#define SOL_BLOB_TYPE_API_VERSION (1)
    uint16_t api_version; /**< @brief API version */
    uint16_t sub_api; /**< @brief Type API version */
#endif
    void (*free)(struct sol_blob *blob); /**< @brief Callback to free an instance */
} sol_blob_type;

/**
 * @brief Blob type object for the default implementation.
 *
 * The default type uses free() to release the blob's memory
 */
extern const struct sol_blob_type SOL_BLOB_TYPE_DEFAULT;

/**
 * @brief Blob type object for the @c nofree implementation.
 *
 * The no-free type doesn't free blob's data memory. Used when pointing to inner
 * position of a pre existing blob or any other case when blob's data memory
 * shouldn't be freed.
 *
 * @note Blob's struct memory will be freed.
 */
extern const struct sol_blob_type SOL_BLOB_TYPE_NO_FREE_DATA;

/**
 * @brief Blob type object for the @c nofree implementation.
 *
 * The no-free type doesn't free blob's data memory and the blob itself.
 * Used when creating a blob in the application's stack with constant data.
 */
extern const struct sol_blob_type SOL_BLOB_TYPE_NO_FREE;

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
 * @brief Creates a new blob duplicating target memory,
 *
 * Instead creating a blob wrapping any given memory, it duplicates
 * given memory. Useful, for instance, to create blobs for packet types.
 *
 * @param mem memory that blob will duplicate and refers to
 * @param size size of memory block
 *
 * @return new sol_blob on success. @c NULL on failure
 */
static inline struct sol_blob *
sol_blob_new_dup(const void *mem, size_t size)
{
    struct sol_blob *blob;
    void *v;

    if (!mem)
        return NULL;

    v = malloc(size);
    if (!v)
        return NULL;

    memcpy(v, mem, size);

    blob = sol_blob_new(&SOL_BLOB_TYPE_DEFAULT, NULL, v, size);
    if (!blob)
        goto fail;

    return blob;

fail:
    free(v);
    return NULL;
}

/**
 * Helper macro to create a new blob duplicating target memory
 * calculating target size
 */
#define SOL_BLOB_NEW_DUP(mem_) sol_blob_new_dup((&mem_), sizeof(mem_))

/**
 * @brief Creates a new blob duplicating target NUL terminated string
 *
 * @param str string to be duplicated in a blob.
 *
 * @return new sol_blob on success. @c NULL on failure
 */
static inline struct sol_blob *
sol_blob_new_dup_str(const char *str)
{
    if (!str)
        return NULL;

    return sol_blob_new_dup(str, strlen(str) + 1);
}

/**
 * @brief Data type to describe <key, value> pairs of strings.
 */
typedef struct sol_key_value {
    const char *key; /**< @brief Pair's key */
    const char *value; /**< @brief Pair's value */
} sol_key_value;

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
