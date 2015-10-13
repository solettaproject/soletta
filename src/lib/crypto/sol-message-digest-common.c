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
#include <unistd.h>
#include <fcntl.h>

#include "sol-message-digest-common.h"

SOL_LOG_INTERNAL_DECLARE(_sol_message_digest_common_log_domain, "message-digest");

#include "sol-crypto.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-vector.h"

int
sol_message_digest_common_init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;

    return 0;
}

void
sol_message_digest_common_shutdown(void)
{
}

#if defined(PTHREAD) && defined(WORKER_THREAD)
#define MESSAGE_DIGEST_USE_THREAD
#endif

#if !defined(MESSAGE_DIGEST_USE_THREAD) && !defined(MESSAGE_DIGEST_MAX_FEED_BLOCK_SIZE)
#define MESSAGE_DIGEST_MAX_FEED_BLOCK_SIZE 40960
#endif


#ifdef MESSAGE_DIGEST_USE_THREAD
#include <pthread.h>
#include "sol-worker-thread.h"
#endif

struct sol_message_digest_pending_feed {
    struct sol_blob *blob;
    size_t offset;
    bool is_last;
};

#ifdef MESSAGE_DIGEST_USE_THREAD
struct sol_message_digest_pending_dispatch {
    struct sol_blob *blob;
    bool is_digest;
};
#endif

struct sol_message_digest {
    void (*on_digest_ready)(void *data, struct sol_message_digest *handle, struct sol_blob *output);
    void (*on_feed_done)(void *data, struct sol_message_digest *handle, struct sol_blob *input);
    const void *data;
    const struct sol_message_digest_common_ops *ops;
#ifdef MESSAGE_DIGEST_USE_THREAD
    struct sol_worker_thread *thread; /* current kcapi is not poll() friendly, it won't report IN/OUT, thus we use a thread */
    struct sol_vector pending_dispatch;
    int thread_pipe[2];
    pthread_mutex_t lock;
#else
    struct sol_timeout *timer; /* current kcapi is not poll() friendly, it won't report IN/OUT, thus we use a timer to poll */
#endif
    struct sol_blob *digest;
    struct sol_vector pending_feed;
    size_t digest_offset;
    size_t digest_size;
    uint32_t refcnt;
    bool deleted;
};

static void
_sol_message_digest_lock(struct sol_message_digest *handle)
{
#ifdef MESSAGE_DIGEST_USE_THREAD
    pthread_mutex_lock(&handle->lock);
#endif
}

static void
_sol_message_digest_unlock(struct sol_message_digest *handle)
{
#ifdef MESSAGE_DIGEST_USE_THREAD
    pthread_mutex_unlock(&handle->lock);
#endif
}

#ifdef MESSAGE_DIGEST_USE_THREAD
static void
_sol_message_digest_thread_send(struct sol_message_digest *handle, char cmd)
{
    while (write(handle->thread_pipe[1], &cmd, 1) != 1) {
        if (errno != EAGAIN && errno != EINTR) {
            SOL_WRN("handle %p couldn't send thread command %c: %s",
                handle, cmd, sol_util_strerrora(errno));
            return;
        }
    }
}

static char
_sol_message_digest_thread_recv(struct sol_message_digest *handle)
{
    char cmd;

    while (read(handle->thread_pipe[0], &cmd, 1) != 1) {
        if (errno != EAGAIN && errno != EINTR) {
            SOL_WRN("handle %p couldn't receive thread command: %s",
                handle, sol_util_strerrora(errno));
            return 0;
        }
    }

    return cmd;
}
#endif

static int
_sol_message_digest_thread_init(struct sol_message_digest *handle)
{
#ifdef MESSAGE_DIGEST_USE_THREAD
    int err;
    if (pipe2(handle->thread_pipe, O_CLOEXEC) < 0)
        return errno;

    sol_vector_init(&handle->pending_dispatch,
        sizeof(struct sol_message_digest_pending_dispatch));
    err = pthread_mutex_init(&handle->lock, NULL);
    if (err) {
        close(handle->thread_pipe[0]);
        close(handle->thread_pipe[1]);
    }
    return err;
#else
    return 0;
#endif
}

