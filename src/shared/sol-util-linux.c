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

#include "sol-buffer.h"
#include "sol-log.h"
#include "sol-random.h"
#include "sol-util.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct timespec
sol_util_timespec_get_current(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return t;
}
int
sol_util_timespec_get_realtime(struct timespec *t)
{
    return clock_gettime(CLOCK_REALTIME, t);
}

static struct sol_uuid
assert_uuid_v4(struct sol_uuid id)
{
    id.bytes[6] = (id.bytes[6] & 0x0F) | 0x40;
    id.bytes[8] = (id.bytes[8] & 0x3F) | 0x80;

    return id;
}

static int
uuid_gen(struct sol_uuid *ret)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(ret, sizeof(*ret),
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    struct sol_random *engine;
    ssize_t size;

    SOL_NULL_CHECK(ret, -EINVAL);
    engine = sol_random_new(SOL_RANDOM_DEFAULT, 0);
    SOL_NULL_CHECK(engine, -errno);

    size = sol_random_fill_buffer(engine, &buf, sizeof(*ret));
    sol_random_del(engine);
    sol_buffer_fini(&buf);

    if (size != (ssize_t)sizeof(*ret))
        return -EIO;

    *ret = assert_uuid_v4(*ret);
    return 0;
}

// 37 = 2 * 16 (chars) + 4 (hyphens) + 1 (\0)
int
sol_util_uuid_gen(bool upcase,
    bool with_hyphens,
    char id[static 37])
{
    static struct sol_str_slice hyphen = SOL_STR_SLICE_LITERAL("-");
    /* hyphens on positions 8, 13, 18, 23 (from 0) */
    static const int hyphens_pos[] = { 8, 13, 18, 23 };
    struct sol_uuid uuid = { 0 };
    unsigned i;
    int r;

    struct sol_buffer buf = { 0 };

    sol_buffer_init_flags(&buf, id, 37,
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);

    r = uuid_gen(&uuid);
    SOL_INT_CHECK(r, < 0, r);

    for (i = 0; i < ARRAY_SIZE(uuid.bytes); i++) {
        r = sol_buffer_append_printf(&buf, upcase ? "%02hhX" : "%02hhx",
            uuid.bytes[i]);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }

    if (with_hyphens) {
        for (i = 0; i < ARRAY_SIZE(hyphens_pos); i++) {
            r = sol_buffer_insert_slice(&buf, hyphens_pos[i], hyphen);
            SOL_INT_CHECK_GOTO(r, < 0, err);
        }
    }

err:
    sol_buffer_fini(&buf);
    return r;
}
