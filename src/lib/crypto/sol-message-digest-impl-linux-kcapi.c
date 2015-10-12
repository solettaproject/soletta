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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_alg.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef AF_ALG
#define AF_ALG 38
#endif
#ifndef SOL_ALG
#define SOL_ALG 279
#endif

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "message-digest");

#include "sol-crypto.h"
#include "sol-mainloop.h"
#include "sol-message-digest.h"
#include "sol-util.h"
#include "sol-vector.h"

/* BEGIN: these definitions come from kernel and are still not
 * exported as an userspace header. libkcapi exports it as cryptouser.h
 *
 * See https://github.com/smuellerDD/libkcapi/blob/master/lib/cryptouser.h
 */
#define CRYPTO_MAX_ALG_NAME 64 /* from kernel */
enum {
    CRYPTO_MSG_BASE = 0x10,
    CRYPTO_MSG_NEWALG = 0x10,
    CRYPTO_MSG_DELALG,
    CRYPTO_MSG_UPDATEALG,
    CRYPTO_MSG_GETALG,
    CRYPTO_MSG_DELRNG,
    __CRYPTO_MSG_MAX
#define CRYPTO_MSG_MAX (__CRYPTO_MSG_MAX - 1)
};
#define CR_RTA(x) ((struct rtattr *)(((char *)(x)) + NLMSG_ALIGN(sizeof(struct crypto_user_alg))))

#define CRYPTO_MAX_NAME CRYPTO_MAX_ALG_NAME

/* Netlink message attributes.  */
enum crypto_attr_type_t {
    CRYPTOCFGA_UNSPEC,
    CRYPTOCFGA_PRIORITY_VAL,     /* __u32 */
    CRYPTOCFGA_REPORT_LARVAL,    /* struct crypto_report_larval */
    CRYPTOCFGA_REPORT_HASH,      /* struct crypto_report_hash */
    CRYPTOCFGA_REPORT_BLKCIPHER, /* struct crypto_report_blkcipher */
    CRYPTOCFGA_REPORT_AEAD,      /* struct crypto_report_aead */
    CRYPTOCFGA_REPORT_COMPRESS,  /* struct crypto_report_comp */
    CRYPTOCFGA_REPORT_RNG,       /* struct crypto_report_rng */
    CRYPTOCFGA_REPORT_CIPHER,    /* struct crypto_report_cipher */
    CRYPTOCFGA_REPORT_AKCIPHER,  /* struct crypto_report_akcipher */
    __CRYPTOCFGA_MAX
#define CRYPTOCFGA_MAX (__CRYPTOCFGA_MAX - 1)
};

struct crypto_user_alg {
    char cru_name[CRYPTO_MAX_ALG_NAME];
    char cru_driver_name[CRYPTO_MAX_ALG_NAME];
    char cru_module_name[CRYPTO_MAX_ALG_NAME];
    __u32 cru_type;
    __u32 cru_mask;
    __u32 cru_refcnt;
    __u32 cru_flags;
};

struct crypto_report_hash {
    char type[CRYPTO_MAX_NAME];
    unsigned int blocksize;
    unsigned int digestsize;
};
/* END */


struct sol_message_digest_algorithm_info {
    char name[CRYPTO_MAX_ALG_NAME];
    size_t digest_size;
};
static struct sol_vector _algorithms_info = SOL_VECTOR_INIT(struct sol_message_digest_algorithm_info);

int
sol_message_digest_init(void)
{
    struct sockaddr_alg sa;

    SOL_LOG_INTERNAL_INIT_ONCE;

    assert(sizeof(sa.salg_name) == CRYPTO_MAX_ALG_NAME);

    return 0;
}

void
sol_message_digest_shutdown(void)
{
    sol_vector_clear(&_algorithms_info);
}

