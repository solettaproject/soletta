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

#include "sol-util.h"
#include "sol-log.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_GETRANDOM
#include <linux/random.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

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

static int
rand_gen(struct sol_uuid *p)
{
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    int n = sizeof(*p);
    int fd = -1;
    int r;

#ifdef HAVE_GETRANDOM
    r = syscall(__NR_getrandom, p, n, GRND_NONBLOCK);
    if (r == n)
        return 0;

    if (r < 0) {
        if (errno != EAGAIN) { /* fallback to /dev/urandom on EAGAIN
                                * -- good enough for our uses */
            return -errno;
        }
    } else
        return -ENODATA;
#endif

    fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC | O_NOCTTY);
    if (fd < 0)
        return errno == ENOENT ? -ENOSYS : -errno;

    r = sol_buffer_ensure(&buffer, n);
    if (r < 0) {
        close(fd);
        return -ENOMEM;
    }
    r = sol_util_fill_buffer(fd, &buffer, n);
    if (r < 0)
        goto end;

    memcpy(p, buffer.data, n);

end:
    close(fd);
    sol_buffer_fini(&buffer);
    return r;
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
    struct sol_uuid t;
    int r;

    SOL_NULL_CHECK(ret, -EINVAL);

    r = rand_gen(&t);
    SOL_INT_CHECK(r, < 0, r);

    *ret = assert_uuid_v4(t);
    return 0;
}

// 37 = 2 * 16 (chars) + 4 (hyphens) + 1 (\0)
int
sol_util_uuid_gen(bool upcase,
    bool with_hyphens,
    char id[37])
{
    struct sol_str_slice hyphen = SOL_STR_SLICE_LITERAL("-");
    /* hyphens on positions 8, 13, 18, 23 (from 0) */
    const int hyphens_pos[] = { 8, 13, 18, 23 };
    struct sol_uuid uuid;
    char uuid_str[37];
    unsigned i;
    int r;

    struct sol_buffer buf = SOL_BUFFER_INIT_FLAGS(&uuid_str, 37,
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);

    r = uuid_gen(&uuid);
    SOL_INT_CHECK(r, < 0, r);

    for (i = 0; i < ARRAY_SIZE(uuid.bytes); i++) {
        char low, high;
        struct sol_str_slice slice;

        high = (unsigned char)((uuid.bytes[i] & 0xf0) >> 4) + '0';
        low = (unsigned char)(uuid.bytes[i] & 0x0f) + '0';

        /* according to ascii table */
        if ((unsigned char)high > '9') {
            high += upcase ? 7 : 39;
        }
        if ((unsigned char)low > '9') {
            low += upcase ? 7 : 39;
        }

        slice = SOL_STR_SLICE_STR(&high, 1);
        r = sol_buffer_append_slice(&buf, slice);
        SOL_INT_CHECK_GOTO(r, < 0, err);

        slice = SOL_STR_SLICE_STR(&low, 1);
        sol_buffer_append_slice(&buf, slice);
        SOL_INT_CHECK_GOTO(r, < 0, err);
    }
    *(char *)sol_buffer_at_end(&buf) = '\0';

    if (with_hyphens) {
        for (i = 0; i < ARRAY_SIZE(hyphens_pos); i++) {
            r = sol_buffer_insert_slice(&buf, hyphens_pos[i], hyphen);
            SOL_INT_CHECK_GOTO(r, < 0, err);
        }
    }

    memcpy(id, buf.data, buf.used);
err:
    sol_buffer_fini(&buf);
    return r;
}
