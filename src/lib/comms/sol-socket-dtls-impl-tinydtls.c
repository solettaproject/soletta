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

#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-network.h"
#include "sol-network-util.h"
#include "sol-util-internal.h"

#include "sol-socket.h"
#include "sol-socket-dtls.h"
#include "sol-socket-impl.h"

#include "dtls.h"

static const uint32_t dtls_magic = 'D' << 24 | 't' << 16 | 'L' << 8 | 's';

struct queue_item {
    struct sol_buffer buffer;
    struct sol_network_link_addr addr;
};

struct sol_socket_dtls {
    struct sol_socket base;
    uint32_t dtls_magic;

    struct sol_socket *wrapped;
    struct sol_timeout *retransmit_timeout;
    dtls_context_t *context;
    dtls_handler_t handler;
    dtls_ecdsa_key_t ecdsa_key;
    struct {
        bool (*cb)(void *data, struct sol_socket *s);
        struct sol_vector queue;
    } read, write;
    const void *data;

    const struct sol_socket_dtls_credential_cb *credentials;
};

static bool encrypt_payload(struct sol_socket_dtls *s);

static int
from_sockaddr(const struct sockaddr *sockaddr, socklen_t socklen,
    struct sol_network_link_addr *addr)
{
    SOL_NULL_CHECK(sockaddr, -EINVAL);
    SOL_NULL_CHECK(addr, -EINVAL);

    addr->family = sol_network_af_to_sol(sockaddr->sa_family);

    if (sockaddr->sa_family == AF_INET) {
        struct sockaddr_in *sock4 = (struct sockaddr_in *)sockaddr;
        if (socklen < sizeof(struct sockaddr_in))
            return -EINVAL;

        addr->port = ntohs(sock4->sin_port);
        memcpy(&addr->addr.in, &sock4->sin_addr, sizeof(sock4->sin_addr));
    } else if (sockaddr->sa_family == AF_INET6) {
        struct sockaddr_in6 *sock6 = (struct sockaddr_in6 *)sockaddr;
        if (socklen < sizeof(struct sockaddr_in6))
            return -EINVAL;

        addr->port = ntohs(sock6->sin6_port);
        memcpy(&addr->addr.in6, &sock6->sin6_addr, sizeof(sock6->sin6_addr));
    } else {
        return -EINVAL;
    }

    return 0;
}

static int
to_sockaddr(const struct sol_network_link_addr *addr, struct sockaddr *sockaddr, socklen_t *socklen)
{
    SOL_NULL_CHECK(addr, -EINVAL);
    SOL_NULL_CHECK(sockaddr, -EINVAL);
    SOL_NULL_CHECK(socklen, -EINVAL);

    if (addr->family == SOL_NETWORK_FAMILY_INET) {
        struct sockaddr_in *sock4 = (struct sockaddr_in *)sockaddr;
        if (*socklen < sizeof(struct sockaddr_in))
            return -EINVAL;

        memcpy(&sock4->sin_addr, addr->addr.in, sizeof(addr->addr.in));
        sock4->sin_port = htons(addr->port);
        sock4->sin_family = AF_INET;
        *socklen = sizeof(*sock4);
    } else if (addr->family == SOL_NETWORK_FAMILY_INET6) {
        struct sockaddr_in6 *sock6 = (struct sockaddr_in6 *)sockaddr;
        if (*socklen < sizeof(struct sockaddr_in6))
            return -EINVAL;

        memcpy(&sock6->sin6_addr, addr->addr.in6, sizeof(addr->addr.in6));
        sock6->sin6_port = htons(addr->port);
        sock6->sin6_family = AF_INET6;
        *socklen = sizeof(*sock6);
    } else {
        return -EINVAL;
    }

    return *socklen;
}

static bool
session_from_linkaddr(const struct sol_network_link_addr *addr,
    session_t *session)
{
    memset(session, 0, sizeof(*session));
    session->size = sizeof(session->addr);

    return to_sockaddr(addr, &session->addr.sa, &session->size) >= 0;
}

static void
clear_queue_item(struct queue_item *item)
{
    sol_util_clear_memory_secure(item->buffer.data, item->buffer.capacity);
    sol_buffer_fini(&item->buffer);
    sol_util_clear_memory_secure(item, sizeof(*item));
}

