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

/*
 * This is a regular Soletta application, there are no linux-micro
 * specific bits in it, just a timer, an optional gpio writer and some
 * monitors for platform and service states. The purpose is to show
 * that it can be considered a /init (PID1) binary if Soletta is
 * compiled with linux-micro platform and if it runs as PID1, then
 * /proc, /sys, /dev are all mounted as well as other bits of the
 * system are configured.
 */

#include <stdio.h>
#include <errno.h>

#include "sol-buffer.h"
#include "sol-file-reader.h"
#include "sol-mainloop.h"
#include "sol-message-digest.h"
#include "sol-util.h"

static uint32_t pending;

struct feed_ctx {
    const char *file;
    struct timespec start;
    size_t done;
    size_t chunk_size;
    uint32_t idx;
};

static void
print_time(const struct feed_ctx *ctx, size_t amount, const char *prefix)
{
    struct timespec now = sol_util_timespec_get_current();
    struct timespec elapsed;
    double size, rate, seconds;
    const char *s_unit, *r_unit;

    sol_util_timespec_sub(&now, &ctx->start, &elapsed);
    seconds = elapsed.tv_sec + (double)elapsed.tv_nsec / NSEC_PER_SEC;

    size = amount;
    if (size >= 1.0e9) {
        s_unit = "Gb";
        size /= 1.0e9;
    } else if (size > 1.0e6) {
        s_unit = "Mb";
        size /= 1.0e6;
    } else if (size > 1.0e3) {
        s_unit = "Kb";
        size /= 1.0e3;
    } else {
        s_unit = "b";
    }

    rate = amount / seconds;
    if (rate >= 1.0e9) {
        r_unit = "Gb";
        rate /= 1.0e9;
    } else if (rate > 1.0e6) {
        r_unit = "Mb";
        rate /= 1.0e6;
    } else if (rate > 1.0e3) {
        r_unit = "Kb";
        rate /= 1.0e3;
    } else {
        r_unit = "b";
    }

    printf("%s chunk{#%" PRIu32 ", %zdb] %0.1f%s done in %0.3fseconds: %0.1f%s/s\n",
        prefix, ctx->idx, ctx->chunk_size, size, s_unit, seconds, rate, r_unit);
}

static void
on_feed_done(void *data, struct sol_message_digest *handle, struct sol_blob *input)
{
    struct feed_ctx *ctx = data;

    ctx->done += input->size;
    ctx->idx++;
    print_time(ctx, ctx->done, "feed");
}

static void
on_digest_ready(void *data, struct sol_message_digest *handle, struct sol_blob *digest)
{
    struct feed_ctx *ctx = data;
    struct sol_buffer buf;
    int r;

    sol_buffer_init(&buf);
    r = sol_buffer_append_as_base16(&buf, sol_str_slice_from_blob(digest), false);
    if (r == 0) {
        printf("%s\t%s\n", (char *)buf.data, ctx->file);
    }

    sol_buffer_fini(&buf);

    print_time(ctx, ctx->done, "final");

    sol_message_digest_del(handle);
    free(ctx);

    pending--;
    if (pending == 0)
        sol_quit();
}