static void
_sol_message_digest_thread_fini(struct sol_message_digest *handle)
{
#ifdef MESSAGE_DIGEST_USE_THREAD
    struct sol_message_digest_pending_dispatch *pd;
    uint16_t i;

    _sol_message_digest_thread_send(handle, 'c');
    close(handle->thread_pipe[0]);
    close(handle->thread_pipe[1]);

    if (handle->thread)
        sol_worker_thread_cancel(handle->thread);
    pthread_mutex_destroy(&handle->lock);

    SOL_VECTOR_FOREACH_IDX (&handle->pending_dispatch, pd, i) {
        sol_blob_unref(pd->blob);
    }
    sol_vector_clear(&handle->pending_dispatch);
#else
    if (handle->timer)
        sol_timeout_del(handle->timer);
#endif
}

static void
_sol_message_digest_thread_stop(struct sol_message_digest *handle)
{
#ifdef MESSAGE_DIGEST_USE_THREAD
    _sol_message_digest_thread_send(handle, 'c');
#endif
}

void *
sol_message_digest_common_get_context(const struct sol_message_digest *handle)
{
    size_t padding = sizeof(struct sol_message_digest) % sizeof(void *);

    if (padding > 0)
        padding = sizeof(void *) - padding;

    return (char *)handle + sizeof(struct sol_message_digest) + padding;
}

struct sol_message_digest *
sol_message_digest_common_new(const struct sol_message_digest_common_new_params params)
{
    const struct sol_message_digest_config *config = params.config;
    struct sol_message_digest *handle;
    size_t padding;
    int errno_bkp;

    SOL_NULL_CHECK(params.ops, NULL);
    SOL_NULL_CHECK(params.ops->feed, NULL);
    SOL_NULL_CHECK(params.ops->read_digest, NULL);
    SOL_NULL_CHECK(params.ops->cleanup, NULL);
    SOL_INT_CHECK(params.digest_size, == 0, NULL);

    padding = sizeof(struct sol_message_digest) % sizeof(void *);
    if (padding > 0)
        padding = sizeof(void *) - padding;

    handle = calloc(1, sizeof(struct sol_message_digest) + padding + params.context_size);
    SOL_NULL_CHECK(handle, NULL);

    if (params.context_template) {
        void *context = (char *)handle + sizeof(struct sol_message_digest) + padding;
        memcpy(context, params.context_template, params.context_size);
    }

    handle->ops = params.ops;

    handle->refcnt = 1;
    handle->on_digest_ready = config->on_digest_ready;
    handle->on_feed_done = config->on_feed_done;
    handle->data = config->data;
    sol_vector_init(&handle->pending_feed,
        sizeof(struct sol_message_digest_pending_feed));

    handle->digest_size = params.digest_size;

    errno = _sol_message_digest_thread_init(handle);
    if (errno)
        goto error;

    SOL_DBG("handle %p algorithm=\"%s\"", handle, config->algorithm);

    errno = 0;
    return handle;

error:
    errno_bkp = errno;
    free(handle);
    errno = errno_bkp;
    return NULL;
}

static void
_sol_message_digest_free(struct sol_message_digest *handle)
{
    struct sol_message_digest_pending_feed *pf;
    uint16_t i;

    SOL_DBG("free handle %p pending_feed=%hu, digest=%p",
        handle, handle->pending_feed.len, handle->digest);

    _sol_message_digest_thread_fini(handle);

    SOL_VECTOR_FOREACH_IDX (&handle->pending_feed, pf, i) {
        sol_blob_unref(pf->blob);
    }
    sol_vector_clear(&handle->pending_feed);

    if (handle->digest)
        sol_blob_unref(handle->digest);

    handle->ops->cleanup(handle);

    free(handle);
}

static inline void
_sol_message_digest_unref(struct sol_message_digest *handle)
{
    handle->refcnt--;
    if (handle->refcnt == 0)
        _sol_message_digest_free(handle);
}