static void
clear_queue(struct sol_vector *vec)
{
    struct queue_item *item;
    uint16_t idx;

    SOL_VECTOR_FOREACH_IDX (vec, item, idx)
        clear_queue_item(item);

    sol_vector_clear(vec);
}

static void
free_queue(struct sol_vector *vec)
{
    clear_queue(vec);
    sol_util_clear_memory_secure(vec, sizeof(*vec));
}

static void
sol_socket_dtls_del(struct sol_socket *socket)
{
    struct sol_socket_dtls *s = (struct sol_socket_dtls *)socket;

    free_queue(&s->read.queue);
    free_queue(&s->write.queue);

    if (s->retransmit_timeout)
        sol_timeout_del(s->retransmit_timeout);

    dtls_free_context(s->context);

    sol_socket_del(s->wrapped);

    free(s->ecdsa_key.priv_key);
    free(s->ecdsa_key.pub_key_x);
    free(s->ecdsa_key.pub_key_y);

    sol_util_clear_memory_secure(s, sizeof(*s));
    free(s);
}

static int
remove_item_from_vector(struct sol_vector *vec, struct queue_item *item,
    int retval)
{
    clear_queue_item(item);
    sol_vector_del(vec, 0);

    return retval;
}

static ssize_t
sol_socket_dtls_recvmsg(struct sol_socket *socket, struct sol_buffer *buf,
    struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_dtls *s = (struct sol_socket_dtls *)socket;
    struct sol_buffer new_buf;
    void *buf_copy;
    struct queue_item *item;
    int ret;

    if (s->read.queue.len == 0) {
        SOL_DBG("Receive queue empty, returning 0");
        return 0;
    }

    item = sol_vector_get(&s->read.queue, 0);
    if (!item) {
        SOL_DBG("Could not get first item in queue");
        return -EAGAIN;
    }

    memcpy(cliaddr, &item->addr, sizeof(*cliaddr));

    ret = sol_buffer_set_buffer(buf, &item->buffer);
    SOL_INT_CHECK(ret, < 0, ret);

    return remove_item_from_vector(&s->read.queue, item, buf->used);
}

static ssize_t
sol_socket_dtls_sendmsg(struct sol_socket *socket, const struct sol_buffer *buf,
    const struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_dtls *s = (struct sol_socket_dtls *)socket;
    struct queue_item *item;
    int ret;

    if (s->write.queue.len > 4) {
        SOL_WRN("Transmission queue too long");
        return -ENOMEM;
    }

    item = sol_vector_append(&s->write.queue);
    SOL_NULL_CHECK(item, -ENOMEM);

    item->addr = *cliaddr;
    sol_buffer_init_flags(&item->buffer, NULL, 0,
        SOL_BUFFER_FLAGS_NO_NUL_BYTE | SOL_BUFFER_FLAGS_CLEAR_MEMORY);
    ret = sol_buffer_set_buffer(&item->buffer, buf);
    if (ret < 0) {
        SOL_WRN("Could not append the buffer");
        sol_buffer_fini(&item->buffer);
        sol_vector_del_last(&s->write.queue);
        return ret;
    }

    encrypt_payload(s);

    return buf->used;
}

static int
sol_socket_dtls_join_group(struct sol_socket *socket, int ifindex, const struct sol_network_link_addr *group)
{
    /* DTLS servers are unicast only */
    return 0;
}

static int
sol_socket_dtls_bind(struct sol_socket *socket, const struct sol_network_link_addr *addr)
{
    struct sol_socket_dtls *s = (struct sol_socket_dtls *)socket;

    return sol_socket_bind(s->wrapped, addr);
}

static void
init_dtls_if_needed(void)
{
    static bool initialized = false;

    if (SOL_UNLIKELY(!initialized)) {
        dtls_init();
        initialized = true;
        SOL_DBG("TinyDTLS initialized");
    }
}

/* Called whenever the wrapped socket can be read. This receives a packet
 * from that socket, and passes to TinyDTLS. When it's done decrypting
 * the payload, dtls_handle_message() will call call_user_read_cb(), which
 * in turn will call the user callback with the decrypted data. */
