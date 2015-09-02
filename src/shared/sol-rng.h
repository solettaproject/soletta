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

#include <stdint.h>

struct sol_rng_engine;
struct sol_rng_engine_impl;

extern const struct sol_rng_engine_impl *SOL_RNG_ENGINE_IMPL_MT19937;
extern const struct sol_rng_engine_impl *SOL_RNG_ENGINE_IMPL_URANDOM;
extern const struct sol_rng_engine_impl *SOL_RNG_ENGINE_IMPL_RANDOMR;
extern const struct sol_rng_engine_impl *SOL_RNG_ENGINE_IMPL_DEFAULT;

struct sol_rng_engine *sol_rng_engine_new(const struct sol_rng_engine_impl *impl);
struct sol_rng_engine *sol_rng_engine_seeded_new(const struct sol_rng_engine_impl *impl);
void sol_rng_engine_destroy(struct sol_rng_engine *engine);

void sol_rng_engine_seed(struct sol_rng_engine *engine, uint64_t value);

size_t sol_rng_engine_generate_bytes(struct sol_rng_engine *engine,
    void *buffer, size_t len);