static inline void
_sol_message_digest_ref(struct sol_message_digest *handle)
{
    handle->refcnt++;
}

SOL_API void
sol_message_digest_del(struct sol_message_digest *handle)
{
    SOL_NULL_CHECK(handle);
    SOL_EXP_CHECK(handle->deleted);
    SOL_INT_CHECK(handle->refcnt, < 1);

    handle->deleted = true;

    _sol_message_digest_thread_stop(handle);

    SOL_DBG("del handle %p refcnt=%" PRIu32
        ", pending_feed=%hu, digest=%p",
        handle, handle->refcnt,
        handle->pending_feed.len, handle->digest);
    _sol_message_digest_unref(handle);
}

static void
_sol_message_digest_setup_receive_digest(struct sol_message_digest *handle)
{
    void *mem;

    if (handle->digest) {
        SOL_WRN("handle %p already have a digest to be received (%p).",
            handle, handle->digest);
        return;
    }

    mem = malloc(handle->digest_size);
    SOL_NULL_CHECK(mem);

    handle->digest = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL,
        mem, handle->digest_size);
    SOL_NULL_CHECK_GOTO(handle->digest, error);

    SOL_DBG("handle %p to receive digest of %zd bytes at blob %p mem=%p",
        handle, handle->digest_size,
        handle->digest, handle->digest->mem);

    return;

error:
    free(mem);
}

static void
_sol_message_digest_report_feed_blob(struct sol_message_digest *handle, struct sol_blob *input)
{
#ifdef MESSAGE_DIGEST_USE_THREAD
    struct sol_message_digest_pending_dispatch *pd;

    _sol_message_digest_lock(handle);

    pd = sol_vector_append(&handle->pending_dispatch);
    SOL_NULL_CHECK_GOTO(pd, error);
    pd->blob = input;
    pd->is_digest = false;

    _sol_message_digest_unlock(handle);
    sol_worker_thread_feedback(handle->thread);
    return;

error:
    _sol_message_digest_unlock(handle);
    sol_blob_unref(input); /* this may cause problems if main thread changes blob refcnt */

#else
    _sol_message_digest_ref(handle);

    if (handle->on_feed_done)
        handle->on_feed_done((void *)handle->data, handle, input);

    sol_blob_unref(input);
    _sol_message_digest_unref(handle);
#endif
}

static void
_sol_message_digest_report_digest_ready(struct sol_message_digest *handle)
{
#ifdef MESSAGE_DIGEST_USE_THREAD
    struct sol_message_digest_pending_dispatch *pd;

    _sol_message_digest_lock(handle);

    pd = sol_vector_append(&handle->pending_dispatch);
    SOL_NULL_CHECK_GOTO(pd, end);
    pd->blob = handle->digest;
    pd->is_digest = true;

    handle->digest = NULL;

end:
    _sol_message_digest_unlock(handle);
    sol_worker_thread_feedback(handle->thread);

#else
    _sol_message_digest_ref(handle);

    handle->on_digest_ready((void *)handle->data, handle, handle->digest);

    sol_blob_unref(handle->digest);
    handle->digest = NULL;

    _sol_message_digest_unref(handle);
#endif
}