static bool
read_encrypted(void *data, struct sol_socket *wrapped)
{
    struct sol_socket_dtls *socket = data;
    struct sol_network_link_addr cliaddr;
    session_t session = { 0 };
    int len;

    SOL_BUFFER_DECLARE_STATIC(buffer, DTLS_MAX_BUF);

    SOL_DBG("Reading encrypted data from wrapped socket");

    len = sol_socket_recvmsg(socket->wrapped, &buffer, &cliaddr);
    SOL_INT_CHECK(len, < 0, false);

    session.size = sizeof(session.addr);
    if (to_sockaddr(&cliaddr, &session.addr.sa, &session.size) < 0)
        return false;

    return dtls_handle_message(socket->context, &session, buffer.data, len) == 0;
}

static int
call_user_read_cb(struct dtls_context_t *ctx, session_t *session, uint8_t *buf, size_t len)
{
    struct sol_socket_dtls *socket = dtls_get_app_data(ctx);
    struct sol_network_link_addr addr;
    struct queue_item *item;
    void *buf_copy;

    if (socket->read.queue.len > 4) {
        SOL_WRN("Read queue too long, dropping packet");
        return -ENOMEM;
    }

    if (from_sockaddr(&session->addr.sa, session->size, &addr) < 0) {
        SOL_DBG("Could not get link address from session");
        return -EINVAL;
    }

    buf_copy = sol_util_memdup(buf, len);
    sol_util_clear_memory_secure(buf, len);
    SOL_NULL_CHECK(buf_copy, -EINVAL);

    item = sol_vector_append(&socket->read.queue);
    SOL_NULL_CHECK_GOTO(item, no_item);

    item->addr = addr;
    sol_buffer_init_flags(&item->buffer, buf_copy, len,
        SOL_BUFFER_FLAGS_CLEAR_MEMORY | SOL_BUFFER_FLAGS_NO_NUL_BYTE |
        SOL_BUFFER_FLAGS_FIXED_CAPACITY);
    item->buffer.used = len;

    if (!socket->read.cb) {
        /* No need to free buf_copy/item, cb might be set later */
        return -EINVAL;
    }

    if (socket->read.cb((void *)socket->data, socket))
        return len;

    return -EINVAL;

no_item:
    sol_util_clear_memory_secure(buf_copy, len);
    free(buf_copy);
    return -ENOMEM;
}

/* Called whenever the wrapped socket can be written to. This gets the
 * unencrypted data previously set with sol_socket_dtls_sendmsg() and cached in
 * the sol_socket_dtls struct, passes through TinyDTLS, and when it's done
 * encrypting the payload, it calls write_encrypted() to finally pass it to
 * the wire through sol_socket_sendmsg().  */
static bool
encrypt_payload(struct sol_socket_dtls *s)
{
    struct queue_item *item;
    session_t session;
    int r;

    item = sol_vector_get(&s->write.queue, 0);
    if (!item) {
        SOL_WRN("Write transmission queue empty");
        return false;
    }

    if (!session_from_linkaddr(&item->addr, &session)) {
        SOL_DBG("Could not create create session from link address");
        return false;
    }

    r = dtls_write(s->context, &session, item->buffer.data, item->buffer.used);
    if (r == 0) {
        SOL_DBG("Peer state is not connected, keeping buffer in memory to try again");
        return true;
    }
    if (r < 0) {
        SOL_WRN("Could not send data through the secure channel, will try again");
        return true;
    }

    if (r < (int)item->buffer.used)
        SOL_WRN("Could not send all of the enqueued data, will discard");
    else
        SOL_DBG("Sent everything, will remove from queue");

    sol_buffer_fini(&item->buffer);
    sol_util_clear_memory_secure(item, sizeof(*item));
    sol_vector_del(&s->write.queue, 0);

    return true;
}

static int
write_encrypted(struct dtls_context_t *ctx, session_t *session, uint8_t *buf, size_t len)
{
    struct sol_socket_dtls *socket = dtls_get_app_data(ctx);
    struct sol_network_link_addr addr;
    struct sol_buffer buffer = SOL_BUFFER_INIT_CONST(buf, len);
    int r;

    if (from_sockaddr(&session->addr.sa, session->size, &addr) < 0) {
        SOL_DBG("Could not get link address from session");
        return -EINVAL;
    }

    return sol_socket_sendmsg(socket->wrapped, &buffer, &addr);
}