static void
startup(void)
{
    const char *algorithm = "md5";
    const char *key = NULL;
    char **argv = sol_argv();
    int i, argc = sol_argc();
    size_t chunk_size = -1;

    if (argc < 2) {
        fprintf(stderr,
            "Usage:\n\t%s [-a <algorithm>] [-c chunk_size] [-k key] <file1> .. <fileN>\n", argv[0]);
        sol_quit_with_code(EXIT_FAILURE);
        return;
    }

    for (i = 1; i < argc; i++) {
        struct feed_ctx *ctx;
        struct sol_message_digest_config cfg = {
            SOL_SET_API_VERSION(.api_version = SOL_MESSAGE_DIGEST_CONFIG_API_VERSION, )
            .algorithm = algorithm,
            .on_feed_done = on_feed_done,
            .on_digest_ready = on_digest_ready,
        };
        struct sol_file_reader *fr;
        struct sol_blob *blob;
        struct sol_message_digest *mdh;
        int r;

        if (argv[i][0] == '-') {
            if (argv[i][1] == 'a') {
                if (i + 1 < argc) {
                    algorithm = argv[i + 1];
                    i++;
                    continue;
                } else
                    fputs("ERROR: argument -a missing value.\n", stderr);
            } else if (argv[i][1] == 'k') {
                if (i + 1 < argc) {
                    key = argv[i + 1];
                    i++;
                    continue;
                } else
                    fputs("ERROR: argument -a missing value.\n", stderr);
            } else if (argv[i][1] == 'c') {
                if (i + 1 < argc) {
                    chunk_size = atoi(argv[i + 1]);
                    i++;
                    continue;
                } else
                    fputs("ERROR: argument -c missing value.\n", stderr);
            } else
                fprintf(stderr, "ERROR: unknown option %s\n", argv[i]);
            sol_quit_with_code(EXIT_FAILURE);
            return;
        }

        fr = sol_file_reader_open(argv[i]);
        if (!fr) {
            fprintf(stderr, "ERROR: could not open file '%s': %s\n",
                argv[i], sol_util_strerrora(errno));
            continue;
        }

        blob = sol_file_reader_to_blob(fr);
        if (!blob) {
            fprintf(stderr, "ERROR: could not create blob for file '%s'\n",
                argv[i]);
            continue;
        }

        cfg.data = ctx = calloc(1, sizeof(struct feed_ctx));
        if (!ctx) {
            fprintf(stderr, "ERROR: could not allocate context memory "
                "to process file '%s'\n", argv[i]);
            sol_blob_unref(blob);
            continue;
        }

        ctx->file = argv[i];
        ctx->start = sol_util_timespec_get_current();
        ctx->done = 0;
        ctx->chunk_size = chunk_size;


        if (key)
            cfg.key = sol_str_slice_from_str(key);

        mdh = sol_message_digest_new(&cfg);
        if (!mdh) {
            fprintf(stderr, "ERROR: could not create message digest for "
                " algorithm \"%s\": %s\n",
                algorithm, sol_util_strerrora(errno));
            sol_blob_unref(blob);
            free(ctx);
            continue;
        }

        if (chunk_size <= 0) {
            r = sol_message_digest_feed(mdh, blob, true);
            if (r < 0) {
                fprintf(stderr, "ERROR: could not feed message for "
                    " algorithm \"%s\": %s\n",
                    algorithm, sol_util_strerrora(-r));
                sol_blob_unref(blob);
                sol_message_digest_del(mdh);
                free(ctx);
                continue;
            }
        } else {
            size_t offset = 0;
            while (offset < blob->size) {
                size_t remaining = blob->size - offset;
                size_t clen = remaining > chunk_size ? chunk_size : remaining;
                uint8_t *cmem = (uint8_t *)blob->mem + offset;
                bool is_last = offset + clen == blob->size;
                struct sol_blob *chunk = sol_blob_new(SOL_BLOB_TYPE_NOFREE,
                    blob, cmem, clen);
                if (!chunk) {
                    fprintf(stderr, "ERROR: could not create chunk blob at "
                        "mem %p, size=%zd\n", cmem, clen);
                    sol_blob_unref(blob);
                    sol_message_digest_del(mdh);
                    free(ctx);
                    continue;
                }

                r = sol_message_digest_feed(mdh, chunk, is_last);
                if (r < 0) {
                    fprintf(stderr, "ERROR: could not feed chunk for "
                        " algorithm \"%s\": %s\n",
                        algorithm, sol_util_strerrora(-r));
                    sol_blob_unref(blob);
                    sol_blob_unref(chunk);
                    sol_message_digest_del(mdh);
                    free(ctx);
                    continue;
                }

                sol_blob_unref(chunk);
                offset += clen;
            }
        }

        sol_blob_unref(blob);
        pending++;
    }

    if (pending == 0)
        sol_quit_with_code(EXIT_FAILURE);
}

static void
shutdown(void)
{
}

SOL_MAIN_DEFAULT(startup, shutdown);