static void
_sol_message_digest_feed_blob(struct sol_message_digest *handle)
{
    struct sol_message_digest_pending_feed *pf;
    struct sol_blob *input;
    const uint8_t *mem;
    bool is_last;
    size_t offset, len;
    ssize_t n;

    _sol_message_digest_lock(handle);
    pf = sol_vector_get(&handle->pending_feed, 0);
    SOL_NULL_CHECK_GOTO(pf, error);

    input = pf->blob;
    mem = input->mem;
    offset = pf->offset;
    mem += offset;
    len = input->size - offset;
    is_last = pf->is_last;

#ifdef MESSAGE_DIGEST_MAX_FEED_BLOCK_SIZE
    if (len > MESSAGE_DIGEST_MAX_FEED_BLOCK_SIZE) {
        len = MESSAGE_DIGEST_MAX_FEED_BLOCK_SIZE;
        if (is_last)
            is_last = false;
    }
#endif

    _sol_message_digest_unlock(handle);

    n = handle->ops->feed(handle, mem, len, is_last);
    SOL_DBG("handle %p feed mem=%p (%zd bytes) (pending=%hu) is_last=%hhu:"
        " %zd bytes",
        handle, mem, len, handle->pending_feed.len, is_last, n);
    if (n >= 0) {
        if (offset + n < input->size) { /* not fully sent, need to try again later */
            /* fetch first pending again as it's a sol_vector and
             * calls to sol_message_digest_feed() may realloc() the vector,
             * resulting in new pointer for the first element.
             */
            _sol_message_digest_lock(handle);
            pf = sol_vector_get(&handle->pending_feed, 0);
            SOL_NULL_CHECK_GOTO(pf, error);
            pf->offset += n;
            _sol_message_digest_unlock(handle);
            return;
        }

        if (is_last)
            _sol_message_digest_setup_receive_digest(handle);

        _sol_message_digest_lock(handle);
        sol_vector_del(&handle->pending_feed, 0);
        _sol_message_digest_unlock(handle);

        _sol_message_digest_report_feed_blob(handle, input);

    } else {
        errno = -n;
        if (errno != EAGAIN && errno != EINTR) {
            SOL_WRN("couldn't feed handle %p with %p of %zd bytes: %s",
                handle, mem, len, sol_util_strerrora(errno));
        }
    }

    return;

error:
    _sol_message_digest_unlock(handle);
    SOL_WRN("no pending feed for handle %p", handle);
}

static void
_sol_message_digest_receive_digest(struct sol_message_digest *handle)
{
    uint8_t *mem;
    size_t len;
    ssize_t n;

    mem = handle->digest->mem;
    mem += handle->digest_offset;
    len = handle->digest->size - handle->digest_offset;

    n = handle->ops->read_digest(handle, mem, len);
    SOL_DBG("handle %p read digest mem=%p (%zd bytes): %zd bytes",
        handle, mem, len, n);
    if (n >= 0) {
        handle->digest_offset += n;
        if (handle->digest_offset < handle->digest->size) /* more to do... */
            return;

        _sol_message_digest_report_digest_ready(handle);

    } else {
        errno = -n;
        if (errno != EAGAIN && errno != EINTR) {
            SOL_WRN("couldn't recv digest handle %p with %p of %zd bytes: %s",
                handle, mem, len, sol_util_strerrora(errno));
        }
    }
}

#ifdef MESSAGE_DIGEST_USE_THREAD

static struct sol_blob *
_sol_message_digest_peek_first_pending_blob(struct sol_message_digest *handle)
{
    struct sol_message_digest_pending_feed *pf;
    struct sol_blob *blob = NULL;

    _sol_message_digest_lock(handle);
    if (handle->pending_feed.len) {
        pf = sol_vector_get(&handle->pending_feed, 0);
        if (pf)
            blob = pf->blob;
    }
    _sol_message_digest_unlock(handle);

    return blob;
}

static bool
_sol_message_digest_thread_iterate(void *data)
{
    struct sol_message_digest *handle = data;
    struct sol_blob *current = NULL;
    char cmd;

    cmd = _sol_message_digest_thread_recv(handle);
    if (cmd == 'c' || cmd == 0)
        return false;

    current = _sol_message_digest_peek_first_pending_blob(handle);
    while (current && !sol_worker_thread_cancel_check(handle->thread)) {
        struct sol_blob *blob;

        _sol_message_digest_feed_blob(handle);

        blob = _sol_message_digest_peek_first_pending_blob(handle);
        if (blob != current)
            break;
    }

    while (handle->digest && !sol_worker_thread_cancel_check(handle->thread))
        _sol_message_digest_receive_digest(handle);

    return true;
}

static void
_sol_message_digest_thread_finished(void *data)
{
    struct sol_message_digest *handle = data;

    handle->thread = NULL;
    _sol_message_digest_unref(handle);
}

