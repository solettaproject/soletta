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

#include "random-gen.h"
#include "sol-flow-internal.h"

#include <errno.h>
#include <float.h>
#include <sol-util.h>
#include <stdlib.h>
#include <time.h>

#ifdef SOL_PLATFORM_LINUX
#include <linux/random.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#endif

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
    if (gr_ret > 0 && (size_t)(unsigned long)gr_ret == buflen)
        return 0;
#endif

    fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return errno == ENOENT ? -ENOSYS : -errno;

    ret = read(fd, buf, buflen);
    close(fd);

    return ret;
}
#endif /* SOL_PLATFORM_LINUX */

static int
get_platform_seed(int seed)
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
    return (int)time(NULL);
}

#ifdef HAVE_RANDOM_R

struct random_node_data {
    char buffer[32];
    struct random_data state;
};

static unsigned int
get_random_uint(struct random_node_data *mdata)
{
    int32_t result;

    /* Return code ignored: no error case is possible at this point. */
    (void)random_r(&mdata->state, &result);

    return result;
}

static int
get_random_int(struct random_node_data *mdata)
{
    return get_random_uint(mdata);
}

static bool
initialize_seed(struct random_node_data *mdata, int seed)
{
    memset(&mdata->state, 0, sizeof(mdata->state));

    /* Return code ignored: no error case is possible at this point. */
    (void)initstate_r(get_platform_seed(seed), mdata->buffer, sizeof(mdata->buffer), &mdata->state);

    return true;
}

#else /* HAVE_RANDOM_R */

/* This implementation is a direct pseudocode-to-C conversion from the Wikipedia
 * article about Mersenne Twister (MT19937). */

struct random_node_data {
    unsigned int state[624];
    int index;
};

static unsigned int
get_random_uint(struct random_node_data *mdata)
{
    const size_t state_array_size = ARRAY_SIZE(mdata->state);
    unsigned int y;

    if (mdata->index == 0) {
        size_t i;

        for (i = 0; i < state_array_size; i++) {
            y = (mdata->state[i] & 0x80000000UL);
            y += (mdata->state[(i + 1UL) % state_array_size] & 0x7fffffffUL);

            mdata->state[i] = mdata->state[(i + 397UL) % state_array_size] ^ (y >> 1UL);
            if (y % 2 != 0)
                mdata->state[i] ^= 0x9908b0dfUL;
        }
    }

    y = mdata->state[mdata->index];
    y ^= y >> 11UL;
    y ^= (y << 7UL) & 0x9d2c5680UL;
    y ^= (y << 15UL) & 0xefc60000UL;
    y ^= (y >> 18UL);

    mdata->index = (mdata->index + 1) % state_array_size;

    return y;
}

static int
get_random_int(struct random_node_data *mdata)
{
    unsigned int value = get_random_uint(mdata);

    return (int)(value >> 1UL); /* kill sign bit */
}

static bool
initialize_seed(struct random_node_data *mdata, int seed)
{
    const size_t state_array_size = ARRAY_SIZE(mdata->state);
    size_t i;

    mdata->index = 0;
    mdata->state[0] = get_platform_seed(seed);
    for (i = 1; i < state_array_size; i++)
        mdata->state[i] = i + 0x6c078965UL * (mdata->state[i - 1] ^ (mdata->state[i - 1] >> 30UL));

    return true;
}

#endif /* HAVE_RANDOM_R */

static int
random_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct random_node_data *mdata = data;
    const struct sol_flow_node_type_random_int_options *opts;

    /* TODO find some way to share the same options struct between
       multiple node types */
    opts = (const struct sol_flow_node_type_random_int_options *)options;

    if (!initialize_seed(mdata, opts->seed.val)) {
        SOL_ERR("Could not initialize random seed: %s", sol_util_strerrora(errno));
        return -EINVAL;
    }

    return 0;
}

/*
 * WARNING: Do NOT use it for anything else than test purposes.
 */
static int
random_int_generate(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct sol_irange value = { 0, 0, INT32_MAX, 1 };
    struct random_node_data *mdata = data;

    value.val = get_random_int(mdata);

    return sol_flow_send_irange_packet(node,
        SOL_FLOW_NODE_TYPE_RANDOM_INT__OUT__OUT,
        &value);
}

/*
 * WARNING: Do NOT use it for anything else than test purposes.
 */
static int
random_float_generate(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct random_node_data *mdata = data;
    struct sol_drange out_value = { 0, 0, INT32_MAX, 1 };
    int value, fraction;

    value = get_random_int(mdata);
    fraction = get_random_int(mdata);

    out_value.val = value * ((double)(INT32_MAX - 1) / INT32_MAX) +
        (double)fraction / INT32_MAX;

    return sol_flow_send_drange_packet(node,
        SOL_FLOW_NODE_TYPE_RANDOM_FLOAT__OUT__OUT,
        &out_value);
}

/*
 * WARNING: Do NOT use it for anything else than test purposes.
 */
static int
random_byte_generate(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct random_node_data *mdata = data;
    unsigned int value;

    value = get_random_uint(mdata) & 0xff;

    return sol_flow_send_byte_packet(node,
        SOL_FLOW_NODE_TYPE_RANDOM_BYTE__OUT__OUT,
        value);
}

/*
 * WARNING: Do NOT use it for anything else than test purposes.
 */
static int
random_boolean_generate(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct random_node_data *mdata = data;
    unsigned int value;

    value = get_random_uint(mdata);

    return sol_flow_send_boolean_packet(node,
        SOL_FLOW_NODE_TYPE_RANDOM_BOOLEAN__OUT__OUT,
        value % 2);
}

static int
random_seed_set(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct random_node_data *mdata = data;
    int r;
    int32_t in_value;

    r = sol_flow_packet_get_irange_value(packet, &in_value);
    SOL_INT_CHECK(r, < 0, r);
    initialize_seed(mdata, in_value);
    return 0;
}

#include "random-gen.c"
