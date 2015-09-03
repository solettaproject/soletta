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
#include <sol-log.h>
#include <sol-util.h>
#include <time.h>

#ifdef SOL_PLATFORM_LINUX
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "sol-rng.h"

struct sol_rng_engine {
    const struct sol_rng_engine_impl *impl;
};

struct sol_rng_engine_impl {
    bool (*init)(struct sol_rng_engine *engine, uint64_t seed);
    void (*shutdown)(struct sol_rng_engine *engine);
    size_t (*generate_bytes)(struct sol_rng_engine *engine, unsigned char *buffer, size_t len);
    size_t struct_size;
};

#ifdef SOL_PLATFORM_LINUX
static int
getrandom_shim(void *buf, size_t buflen, unsigned int flags)
{
    int fd;
    ssize_t ret;

#ifdef HAVE_GETRANDOM
    /* No wrappers are commonly available for this system call yet, so
     * use syscall(2) directly. */
    long gr_ret = syscall(SYS_getrandom, buf, buflen, flags);
    if (gr_ret >= 0)
        return gr_ret;
#endif

    fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        errno = EIO;
        return -1;
    }

    ret = read(fd, buf, buflen);
    close(fd);

    return ret;
}
#endif /* SOL_PLATFORM_LINUX */

static uint64_t
get_platform_seed(uint64_t seed)
{
    int ret;

    /* If a seed is provided, use it. */
    if (seed)
        return seed;

#ifdef SOL_PLATFORM_LINUX
    /* Use Linux-specific getrandom(2) if available to initialize the
     * seed.  If syscall isn't available, read from /dev/urandom instead. */
    ret = getrandom_shim(&seed, sizeof(seed), 0);
    if (ret == sizeof(seed))
        return seed;
#endif /* SOL_PLATFORM_LINUX */

    /* Fall back to using a bad source of entropy if platform-specific,
     * higher quality random sources, are unavailable. */
    return (uint64_t)time(NULL);
}

static inline size_t
min(size_t a, size_t b)
{
    return a < b ? a : b;
}

struct sol_rng_engine_mt19937 {
    struct sol_rng_engine base;
    unsigned int state[624];
    int index;
};

static bool
engine_mt19937_init(struct sol_rng_engine *generic, uint64_t seed)
{
    struct sol_rng_engine_mt19937 *engine = (struct sol_rng_engine_mt19937 *)generic;
    const size_t state_array_size = ARRAY_SIZE(engine->state);
    size_t i;

    engine->index = 0;
    engine->state[0] = (unsigned int)seed;
    for (i = 1; i < state_array_size; i++)
        engine->state[i] = i + 0x6c078965UL * (engine->state[i - 1] ^ (engine->state[i - 1] >> 30UL));

    return true;
}

static unsigned int
engine_mt19937_generate_uint(struct sol_rng_engine_mt19937 *engine)
{
    const size_t state_array_size = ARRAY_SIZE(engine->state);
    unsigned int y;

    if (engine->index == 0) {
        size_t i;

        for (i = 0; i < state_array_size; i++) {
            y = (engine->state[i] & 0x80000000UL);
            y += (engine->state[(i + 1UL) % state_array_size] & 0x7fffffffUL);

            engine->state[i] = engine->state[(i + 397UL) % state_array_size] ^ (y >> 1UL);
            if (y % 2 != 0)
                engine->state[i] ^= 0x9908b0dfUL;
        }
    }

    y = engine->state[engine->index];
    y ^= y >> 11UL;
    y ^= (y << 7UL) & 0x9d2c5680UL;
    y ^= (y << 15UL) & 0xefc60000UL;
    y ^= (y >> 18UL);

    engine->index = (engine->index + 1) % state_array_size;

    return y;
}

static size_t
engine_mt19937_generate(struct sol_rng_engine *generic, unsigned char *buffer,
    size_t length)
{
    struct sol_rng_engine_mt19937 *engine = (struct sol_rng_engine_mt19937 *)generic;
    ssize_t total = (ssize_t)length;

    while (total > 0) {
        unsigned int value = engine_mt19937_generate_uint(engine);
        size_t to_copy = min(sizeof(value), (size_t)total);

        memcpy(buffer, &value, to_copy);
        total -= to_copy;
        buffer += to_copy;
    }

    return length;
}

const struct sol_rng_engine_impl *SOL_RNG_ENGINE_IMPL_MT19937 =
    &(struct sol_rng_engine_impl) {
    .init = engine_mt19937_init,
    .shutdown = NULL,
    .generate_bytes = engine_mt19937_generate,
    .struct_size = sizeof(struct sol_rng_engine_mt19937)
};