static bool
call_user_write_cb(void *data, struct sol_socket *wrapped)
{
    struct sol_socket_dtls *socket = data;

    if (!socket->write.cb) {
        SOL_DBG("No wrapped write callback");
        return false;
    }

    if (socket->write.cb((void *)socket->data, socket)) {
        SOL_DBG("User func@%p returned success, encrypting payload",
            socket->write.cb);

        return true;
    }

    return false;
}

static void
retransmit_timer_disable(struct sol_socket_dtls *s)
{
    if (s->retransmit_timeout) {
        SOL_DBG("Disabling DTLS retransmit timer");

        sol_timeout_del(s->retransmit_timeout);
        s->retransmit_timeout = NULL;
    }
}

static bool
retransmit_timer_cb(void *data)
{
    struct sol_socket_dtls *socket = data;

    SOL_DBG("Retransmitting DTLS packets");
    dtls_check_retransmit(socket->context, NULL);
    socket->retransmit_timeout = NULL;

    return false;
}

static void
retransmit_timer_enable(struct sol_socket_dtls *s, clock_time_t next)
{
    SOL_DBG("Next DTLS retransmission will happen in %u seconds", next);

    if (s->retransmit_timeout)
        sol_timeout_del(s->retransmit_timeout);

    s->retransmit_timeout = sol_timeout_add(next * 1000, retransmit_timer_cb,
        socket);
}

static void
retransmit_timer_check(struct sol_socket_dtls *s)
{
    clock_time_t next_retransmit = 0;

    dtls_check_retransmit(s->context, &next_retransmit);
    if (next_retransmit == 0)
        retransmit_timer_disable(s);
    else
        retransmit_timer_enable(s, next_retransmit);
}

static int
handle_dtls_event(struct dtls_context_t *ctx, session_t *session,
    dtls_alert_level_t level, unsigned short code)
{
    struct sol_socket_dtls *socket = dtls_get_app_data(ctx);
    const char *msg;

    if (code == DTLS_EVENT_CONNECT)
        msg = "handshake_init";
    else if (code == DTLS_EVENT_CONNECTED)
        msg = "handshake_or_renegotiation_done";
    else if (code == DTLS_EVENT_RENEGOTIATE)
        msg = "renegotiation_started";
    else if (code == DTLS_ALERT_CLOSE_NOTIFY)
        msg = "close_notify";
    else if (code == DTLS_ALERT_UNEXPECTED_MESSAGE)
        msg = "unexpected_message";
    else if (code == DTLS_ALERT_BAD_RECORD_MAC)
        msg = "bad_record_mac";
    else if (code == DTLS_ALERT_RECORD_OVERFLOW)
        msg = "record_overflow";
    else if (code == DTLS_ALERT_DECOMPRESSION_FAILURE)
        msg = "decompression_failure";
    else if (code == DTLS_ALERT_HANDSHAKE_FAILURE)
        msg = "handshake_failure";
    else if (code == DTLS_ALERT_BAD_CERTIFICATE)
        msg = "bad_certificate";
    else if (code == DTLS_ALERT_UNSUPPORTED_CERTIFICATE)
        msg = "unsupported_certificate";
    else if (code == DTLS_ALERT_CERTIFICATE_REVOKED)
        msg = "certificate_revoked";
    else if (code == DTLS_ALERT_CERTIFICATE_EXPIRED)
        msg = "certificate_expired";
    else if (code == DTLS_ALERT_CERTIFICATE_UNKNOWN)
        msg = "certificate_unknown";
    else if (code == DTLS_ALERT_ILLEGAL_PARAMETER)
        msg = "illegal_parameter";
    else if (code == DTLS_ALERT_UNKNOWN_CA)
        msg = "unknown_ca";
    else if (code == DTLS_ALERT_ACCESS_DENIED)
        msg = "access_denied";
    else if (code == DTLS_ALERT_DECODE_ERROR)
        msg = "decode_error";
    else if (code == DTLS_ALERT_DECRYPT_ERROR)
        msg = "decrypt_error";
    else if (code == DTLS_ALERT_PROTOCOL_VERSION)
        msg = "protocol_version";
    else if (code == DTLS_ALERT_INSUFFICIENT_SECURITY)
        msg = "insufficient_security";
    else if (code == DTLS_ALERT_INTERNAL_ERROR)
        msg = "internal_error";
    else if (code == DTLS_ALERT_USER_CANCELED)
        msg = "user_canceled";
    else if (code == DTLS_ALERT_NO_RENEGOTIATION)
        msg = "no_renegotiation";
    else if (code == DTLS_ALERT_UNSUPPORTED_EXTENSION)
        msg = "unsupported_extension";
    else
        msg = "unknown_event";

    if (level == DTLS_ALERT_LEVEL_WARNING) {
        SOL_WRN("DTLS warning for socket %p: %s", socket, msg);
    } else if (level == DTLS_ALERT_LEVEL_FATAL) {
        /* FIXME: What to do here? Destroy the wrapped socket? Renegotiate? */
        SOL_ERR("DTLS fatal error for socket %p: %s", socket, msg);
    } else {
        SOL_DBG("TLS session changed for socket %p: %s", socket, msg);

        if (code == DTLS_EVENT_CONNECTED) {
            struct queue_item *item;
            uint16_t idx;

            SOL_DBG("Sending %d enqueued packets in write queue", socket->write.queue.len);
            SOL_VECTOR_FOREACH_IDX (&socket->write.queue, item, idx) {
                session_t session;

                if (!session_from_linkaddr(&item->addr, &session))
                    continue;

                (void)dtls_write(socket->context, &session, item->buffer.data, item->buffer.used);
                clear_queue_item(item);
            }
            clear_queue(&socket->write.queue);
        }
    }

    retransmit_timer_check(socket);

    return 0;
}