static bool
_sol_message_digest_fill_algorithm_info(struct sol_message_digest_algorithm_info *info)
{
    struct {
        struct nlmsghdr hdr;
        struct crypto_user_alg cua;
    } req;
    struct sockaddr_nl snl = {
        .nl_family = AF_NETLINK,
    };
    struct iovec iov;
    struct msghdr msg;
    bool ret = false;
    int fd;

    fd =  socket(AF_NETLINK, SOCK_RAW, NETLINK_CRYPTO);
    if (fd < 0) {
        SOL_WRN("socket(AF_NETLINK, SOCK_RAW, NETLINK_CRYPTO): %s",
            sol_util_strerrora(errno));
        return false;
    }

    if (bind(fd, (struct sockaddr *)&snl, sizeof(snl)) < 0) {
        SOL_WRN("bind(%d, {AF_NETLINK}): %s",
            fd, sol_util_strerrora(errno));
        goto error_bind;
    }

    memset(&req, 0, sizeof(req));
    req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(req.cua));
    req.hdr.nlmsg_flags = NLM_F_REQUEST;
    req.hdr.nlmsg_type = CRYPTO_MSG_GETALG;
    memcpy(req.cua.cru_name, info->name, strlen(info->name));

    iov.iov_base = (void *)&req.hdr;
    iov.iov_len = req.hdr.nlmsg_len;

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &snl;
    msg.msg_namelen = sizeof(snl);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (sendmsg(fd, &msg, 0) < 0) {
        SOL_WRN("sendmsg(%d, {AF_NETLINK, iov=%p}): %s",
            fd, &iov, sol_util_strerrora(errno));
        goto error_sendmsg;
    }

    while (!ret) {
        char buf[4096];
        struct nlmsghdr *h = (struct nlmsghdr *)buf;
        ssize_t len;

        iov.iov_base = buf;
        iov.iov_len = sizeof(buf);
        len  = recvmsg(fd, &msg, 0);
        if (len < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;

            SOL_WRN("recvmsg(%d, {AF_NETLINK, iov=%p}): %s",
                fd, &iov, sol_util_strerrora(errno));
            goto error_recvmsg;
        } else if (len == 0) {
            SOL_WRN("recvmsg(%d, {AF_NETLINK, iov=%p}): no data",
                fd, &iov);
            break;
        }

        len = h->nlmsg_len;
        if (h->nlmsg_type == NLMSG_ERROR) {
            SOL_WRN("read_netlink: Message is an error");
            goto error_recvmsg;
        }

        if (h->nlmsg_type == CRYPTO_MSG_GETALG) {
            struct crypto_user_alg *cua = NLMSG_DATA(h);
            struct rtattr *rta;

            len -= NLMSG_SPACE(sizeof(*cua));
            if (len < 0) {
                SOL_WRN("read_netlink: message is too small: %zd", len);
                goto error_recvmsg;
            }

            for (rta = CR_RTA(cua);
                RTA_OK(rta, len) && rta->rta_type <= CRYPTOCFGA_MAX;
                rta = RTA_NEXT(rta, len)) {
                struct crypto_report_hash *rsh = RTA_DATA(rta);

                if (rta->rta_type == CRYPTOCFGA_REPORT_HASH) {
                    info->digest_size = rsh->digestsize;
                    SOL_DBG("message digest algorithm \"%s\" digest_size=%zd",
                        info->name, info->digest_size);
                    ret = true;
                    break;
                }
            }
        }
    }

error_sendmsg:
error_recvmsg:
error_bind:
    close(fd);

    return ret;
}

static const struct sol_message_digest_algorithm_info *
_sol_message_digest_get_algorithm_info(const char *name)
{
    struct sol_message_digest_algorithm_info *info;
    uint16_t i;
    size_t namelen;

    namelen = strlen(name);
    SOL_INT_CHECK(namelen, >= CRYPTO_MAX_ALG_NAME, NULL);

    SOL_VECTOR_FOREACH_IDX (&_algorithms_info, info, i) {
        if (streq(info->name, name)) {
            SOL_DBG("cached algorithm \"%s\" info digest_size=%zd",
                info->name, info->digest_size);
            return info;
        }
    }

    info = sol_vector_append(&_algorithms_info);
    SOL_NULL_CHECK(info, NULL);
    memcpy(info->name, name, namelen + 1);

    if (!_sol_message_digest_fill_algorithm_info(info)) {
        sol_vector_del(&_algorithms_info, _algorithms_info.len - 1);
        return NULL;
    }

    return info;
}

#if defined(PTHREAD) && defined(WORKER_THREAD)
#define MESSAGE_DIGEST_USE_THREAD
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
#ifdef MESSAGE_DIGEST_USE_THREAD
    struct sol_worker_thread *thread; /* current kcapi is not poll() friendly, it won't report IN/OUT, thus we use a thread */
    struct sol_vector pending_dispatch;
    int thread_pipe[2];
    pthread_mutex_t lock;
#else
    struct sol_timeout *timer; /* current kcapi is not poll() friendly, it won't report IN/OUT, thus we use a timer to poll */