static void
_sol_message_digest_thread_feedback(void *data)
{
    struct sol_message_digest *handle = data;
    struct sol_message_digest_pending_dispatch *pd;
    struct sol_vector v;
    uint16_t i;

    _sol_message_digest_lock(handle);
    v = handle->pending_dispatch;
    sol_vector_init(&handle->pending_dispatch,
        sizeof(struct sol_message_digest_pending_dispatch));
    _sol_message_digest_unlock(handle);

    _sol_message_digest_ref(handle);

    SOL_VECTOR_FOREACH_IDX (&v, pd, i) {
        if (!handle->deleted) {
            if (pd->is_digest)
                handle->on_digest_ready((void *)handle->data, handle, pd->blob);
            else if (handle->on_feed_done)
                handle->on_feed_done((void *)handle->data, handle, pd->blob);
        }
        sol_blob_unref(pd->blob);
    }

    _sol_message_digest_unref(handle);

    sol_vector_clear(&v);
}

#else
static bool
_sol_message_digest_on_timer(void *data)
{
    struct sol_message_digest *handle = data;
    bool ret;

    SOL_DBG("handle %p pending=%hu, digest=%p",
        handle, handle->pending_feed.len, handle->digest);

    _sol_message_digest_ref(handle);

    if (handle->pending_feed.len > 0)
        _sol_message_digest_feed_blob(handle);

    if (handle->digest)
        _sol_message_digest_receive_digest(handle);

    ret = (handle->pending_feed.len > 0 || handle->digest);
    if (!ret)
        handle->timer = NULL;

    _sol_message_digest_unref(handle);
    return ret;
}
#endif

static int
_sol_message_digest_thread_start(struct sol_message_digest *handle)
{
#ifdef MESSAGE_DIGEST_USE_THREAD
    struct sol_worker_thread_spec spec = {
        .api_version = SOL_WORKER_THREAD_SPEC_API_VERSION,
        .data = handle,
        .iterate = _sol_message_digest_thread_iterate,
        .finished = _sol_message_digest_thread_finished,
        .feedback = _sol_message_digest_thread_feedback
    };

    if (handle->thread)
        goto end;

    _sol_message_digest_ref(handle);
    handle->thread = sol_worker_thread_new(&spec);
    SOL_NULL_CHECK_GOTO(handle->thread, error);

end:
    _sol_message_digest_thread_send(handle, 'a');

    return 0;

error:
    _sol_message_digest_unref(handle);
    return -ENOMEM;

#else
    if (handle->timer)
        return 0;

    handle->timer = sol_timeout_add(0, _sol_message_digest_on_timer, handle);
    SOL_NULL_CHECK(handle->timer, -ENOMEM);

    return 0;
#endif
}

SOL_API int
sol_message_digest_feed(struct sol_message_digest *handle, struct sol_blob *input, bool is_last)
{
    struct sol_message_digest_pending_feed *pf;
    int r;

    SOL_NULL_CHECK(handle, -EINVAL);
    SOL_EXP_CHECK(handle->deleted, -EINVAL);
    SOL_INT_CHECK(handle->refcnt, < 1, -EINVAL);
    SOL_NULL_CHECK(input, -EINVAL);

    _sol_message_digest_lock(handle);
    pf = sol_vector_append(&handle->pending_feed);
    SOL_NULL_CHECK_GOTO(pf, error_append);

    pf->blob = sol_blob_ref(input);
    pf->offset = 0;
    pf->is_last = is_last;

    r = _sol_message_digest_thread_start(handle);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    _sol_message_digest_unlock(handle);

    SOL_DBG("handle %p blob=%p (%zd bytes), pending %hu",
        handle, input, input->size, handle->pending_feed.len);

    return 0;

error:
    sol_blob_unref(input);
    sol_vector_del(&handle->pending_feed, handle->pending_feed.len - 1);

error_append:
    _sol_message_digest_unlock(handle);

    return -ENOMEM;
}