static int
sol_socket_dtls_set_read_monitor(struct sol_socket *socket, bool on)
{
    struct sol_socket_dtls *s = (struct sol_socket_dtls *)socket;


    SOL_DBG("setting onread of socket %p to <%d>", socket, on);

    return sol_socket_set_read_monitor(s->wrapped, on);
}

static int
sol_socket_dtls_set_write_monitor(struct sol_socket *socket, bool on)
{
    struct sol_socket_dtls *s = (struct sol_socket_dtls *)socket;

    SOL_DBG("setting onwrite of socket %p to <%d>", socket, on);

    return sol_socket_set_write_monitor(s->wrapped, on);
}

static int
get_psk_info(struct dtls_context_t *ctx, const session_t *session,
    dtls_credentials_type_t type, const char *desc, size_t desc_len,
    char *result, size_t result_len)
{
    struct sol_socket_dtls *socket = dtls_get_app_data(ctx);
    ssize_t len;
    int r = -1;

    SOL_NULL_CHECK(socket->credentials,
        dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR));
    SOL_NULL_CHECK(socket->credentials->get_psk,
        dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR));
    SOL_NULL_CHECK(socket->credentials->get_id,
        dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR));

    if (socket->credentials->init &&
        socket->credentials->init(socket->credentials->data) < 0) {
        SOL_WRN("Could not initialize credential storage");
        return dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR);
    }

    if (type == DTLS_PSK_IDENTITY || type == DTLS_PSK_HINT) {
        struct sol_network_link_addr addr;

        SOL_DBG("Server asked for PSK %s with %zu bytes, have %d",
            type == DTLS_PSK_IDENTITY ? "identity" : "hint",
            result_len, SOL_DTLS_PSK_ID_LEN);

        if (from_sockaddr(&session->addr.sa, session->size, &addr) < 0) {
            SOL_DBG("Could not get link address from session");
            return -EINVAL;
        }

        len = socket->credentials->get_id(socket->credentials->data, &addr,
            result, result_len);
        if (len > SOL_DTLS_PSK_ID_LEN) {
            SOL_DBG("Not enough space to write key ID");
            r = dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR);
        } else {
            r = (int)len;
        }
    } else if (type != DTLS_PSK_KEY) {
        SOL_WRN("Expecting request for PSK, got something else instead (got %d, expected %d)",
            type, DTLS_PSK_KEY);
        r = dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR);
    } else {
        len = socket->credentials->get_psk(socket->credentials->data,
            SOL_STR_SLICE_STR(desc, desc_len), result, result_len);
        if (len != SOL_DTLS_PSK_KEY_LEN) {
            if (len < 0)
                SOL_WRN("Expecting PSK key but no space to write it (need %d, got %zd <%s>)",
                    SOL_DTLS_PSK_KEY_LEN, len, sol_util_strerrora(-len));
            else
                SOL_WRN("Expecting PSK key but no space to write it (need %d, got %zd)",
                    SOL_DTLS_PSK_KEY_LEN, len);
            r = dtls_alert_fatal_create(DTLS_ALERT_ILLEGAL_PARAMETER);
        } else {
            r = (int)len;
        }
    }

    if (socket->credentials->clear)
        socket->credentials->clear(socket->credentials->data);

    return r;
}

