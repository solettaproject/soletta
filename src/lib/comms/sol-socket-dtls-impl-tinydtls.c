/*
 * This file is part of the Soletta Project
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
    sol_util_secure_clear_memory(item->buffer.data, item->buffer.capacity);
    sol_buffer_fini(&item->buffer);
    sol_util_secure_clear_memory(item, sizeof(*item));
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
    sol_util_secure_clear_memory(vec, sizeof(*vec));
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

    sol_util_secure_clear_memory(s, sizeof(*s));
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
sol_socket_dtls_recvmsg(struct sol_socket *socket, void *buf, size_t len, struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_dtls *s = (struct sol_socket_dtls *)socket;
    struct sol_buffer new_buf;
    void *buf_copy;
    struct queue_item *item;

    if (s->read.queue.len == 0) {
        SOL_DBG("Receive queue empty, returning 0");
        return 0;
    }

    item = sol_vector_get(&s->read.queue, 0);
    if (!item) {
        SOL_DBG("Could not get first item in queue");
        return -EAGAIN;
    }

    if (!buf)
        return item->buffer.used;

    memcpy(cliaddr, &item->addr, sizeof(*cliaddr));

    if (item->buffer.used <= len) {
        memcpy(buf, item->buffer.data, item->buffer.used);
        return remove_item_from_vector(&s->read.queue, item,
            item->buffer.used);
    }

    memcpy(buf, item->buffer.data, len);

    buf_copy = sol_util_memdup((const char *)item->buffer.data + len,
        item->buffer.used - len);
    SOL_NULL_CHECK_GOTO(buf_copy, clear_buf);

    sol_buffer_init_flags(&new_buf, buf_copy, len,
        SOL_BUFFER_FLAGS_CLEAR_MEMORY | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    new_buf.used = item->buffer.used - len;

    sol_buffer_fini(&item->buffer);
    item->buffer = new_buf;

    return len;

clear_buf:
    SOL_WRN("Could not copy buffer for short read, discarding unencrypted data");
    return remove_item_from_vector(&s->read.queue, item, -ENOMEM);
}

static ssize_t
sol_socket_dtls_sendmsg(struct sol_socket *socket, const void *buf, size_t len,
    const struct sol_network_link_addr *cliaddr)
{
    struct sol_socket_dtls *s = (struct sol_socket_dtls *)socket;
    struct queue_item *item;
    void *buf_copy;

    if (s->write.queue.len > 4) {
        SOL_WRN("Transmission queue too long");
        return -ENOMEM;
    }

    buf_copy = sol_util_memdup(buf, len);
    SOL_NULL_CHECK(buf_copy, -ENOMEM);

    item = sol_vector_append(&s->write.queue);
    SOL_NULL_CHECK(item, -ENOMEM);

    item->addr = *cliaddr;
    sol_buffer_init_flags(&item->buffer, buf_copy, len,
        SOL_BUFFER_FLAGS_CLEAR_MEMORY | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    item->buffer.used = len;

    encrypt_payload(s);

    return len;
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
 * the payload, dtls_handle_message() will call sol_dtls_read(), which
 * in turn will call the user callback with the decrypted data. */
static bool
read_encrypted(void *data, struct sol_socket *wrapped)
{
    struct sol_socket_dtls *socket = data;
    struct sol_network_link_addr cliaddr;
    session_t session = { 0 };
    uint8_t buf[DTLS_MAX_BUF];
    int len;

    SOL_DBG("Reading encrypted data from wrapped socket");

    len = sol_socket_recvmsg(socket->wrapped, buf, sizeof(buf), &cliaddr);
    SOL_INT_CHECK(len, < 0, false);

    session.size = sizeof(session.addr);
    if (to_sockaddr(&cliaddr, &session.addr.sa, &session.size) < 0)
        return false;

    return dtls_handle_message(socket->context, &session, buf, len) == 0;
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
    sol_util_secure_clear_memory(buf, len);
    SOL_NULL_CHECK(buf_copy, -EINVAL);

    item = sol_vector_append(&socket->read.queue);
    SOL_NULL_CHECK_GOTO(item, no_item);

    item->addr = addr;
    sol_buffer_init_flags(&item->buffer, buf_copy, len,
        SOL_BUFFER_FLAGS_CLEAR_MEMORY | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    item->buffer.used = len;

    if (!socket->read.cb) {
        /* No need to free buf_copy/item, cb might be set later */
        return -EINVAL;
    }

    if (socket->read.cb((void *)socket->data, socket))
        return len;

    return -EINVAL;

no_item:
    sol_util_secure_clear_memory(buf_copy, len);
    free(buf_copy);
    return -ENOMEM;
}

/* Called whenever the wrapped socket can be written to. This gets the
 * unencrypted data previously set with sol_socket_sendmsg() and cached in
 * the sol_socket_dtls struct, passes through TinyDTLS, and when it's done
 * encrypting the payload, it calls sol_socket_sendmsg() to finally pass it
 * to the wire.  */
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
    sol_util_secure_clear_memory(item, sizeof(*item));
    sol_vector_del(&s->write.queue, 0);

    return true;
}