#ifdef HAVE_RANDOMR
struct sol_rng_engine_randomr {
    struct sol_rng_engine base;
    unsigned char buffer[32];
    struct random_data state;
};

static bool
engine_randomr_init(struct sol_rng_engine *generic)
{
    struct sol_rng_engine_randomr *engine = (struct sol_rng_engine_randomr *)generic;

    memset(engine->state, 0, sizeof(engine->state));

    /* Return code ignored: no error case is possible at this point. */
    (void)initstate_r(seed, engine->buffer, sizeof(engine->buffer), &engine->state);

    return true;
}

static size_t
engine_randomr_generate(struct sol_rng_engine *generic,
    unsigned char *buffer, size_t length)
{
    struct sol_rng_engine_randomr *engine = (struct sol_rng_engine_randomr *)generic;
    ssize_t total = (ssize_t)length;

    while (total > 0) {
        int32_t value;
        size_t to_copy = min(sizeof(value), (size_t)total);

        /* Return code ignored: no error case is possible at this point. */
        (void)random_r(&engine->state, &value);

        memcpy(buffer, &value, to_copy);
        total -= to_copy;
        buffer += to_copy;
    }

    return length;
}

struct sol_rng_engine_impl *SOL_RNG_ENGINE_IMPL_URANDOM =
    &(struct sol_rng_engine_impl) {
    .init = engine_randomr_init,
    .shutdown = NULL,
    .generate_bytes = engine_randomr_generate,
    .struct_size = sizeof(struct sol_rng_engine_randomr)
};
#else
const struct sol_rng_engine_impl *SOL_RNG_ENGINE_IMPL_RANDOMR = NULL;
#endif

#ifdef SOL_PLATFORM_LINUX
struct sol_rng_engine_urandom {
    struct sol_rng_engine base;
    int fd;
};

static bool
engine_urandom_init(struct sol_rng_engine *generic, uint64_t seed)
{
    struct sol_rng_engine_urandom *engine = (struct sol_rng_engine_urandom *)generic;

    engine->fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (engine->fd < 0) {
        SOL_WRN("Could not open /dev/urandom: %s", sol_util_strerrora(errno));
        return false;
    }

    return true;
}

static void
engine_urandom_shutdown(struct sol_rng_engine *generic)
{
    struct sol_rng_engine_urandom *engine = (struct sol_rng_engine_urandom *)generic;

    close(engine->fd);
}

static size_t
engine_urandom_generate(struct sol_rng_engine *generic,
    unsigned char *buffer, size_t length)
{
    struct sol_rng_engine_urandom *engine = (struct sol_rng_engine_urandom *)generic;

    return read(engine->fd, buffer, length);
}

const struct sol_rng_engine_impl *SOL_RNG_ENGINE_IMPL_URANDOM =
    &(struct sol_rng_engine_impl) {
    .init = engine_urandom_init,
    .shutdown = engine_urandom_shutdown,
    .generate_bytes = engine_urandom_generate,
    .struct_size = sizeof(struct sol_rng_engine_urandom)
};
#else
const struct sol_rng_engine_impl *SOL_RNG_ENGINE_IMPL_URANDOM = NULL;
#endif

const struct sol_rng_engine_impl *SOL_RNG_ENGINE_IMPL_DEFAULT = NULL;

struct sol_rng_engine *
sol_rng_engine_new(const struct sol_rng_engine_impl *impl, uint64_t seed)
{
    struct sol_rng_engine *engine;

    if (!impl)
        impl = SOL_RNG_ENGINE_IMPL_MT19937;

    engine = malloc(impl->struct_size);
    SOL_NULL_CHECK(engine, NULL);

    engine->impl = impl;

    seed = get_platform_seed(seed);
    if (!impl->init(engine, seed)) {
        free(engine);
        return NULL;
    }

    return engine;
}

void
sol_rng_engine_del(struct sol_rng_engine *engine)
{
    SOL_NULL_CHECK(engine);

    if (engine->impl->shutdown)
        engine->impl->shutdown(engine);
    free(engine);
}

size_t
sol_rng_engine_generate_bytes(struct sol_rng_engine *engine, void *buffer,
    size_t len)
{
    SOL_NULL_CHECK(engine, 0);
    SOL_NULL_CHECK(engine->impl, 0);
    SOL_NULL_CHECK(engine->impl->generate_bytes, 0);

    return engine->impl->generate_bytes(engine, buffer, len);
}
