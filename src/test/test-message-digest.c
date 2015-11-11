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

#include "sol-mainloop.h"
#include "sol-message-digest.h"
#include "sol-buffer.h"

#include "test.h"

static char big_blob_of_zeros[40960];
static uint8_t big_blob_of_chars[40960];

static void
init_big_blobs(void)
{
    uint8_t v = 0, *itr, *end;

    memset(big_blob_of_zeros, 0, sizeof(big_blob_of_zeros));

    itr = big_blob_of_chars;
    end = itr + sizeof(big_blob_of_chars);
    for (; itr < end; itr++, v++)
        *itr = v;
}

struct md_test {
    const char *algorithm;
    struct sol_str_slice key;
    const void *mem;
    size_t len;
    const char *hex_digest;
#define MD_TEST(a, k, m, h) { a, k, m, sizeof(m), h }
#define MD_TEST_SIZE(a, k, m, s, h) { a, k, m, s, h }
#define MD_TEST_END { NULL, SOL_STR_SLICE_EMPTY, NULL, 0, NULL }
};

static uint32_t pending;

static void
on_digest_ready_simple(void *data, struct sol_message_digest *handle, struct sol_blob *digest)
{
    const struct md_test *t = data;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    const char *hex_digest;
    int r;

    r = sol_buffer_append_as_base16(&buf, sol_str_slice_from_blob(digest), false);
    ASSERT_INT_EQ(r, 0);

    hex_digest = buf.data;
    ASSERT_STR_EQ(t->hex_digest, hex_digest);

    sol_buffer_fini(&buf);
    sol_message_digest_del(handle);

    pending--;
    if (pending == 0)
        sol_quit();
}

static bool
on_timeout_do_single(void *data)
{
    const struct md_test *itr = data;

    for (; itr->mem != NULL; itr++) {
        struct sol_message_digest_config cfg = {
            SOL_SET_API_VERSION(.api_version = SOL_MESSAGE_DIGEST_CONFIG_API_VERSION, )
            .algorithm = itr->algorithm,
            .key = itr->key,
            .on_digest_ready = on_digest_ready_simple,
            .on_feed_done = NULL,
            .data = itr
        };
        struct sol_message_digest *md;
        struct sol_blob *blob;
        int r;

        blob = sol_blob_new(SOL_BLOB_TYPE_NOFREE, NULL, itr->mem, itr->len);
        ASSERT(blob != NULL);

        md = sol_message_digest_new(&cfg);
        ASSERT(md != NULL);

        r = sol_message_digest_feed(md, blob, true);
        ASSERT_INT_EQ(r, 0);

        sol_blob_unref(blob);

        pending++;
    }

    return false;
}

#define CHUNK_SIZE 64
struct chunked_ctx {
    const struct md_test *t;
    struct sol_message_digest *md;
    struct sol_timeout *timer;
    size_t offset;
};

static bool
on_timeout_do_chunked_internal(void *data)
{
    struct chunked_ctx *ctx = data;
    uint8_t i;

    if (!ctx->timer)
        return false;

    if (ctx->offset >= ctx->t->len) {
        sol_timeout_del(ctx->timer); /* also delete as we may be called from on_feed_done */
        ctx->timer = NULL;
        return false;
    }

    /* feed 3 blobs from within the same main loop iteration, then
     * wait to be completed and send more.
     */
    for (i = 0; i < 3; i++) {
        struct sol_blob *blob;
        bool is_final = false;
        size_t len = CHUNK_SIZE;
        const void *mem;
        int r;

        if (ctx->offset + CHUNK_SIZE >= ctx->t->len) {
            is_final = true;
            len = ctx->t->len - ctx->offset;
        }

        mem = (char *)ctx->t->mem + ctx->offset;
        blob = sol_blob_new(SOL_BLOB_TYPE_NOFREE, NULL, mem, len);
        ASSERT(blob != NULL);

        ctx->offset += len;

        r = sol_message_digest_feed(ctx->md, blob, is_final);
        ASSERT_INT_EQ(r, 0);

        sol_blob_unref(blob);

        if (is_final)
            break;
    }

    /* keep calling this function (will call from different main loop
     * iteration (10ms timer) and possibly before on_feed_done.
     */
    if (ctx->offset >= ctx->t->len) {
        sol_timeout_del(ctx->timer); /* also delete as we may be called from on_feed_done */
        ctx->timer = NULL;
        return false;
    }

    return true;
}