static int
get_ecdsa_key(struct dtls_context_t *ctx, const session_t *session,
    const dtls_ecdsa_key_t **result)
{
    struct sol_socket_dtls *socket = dtls_get_app_data(ctx);
    struct sol_network_link_addr addr;
    int r;

    SOL_DBG("Peer asked for ECDSA Key");

    SOL_NULL_CHECK(socket->credentials,
        dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR));
    SOL_NULL_CHECK(socket->credentials->get_ecdsa_priv_key,
        dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR));
    SOL_NULL_CHECK(socket->credentials->get_ecdsa_pub_key_x,
        dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR));
    SOL_NULL_CHECK(socket->credentials->get_ecdsa_pub_key_y,
        dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR));

    if (socket->credentials->init &&
        socket->credentials->init(socket->credentials->data) < 0) {
        SOL_WRN("Could not initialize credential storage");
        return dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR);
    }

    if (from_sockaddr(&session->addr.sa, session->size, &addr) < 0) {
        SOL_DBG("Could not get link address from session");
        return -EINVAL;
    }

    socket->ecdsa_key.priv_key = calloc(SOL_DTLS_ECDSA_PRIV_KEY_LEN, sizeof(unsigned char));
    SOL_NULL_CHECK(socket->ecdsa_key.priv_key, -ENOMEM);

    socket->ecdsa_key.pub_key_x = calloc(SOL_DTLS_ECDSA_PUB_KEY_X_LEN, sizeof(unsigned char));
    r = -ENOMEM;
    SOL_NULL_CHECK_GOTO(socket->ecdsa_key.pub_key_x, err_pub_key_x);

    socket->ecdsa_key.pub_key_y = calloc(SOL_DTLS_ECDSA_PUB_KEY_Y_LEN, sizeof(unsigned char));
    r = -ENOMEM;
    SOL_NULL_CHECK_GOTO(socket->ecdsa_key.pub_key_y, err_pub_key_y);

    r = socket->credentials->get_ecdsa_priv_key(socket->credentials->data,
        &addr, socket->ecdsa_key.priv_key);
    SOL_INT_CHECK_GOTO(r, < 0, err_getters);

    r = socket->credentials->get_ecdsa_pub_key_x(socket->credentials->data,
        &addr, socket->ecdsa_key.pub_key_x);
    SOL_INT_CHECK_GOTO(r, < 0, err_getters);

    r = socket->credentials->get_ecdsa_pub_key_y(socket->credentials->data,
        &addr, socket->ecdsa_key.pub_key_y);
    SOL_INT_CHECK_GOTO(r, < 0, err_getters);

    /* From CoAP [RFC7252], Section 9.1.3.2. Raw Public Key Certificates:
       Implementations in RawPublicKey mode MUST support the mandatory-to-implement
       cipher suite TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8 as specified in [RFC7251],
       [RFC5246], and [RFC4492].  The key used MUST be ECDSA capable.
       The curve secp256r1 MUST be supported [RFC4492]; this curve is equivalent to
       the NIST P-256 curve.  The hash algorithm is SHA-256. */
    socket->ecdsa_key.curve = DTLS_ECDH_CURVE_SECP256R1;

    *result = &socket->ecdsa_key;

    if (socket->credentials->clear)
        socket->credentials->clear(socket->credentials->data);

    return 0;

err_getters:
    free(socket->ecdsa_key.pub_key_y);
err_pub_key_y:
    free(socket->ecdsa_key.pub_key_x);
err_pub_key_x:
    free(socket->ecdsa_key.priv_key);
    return r;
}

