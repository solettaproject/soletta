/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

/**
 * @file
 * @brief This is a subset of the C11 atomic API for Soletta
 */

/**
 * @defgroup Atomic Atomic
 *
 * @{
 */

/**
 * @def SOL_ATOMIC_INIT
 * Used to initialize one of the sol_atomic_ variables statically.
 */

/**
 * @def SOL_ATOMIC_FLAG_INT
 * Used to initialize a sol_atommic_flag statically.
 */

/**
 * @def SOL_ATOMIC_RELAXED
 * Equivalent to C11 memory_order_relaxed.
 */

/**
 * @def SOL_ATOMIC_CONSUME
 * Equivalent to C11 memory_order_consume.
 */

/**
 * @def SOL_ATOMIC_ACQUIRE
 * Equivalent to C11 memory_order_acquire.
 */

/**
 * @def SOL_ATOMIC_RELEASE
 * Equivalent to C11 memory_order_release.
 */

/**
 * @def SOL_ATOMIC_ACQ_REL
 * Equivalent to C11 memory_order_acq_re (acquire and release).
 */

/**
 * @def SOL_ATOMIC_SEQ_CST
 * Equivalent to C11 memory_order_seq_cst (sequentially consistent).
 */

/**
 * @typedef sol_atomic_flag
 * An atomic variable that can contain only two states: set or unset.
 */

/**
 * @fn bool sol_atomic_test_and_set(atomic_flag *flag, int memory_order)
 * Returns the previous value state of @p flag and marks it as set, using memory order @p memory_order.
 */

/**
 * @fn void sol_atomic_clear(atomic_flag *flag, int memory_order)
 * Clears the state in the flag @p flag using memory order @p memory_order.
 */

/**
 * @typedef sol_atomic_int
 * An atomic variable compatible with an int.
 */

/**
 * @typedef sol_atomic_uint
 * An atomic variable compatible with an unsigned int.
 */

/**
 * @typedef sol_atomic_size_t
 * An atomic variable compatible with a size_t.
 */

/**
 * @typedef sol_atomic_uintptr_t
 * An atomic variable compatible with an uintptr_t.
 */

/**
 * @fn void sol_atomic_store(atomic *object, type value, int memory_order)
 * Stores value @p value at @p object, using the memory order @p memory_order.
 */

/**
 * @fn value sol_atomic_load(atomic *object, int memory_order)
 * Loads a value from @p object using memory order @p memory_order and returns it.
 */

/**
 * @fn value sol_atomic_exchange(atomic *object, value new_value, int memory_order)
 * Atomically replaces the value stored at @p object with new_value, using memory order
 * @p memory_order, and returns the old value.
 */

/**
 * @fn bool sol_atomic_compare_exchange(atomic *object, value *expected, value desired, int memory_order_success, int memory_order_failure)
 * Executes an atomic compare-and-swap operation at @p object: if the value stored
 * there is @p expected, then this function replaces it with @p desired, using memory
 * order @p memory_order_success and returns @c true. If the value at @p object
 * is not @p desired, then this function loads the current value into @p expected using
 * memory order @p memory_order_failure and returns @c false.
 *
 * This function is equivalent to C11's strong compare_exchange and its requirements on
 * the memory ordering arguments apply here too.
 */

/**
 * @fn value sol_atomic_fetch_add(atomic *object, value addend, int memory_order)
 * Atomically adds @p added to @p object and returns the new value, using memory order @p memory_order.
 */

#if (defined(__STDC_VERSION__) && __STDC_VERSION__ > 201112L) \
    || (defined(__GNUC__) && !defined(__clang__) && (__GNUC__ * 100 + __GNUC_MINOR__ > 408)) \
    || (defined(__clang__) && (__clang_major__ * 100 + __clang_minor__ >= 307)) \
    || defined(DOXYGEN_RUN)
/* GCC 4.9, Clang 3.7 provide <stdatomic.h>, a C11 header */
#include <stdatomic.h>

/* Initialization */
#define SOL_ATOMIC_INIT         ATOMIC_VAR_INIT
#define SOL_ATOMIC_FLAG_INIT    ATOMIC_FLAG_INIT

/* Order levels */
#define SOL_ATOMIC_RELAXED memory_order_relaxed
#define SOL_ATOMIC_CONSUME memory_order_consume
#define SOL_ATOMIC_ACQUIRE memory_order_acquire
#define SOL_ATOMIC_RELEASE memory_order_release
#define SOL_ATOMIC_ACQ_REL memory_order_acq_rel
#define SOL_ATOMIC_SEQ_CST memory_order_seq_cst

/* Types */
typedef atomic_int sol_atomic_int;
typedef atomic_uint sol_atomic_uint;
typedef atomic_size_t sol_atomic_size_t;
typedef atomic_uintptr_t sol_atomic_uintptr_t;

/* Functions */
#define sol_atomic_test_and_set     atomic_flag_test_and_set_explicit
#define sol_atomic_clear            atomic_flag_clear_explicit
#define sol_atomic_store            atomic_store_explicit
#define sol_atomic_load             atomic_load_explicit
#define sol_atomic_exchange         atomic_exchange_explicit
#define sol_atomic_compare_exchange atomic_compare_exchange_strong_explicit
#define sol_fetch_add               atomic_fetch_add_explicit

#elif (defined(__GNUC__) && !defined(__clang__) && (__GNUC__ * 100 + __GNUC_MINOR__ > 406))
/* GCC 4.6 has intrinsics that allow us to implement the atomic API */
#include <stddef.h>
#include <stdint.h>

/* Initialization */
#define SOL_ATOMIC_INIT(x)      (x)
#define SOL_ATOMIC_FLAG_INIT    false

/* Order levels */
#define SOL_ATOMIC_RELAXED __ATOMIC_RELAXED
#define SOL_ATOMIC_CONSUME __ATOMIC_CONSUME
#define SOL_ATOMIC_ACQUIRE __ATOMIC_ACQUIRE
#define SOL_ATOMIC_RELEASE __ATOMIC_RELEASE
#define SOL_ATOMIC_ACQ_REL __ATOMIC_ACQ_REL
#define SOL_ATOMIC_SEQ_CST __ATOMIC_SEQ_CST

/* Types */
typedef unsigned char sol_atomic_flag;
typedef int sol_atomic_int;
typedef unsigned sol_atomic_uint;
typedef size_t sol_atomic_size_t;
typedef uintptr_t sol_atomic_uintptr_t;

/* Functions */
#define sol_atomic_test_and_set     __atomic_test_and_set
#define sol_atomic_clear            __atomic_clear
#define sol_atomic_store            __atomic_store_n
#define sol_atomic_load             __atomic_load_n
#define sol_atomic_exchange         __atomic_exchange_n
#define sol_atomic_compare_exchange(object, expected, desired, mo_suc, mo_fail) \
    __atomic_compare_exchange_n((object), (expected), (desired), 0, (mo_suc), (mo_fail))
#define sol_fetch_add               __atomic_fetch_add

#else
#error "Atomic API not supported on this platform, please contribute."
#endif


/**
 * @}
 */