#endif
    struct sol_vector pending_feed;
    struct sol_blob *digest;
    size_t digest_offset; /* allows partial digest receive */
    size_t digest_size;
    uint32_t refcnt;
    int fd;
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
            SOL_WRN("handle %p fd=%d couldn't send thread command %c: %s",
                handle, handle->fd, cmd, sol_util_strerrora(errno));
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
            SOL_WRN("handle %p fd=%d couldn't receive thread command: %s",
                handle, handle->fd, sol_util_strerrora(errno));
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
    if (pipe2(handle->thread_pipe, O_CLOEXEC) < 0)
        return errno;

    sol_vector_init(&handle->pending_dispatch,
        sizeof(struct sol_message_digest_pending_dispatch));
    errno = pthread_mutex_init(&handle->lock, NULL);
    if (errno) {
        close(handle->thread_pipe[0]);
        close(handle->thread_pipe[1]);
    }
    return errno;
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

SOL_API struct sol_message_digest *
sol_message_digest_new(const struct sol_message_digest_config *config)
{
    const struct sol_message_digest_algorithm_info *info;
    struct sol_message_digest *handle;
    struct sockaddr_alg sa;
    int bfd, errno_bkp;

    errno = EINVAL;
    SOL_NULL_CHECK(config, NULL);
    SOL_NULL_CHECK(config->on_digest_ready, NULL);
    SOL_NULL_CHECK(config->algorithm, NULL);

    if (config->api_version != SOL_MESSAGE_DIGEST_CONFIG_API_VERSION) {
        SOL_WRN("sol_message_digest_config->api_version=%hu, "
            "expected version is %hu.",
            config->api_version, SOL_MESSAGE_DIGEST_CONFIG_API_VERSION);
        return NULL;
    }

    if (strlen(config->algorithm) + 1 >= sizeof(sa.salg_name)) {
        SOL_WRN("algorithm \"%s\" is too long\n", config->algorithm);
        return NULL;
    }
    memset(&sa, 0, sizeof(sa));
    memcpy(sa.salg_name, config->algorithm, strlen(config->algorithm) + 1);
    memcpy(sa.salg_type, "hash", sizeof("hash"));
    sa.salg_family = AF_ALG;

    bfd = socket(AF_ALG, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (bfd < 0) {
        SOL_WRN("socket(AF_ALG, SOCK_SEQPACKET): %s",
            sol_util_strerrora(errno));
        return NULL;
    }

    if (bind(bfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        SOL_WRN("bind(%d, {AF_ALG, hash, \"%s\"}): %s",
            bfd, config->algorithm, sol_util_strerrora(errno));
        goto error_bfd;
    }

    if (config->key.len > 0) {
        int r = setsockopt(bfd, SOL_ALG, ALG_SET_KEY,
            config->key.data, config->key.len);
        if (r < 0) {
            SOL_WRN("algorithm \"%s\", failed to set key len=%zd \"%.*s\"",
                config->algorithm, config->key.len,
                SOL_STR_SLICE_PRINT(config->key));
            goto error_bfd;
        }
    }

    handle = calloc(1, sizeof(struct sol_message_digest));
    SOL_NULL_CHECK_GOTO(handle, error_bfd);

    handle->refcnt = 1;
    handle->on_digest_ready = config->on_digest_ready;
    handle->on_feed_done = config->on_feed_done;
    handle->data = config->data;
    sol_vector_init(&handle->pending_feed,
        sizeof(struct sol_message_digest_pending_feed));

    info = _sol_message_digest_get_algorithm_info(config->algorithm);
    SOL_NULL_CHECK_GOTO(info, error_info);

    handle->digest_size = info->digest_size;

    handle->fd = accept4(bfd, NULL, 0, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (handle->fd < 0) {
        SOL_WRN("algorithm \"%s\" failed accept4(%d): %s",
            config->algorithm, bfd, sol_util_strerrora(errno));
        goto error_accept;
    }

    errno = _sol_message_digest_thread_init(handle);
    if (errno)
        goto error_thread_init;

    SOL_DBG("handle %p fd=%d algorithm=\"%s\"",
        handle, handle->fd, config->algorithm);

    errno = 0;
    return handle;

error_thread_init:
    errno_bkp = errno;
    close(handle->fd);
    errno = errno_bkp;

error_accept:
error_info:
    errno_bkp = errno;
    free(handle);
    errno = errno_bkp;

error_bfd:
    errno_bkp = errno;
    close(bfd);
    errno = errno_bkp;
    return NULL;
}

static void
_sol_message_digest_free(struct sol_message_digest *handle)
{
    struct sol_message_digest_pending_feed *pf;
    uint16_t i;

    SOL_DBG("free handle %p fd=%d, pending_feed=%hu, digest=%p",
        handle, handle->fd, handle->pending_feed.len, handle->digest);

    _sol_message_digest_thread_fini(handle);

    SOL_VECTOR_FOREACH_IDX (&handle->pending_feed, pf, i) {
        sol_blob_unref(pf->blob);
    }
    sol_vector_clear(&handle->pending_feed);

    if (handle->digest)
        sol_blob_unref(handle->digest);

    close(handle->fd);
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

    SOL_DBG("del handle %p fd=%d, refcnt=%" PRIu32
        ", pending_feed=%hu, digest=%p",
        handle, handle->fd, handle->refcnt,
        handle->pending_feed.len, handle->digest);
    _sol_message_digest_unref(handle);
}

static void
_sol_message_digest_setup_receive_digest(struct sol_message_digest *handle)
{
    void *mem;

    if (handle->digest) {
        SOL_WRN("handle %p fd=%d already have a digest to be received (%p).",
            handle, handle->fd, handle->digest);
        return;
    }

    mem = malloc(handle->digest_size);
    SOL_NULL_CHECK(mem);

    handle->digest = sol_blob_new(SOL_BLOB_TYPE_DEFAULT, NULL,
        mem, handle->digest_size);
    SOL_NULL_CHECK_GOTO(handle->digest, error);

    handle->digest_offset = 0;

    SOL_DBG("handle %p fd=%d to receive digest of %zd bytes at blob %p mem=%p",
        handle, handle->fd, handle->digest_size,
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
    handle->digest_offset = 0;

end:
    _sol_message_digest_unlock(handle);
    sol_worker_thread_feedback(handle->thread);

#else
    _sol_message_digest_ref(handle);

    handle->on_digest_ready((void *)handle->data, handle, handle->digest);

    sol_blob_unref(handle->digest);
    handle->digest = NULL;
    handle->digest_offset = 0;

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
    size_t len;
    ssize_t n;
    int flags;

    _sol_message_digest_lock(handle);
    pf = sol_vector_get(&handle->pending_feed, 0);
    SOL_NULL_CHECK_GOTO(pf, error);

    input = pf->blob;
    mem = input->mem;
    mem += pf->offset;
    len = input->size - pf->offset;
    is_last = pf->is_last;
    flags = is_last ? 0 : MSG_MORE;

    _sol_message_digest_unlock(handle);

    /* TODO: change this to sendmsg() using iov with all blobs.  then
     * check the return to see which blobs were consumed (they may be
     * partial), and adjust pending_feed accordingly.
     */

    n = send(handle->fd, mem, len, flags);
    SOL_DBG("handle %p fd=%d sent mem=%p of %zd bytes (pending=%hu) flags=%#x:"
        " %zd",
        handle, handle->fd, mem, len, handle->pending_feed.len, flags, n);
    if (n >= 0) {
        if ((size_t)n < len) { /* not fully sent, need to try again later */
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

    } else if (errno != EAGAIN && errno != EINTR) {
        SOL_WRN("couldn't feed handle %p fd=%d with %p of %zd bytes: %s",
            handle, handle->fd, mem, len, sol_util_strerrora(errno));
    }

    return;

error:
    _sol_message_digest_unlock(handle);
    SOL_WRN("no pending feed for handle %p fd=%d", handle, handle->fd);
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

    n = recv(handle->fd, mem, len, 0);
    SOL_DBG("handle %p fd=%d recv mem=%p of %zd bytes: %zd",
        handle, handle->fd, mem, len, n);
    if (n >= 0) {
        handle->digest_offset += n;
        if (handle->digest_offset < handle->digest->size) /* more to do... */
            return;

        _sol_message_digest_report_digest_ready(handle);

    } else if (errno != EAGAIN && errno != EINTR) {
        SOL_WRN("couldn't recv digest handle %p fd=%d with %p of %zd bytes: %s",
            handle, handle->fd, mem, len, sol_util_strerrora(errno));
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

    SOL_DBG("handle %p fd=%d pending=%hu, digest=%p",
        handle, handle->fd, handle->pending_feed.len, handle->digest);

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
        return 0;

    _sol_message_digest_ref(handle);
    handle->thread = sol_worker_thread_new(&spec);
    SOL_NULL_CHECK_GOTO(handle->thread, error);

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

    SOL_DBG("handle %p fd=%d blob=%p (%zd bytes), pending %hu",
        handle, handle->fd, input, input->size, handle->pending_feed.len);

    return 0;

error:
    sol_blob_unref(input);
    sol_vector_del(&handle->pending_feed, handle->pending_feed.len - 1);

error_append:
    _sol_message_digest_unlock(handle);

    return -ENOMEM;
}
