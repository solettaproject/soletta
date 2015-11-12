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

#include "sol-message-digest-common.h"
#include "sol-crypto.h"
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

    assert(sizeof(sa.salg_name) == CRYPTO_MAX_ALG_NAME);

    return sol_message_digest_common_init();
}

void
sol_message_digest_shutdown(void)
{
    sol_message_digest_common_shutdown();

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

    fd =  socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_CRYPTO);
    if (fd < 0) {
        SOL_WRN("socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_CRYPTO): %s",
            sol_util_strerrora(errno));
        return false;
    }

    if (bind(fd, (struct sockaddr *)&snl, sizeof(snl)) < 0) {
        SOL_WRN("bind(%d, {AF_NETLINK}): %s",
            fd, sol_util_strerrora(errno));
        goto error;
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
        goto error;
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
            goto error;
        } else if (len == 0) {
            SOL_WRN("recvmsg(%d, {AF_NETLINK, iov=%p}): no data",
                fd, &iov);
            break;
        }

        len = h->nlmsg_len;
        if (h->nlmsg_type == NLMSG_ERROR) {
            SOL_WRN("read_netlink: Message is an error");
            goto error;
        }

        if (h->nlmsg_type == CRYPTO_MSG_GETALG) {
            struct crypto_user_alg *cua = NLMSG_DATA(h);
            struct rtattr *rta;

            len -= NLMSG_SPACE(sizeof(*cua));
            if (len < 0) {
                SOL_WRN("read_netlink: message is too small: %zd", len);
                goto error;
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

error:
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
        sol_vector_del_last(&_algorithms_info);
        return NULL;
    }

    return info;
}

static ssize_t
_sol_message_digest_linux_kcapi_feed(struct sol_message_digest *handle, const void *mem, size_t len, bool is_last)
{
    int *pfd = sol_message_digest_common_get_context(handle);
    ssize_t n = send(*pfd, mem, len, is_last ? 0 : MSG_MORE);

    if (n >= 0)
        return n;
    else
        return -errno;
}

static ssize_t
_sol_message_digest_linux_kcapi_read_digest(struct sol_message_digest *handle, void *mem, size_t len)
{
    int *pfd = sol_message_digest_common_get_context(handle);
    ssize_t n = recv(*pfd, mem, len, 0);

    if (n >= 0)
        return n;
    else
        return -errno;
}

static void
_sol_message_digest_linux_kcapi_cleanup(struct sol_message_digest *handle)
{
    int *pfd = sol_message_digest_common_get_context(handle);

    close(*pfd);
}

static const struct sol_message_digest_common_ops _sol_message_digest_linux_kcapi_ops = {
    .feed = _sol_message_digest_linux_kcapi_feed,
    .read_digest = _sol_message_digest_linux_kcapi_read_digest,
    .cleanup = _sol_message_digest_linux_kcapi_cleanup
};

SOL_API struct sol_message_digest *
sol_message_digest_new(const struct sol_message_digest_config *config)
{
    struct sol_message_digest_common_new_params params;
    const struct sol_message_digest_algorithm_info *info;
    struct sol_message_digest *handle;
    struct sockaddr_alg sa;
    int afd, bfd, errno_bkp;

    errno = EINVAL;
    SOL_NULL_CHECK(config, NULL);
    SOL_NULL_CHECK(config->on_digest_ready, NULL);
    SOL_NULL_CHECK(config->algorithm, NULL);

#ifndef SOL_NO_API_VERSION
    if (config->api_version != SOL_MESSAGE_DIGEST_CONFIG_API_VERSION) {
        SOL_WRN("sol_message_digest_config->api_version=%hu, "
            "expected version is %hu.",
            config->api_version, SOL_MESSAGE_DIGEST_CONFIG_API_VERSION);
        return NULL;
    }
#endif

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
        goto error;
    }

    if (config->key.len > 0) {
        int r = setsockopt(bfd, SOL_ALG, ALG_SET_KEY,
            config->key.data, config->key.len);
        if (r < 0) {
            SOL_WRN("algorithm \"%s\", failed to set key len=%zd \"%.*s\"",
                config->algorithm, config->key.len,
                SOL_STR_SLICE_PRINT(config->key));
            goto error;
        }
    }

    info = _sol_message_digest_get_algorithm_info(config->algorithm);
    SOL_NULL_CHECK_GOTO(info, error);

    afd = accept4(bfd, NULL, 0, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (afd < 0) {
        SOL_WRN("algorithm \"%s\" failed accept4(%d): %s",
            config->algorithm, bfd, sol_util_strerrora(errno));
        goto error;
    }

    params.config = config;
    params.ops = &_sol_message_digest_linux_kcapi_ops;
    params.context_template = &afd;
    params.context_size = sizeof(afd);
    params.digest_size = info->digest_size;

    handle = sol_message_digest_common_new(params);
    SOL_NULL_CHECK_GOTO(handle, error_handle);

    close(bfd);

    return handle;

error_handle:
    errno_bkp = errno;
    close(afd);
    errno = errno_bkp;

error:
    errno_bkp = errno;
    close(bfd);
    errno = errno_bkp;
    return NULL;
}