static void
on_feed_done_chunked(void *data, struct sol_message_digest *handle, struct sol_blob *input)
{
    /* feed more after we're done with our previous data */
    on_timeout_do_chunked_internal(data);
}

static void
on_digest_ready_chunked(void *data, struct sol_message_digest *handle, struct sol_blob *digest)
{
    struct chunked_ctx *ctx = data;

    on_digest_ready_simple((void *)ctx->t, handle, digest);
    if (ctx->timer)
        sol_timeout_del(ctx->timer);
    free(ctx);
}

static bool
on_timeout_do_chunked(void *data)
{
    const struct md_test *itr = data;

    for (; itr->mem != NULL; itr++) {
        struct chunked_ctx *ctx;
        struct sol_message_digest_config cfg = {
            SOL_SET_API_VERSION(.api_version = SOL_MESSAGE_DIGEST_CONFIG_API_VERSION, )
            .algorithm = itr->algorithm,
            .key = itr->key,
            .on_digest_ready = on_digest_ready_chunked,
            .on_feed_done = on_feed_done_chunked,
        };

        ctx = malloc(sizeof(*ctx));
        ASSERT(ctx != NULL);
        ctx->t = itr;
        ctx->offset = 0;
        cfg.data = ctx;

        ctx->md = sol_message_digest_new(&cfg);
        ASSERT(ctx->md != NULL);

        ctx->timer = sol_timeout_add(10, on_timeout_do_chunked_internal, ctx);

        pending++;
    }

    return false;
}

DEFINE_TEST(test_md5_single);

static void
test_md5_single(void)
{
    /* note: mem includes string trailing null byte */
    static const struct md_test md5_single_test[] = {
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, "test", "e2a3e68d23ce348b8f68b3079de3d4c9"),
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, "long line of text bla bla bla more text here yada yada", "18511ce4f220de4744390ca3ae72873f"),
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, big_blob_of_zeros, "ab893875d697a3145af5eed5309bee26"),
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, big_blob_of_chars, "9a36eacb09f8e98e103e9ee897f8e31c"),
        MD_TEST_SIZE("md5", SOL_STR_SLICE_EMPTY, "", 0, "d41d8cd98f00b204e9800998ecf8427e"),
        MD_TEST_END
    };

    init_big_blobs();

    sol_timeout_add(0, on_timeout_do_single, md5_single_test);
    sol_run();
}

DEFINE_TEST(test_sha512_single);

static void
test_sha512_single(void)
{
    /* note: mem includes string trailing null byte */
    static const struct md_test sha512_single_test[] = {
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, "test", "d55ced17163bf5386f2cd9ff21d6fd7fe576a915065c24744d09cfae4ec84ee1ef6ef11bfbc5acce3639bab725b50a1fe2c204f8c820d6d7db0df0ecbc49c5ca"),
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, "long line of text bla bla bla more text here yada yada", "33e8b361e2f1b1d015e3f661b72633c411b2b0f7bc253373875c570af92d79af38eac9f98f44af7fa32e46050d029200b7d33e7a76c3bc425aa74759fb97308a"),
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, big_blob_of_zeros, "6b65c0a1956ce18df2d271205f53274d2905c803d059a0801bf8331ccaa28a1d4842d3585dd9c2b01502a4be6664bde2e965b15fcfec981e85eed37c595cd6bc"),
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, big_blob_of_chars, "b8f8002d7512d979e65ae4244c6c86a13cfd9978f0c2d642f110e4377b87eb3168325f582acfb0974d1578b8a152798363446354e2750b14289dbb3f2e325e88"),
        MD_TEST_SIZE("sha512", SOL_STR_SLICE_EMPTY, "", 0, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"),
        MD_TEST_END
    };

    init_big_blobs();

    sol_timeout_add(0, on_timeout_do_single, sha512_single_test);
    sol_run();
}