static int
verify_ecdsa_key(struct dtls_context_t *ctx, const session_t *session,
    const unsigned char *other_pub_x, const unsigned char *other_pub_y,
    size_t key_size)
{
    struct sol_socket_dtls *socket = dtls_get_app_data(ctx);
    struct sol_network_link_addr addr;

    SOL_DBG("Verifying peer's ECDSA Public Key");

    SOL_INT_CHECK(key_size, != SOL_DTLS_ECDSA_PUB_KEY_X_LEN, -EINVAL);
    SOL_INT_CHECK(key_size, != SOL_DTLS_ECDSA_PUB_KEY_Y_LEN, -EINVAL);

    SOL_NULL_CHECK(socket->credentials,
        dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR));
    SOL_NULL_CHECK(socket->credentials->verify_ecdsa_key,
        dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR));

    if (from_sockaddr(&session->addr.sa, session->size, &addr) < 0) {
        SOL_DBG("Could not get link address from session");
        return -EINVAL;
    }

    return socket->credentials->verify_ecdsa_key(socket->credentials->data,
        &addr, other_pub_x, other_pub_y, key_size);
}

struct sol_socket *
sol_socket_default_dtls_new(const struct sol_socket_options *options)
{
    static const struct sol_socket_type type = {
        SOL_SET_API_VERSION(.api_version = SOL_SOCKET_TYPE_API_VERSION, )
        .bind = sol_socket_dtls_bind,
        .join_group = sol_socket_dtls_join_group,
        .sendmsg = sol_socket_dtls_sendmsg,
        .recvmsg = sol_socket_dtls_recvmsg,
        .set_write_monitor = sol_socket_dtls_set_write_monitor,
        .set_read_monitor = sol_socket_dtls_set_read_monitor,
        .del = sol_socket_dtls_del
    };
    struct sol_socket_ip_options opts;
    struct sol_socket_dtls *s;
    int r = 0;
    uint16_t i;

    SOL_SOCKET_OPTIONS_CHECK_SUB_API_VERSION(options, SOL_SOCKET_IP_OPTIONS_SUB_API_VERSION, NULL);

    opts = *(struct sol_socket_ip_options *)options;
    init_dtls_if_needed();

    s = calloc(1, sizeof(*s));
    SOL_NULL_CHECK_ERRNO(s, ENOMEM, NULL);

    s->write.cb = opts.base.on_can_write;
    s->read.cb = opts.base.on_can_read;
    s->data = opts.base.data;

    opts.base.data = s;
    opts.base.on_can_read = read_encrypted;
    opts.base.on_can_write = call_user_write_cb;

    s->wrapped = sol_socket_ip_default_new(&opts);
    if (!s->wrapped) {
        r = -errno;
        goto err;
    }

    s->context = dtls_new_context(s);
    if (!s->context) {
        SOL_WRN("Could not create DTLS context");
        sol_socket_del(s->wrapped);
        r = -errno;
        goto err;
    }

    s->handler.write = write_encrypted;
    s->handler.read = call_user_read_cb;
    s->handler.event = handle_dtls_event;

    for (i = 0; i < opts.cipher_suites_len; i++) {
        switch (opts.cipher_suites[i]) {
        case SOL_SOCKET_DTLS_CIPHER_PSK_AES128_CCM8:
            SOL_DBG("Adding get_psk_info callback to %p handler", &s->handler);
            s->handler.get_psk_info = get_psk_info;
            break;
        case SOL_SOCKET_DTLS_CIPHER_ECDHE_ECDSA_AES128_CCM8:
            SOL_DBG("Adding get_ecdsa_* callbacks to %p handler", &s->handler);
            s->handler.get_ecdsa_key = get_ecdsa_key;
            s->handler.verify_ecdsa_key = verify_ecdsa_key;
            break;
        case SOL_SOCKET_DTLS_CIPHER_ECDH_ANON_AES128_CBC_SHA256:
            SOL_WRN("Unsupported DTLS Cipher Suite at position %" PRIu16
                ": %d", i, opts.cipher_suites[i]);
            errno = EINVAL;
            goto err;
        default:
            SOL_WRN("Unsupported DTLS Cipher Suite at position %" PRIu16
                ": %d", i, opts.cipher_suites[i]);
            errno = EINVAL;
            goto err;
        }
    }

    dtls_set_handler(s->context, &s->handler);

    s->base.type = &type;
    s->dtls_magic = dtls_magic;

    sol_vector_init(&s->write.queue, sizeof(struct queue_item));
    sol_vector_init(&s->read.queue, sizeof(struct queue_item));

    SOL_DBG("sol_socket_dtls %p with wrapped socket %p, base socket %p,"
        " base.type %p, context %p and handler %p created!",
        s, s->wrapped, &s->base, s->base.type, s->context, &s->handler);

    return &s->base;

err:
    free(s);
    errno = -r;
    return NULL;
}