static int
write_encrypted(struct dtls_context_t *ctx, session_t *session, uint8_t *buf, size_t len)
{
    struct sol_socket_dtls *socket = dtls_get_app_data(ctx);
    struct sol_network_link_addr addr;
    int r;

    if (from_sockaddr(&session->addr.sa, session->size, &addr) < 0) {
        SOL_DBG("Could not get link address from session");
        return -EINVAL;
    }

    return sol_socket_sendmsg(socket->wrapped, buf, len, &addr);
}

static bool
call_user_write_cb(void *data, struct sol_socket *wrapped)
{
    struct sol_socket_dtls *socket = data;

    if (!socket->write.cb) {
        SOL_DBG("No wrapped write callback");
        return false;
    }

    if (socket->write.cb(socket->data, socket)) {
        SOL_DBG("User func@%p returned success, encrypting payload",
            socket->write.cb);

        if (encrypt_payload(data)) {
            SOL_DBG("Data encrypted, should have been passed to the "
                "wrapped socket");
            return true;
        }

        SOL_DBG("Could not encrypt payload");
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
    void *creds;
    int r = -1;

    SOL_NULL_CHECK(socket->credentials,
        dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR));
    SOL_NULL_CHECK(socket->credentials->init,
        dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR));
    SOL_NULL_CHECK(socket->credentials->clear,
        dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR));
    SOL_NULL_CHECK(socket->credentials->get_psk,
        dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR));
    SOL_NULL_CHECK(socket->credentials->get_id,
        dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR));

    creds = socket->credentials->init(socket->credentials->data);
    if (!creds) {
        SOL_WRN("Could not initialize credential storage");
        return dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR);
    }

    if (type == DTLS_PSK_IDENTITY || type == DTLS_PSK_HINT) {
        SOL_DBG("Server asked for PSK %s with %zu bytes, have %d",
            type == DTLS_PSK_IDENTITY ? "identity" : "hint",
            result_len, SOL_DTLS_PSK_ID_LEN);

        len = socket->credentials->get_id(creds, result, result_len);
        if (len != SOL_DTLS_PSK_ID_LEN) {
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
        len = socket->credentials->get_psk(creds,
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

    socket->credentials->clear(creds);
    return r;
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
    static const dtls_handler_t dtls_handler = {
        .write = write_encrypted,
        .read = call_user_read_cb,
        .event = handle_dtls_event,
        .get_psk_info = get_psk_info
    };
    struct sol_socket_ip_options opts;
    struct sol_socket_dtls *s;

    SOL_SOCKET_OPTIONS_CHECK_SUB_API_VERSION(options, SOL_SOCKET_IP_OPTIONS_SUB_API_VERSION, NULL);

    opts = *(struct sol_socket_ip_options *)options;
    init_dtls_if_needed();
    opts.base.on_can_read = read_encrypted;
    opts.base.on_can_write = call_user_write_cb;

    s = calloc(1, sizeof(*s));
    SOL_NULL_CHECK(s, NULL);

    s->wrapped = sol_socket_ip_default_new(options);
    SOL_NULL_CHECK_GOTO(s, err);

    s->context = dtls_new_context(s);
    if (!s->context) {
        SOL_WRN("Could not create DTLS context");
        sol_socket_del(s->wrapped);
        goto err;
    }

    dtls_set_handler(s->context, &dtls_handler);

    s->base.type = &type;
    s->dtls_magic = dtls_magic;

    sol_vector_init(&s->write.queue, sizeof(struct queue_item));
    sol_vector_init(&s->read.queue, sizeof(struct queue_item));

    return &s->base;

err:
    free(s);
    return NULL;
}

int
sol_socket_dtls_set_handshake_cipher(struct sol_socket *s,
    enum sol_socket_dtls_cipher cipher)
{
    static const dtls_cipher_t conv_tbl[] = {
        [SOL_SOCKET_DTLS_CIPHER_ECDH_ANON_AES128_CBC_SHA256] = TLS_ECDH_anon_WITH_AES_128_CBC_SHA_256,
        [SOL_SOCKET_DTLS_CIPHER_PSK_AES128_CCM8] = TLS_PSK_WITH_AES_128_CCM_8,
        [SOL_SOCKET_DTLS_CIPHER_ECDHE_ECDSA_AES128_CCM8] = TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8
    };
    struct sol_socket_dtls *socket = (struct sol_socket_dtls *)s;

    SOL_INT_CHECK(socket->dtls_magic, != dtls_magic, -EINVAL);

    if ((size_t)cipher >= SOL_UTIL_ARRAY_SIZE(conv_tbl))
        return -EINVAL;

    dtls_select_cipher(socket->context, conv_tbl[cipher]);

    return 0;
}

int
sol_socket_dtls_set_anon_ecdh_enabled(struct sol_socket *s, bool setting)
{
    struct sol_socket_dtls *socket = (struct sol_socket_dtls *)s;

    SOL_INT_CHECK(socket->dtls_magic, != dtls_magic, -EINVAL);

    dtls_enables_anon_ecdh(socket->context,
        setting ? DTLS_CIPHER_ENABLE : DTLS_CIPHER_DISABLE);

    return 0;
}

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
