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

#include <errno.h>
#include <sol-log.h>
#include <sol-util-internal.h>
#include <time.h>

#ifdef SOL_PLATFORM_ZEPHYR
#include <drivers/rand32.h>
#endif

#ifdef SOL_PLATFORM_LINUX
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "sol-random.h"
#include "sol-util-internal.h"

struct sol_random {
    const struct sol_random_impl *impl;
};

struct sol_random_impl {
    bool (*init)(struct sol_random *engine, uint64_t seed);
    void (*shutdown)(struct sol_random *engine);
    uint32_t (*generate_uint32)(struct sol_random *engine);
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
#ifdef SOL_PLATFORM_LINUX
    int ret;
#endif /* SOL_PLATFORM_LINUX */

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
#ifdef SOL_PLATFORM_ZEPHYR
    return ((uint64_t)sys_rand32_get() << 32) | sys_rand32_get();
#else
    return (uint64_t)time(NULL);
#endif
}

struct sol_random_mt19937 {
    struct sol_random base;
    unsigned int state[624];
    int index;
};

static bool
engine_mt19937_init(struct sol_random *generic, uint64_t seed)
{
    struct sol_random_mt19937 *engine = (struct sol_random_mt19937 *)generic;
    const size_t state_array_size = sol_util_array_size(engine->state);
    size_t i;

    if (!seed)
        seed = get_platform_seed(seed);

    engine->index = 0;
    engine->state[0] = (unsigned int)seed;
    for (i = 1; i < state_array_size; i++)
        engine->state[i] = i + 0x6c078965U * (engine->state[i - 1] ^ (engine->state[i - 1] >> 30U));

    return true;
}

static uint32_t
engine_mt19937_generate_uint32(struct sol_random *generic)
{
    struct sol_random_mt19937 *engine = (struct sol_random_mt19937 *)generic;
    const size_t state_array_size = sol_util_array_size(engine->state);
    uint32_t y;

    if (engine->index == 0) {
        size_t i;

        for (i = 0; i < state_array_size; i++) {
            y = (engine->state[i] & 0x80000000U);
            y += (engine->state[(i + 1U) % state_array_size] & 0x7fffffffU);

            engine->state[i] = engine->state[(i + 397U) % state_array_size] ^ (y >> 1U);
            if (y % 2 != 0)
                engine->state[i] ^= 0x9908b0dfU;
        }
    }

    y = engine->state[engine->index];
    y ^= y >> 11U;
    y ^= (y << 7U) & 0x9d2c5680U;
    y ^= (y << 15U) & 0xefc60000U;
    y ^= (y >> 18U);

    engine->index = (engine->index + 1) % state_array_size;

    return y;
}

const struct sol_random_impl *SOL_RANDOM_MT19937 =
    &(struct sol_random_impl) {
    .init = engine_mt19937_init,
    .shutdown = NULL,
    .generate_uint32 = engine_mt19937_generate_uint32,
    .struct_size = sizeof(struct sol_random_mt19937)
};

#ifdef HAVE_RANDOM_R
struct sol_random_randomr {
    struct sol_random base;
    char buffer[32];
    struct random_data state;
};

static bool
engine_randomr_init(struct sol_random *generic, uint64_t seed)
{
    struct sol_random_randomr *engine = (struct sol_random_randomr *)generic;

    if (!seed)
        seed = get_platform_seed(seed);
    memset(&engine->state, 0, sizeof(engine->state));

    /* Return code ignored: no error case is possible at this point. */
    (void)initstate_r(seed, engine->buffer, sizeof(engine->buffer), &engine->state);

    return true;
}

static uint32_t
engine_randomr_generate_uint32(struct sol_random *generic)
{
    struct sol_random_randomr *engine = (struct sol_random_randomr *)generic;
    int32_t value;

    /* Return code ignored: no error case is possible at this point. */
    (void)random_r(&engine->state, &value);

    return (uint32_t)value;
}

const struct sol_random_impl *SOL_RANDOM_RANDOMR =
    &(struct sol_random_impl) {
    .init = engine_randomr_init,
    .shutdown = NULL,
    .generate_uint32 = engine_randomr_generate_uint32,
    .struct_size = sizeof(struct sol_random_randomr)
};
#else
const struct sol_random_impl *SOL_RANDOM_RANDOMR = NULL;
#endif

#ifdef SOL_PLATFORM_LINUX
struct sol_random_urandom {
    struct sol_random base;
    int fd;
};

static bool
engine_urandom_init(struct sol_random *generic, uint64_t seed)
{
    struct sol_random_urandom *engine = (struct sol_random_urandom *)generic;

    if (seed) {
        SOL_WRN("Explicit seed not supported by this random implementation");
        return false;
    }

    engine->fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (engine->fd < 0) {
        SOL_WRN("Could not open /dev/urandom: %s", sol_util_strerrora(errno));
        return false;
    }

    return true;
}

static void
engine_urandom_shutdown(struct sol_random *generic)
{
    struct sol_random_urandom *engine = (struct sol_random_urandom *)generic;

    close(engine->fd);
}

static uint32_t
engine_urandom_generate_uint32(struct sol_random *generic)
{
    struct sol_random_urandom *engine = (struct sol_random_urandom *)generic;

    while (true) {
        uint32_t value;
        ssize_t n = read(engine->fd, &value, sizeof(value));

        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            break;
        }

        if (n == sizeof(value))
            return value;
    }

    SOL_ERR("Could not read from /dev/urandom: %s", sol_util_strerrora(errno));
    return 0;
}

const struct sol_random_impl *SOL_RANDOM_URANDOM =
    &(struct sol_random_impl) {
    .init = engine_urandom_init,
    .shutdown = engine_urandom_shutdown,
    .generate_uint32 = engine_urandom_generate_uint32,
    .struct_size = sizeof(struct sol_random_urandom)
};
#else
const struct sol_random_impl *SOL_RANDOM_URANDOM = NULL;
#endif

const struct sol_random_impl *SOL_RANDOM_DEFAULT = NULL;

struct sol_random *
sol_random_new(const struct sol_random_impl *impl, uint64_t seed)
{
    struct sol_random *engine;

    if (!impl)
        impl = SOL_RANDOM_MT19937;

    engine = malloc(impl->struct_size);
    SOL_NULL_CHECK(engine, NULL);

    engine->impl = impl;
    if (!engine->impl->init(engine, seed)) {
        sol_util_clear_memory_secure(engine, impl->struct_size);
        free(engine);
        return NULL;
    }

    return engine;
}

void
sol_random_del(struct sol_random *engine)
{
    SOL_NULL_CHECK(engine);

    if (engine->impl->shutdown)
        engine->impl->shutdown(engine);

    sol_util_clear_memory_secure(engine, engine->impl->struct_size);
    free(engine);
}

ssize_t
sol_random_fill_buffer(struct sol_random *engine, struct sol_buffer *buffer,
    size_t len)
{
    uint32_t value;
    struct sol_str_slice slice = SOL_STR_SLICE_STR((const char *)&value, sizeof(value));
    ssize_t total;

    SOL_NULL_CHECK(engine, -EINVAL);
    SOL_NULL_CHECK(engine->impl, -EINVAL);

    for (total = (ssize_t)len; total > 0; total -= sizeof(value)) {
        int r;

        value = engine->impl->generate_uint32(engine);
        r = sol_buffer_append_slice(buffer, slice);
        if (r < 0)
            return r;
    }

    return len;
}