DEFINE_TEST(test_multiple_single);

static void
test_multiple_single(void)
{
    /* note: mem includes string trailing null byte */
    static const struct md_test multiple_single_test[] = {
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, "test", "e2a3e68d23ce348b8f68b3079de3d4c9"),
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, big_blob_of_chars, "b8f8002d7512d979e65ae4244c6c86a13cfd9978f0c2d642f110e4377b87eb3168325f582acfb0974d1578b8a152798363446354e2750b14289dbb3f2e325e88"),
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, "long line of text bla bla bla more text here yada yada", "18511ce4f220de4744390ca3ae72873f"),
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, "long line of text bla bla bla more text here yada yada", "33e8b361e2f1b1d015e3f661b72633c411b2b0f7bc253373875c570af92d79af38eac9f98f44af7fa32e46050d029200b7d33e7a76c3bc425aa74759fb97308a"),
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, big_blob_of_zeros, "ab893875d697a3145af5eed5309bee26"),
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, "test", "d55ced17163bf5386f2cd9ff21d6fd7fe576a915065c24744d09cfae4ec84ee1ef6ef11bfbc5acce3639bab725b50a1fe2c204f8c820d6d7db0df0ecbc49c5ca"),
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, big_blob_of_chars, "9a36eacb09f8e98e103e9ee897f8e31c"),
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, big_blob_of_zeros, "6b65c0a1956ce18df2d271205f53274d2905c803d059a0801bf8331ccaa28a1d4842d3585dd9c2b01502a4be6664bde2e965b15fcfec981e85eed37c595cd6bc"),
        MD_TEST_END
    };

    init_big_blobs();

    sol_timeout_add(0, on_timeout_do_single, multiple_single_test);
    sol_run();
}


DEFINE_TEST(test_md5_chunked);

static void
test_md5_chunked(void)
{
    /* note: mem includes string trailing null byte */
    static const struct md_test md5_chunked_test[] = {
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, "test", "e2a3e68d23ce348b8f68b3079de3d4c9"),
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, "long line of text bla bla bla more text here yada yada", "18511ce4f220de4744390ca3ae72873f"),
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, big_blob_of_zeros, "ab893875d697a3145af5eed5309bee26"),
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, big_blob_of_chars, "9a36eacb09f8e98e103e9ee897f8e31c"),
        MD_TEST_END
    };

    init_big_blobs();

    sol_timeout_add(0, on_timeout_do_chunked, md5_chunked_test);
    sol_run();
}

DEFINE_TEST(test_sha512_chunked);

static void
test_sha512_chunked(void)
{
    /* note: mem includes string trailing null byte */
    static const struct md_test sha512_chunked_test[] = {
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, "test", "d55ced17163bf5386f2cd9ff21d6fd7fe576a915065c24744d09cfae4ec84ee1ef6ef11bfbc5acce3639bab725b50a1fe2c204f8c820d6d7db0df0ecbc49c5ca"),
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, "long line of text bla bla bla more text here yada yada", "33e8b361e2f1b1d015e3f661b72633c411b2b0f7bc253373875c570af92d79af38eac9f98f44af7fa32e46050d029200b7d33e7a76c3bc425aa74759fb97308a"),
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, big_blob_of_zeros, "6b65c0a1956ce18df2d271205f53274d2905c803d059a0801bf8331ccaa28a1d4842d3585dd9c2b01502a4be6664bde2e965b15fcfec981e85eed37c595cd6bc"),
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, big_blob_of_chars, "b8f8002d7512d979e65ae4244c6c86a13cfd9978f0c2d642f110e4377b87eb3168325f582acfb0974d1578b8a152798363446354e2750b14289dbb3f2e325e88"),
        MD_TEST_END
    };

    init_big_blobs();

    sol_timeout_add(0, on_timeout_do_chunked, sha512_chunked_test);
    sol_run();
}