int
sol_socket_dtls_set_handshake_cipher(struct sol_socket *s,
    enum sol_socket_dtls_cipher cipher)
{
    static const dtls_cipher_t conv_tbl[] = {
#ifdef DTLS_EXTRAS
        [SOL_SOCKET_DTLS_CIPHER_ECDH_ANON_AES128_CBC_SHA256] = TLS_ECDH_anon_WITH_AES_128_CBC_SHA_256,
#endif
        [SOL_SOCKET_DTLS_CIPHER_PSK_AES128_CCM8] = TLS_PSK_WITH_AES_128_CCM_8,
        [SOL_SOCKET_DTLS_CIPHER_ECDHE_ECDSA_AES128_CCM8] = TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8
    };
    struct sol_socket_dtls *socket = (struct sol_socket_dtls *)s;

    SOL_INT_CHECK(socket->dtls_magic, != dtls_magic, -EINVAL);

#ifndef DTLS_EXTRAS
    if (cipher == SOL_SOCKET_DTLS_CIPHER_ECDH_ANON_AES128_CBC_SHA256) {
        SOL_WRN("To enable SOL_SOCKET_DTLS_CIPHER_ECDH_ANON_AES128_CBC_SHA256 compile Soletta with DTLS Extras");
        return -EINVAL;
    }
#endif

    if ((size_t)cipher >= sol_util_array_size(conv_tbl))
        return -EINVAL;

    dtls_select_cipher(socket->context, conv_tbl[cipher]);

    return 0;
}

#ifdef DTLS_EXTRAS
int
sol_socket_dtls_set_anon_ecdh_enabled(struct sol_socket *s, bool setting)
{
    struct sol_socket_dtls *socket = (struct sol_socket_dtls *)s;

    SOL_INT_CHECK(socket->dtls_magic, != dtls_magic, -EINVAL);

    dtls_enables_anon_ecdh(socket->context,
        setting ? DTLS_CIPHER_ENABLE : DTLS_CIPHER_DISABLE);

    return 0;
}
#else
int
sol_socket_dtls_set_anon_ecdh_enabled(struct sol_socket *s, bool setting)
{
    SOL_WRN("To enable sol_socket_dtls_set_anon_ecdh_enabled() compile Soletta with DTLS Extras");
    return -ENOSYS;
}
#endif

int
sol_socket_dtls_prf_keyblock(struct sol_socket *s,
    const struct sol_network_link_addr *addr, struct sol_str_slice label,
    struct sol_str_slice random1, struct sol_str_slice random2,
    struct sol_buffer *buffer)
{
    struct sol_socket_dtls *socket = (struct sol_socket_dtls *)s;
    session_t session;
    size_t r;

    SOL_INT_CHECK(socket->dtls_magic, != dtls_magic, -EINVAL);

    if (!session_from_linkaddr(addr, &session))
        return -EINVAL;

    r = dtls_prf_with_current_keyblock(socket->context, &session,
        (const uint8_t *)label.data, label.len, (const uint8_t *)random1.data, random1.len,
        (const uint8_t *)random2.data, random1.len, buffer->data, buffer->capacity);
    if (!r)
        return -EINVAL;

    buffer->used = r;

    return 0;
}

int
sol_socket_dtls_set_credentials_callbacks(struct sol_socket *s,
    const struct sol_socket_dtls_credential_cb *cb)
{
    struct sol_socket_dtls *socket = (struct sol_socket_dtls *)s;

    SOL_INT_CHECK(socket->dtls_magic, != dtls_magic, -EINVAL);

    socket->credentials = cb;

    return 0;
}