DEFINE_TEST(test_multiple_chunked);

static void
test_multiple_chunked(void)
{
    /* note: mem includes string trailing null byte */
    static const struct md_test multiple_chunked_test[] = {
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, "test", "e2a3e68d23ce348b8f68b3079de3d4c9"),
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, big_blob_of_chars, "b8f8002d7512d979e65ae4244c6c86a13cfd9978f0c2d642f110e4377b87eb3168325f582acfb0974d1578b8a152798363446354e2750b14289dbb3f2e325e88"),
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, "long line of text bla bla bla more text here yada yada", "18511ce4f220de4744390ca3ae72873f"),
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, "long line of text bla bla bla more text here yada yada", "33e8b361e2f1b1d015e3f661b72633c411b2b0f7bc253373875c570af92d79af38eac9f98f44af7fa32e46050d029200b7d33e7a76c3bc425aa74759fb97308a"),
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, big_blob_of_zeros, "ab893875d697a3145af5eed5309bee26"),
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, "test", "d55ced17163bf5386f2cd9ff21d6fd7fe576a915065c24744d09cfae4ec84ee1ef6ef11bfbc5acce3639bab725b50a1fe2c204f8c820d6d7db0df0ecbc49c5ca"),
        MD_TEST("md5", SOL_STR_SLICE_EMPTY, big_blob_of_chars, "9a36eacb09f8e98e103e9ee897f8e31c"),
        MD_TEST("sha512", SOL_STR_SLICE_EMPTY, big_blob_of_zeros, "6b65c0a1956ce18df2d271205f53274d2905c803d059a0801bf8331ccaa28a1d4842d3585dd9c2b01502a4be6664bde2e965b15fcfec981e85eed37c595cd6bc"),
        MD_TEST_END
    };

    init_big_blobs();

    sol_timeout_add(0, on_timeout_do_chunked, multiple_chunked_test);
    sol_run();
}

DEFINE_TEST(test_feed_after_last);

static void
on_digest_ready_feed_after_last(void *data, struct sol_message_digest *handle, struct sol_blob *digest)
{
    struct sol_blob *blob;
    static char mem[] = "x";
    int r;

    blob = sol_blob_new(SOL_BLOB_TYPE_NOFREE, NULL, mem, sizeof(mem));
    ASSERT(blob != NULL);

    r = sol_message_digest_feed(handle, blob, true);
    ASSERT_INT_EQ(r, -EINVAL);

    sol_message_digest_del(handle);
    sol_blob_unref(blob);
    sol_quit();
}

static bool
on_timeout_feed_after_last(void *data)
{
    struct sol_message_digest_config cfg = {
        SOL_SET_API_VERSION(.api_version = SOL_MESSAGE_DIGEST_CONFIG_API_VERSION, )
        .algorithm = "md5",
        .on_digest_ready = on_digest_ready_feed_after_last,
    };
    struct sol_message_digest *md;
    struct sol_blob *blob;
    static char mem[] = "x";
    int r;

    blob = sol_blob_new(SOL_BLOB_TYPE_NOFREE, NULL, mem, sizeof(mem));
    ASSERT(blob != NULL);

    md = sol_message_digest_new(&cfg);
    ASSERT(md != NULL);

    r = sol_message_digest_feed(md, blob, true);
    ASSERT_INT_EQ(r, 0);

    sol_blob_unref(blob);
    return false;
}

static void
test_feed_after_last(void)
{
    sol_timeout_add(0, on_timeout_feed_after_last, NULL);
    sol_run();
}

TEST_MAIN();
