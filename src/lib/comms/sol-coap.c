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

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOL_LOG_DOMAIN &_sol_coap_log_domain
#include "sol-log-internal.h"
#include "sol-macros.h"
#include "sol-mainloop.h"
#include "sol-network.h"
#include "sol-reentrant.h"
#include "sol-socket.h"
#include "sol-str-slice.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

#include "coap.h"

#include "sol-coap.h"

SOL_LOG_INTERNAL_DECLARE(_sol_coap_log_domain, "coap");

#define IPV4_ALL_COAP_NODES_GROUP "224.0.1.187"

#define IPV6_ALL_COAP_NODES_SCOPE_LOCAL "ff02::fd"
#define IPV6_ALL_COAP_NODES_SCOPE_SITE "ff05::fd"

/*
 * FIXME: use a random number between ACK_TIMEOUT (2000ms)
 * and ACK_TIMEOUT * ACK_RANDOM_FACTOR (3000ms)
 */
#define ACK_TIMEOUT_MS 2345
#define MAX_RETRANSMIT 4
#define MAX_PKT_TIMEOUT_MS (ACK_TIMEOUT_MS << MAX_RETRANSMIT)

#ifndef SOL_NO_API_VERSION
#define COAP_RESOURCE_CHECK_API(...) \
    do { \
        if (SOL_UNLIKELY(resource->api_version != \
            SOL_COAP_RESOURCE_API_VERSION)) { \
            SOL_WRN("Couldn't handle resource that has unsupported version " \
                "'%" PRIu16 "', expected version is '%" PRIu16 "'", \
                resource->api_version, SOL_COAP_RESOURCE_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0)
#define COAP_RESOURCE_CHECK_API_GOTO(...) \
    do { \
        if (SOL_UNLIKELY(resource->api_version != \
            SOL_COAP_RESOURCE_API_VERSION)) { \
            SOL_WRN("Couldn't handle resource that has unsupported version " \
                "'%u', expected version is '%u'", \
                resource->api_version, SOL_COAP_RESOURCE_API_VERSION); \
            goto __VA_ARGS__; \
        } \
    } while (0)
#else
#define COAP_RESOURCE_CHECK_API(...)
#define COAP_RESOURCE_CHECK_API_GOTO(...)
#endif

struct sol_coap_server {
    struct sol_vector contexts;
    struct sol_ptr_vector pending; /* waiting pending replies */
    struct sol_ptr_vector outgoing; /* in case we need to retransmit */
    struct sol_socket *socket;
    int (*unknown_handler)(void *data,
        struct sol_coap_server *server,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr);
    const void *unknown_handler_data;
    int refcnt;
    bool secure;
};

struct resource_context {
    const struct sol_coap_resource *resource;
    struct sol_ptr_vector observers;
    const void *data;
    uint16_t age;
};

struct resource_observer {
    struct sol_network_link_addr cliaddr;
    uint8_t tkl;
    uint8_t token[0];
};

struct pending_reply {
    struct sol_coap_server *server;
    struct sol_timeout *timeout;
    bool (*cb)(void *data, struct sol_coap_server *server, struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr);
    const void *data;
    char *path;
    struct sol_coap_packet *pkt; /* We may need a week ref to the
                                  * original packet to address the
                                  * sol_coap_cancel_send_packet() case
                                  * on NONCON packets sent with
                                  * replies. */
    struct sol_reentrant reentrant;
    bool observing;
    uint16_t id;
    uint8_t tkl;
    uint8_t token[0];
};

struct outgoing {
    struct sol_coap_server *server;
    struct sol_coap_packet *pkt;
    /* When present this header will overwrite the header from 'pkt'. */
    struct sol_coap_packet *header;
    struct sol_timeout *timeout;
    struct sol_network_link_addr cliaddr;
    int counter; /* How many times this packet was retransmited. */
};

static bool on_can_write(void *data, struct sol_socket *s);

/* This is an internal API and shouldn't be marked with SOL_API. */
struct sol_socket *sol_coap_server_get_socket(const struct sol_coap_server *server);

struct sol_socket *
sol_coap_server_get_socket(const struct sol_coap_server *server)
{
    SOL_NULL_CHECK_ERRNO(server, EINVAL, NULL);

    return server->socket;
}

SOL_API bool
sol_coap_server_is_secure(const struct sol_coap_server *server)
{
    SOL_NULL_CHECK_ERRNO(server, EINVAL, NULL);

    return server->secure;
}

SOL_API int
sol_coap_header_get_version(const struct sol_coap_packet *pkt, uint8_t *version)
{
    struct coap_header *hdr;

    SOL_NULL_CHECK(pkt, -EINVAL);
    SOL_NULL_CHECK(version, -EINVAL);

    hdr = (struct coap_header *)pkt->buf.data;
    *version = hdr->ver;

    return 0;
}

SOL_API int
sol_coap_header_get_type(const struct sol_coap_packet *pkt, uint8_t *type)
{
    struct coap_header *hdr;

    SOL_NULL_CHECK(pkt, -EINVAL);
    SOL_NULL_CHECK(type, -EINVAL);

    hdr = (struct coap_header *)pkt->buf.data;
    *type = hdr->type;

    return 0;
}

SOL_API uint8_t *
sol_coap_header_get_token(const struct sol_coap_packet *pkt, uint8_t *len)
{
    struct coap_header *hdr;

    SOL_NULL_CHECK_ERRNO(pkt, EINVAL, NULL);

    hdr = (struct coap_header *)pkt->buf.data;

    if (len)
        *len = hdr->tkl;

    if (hdr->tkl == 0) {
        errno = EINVAL;
        return NULL;
    }

    return (uint8_t *)pkt->buf.data + sizeof(*hdr);
}

SOL_API int
sol_coap_header_get_id(const struct sol_coap_packet *pkt, uint16_t *id)
{
    struct coap_header *hdr;

    SOL_NULL_CHECK(pkt, -EINVAL);
    SOL_NULL_CHECK(id, -EINVAL);

    hdr = (struct coap_header *)pkt->buf.data;
    *id = sol_util_be16_to_cpu(hdr->id);

    return 0;
}

SOL_API int
sol_coap_header_get_code(const struct sol_coap_packet *pkt, uint8_t *code)
{
    struct coap_header *hdr;

    SOL_NULL_CHECK(pkt, -EINVAL);
    SOL_NULL_CHECK(code, -EINVAL);

    hdr = (struct coap_header *)pkt->buf.data;

    switch (hdr->code) {
    /* Methods are encoded in the code field too */
    case SOL_COAP_METHOD_GET:
    case SOL_COAP_METHOD_POST:
    case SOL_COAP_METHOD_PUT:
    case SOL_COAP_METHOD_DELETE:

    /* All the defined response codes */
    case SOL_COAP_RESPONSE_CODE_OK:
    case SOL_COAP_RESPONSE_CODE_CREATED:
    case SOL_COAP_RESPONSE_CODE_DELETED:
    case SOL_COAP_RESPONSE_CODE_VALID:
    case SOL_COAP_RESPONSE_CODE_CHANGED:
    case SOL_COAP_RESPONSE_CODE_CONTENT:
    case SOL_COAP_RESPONSE_CODE_BAD_REQUEST:
    case SOL_COAP_RESPONSE_CODE_UNAUTHORIZED:
    case SOL_COAP_RESPONSE_CODE_BAD_OPTION:
    case SOL_COAP_RESPONSE_CODE_FORBIDDEN:
    case SOL_COAP_RESPONSE_CODE_NOT_FOUND:
    case SOL_COAP_RESPONSE_CODE_NOT_ALLOWED:
    case SOL_COAP_RESPONSE_CODE_NOT_ACCEPTABLE:
    case SOL_COAP_RESPONSE_CODE_PRECONDITION_FAILED:
    case SOL_COAP_RESPONSE_CODE_REQUEST_TOO_LARGE:
    case SOL_COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT:
    case SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR:
    case SOL_COAP_RESPONSE_CODE_NOT_IMPLEMENTED:
    case SOL_COAP_RESPONSE_CODE_BAD_GATEWAY:
    case SOL_COAP_RESPONSE_CODE_SERVICE_UNAVAILABLE:
    case SOL_COAP_RESPONSE_CODE_GATEWAY_TIMEOUT:
    case SOL_COAP_RESPONSE_CODE_PROXYING_NOT_SUPPORTED:
    case SOL_COAP_CODE_EMPTY:
        *code = hdr->code;
        break;
    default:
        SOL_WRN("Invalid code (%d)", hdr->code);
        return -EINVAL;
    }

    return 0;
}

/* NB: At all _set_ functions, we assign the hdr ptr *after* the
 * buffer operation, that can lead to reallocs */

SOL_API int
sol_coap_header_set_version(struct sol_coap_packet *pkt, uint8_t ver)
{
    struct coap_header *hdr;
    int r;

    SOL_NULL_CHECK(pkt, -EINVAL);

    r = sol_buffer_ensure(&pkt->buf, sizeof(struct coap_header));
    SOL_INT_CHECK(r, < 0, r);

    hdr = (struct coap_header *)pkt->buf.data;
    hdr->ver = ver;

    return 0;
}

SOL_API int
sol_coap_header_set_type(struct sol_coap_packet *pkt, uint8_t type)
{
    struct coap_header *hdr;
    int r;

    SOL_NULL_CHECK(pkt, -EINVAL);

    r = sol_buffer_ensure(&pkt->buf, sizeof(struct coap_header));
    SOL_INT_CHECK(r, < 0, r);

    hdr = (struct coap_header *)pkt->buf.data;
    hdr->type = type;

    return 0;
}

SOL_API int
sol_coap_header_set_token(struct sol_coap_packet *pkt, uint8_t *token, uint8_t tokenlen)
{
    struct sol_str_slice s = SOL_STR_SLICE_STR((char *)token,
        (size_t)tokenlen);
    struct coap_header *hdr;
    int r;

    SOL_NULL_CHECK(pkt, -EINVAL);

    r = sol_buffer_ensure(&pkt->buf, sizeof(struct coap_header));
    SOL_INT_CHECK(r, < 0, r);

    r = sol_buffer_insert_slice(&pkt->buf, sizeof(*hdr), s);
    SOL_INT_CHECK(r, < 0, r);

    hdr = (struct coap_header *)pkt->buf.data;
    /* adjust back token len */
    hdr->tkl = tokenlen;

    return 0;
}

SOL_API int
sol_coap_header_set_id(struct sol_coap_packet *pkt, uint16_t id)
{
    struct coap_header *hdr;
    int r;

    SOL_NULL_CHECK(pkt, -EINVAL);

    r = sol_buffer_ensure(&pkt->buf, sizeof(struct coap_header));
    SOL_INT_CHECK(r, < 0, r);

    hdr = (struct coap_header *)pkt->buf.data;
    hdr->id = sol_util_cpu_to_be16(id);

    return 0;
}

SOL_API int
sol_coap_header_set_code(struct sol_coap_packet *pkt, uint8_t code)
{
    struct coap_header *hdr;
    int r;

    SOL_NULL_CHECK(pkt, -EINVAL);

    r = sol_buffer_ensure(&pkt->buf, sizeof(struct coap_header));
    SOL_INT_CHECK(r, < 0, r);

    hdr = (struct coap_header *)pkt->buf.data;
    hdr->code = code;

    return 0;
}

static bool
uri_path_eq(const struct sol_coap_packet *req, const struct sol_str_slice path[])
{
    struct sol_str_slice options[16];
    unsigned int i;
    int r;
    uint16_t count = 16;

    SOL_NULL_CHECK(req, false);
    SOL_NULL_CHECK(path, false);

    r = sol_coap_find_options(req, SOL_COAP_OPTION_URI_PATH, options, count);
    if (r < 0)
        return false;

    count = r;

    for (i = 0; path[i].len && i < count; i++) {
        if (!sol_str_slice_eq(options[i], path[i]))
            return false;
    }

    return path[i].len == 0 && i == count;
}

SOL_API int
sol_coap_path_to_buffer(const struct sol_str_slice path[],
    struct sol_buffer *buf, size_t offset, size_t *size)
{
    size_t cur, new_cur;
    unsigned int i;
    int r = 0;

    SOL_NULL_CHECK(path, -EINVAL);
    SOL_NULL_CHECK(buf, -EINVAL);

    cur = offset;
    for (i = 0; path[i].len; i++) {
        r = sol_util_size_add(cur, path[i].len + 1, &new_cur);
        SOL_INT_CHECK_GOTO(r, < 0, end);

        r = sol_buffer_insert_char(buf, cur++, '/');
        SOL_INT_CHECK_GOTO(r, < 0, end);

        r = sol_buffer_insert_slice(buf, cur, path[i]);
        SOL_INT_CHECK_GOTO(r, < 0, end);

        cur = new_cur;
    }

end:
    if (size)
        *size = cur;
    return r;
}

static int
packet_extract_path(const struct sol_coap_packet *req, char **path_str)
{
    const int max_count = 16;
    struct sol_str_slice options[max_count];
    struct sol_str_slice *path;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    size_t path_len;
    unsigned int i;
    uint16_t count;
    int r;

    SOL_NULL_CHECK(path_str, -EINVAL);
    SOL_NULL_CHECK(req, -EINVAL);

    r = sol_coap_find_options(req, SOL_COAP_OPTION_URI_PATH, options, max_count);
    SOL_INT_CHECK(r, < 0, r);
    SOL_INT_CHECK(r, > max_count, -EINVAL);
    count = r;

    path = alloca(sizeof(struct sol_str_slice) * (count + 1));
    path_len = 1;
    for (i = 0; i < count; i++) {
        path[i] = options[i];
        r = sol_util_size_add(path_len, options[i].len + 1, &path_len);
        SOL_INT_CHECK(r, < 0, r);
    }
    path[count] = SOL_STR_SLICE_STR(NULL, 0);

    r = sol_coap_path_to_buffer(path, &buf, 0, NULL);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    *path_str = sol_buffer_steal(&buf, NULL);
    if (!*path_str) {
        r = -EINVAL;
        goto error;
    }

    return 0;

error:
    sol_buffer_fini(&buf);
    free(*path_str);
    return r;
}

static int(*find_resource_cb(const struct sol_coap_packet *req,
    const struct sol_coap_resource *resource)) (
    void *data, struct sol_coap_server *server,
    const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr){
    uint8_t opcode;

    SOL_NULL_CHECK(resource, NULL);

    if (!uri_path_eq(req, resource->path))
        return NULL;

    sol_coap_header_get_code(req, &opcode);

    switch (opcode) {
    case SOL_COAP_METHOD_GET:
        return resource->get;
    case SOL_COAP_METHOD_POST:
        return resource->post;
    case SOL_COAP_METHOD_PUT:
        return resource->put;
    case SOL_COAP_METHOD_DELETE:
        return resource->del;
    }

    return NULL;
}

SOL_API struct sol_coap_packet *
sol_coap_packet_ref(struct sol_coap_packet *pkt)
{
    SOL_NULL_CHECK(pkt, NULL);

    pkt->refcnt++;
    return pkt;
}

SOL_API void
sol_coap_packet_unref(struct sol_coap_packet *pkt)
{
    SOL_NULL_CHECK(pkt);

    if (pkt->refcnt > 1) {
        pkt->refcnt--;
        return;
    }

    sol_buffer_fini(&pkt->buf);
    free(pkt);
}

static struct sol_coap_packet *
packet_new(struct sol_buffer *buf)
{
    struct sol_coap_packet *pkt;
    int r = 0;

    pkt = calloc(1, sizeof(struct sol_coap_packet));
    SOL_NULL_CHECK_ERRNO(pkt, ENOMEM, NULL);

    pkt->refcnt = 1;
    pkt->buf = buf ? *buf :
        SOL_BUFFER_INIT_FLAGS(NULL, 0, SOL_BUFFER_FLAGS_NO_NUL_BYTE);

    r = sol_buffer_ensure(&pkt->buf, sizeof(struct coap_header));
    SOL_INT_CHECK_GOTO(r, < 0, err);

    memset(pkt->buf.data, 0, sizeof(struct coap_header));

    r = sol_coap_header_set_version(pkt, COAP_VERSION);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    pkt->buf.used = sizeof(struct coap_header);

    return pkt;

err:
    sol_buffer_fini(&pkt->buf);
    free(pkt);
    errno = r;
    return NULL;
}

SOL_API struct sol_coap_packet *
sol_coap_packet_new(struct sol_coap_packet *old)
{
    struct sol_coap_packet *pkt;
    int r;

    pkt = packet_new(NULL);
    SOL_NULL_CHECK(pkt, NULL);

    if (old) {
        uint8_t *token;
        uint8_t type;
        uint8_t tkl;
        uint16_t id;

        sol_coap_header_get_id(old, &id);

        r = sol_coap_header_set_id(pkt, id);
        SOL_INT_CHECK_GOTO(r, < 0, err);
        sol_coap_header_get_type(old, &type);
        if (type == SOL_COAP_MESSAGE_TYPE_CON)
            r = sol_coap_header_set_type(pkt, SOL_COAP_MESSAGE_TYPE_ACK);
        else if (type == SOL_COAP_MESSAGE_TYPE_NON_CON)
            r = sol_coap_header_set_type(pkt, SOL_COAP_MESSAGE_TYPE_NON_CON);
        SOL_INT_CHECK_GOTO(r, < 0, err);
        token = sol_coap_header_get_token(old, &tkl);
        if (token) {
            r = sol_coap_header_set_token(pkt, token, tkl);
            SOL_NULL_CHECK_GOTO(token, err);
        }
    }

    return pkt;

err:
    errno = -r;
    free(pkt);
    return NULL;
}

static void
outgoing_free(struct outgoing *outgoing)
{
    if (outgoing->timeout)
        sol_timeout_del(outgoing->timeout);

    sol_coap_packet_unref(outgoing->pkt);

    if (outgoing->header)
        sol_coap_packet_unref(outgoing->header);

    free(outgoing);
}

static struct outgoing *
next_in_queue(struct sol_coap_server *server, int *idx)
{
    struct outgoing *o;
    uint16_t i;

    SOL_NULL_CHECK(idx, NULL);

    SOL_PTR_VECTOR_FOREACH_IDX (&server->outgoing, o, i) {
        /* The timeout expired, time to try again. */
        if (!o->timeout) {
            *idx = i;
            return o;
        }
    }
    return NULL;
}

static bool
timeout_cb(void *data)
{
    uint16_t id;
    struct outgoing *outgoing = data;
    struct sol_coap_server *server = outgoing->server;

    SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

    outgoing->timeout = NULL;

    sol_socket_set_write_monitor(server->socket, true);

    sol_network_link_addr_to_str(&outgoing->cliaddr, &addr);

    sol_coap_header_get_id(outgoing->pkt, &id);

    SOL_DBG("server %p retrying packet id %d to client %.*s",
        server, id,
        SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)));

    return false;
}

static void
pending_reply_free(struct pending_reply *reply)
{
    if (!reply)
        return;

    if (reply->observing)
        free(reply->path);

    if (reply->timeout)
        sol_timeout_del(reply->timeout);

    sol_ptr_vector_remove(&reply->server->pending, reply);

    free(reply);
}

/* This is mostly for !CON packets, which do not go to the outgoing
 * list but also keep a context of their own, for response handling */
static bool
pending_timeout_cb(void *data)
{
    struct pending_reply *reply = data;
    struct sol_coap_server *server = reply->server;
    bool callback_result;

    SOL_REENTRANT_CALL(reply->reentrant) {
        callback_result = reply->cb((void *)reply->data, server, NULL, NULL);
    }

    if (callback_result && !reply->reentrant.delete_me) {
        return true;
    }

    SOL_REENTRANT_FREE(reply->reentrant) {
        pending_reply_free(reply);
    }

    return false;
}

static bool
timeout_expired(struct sol_coap_server *server, struct outgoing *outgoing)
{
    struct outgoing *o;
    int timeout;
    uint16_t i, id;
    uint8_t type;
    bool expired = false;

    sol_coap_header_get_type(outgoing->pkt, &type);
    /* no re-transmissions for !CON packets, we just keep a
     * pending_reply for a while */
    if (type != SOL_COAP_MESSAGE_TYPE_CON)
        return false;

    timeout = ACK_TIMEOUT_MS << outgoing->counter++;

    if (outgoing->counter > MAX_RETRANSMIT) {
        SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&server->outgoing, o, i) {
            if (o == outgoing) {
                sol_coap_header_get_id(outgoing->pkt, &id);
                SOL_DBG("packet id %d dropped, after %d transmissions",
                    id, outgoing->counter);

                sol_ptr_vector_del(&server->outgoing, i);
                outgoing_free(o);
                return true;
            }
        }
    }

    outgoing->timeout = sol_timeout_add(timeout, timeout_cb, outgoing);

    sol_coap_header_get_id(outgoing->pkt, &id);
    SOL_DBG("waiting %d ms to re-try packet id %d", timeout, id);

    return expired;
}

static int
prepare_buffer(struct outgoing *outgoing, struct sol_buffer *buffer)
{
    struct sol_coap_packet *header = outgoing->header;
    struct sol_coap_packet *payload = outgoing->pkt;
    uint16_t new_size, new_offset, old_offset;
    uint8_t new_tkl, old_tkl;
    uint8_t *buf_data;
    int r;

    if (!header) {
        sol_buffer_init_flags(buffer, payload->buf.data, payload->buf.used,
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
        buffer->used = payload->buf.used;
        return 0;
    }

    (void)sol_coap_header_get_token(payload, &old_tkl);
    (void)sol_coap_header_get_token(header, &new_tkl);

    new_size = payload->buf.used + (new_tkl - old_tkl);

    buf_data = malloc(new_size);
    SOL_NULL_CHECK(buf_data, -ENOMEM);

    sol_buffer_init_flags(buffer, buf_data, new_size,
        SOL_BUFFER_FLAGS_FIXED_CAPACITY | SOL_BUFFER_FLAGS_NO_NUL_BYTE);

    new_offset = sizeof(struct coap_header) + new_tkl;
    old_offset = sizeof(struct coap_header) + old_tkl;

    r = sol_buffer_append_bytes(buffer, header->buf.data, new_offset);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    r = sol_buffer_append_bytes(buffer, sol_buffer_at(&payload->buf, old_offset),
        payload->buf.used - old_offset);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    return 0;

error:
    sol_buffer_fini(buffer);
    return -ENOMEM;
}

static bool
on_can_write(void *data, struct sol_socket *s)
{
    struct sol_coap_server *server = data;
    struct outgoing *outgoing;
    struct sol_buffer buf;
    int err, idx;
    ssize_t ret;

    if (sol_ptr_vector_get_len(&server->outgoing) == 0)
        return false;

    while ((outgoing = next_in_queue(server, &idx))) {
        if (!timeout_expired(server, outgoing))
            break;
    }
    if (!outgoing)
        return false;

    err = prepare_buffer(outgoing, &buf);
    if (err)
        return true;

    ret = sol_socket_sendmsg(s, &buf, &outgoing->cliaddr);
    /* Eventually we are going to re-send it. */
    sol_buffer_fini(&buf);

    if ((ret < 0) && errno == EAGAIN)
        return true;

    SOL_DBG("CoAP packet sent (outgoing_len=%d, pending_len=%d)"
        " -- payload of %zu bytes, buffer holding it with %zu bytes",
        sol_ptr_vector_get_len(&server->outgoing),
        sol_ptr_vector_get_len(&server->pending),
        outgoing->pkt->buf.used, outgoing->pkt->buf.capacity);
    sol_coap_packet_debug(outgoing->pkt);
    if (ret < 0) {
        uint16_t id;
        SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

        sol_network_link_addr_to_str(&outgoing->cliaddr, &addr);
        sol_coap_header_get_id(outgoing->pkt, &id);
        SOL_WRN("Could not send packet %d to %.*s (%d): %s", id,
            SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)), -err, sol_util_strerrora(-err));
        return false;
    }

    /* According to RFC 7641, section 4.5, "since RESET messages are
     * transmitted unreliably, the client must be prepared in case the
     * these are not received by the server. Thus, a server can always
     * pretend that a RESET message rejecting a non-confirmable
     * notification was lost. If a server does this, it could
     * accelerate cancellation by sending the following notifications
     * to that client in confirmable messages".
     *
     * If 'timeout' is NULL, it means that the packet doesn't need to
     * be retransmitted. By taking this shortcut we reduce memory
     * usage A LOT and are able to run on very small devices with no
     * memory issues.
     */
    if (!outgoing->timeout) {
        sol_ptr_vector_del(&server->outgoing, idx);
        outgoing_free(outgoing);
    }

    if (sol_ptr_vector_get_len(&server->outgoing) == 0)
        return false;

    return true;
}

static int
enqueue_packet(struct sol_coap_server *server, struct sol_coap_packet *pkt,
    struct sol_coap_packet *header,
    const struct sol_network_link_addr *cliaddr)
{
    struct outgoing *outgoing;
    int r;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(cliaddr, -EINVAL);

    outgoing = calloc(1, sizeof(*outgoing));
    SOL_NULL_CHECK(outgoing, -ENOMEM);

    r = sol_ptr_vector_append(&server->outgoing, outgoing);
    if (r < 0) {
        free(outgoing);
        return r;
    }

    outgoing->server = server;

    memcpy(&outgoing->cliaddr, cliaddr, sizeof(*cliaddr));

    outgoing->pkt = sol_coap_packet_ref(pkt);
    if (header)
        outgoing->header = sol_coap_packet_ref(header);

    sol_socket_set_write_monitor(server->socket, true);

    return 0;
}

SOL_API int
sol_coap_send_packet_with_reply(struct sol_coap_server *server, struct sol_coap_packet *pkt,
    const struct sol_network_link_addr *cliaddr,
    bool (*reply_cb)(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr), const void *data)
{
    struct pending_reply *reply = NULL;
    struct sol_str_slice option = {};
    bool observing = false;
    uint8_t tkl, *token;
    int err = -EINVAL, count;

    SOL_NULL_CHECK(pkt, -EINVAL);
    SOL_NULL_CHECK_GOTO(server, error_pkt);
    SOL_NULL_CHECK_GOTO(cliaddr, error_pkt);

    count = sol_coap_find_options(pkt, SOL_COAP_OPTION_OBSERVE, &option, 1);
    if (count < 0)
        goto error_pkt;

    /* Observing is enabled. */
    if (count == 1 && option.len == 1 && option.data[0] == 0)
        observing = true;

    if (!reply_cb) {
        if (observing) {
            SOL_WRN("Observing a resource without a callback.");
            goto error_pkt;
        }
        goto done;
    }

    token = sol_coap_header_get_token(pkt, &tkl);

    reply = calloc(1, sizeof(*reply) + tkl);
    if (!reply) {
        err = -ENOMEM;
        goto error_pkt;
    }

    sol_coap_header_get_id(pkt, &reply->id);
    reply->cb = reply_cb;
    reply->data = data;
    reply->observing = observing;
    reply->tkl = tkl;
    reply->server = server;
    reply->pkt = pkt;
    reply->timeout = sol_timeout_add(MAX_PKT_TIMEOUT_MS,
        pending_timeout_cb, reply);

    if (token)
        memcpy(reply->token, token, tkl);
    if (observing) {
        err = packet_extract_path(pkt, &reply->path);
        SOL_INT_CHECK_GOTO(err, < 0, error);
    }

done:
    err = enqueue_packet(server, pkt, NULL, cliaddr);
    if (err < 0) {
        SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

        sol_network_link_addr_to_str(cliaddr, &addr);
        SOL_WRN("Could not enqueue packet %p to %.*s (%d): %s", pkt,
            SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)), -err,
            sol_util_strerrora(-err));
        goto error;
    }

    if (reply) {
        err = sol_ptr_vector_append(&server->pending, reply);
        /*
         * FIXME: we have a dangling packet, that will be removed
         * when the reply comes, or as a last resort when the server is destoyed.
         */
        SOL_INT_CHECK_GOTO(err, < 0, error);
    }

    sol_coap_packet_unref(pkt);
    return 0;

error:
    if (reply) {
        SOL_REENTRANT_FREE(reply->reentrant) {
            pending_reply_free(reply);
        }
    }
error_pkt:
    sol_coap_packet_unref(pkt);
    return err;
}

static struct resource_context *
find_context(struct sol_coap_server *server, const struct sol_coap_resource *resource)
{
    struct resource_context *c;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&server->contexts, c, i) {
        if (c->resource == resource)
            return c;
    }
    return NULL;
}

static int
sol_coap_notify_full(struct sol_coap_server *server,
    struct sol_coap_resource *resource, struct sol_coap_packet *pkt,
    int (*cb)(void *cb_data, struct sol_coap_server *server,
    struct sol_coap_resource *resource, struct sol_network_link_addr *addr,
    struct sol_coap_packet **pkt),
    const void *cb_data)
{
    struct resource_observer *o;
    struct resource_context *c;
    struct sol_coap_packet *header;
    uint16_t i;
    int r = 0;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(resource, -EINVAL);

    COAP_RESOURCE_CHECK_API(-EINVAL);

    c = find_context(server, resource);
    if (!c) {
        SOL_WRN("Context not found for specified resource");
        return -ENOENT;
    }

    SOL_PTR_VECTOR_FOREACH_IDX (&c->observers, o, i) {
        uint8_t type, code;

        if (cb) {
            struct sol_coap_packet *cb_pkt = NULL;

            r = cb((void *)cb_data, server, resource, &o->cliaddr, &cb_pkt);
            if (r < 0 && r != -EPERM) {
                SOL_WRN("Error creating notification packet. Reason: %d", r);
                sol_coap_packet_unref(cb_pkt);
                return r;
            } else if (r == -EPERM) {
                SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

                sol_network_link_addr_to_str(&o->cliaddr, &addr);
                SOL_WRN("Observer at %.*s is not authorized for CoAP Resource %p",
                    SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)), resource);
                continue;
            }

            r = sol_coap_header_set_token(cb_pkt, o->token, o->tkl);
            if (r < 0) {
                SOL_WRN("Could not set token %p for packet %p. Reason %d",
                    o->token, cb_pkt, r);
                sol_coap_packet_unref(cb_pkt);
                return r;
            }

            r = enqueue_packet(server, cb_pkt, NULL, &o->cliaddr);
            if (r < 0) {
                SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

                sol_network_link_addr_to_str(&o->cliaddr, &addr);
                SOL_WRN("Failed to enqueue packet %p to %.*s", cb_pkt,
                    SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)));

                sol_coap_packet_unref(cb_pkt);
                return r;
            }

            sol_coap_packet_unref(cb_pkt);
        } else {
            header = sol_coap_packet_new(NULL);
            SOL_NULL_CHECK(header, -ENOMEM);

            sol_coap_header_get_code(pkt, &code);
            r = sol_coap_header_set_code(header, code);
            SOL_INT_CHECK_GOTO(r, < 0, err_header);
            sol_coap_header_get_type(pkt, &type);
            r = sol_coap_header_set_type(header, type);
            SOL_INT_CHECK_GOTO(r, < 0, err_header);
            r = sol_coap_header_set_token(header, o->token, o->tkl);
            SOL_INT_CHECK_GOTO(r, < 0, err_header);

            r = enqueue_packet(server, pkt, header, &o->cliaddr);
            if (r < 0) {
                SOL_BUFFER_DECLARE_STATIC(addr, SOL_NETWORK_INET_ADDR_STR_LEN);

                sol_network_link_addr_to_str(&o->cliaddr, &addr);
                SOL_WRN("Failed to enqueue packet %p to %.*s", header,
                    SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr)));
                goto err_header;
            }

            sol_coap_packet_unref(header);
        }
    }

    return r;

err_header:
    sol_coap_packet_unref(header);
    return r;
}

SOL_API int
sol_coap_notify(struct sol_coap_server *server,
    struct sol_coap_resource *resource, struct sol_coap_packet *pkt)
{
    int r;

    SOL_NULL_CHECK(pkt, -EINVAL);

    r = sol_coap_notify_full(server, resource, pkt, NULL, NULL);

    sol_coap_packet_unref(pkt);

    return r;
}

SOL_API int
sol_coap_notify_by_callback(struct sol_coap_server *server,
    struct sol_coap_resource *resource,
    int (*cb)(void *cb_data, struct sol_coap_server *server,
    struct sol_coap_resource *resource, struct sol_network_link_addr *addr,
    struct sol_coap_packet **pkt),
    const void *cb_data)
{
    SOL_NULL_CHECK(cb, -EINVAL);

    return sol_coap_notify_full(server, resource, NULL, cb, cb_data);
}

SOL_API struct sol_coap_packet *
sol_coap_packet_new_notification(struct sol_coap_server *server, struct sol_coap_resource *resource)
{
    struct resource_context *c;
    struct sol_coap_packet *pkt;
    uint16_t id;
    int r;

    SOL_NULL_CHECK(resource, NULL);
    COAP_RESOURCE_CHECK_API(NULL);

    c = find_context(server, resource);
    SOL_NULL_CHECK(c, NULL);

    if (++c->age == UINT16_MAX)
        c->age = 2;

    id = sol_util_cpu_to_be16(c->age);

    pkt = sol_coap_packet_new(NULL);
    SOL_NULL_CHECK(pkt, NULL);

    r = sol_coap_header_set_type(pkt, SOL_COAP_MESSAGE_TYPE_NON_CON);
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    r = sol_coap_add_option(pkt, SOL_COAP_OPTION_OBSERVE, &id, sizeof(id));
    SOL_INT_CHECK_GOTO(r, < 0, err_exit);

    return pkt;

err_exit:
    sol_coap_packet_unref(pkt);
    return NULL;
}

SOL_API int
sol_coap_send_packet(struct sol_coap_server *server,
    struct sol_coap_packet *pkt,
    const struct sol_network_link_addr *cliaddr)
{
    return sol_coap_send_packet_with_reply(server, pkt, cliaddr, NULL, NULL);
}

SOL_API struct sol_coap_packet *
sol_coap_packet_new_request(enum sol_coap_method method, enum sol_coap_message_type type)
{
    static uint16_t request_id;
    struct sol_coap_packet *pkt;
    int r = 0;

    pkt = sol_coap_packet_new(NULL);
    SOL_NULL_CHECK(pkt, NULL);

    r = sol_coap_header_set_code(pkt, method);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    r = sol_coap_header_set_id(pkt, ++request_id);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    r = sol_coap_header_set_type(pkt, type);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    return pkt;

err:
    errno = -r;
    sol_coap_packet_unref(pkt);
    return NULL;
}

SOL_API int
sol_coap_add_option(struct sol_coap_packet *pkt, uint16_t code, const void *value, uint16_t len)
{
    struct option_context context = { .delta = 0,
                                      .used = 0 };
    int r, offset;

    SOL_NULL_CHECK(pkt, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    if (pkt->payload_start) {
        SOL_WRN("packet %p has a payload, would overwrite it", pkt);
        return -EINVAL;
    }

    offset = coap_get_header_len(pkt);
    if (offset < 0) {
        SOL_WRN("Failed to get header len from packet %p", pkt);
        return -EINVAL;
    }

    context.buf = &pkt->buf;
    context.pos = offset;

    while (context.delta <= code) {
        r = coap_parse_option(&context, NULL, NULL);
        if (r < 0)
            return -ENOENT;

        if (r == 0)
            break;

        /* If the new option code is out of order. */
        if (code < context.delta)
            return -EINVAL;
    }

    r = coap_option_encode(&context, code, value, len);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

SOL_API int
sol_coap_packet_add_uri_path_option(struct sol_coap_packet *pkt, const char *uri)
{
    const char *slash;

    SOL_NULL_CHECK(uri, -EINVAL);

    if (*uri != '/') {
        SOL_WRN("URIs must start with a '/'");
        return -EINVAL;
    }

    if (strlen(uri) == 1)
        return 0;

    for (uri++; *uri; uri = slash + 1) {
        int r;

        slash = strchr(uri, '/');
        if (!slash)
            return sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_PATH, uri, strchr(uri, '\0') - uri);

        r = sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_PATH, uri, slash - uri);
        SOL_INT_CHECK(r, < 0, r);
    }

    return -EINVAL;
}

SOL_API const void *
sol_coap_find_first_option(const struct sol_coap_packet *pkt, uint16_t code, uint16_t *len)
{
    struct sol_str_slice option = {};
    uint16_t count = 1;

    SOL_NULL_CHECK(pkt, NULL);
    SOL_NULL_CHECK(len, NULL);

    if (sol_coap_find_options(pkt, code, &option, count) <= 0)
        return NULL;

    *len = option.len;

    return option.data;
}

SOL_API int
sol_coap_find_options(const struct sol_coap_packet *pkt,
    uint16_t code,
    struct sol_str_slice *vec,
    uint16_t veclen)
{
    struct option_context context = { .delta = 0,
                                      .used = 0 };
    int used, count = 0;
    int hdrlen;
    uint16_t len;

    SOL_NULL_CHECK(vec, -EINVAL);
    SOL_NULL_CHECK(pkt, -EINVAL);

    hdrlen = coap_get_header_len(pkt);
    SOL_INT_CHECK(hdrlen, < 0, -EINVAL);

    context.buf = (struct sol_buffer *)&pkt->buf;
    context.pos = hdrlen;

    while (context.delta <= code && count < veclen) {
        used = coap_parse_option(&context, (uint8_t **)&vec[count].data,
            &len);
        vec[count].len = len;
        if (used < 0)
            return -ENOENT;

        if (used == 0)
            break;

        if (code != context.delta)
            continue;

        count++;
    }

    return count;
}

static int
well_known_get(void *data, struct sol_coap_server *server,
    const struct sol_coap_resource *resource, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
{
    struct resource_context *c;
    struct sol_coap_packet *resp;
    struct sol_buffer *buf;
    size_t offset;
    uint16_t i;
    int r;

    resp = sol_coap_packet_new(req);
    if (!resp) {
        SOL_WRN("Could not build response packet");
        return -EINVAL;
    }

    r = sol_coap_header_set_code(resp, SOL_COAP_RESPONSE_CODE_CONTENT);
    if (r < 0) {
        SOL_WRN("Failed to set header code on packet %p", resp);
        sol_coap_packet_unref(resp);
        return r;
    }

    r = sol_coap_packet_get_payload(resp, &buf, &offset);
    if (r < 0) {
        SOL_WRN("Failed to get payload from packet %p", resp);
        sol_coap_packet_unref(resp);
        return r;
    }

    SOL_VECTOR_FOREACH_IDX (&server->contexts, c, i) {
        size_t tmp = 0;
        const struct sol_coap_resource *res = c->resource;

        if (!(res->flags & SOL_COAP_FLAGS_WELL_KNOWN))
            continue;

        r = sol_buffer_insert_char(buf, offset++, '<');
        SOL_INT_CHECK_GOTO(r, < 0, error);

        r = sol_coap_path_to_buffer(res->path, buf, offset, &tmp);
        SOL_INT_CHECK_GOTO(r, < 0, error);
        offset += tmp;

        r = sol_buffer_insert_char(buf, offset++, '>');
        SOL_INT_CHECK_GOTO(r, < 0, error);

        if (i < server->contexts.len) {
            r = sol_buffer_insert_char(buf, offset++, ',');
            SOL_INT_CHECK_GOTO(r, < 0, error);
        }
    }

    return sol_coap_send_packet(server, resp, cliaddr);

error:
    sol_coap_header_set_code(resp, SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR);
    return sol_coap_send_packet(server, resp, cliaddr);
}

static const struct sol_coap_resource well_known = {
    .path = {
        SOL_STR_SLICE_LITERAL(".well-known"),
        SOL_STR_SLICE_LITERAL("core"),
        SOL_STR_SLICE_EMPTY
    },
    .get = well_known_get,
};

static int
get_observe_option(struct sol_coap_packet *pkt)
{
    struct sol_str_slice option = {};
    int r;
    uint16_t count = 1;

    SOL_NULL_CHECK(pkt, -EINVAL);

    r = sol_coap_find_options(pkt, SOL_COAP_OPTION_OBSERVE, &option, count);
    if (r <= 0)
        return -ENOENT;

    /* The value is in the network order, and has at max 3 bytes. */
    switch (option.len) {
    case 0:
        return 0;
    case 1:
        return option.data[0];
    case 2:
        return (option.data[0] << 0) | (option.data[1] << 8);
    case 3:
        return (option.data[0] << 0) | (option.data[1] << 8) | (option.data[2] << 16);
    default:
        return -EINVAL;
    }

    return -ENOENT;
}

static int
register_observer(struct resource_context *c, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr, int observe)
{
    struct resource_observer *o;
    uint16_t i;
    uint8_t *token, tkl;
    int r;

    token = sol_coap_header_get_token(req, &tkl);

    /* Avoid registering the same observer more than once */
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&c->observers, o, i) {
        if (!memcmp(&o->cliaddr, cliaddr, sizeof(*cliaddr))
            && tkl == o->tkl && !memcmp(token, o->token, tkl)) {
            /* remove if '1', yeah, makes sense. */
            if (observe == 1) {
                sol_ptr_vector_del(&c->observers, i);
                free(o);
            }
            return 0;
        }
    }

    o = calloc(1, sizeof(*o) + tkl);
    SOL_NULL_CHECK(o, -ENOMEM);

    o->tkl = tkl;
    if (tkl)
        memcpy(o->token, token, tkl);

    memcpy(&o->cliaddr, cliaddr, sizeof(*cliaddr));

    r = sol_ptr_vector_append(&c->observers, o);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    return 0;

error:
    free(o);
    return -ENOMEM;
}

static bool
match_reply(struct pending_reply *reply, struct sol_coap_packet *pkt)
{
    uint16_t id;

    sol_coap_header_get_id(pkt, &id);

    /* When observing the match is made using the token. */
    if (reply->observing) {
        uint8_t tkl, *token;
        token = sol_coap_header_get_token(pkt, &tkl);
        return tkl == reply->tkl && !memcmp(token, reply->token, tkl);
    }

    return reply->id == id;
}

static bool
match_observe_reply(struct pending_reply *reply, uint8_t *token, uint8_t tkl)
{
    if (!reply->observing)
        return false;

    return tkl == reply->tkl && !memcmp(token, reply->token, tkl);
}

static int
resource_not_found(struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr,
    struct sol_coap_server *server)
{
    struct sol_coap_packet *resp;
    int r;

    resp = sol_coap_packet_new(req);
    SOL_NULL_CHECK(resp, -ENOMEM);

    r = sol_coap_header_set_code(resp, SOL_COAP_RESPONSE_CODE_NOT_FOUND);
    SOL_INT_CHECK_GOTO(r, < 0, err);

    return sol_coap_send_packet(server, resp, cliaddr);

err:
    sol_coap_packet_unref(resp);
    return r;
}

static void
remove_outgoing_confirmable_packet(struct sol_coap_server *server, struct sol_coap_packet *req)
{
    uint16_t i, id;
    struct outgoing *o;

    sol_coap_header_get_id(req, &id);
    /* If it has the same 'id' as a packet that we are trying to send we will stop now. */
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&server->outgoing, o, i) {
        uint8_t type;
        uint16_t o_id;

        sol_coap_header_get_type(o->pkt, &type);
        sol_coap_header_get_id(o->pkt, &o_id);

        if (id != o_id || type != SOL_COAP_MESSAGE_TYPE_CON) {
            continue;
        }

        SOL_DBG("Received ACK for packet id %d", id);

        sol_ptr_vector_del(&server->outgoing, i);
        outgoing_free(o);
        return;
    }
}

static int
send_unobserve_packet(struct sol_coap_server *server, const struct sol_network_link_addr *cliaddr, const char *path, uint8_t *token, uint8_t tkl)
{
    struct sol_coap_packet *req;
    uint8_t reg = 1;
    int r;

    req = sol_coap_packet_new_request(SOL_COAP_METHOD_GET, SOL_COAP_MESSAGE_TYPE_CON);
    SOL_NULL_CHECK(req, -ENOMEM);

    r = sol_coap_header_set_token(req, token, tkl);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    r = sol_coap_add_option(req, SOL_COAP_OPTION_OBSERVE, &reg, sizeof(reg));
    SOL_INT_CHECK_GOTO(r, < 0, error);

    r = sol_coap_packet_add_uri_path_option(req, path);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    return sol_coap_send_packet(server, req, cliaddr);

error:
    sol_coap_packet_unref(req);
    return -EINVAL;
}

static bool
is_coap_ping(struct sol_coap_packet *req)
{
    uint8_t tokenlen, type, code;

    (void)sol_coap_header_get_token(req, &tokenlen);
    (void)sol_coap_header_get_type(req, &type);
    (void)sol_coap_header_get_code(req, &code);

    return type == SOL_COAP_MESSAGE_TYPE_CON &&
           code == SOL_COAP_CODE_EMPTY &&
           tokenlen == 0 && !sol_coap_packet_has_payload(req);
}

static int
send_reset_msg(struct sol_coap_server *server, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
{
    struct sol_coap_packet *reset;
    int r;

    reset = sol_coap_packet_new(req);
    SOL_NULL_CHECK(reset, -ENOMEM);
    r = sol_coap_header_set_type(reset, SOL_COAP_MESSAGE_TYPE_RESET);
    SOL_INT_CHECK_GOTO(r, < 0, err);
    return sol_coap_send_packet(server, reset, cliaddr);

err:
    sol_coap_packet_unref(reset);
    return r;
}

static int
respond_packet(struct sol_coap_server *server, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
{
    int (*cb)(void *data, struct sol_coap_server *server,
        const struct sol_coap_resource *resource,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr);
    struct pending_reply *reply;
    struct resource_context *c;
    int observe, r = 0;
    uint16_t i;
    uint8_t code;
    bool remove_outgoing = true;

    if (is_coap_ping(req)) {
        SOL_DBG("Coap ping, sending pong");
        return send_reset_msg(server, req, cliaddr);
    }

    sol_coap_header_get_code(req, &code);

    observe = get_observe_option(req);

    /* If it isn't a request. */
    if (code & ~SOL_COAP_REQUEST_MASK) {
        bool found_reply = false;
        SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&server->pending, reply, i) {
            bool reply_callback_result = false;

            if (!match_reply(reply, req))
                continue;

            SOL_REENTRANT_CALL(reply->reentrant) {
                reply_callback_result =
                    reply->cb((void *)reply->data, server, req, cliaddr);
            }

            if (!reply_callback_result) {
                if (reply->observing) {
                    r = send_unobserve_packet(server, cliaddr, reply->path,
                        reply->token, reply->tkl);
                    if (r < 0)
                        SOL_WRN("Could not unobserve packet.");
                }
                SOL_REENTRANT_FREE(reply->reentrant) {
                    pending_reply_free(reply);
                }
            } else if (!reply->observing) {
                remove_outgoing = false;
            } else {
                /*
                   This means that the user wishes to continue observing that resource,
                   so we don't need to keep the reply timeout around.
                 */
                if (reply->timeout) {
                    sol_timeout_del(reply->timeout);
                    reply->timeout = NULL;
                }
            }

            found_reply = true;
        }

        /*
           If we sent a request and we received a reply,
           the request must be removed from the outgoing list.
         */
        if (remove_outgoing)
            remove_outgoing_confirmable_packet(server, req);

        if (observe >= 0 && !found_reply) {
            SOL_DBG("Observing message, but no one is waiting for reply. Reseting.");
            return send_reset_msg(server, req, cliaddr);
        }
        return 0;
    }

    /*
       When a request is made, the receiver may reply with an ACK
       and an empty code. This indicates that the receiver is aware of
       the request, however it will send the data later.
       In this case, the request can be removed from the outgoing list.
     */
    if (code == SOL_COAP_CODE_EMPTY) {
        remove_outgoing_confirmable_packet(server, req);
        return 0;
    }

    /* /.well-known/core well known resource */
    cb = find_resource_cb(req, &well_known);
    if (cb)
        return cb(NULL, server, &well_known, req, cliaddr);

    SOL_VECTOR_FOREACH_IDX (&server->contexts, c, i) {
        const struct sol_coap_resource *resource = c->resource;

        cb = find_resource_cb(req, resource);
        if (!cb)
            continue;

        if (observe >= 0)
            register_observer(c, req, cliaddr, observe);

        return cb((void *)c->data, server, resource, req, cliaddr);
    }

    if (server->unknown_handler)
        return server->unknown_handler((void *)server->unknown_handler_data,
            server, req, cliaddr);

    return resource_not_found(req, cliaddr, server);
}

static bool
on_can_read(void *data, struct sol_socket *s)
{
    struct sol_coap_server *server = data;
    struct sol_network_link_addr cliaddr = { 0 };
    struct sol_coap_packet *pkt;
    ssize_t len;
    int err;

    pkt = sol_coap_packet_new(NULL);
    SOL_NULL_CHECK(pkt, true); /* It may possible that in the next
                                * round there is enough memory. */

    /* FIXME: currently struct sol_socket does not record the socket
     * type. Maybe it should when we support more types than just
     * datagrams, since this *calculate exact needed size* step would
     * have to change on those cases. */

    /* store at the beginning of the buffer and reset 'used' */
    len = sol_socket_recvmsg(s, &pkt->buf, &cliaddr);
    SOL_INT_CHECK_GOTO(len, < 0, err_recv);

    err = coap_packet_parse(pkt);
    if (err < 0) {
        SOL_WRN("Failure parsing coap packet");
        sol_coap_packet_unref(pkt);
        return true;
    }

    SOL_DBG("pkt received and parsed successfully");
    sol_coap_packet_debug(pkt);

    err = respond_packet(server, pkt, &cliaddr);
    if (err < 0) {
        errno = -err;
        SOL_WRN("Couldn't respond to packet (%d): %s", -err, sol_util_strerrora(errno));
    }

    sol_coap_packet_unref(pkt);

    return true;

err_recv:
    err = -len;
    SOL_WRN("Could not read from socket (%d): %s", err,
        sol_util_strerrora(err));
    sol_coap_packet_unref(pkt);
    return true;
}

SOL_API struct sol_coap_server *
sol_coap_server_ref(struct sol_coap_server *server)
{
    SOL_NULL_CHECK(server, NULL);

    server->refcnt++;
    return server;
}

static void
destroy_context(struct resource_context *context)
{
    struct resource_observer *o;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&context->observers, o, i) {
        free(o);
    }
    sol_ptr_vector_clear(&context->observers);
}

static void
sol_coap_server_destroy(struct sol_coap_server *server)
{
    struct resource_context *c;
    struct pending_reply *reply;
    struct outgoing *o;
    uint16_t i;

    sol_socket_del(server->socket);

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&server->outgoing, o, i) {
        sol_ptr_vector_del(&server->outgoing, i);
        outgoing_free(o);
    }

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&server->pending, reply, i) {
        SOL_REENTRANT_CALL(reply->reentrant) {
            reply->cb((void *)reply->data, server, NULL, NULL);
        }
        SOL_REENTRANT_FREE(reply->reentrant) {
            pending_reply_free(reply);
        }
    }

    SOL_VECTOR_FOREACH_REVERSE_IDX (&server->contexts, c, i) {
        destroy_context(c);
    }

    sol_vector_clear(&server->contexts);
    free(server);
}

SOL_API void
sol_coap_server_unref(struct sol_coap_server *server)
{
    SOL_NULL_CHECK(server);

    if (server->refcnt > 1) {
        server->refcnt--;
        return;
    }

    sol_coap_server_destroy(server);
}

static int
join_mcast_groups(struct sol_socket *s, const struct sol_network_link *link)
{
    struct sol_network_link_addr groupaddr = { };
    struct sol_network_link_addr *addr;
    uint16_t i;
    int ret;

    if (!(link->flags & SOL_NETWORK_LINK_RUNNING) && !(link->flags & SOL_NETWORK_LINK_MULTICAST))
        return 0;

    SOL_VECTOR_FOREACH_IDX (&link->addrs, addr, i) {
        groupaddr.family = addr->family;

        if (addr->family == SOL_NETWORK_FAMILY_INET) {
            sol_network_link_addr_from_str(&groupaddr, IPV4_ALL_COAP_NODES_GROUP);
            if ((ret = sol_socket_join_group(s, link->index, &groupaddr)) < 0)
                return ret;

            continue;
        }

        sol_network_link_addr_from_str(&groupaddr, IPV6_ALL_COAP_NODES_SCOPE_LOCAL);
        if ((ret = sol_socket_join_group(s, link->index, &groupaddr)) < 0)
            return ret;

        sol_network_link_addr_from_str(&groupaddr, IPV6_ALL_COAP_NODES_SCOPE_SITE);
        if ((ret = sol_socket_join_group(s, link->index, &groupaddr)) < 0)
            return ret;
    }

    return 0;
}

static void
network_event(void *data, const struct sol_network_link *link, enum sol_network_event event)
{
    struct sol_coap_server *server = data;

    if (event != SOL_NETWORK_LINK_ADDED && event != SOL_NETWORK_LINK_CHANGED)
        return;

    if (!(link->flags & SOL_NETWORK_LINK_RUNNING) && !(link->flags & SOL_NETWORK_LINK_MULTICAST))
        return;

    join_mcast_groups(server->socket, link);
}

static struct sol_coap_server *
sol_coap_server_new_full(struct sol_socket_ip_options *options, const struct sol_network_link_addr *servaddr)
{
    const struct sol_vector *links;
    struct sol_network_link *link;
    struct sol_coap_server *server;
    struct sol_socket *s;
    int ret = 0;
    uint16_t i;

    SOL_LOG_INTERNAL_INIT_ONCE;

    server = calloc(1, sizeof(*server));
    SOL_NULL_CHECK_ERRNO(server, ENOMEM, NULL);

    options->base.data = server;
    s = sol_socket_ip_new(&options->base);
    if (!s) {
        SOL_WRN("Could not create socket (%d): %s", errno, sol_util_strerrora(errno));
        ret = -errno;
        goto err;
    }

    if ((ret = sol_socket_bind(s, servaddr)) < 0) {
        SOL_WRN("Could not bind socket (%d): %s", -ret, sol_util_strerrora(-ret));
        goto err_bind;
    }

    SOL_DBG("server=%p, socket=%p, addr=%p, port=%" PRIu16 ", reuse_addr=%s binded!",
        server, s, servaddr, servaddr->port, options->reuse_addr ? "True" : "False");

    server->refcnt = 1;

    sol_vector_init(&server->contexts, sizeof(struct resource_context));

    sol_ptr_vector_init(&server->pending);
    sol_ptr_vector_init(&server->outgoing);

    server->socket = s;
    ret = sol_socket_set_read_monitor(s, true);
    SOL_INT_CHECK_GOTO(ret, < 0, err_monitor);

    /* If secure is enabled it's only a unicast server. */
    if (!options->secure && servaddr->port) {
        /* From man 7 ip:
         *
         *   imr_address is the address of the local interface with which the
         *   system should join the  multicast  group;  if  it  is  equal  to
         *   INADDR_ANY,  an  appropriate  interface is chosen by the system.
         *
         * We can't join a multicast group on every interface. In the future
         * we may want to add a default multicast route to the system and use
         * that interface.
         */
        links = sol_network_get_available_links();

        if (links) {
            SOL_VECTOR_FOREACH_IDX (links, link, i) {
                /* Not considering an error,
                 * because direct packets will work still.
                 */
                if ((ret = join_mcast_groups(s, link)) < 0) {
                    char *name = sol_network_link_get_name(link);
                    SOL_WRN("Could not join multicast group, iface %s (%d): %s",
                        name, -ret, sol_util_strerrora(-ret));
                    free(name);
                }
            }
        }
    }

    sol_network_subscribe_events(network_event, server);

    server->secure = options->secure;

    SOL_DBG("New server %p on port %d%s", server, servaddr->port,
        !options->secure ? "" : " (secure)");

    return server;

err_monitor:
err_bind:
    sol_socket_del(s);
err:
    free(server);
    errno = -ret;
    return NULL;
}

SOL_API struct sol_coap_server *
sol_coap_server_new(const struct sol_network_link_addr *addr,
    bool secure)
{
    return sol_coap_server_new_full(&((struct sol_socket_ip_options) {
            .base = {
                SOL_SET_API_VERSION(.api_version = SOL_SOCKET_OPTIONS_API_VERSION, )
                SOL_SET_API_VERSION(.sub_api = SOL_SOCKET_IP_OPTIONS_SUB_API_VERSION, )
                .on_can_read = on_can_read,
                .on_can_write = on_can_write,
            },
            .family = addr->family,
            .secure = secure,
            .cipher_suites = secure ?
            (enum sol_socket_dtls_cipher []){ SOL_SOCKET_DTLS_CIPHER_PSK_AES128_CCM8 } : NULL,
            .cipher_suites_len = secure ? 1 : 0,
            .reuse_addr = addr->port ? true : false,
        }), addr);
}

SOL_API struct sol_coap_server *
sol_coap_server_new_by_cipher_suites(
    const struct sol_network_link_addr *addr,
    enum sol_socket_dtls_cipher *cipher_suites, uint16_t cipher_suites_len)
{
    return sol_coap_server_new_full(&((struct sol_socket_ip_options) {
            .base = {
                SOL_SET_API_VERSION(.api_version = SOL_SOCKET_OPTIONS_API_VERSION, )
                SOL_SET_API_VERSION(.sub_api = SOL_SOCKET_IP_OPTIONS_SUB_API_VERSION, )
                .on_can_read = on_can_read,
                .on_can_write = on_can_write,
            },
            .family = addr->family,
            .secure = true,
            .cipher_suites = cipher_suites,
            .cipher_suites_len = cipher_suites_len,
            .reuse_addr = addr->port ? true : false,
        }), addr);
}

SOL_API bool
sol_coap_packet_has_payload(struct sol_coap_packet *pkt)
{
    int offset;

    SOL_NULL_CHECK(pkt, false);

    offset = coap_get_header_len(pkt);
    if (offset < 0) {
        SOL_WRN("Failed to get header len from packet %p", pkt);
        return false;
    }

    return pkt->payload_start ||
           (uint8_t *)pkt->buf.data + pkt->buf.used >
           (uint8_t *)pkt->buf.data + offset;
}

SOL_API int
sol_coap_packet_get_payload(struct sol_coap_packet *pkt,
    struct sol_buffer **buf,
    size_t *offset)
{
    SOL_NULL_CHECK(pkt, -EINVAL);
    SOL_NULL_CHECK(buf, -EINVAL);

    if (!pkt->payload_start) {
        int r;
        r = sol_buffer_append_char(&pkt->buf, COAP_MARKER);
        SOL_INT_CHECK(r, < 0, r);

        pkt->payload_start = pkt->buf.used;
    }
    if (offset)
        *offset = pkt->payload_start;

    *buf = &pkt->buf;

    return 0;
}

SOL_API int
sol_coap_server_register_resource(struct sol_coap_server *server,
    const struct sol_coap_resource *resource, const void *data)
{
    struct resource_context *c;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(resource, -EINVAL);

    COAP_RESOURCE_CHECK_API(-EINVAL);

    if (find_context(server, resource)) {
        SOL_WRN("Attempting to register duplicate resource in CoAP server");
        return -EEXIST;
    }

    c = sol_vector_append(&server->contexts);
    SOL_NULL_CHECK(c, -ENOMEM);

    c->resource = resource;
    c->data = data;
    c->age = 2;

    sol_ptr_vector_init(&c->observers);

    return 0;
}

SOL_API int
sol_coap_server_unregister_resource(struct sol_coap_server *server,
    const struct sol_coap_resource *resource)
{
    struct resource_context *c;
    uint16_t idx;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(resource, -EINVAL);

    COAP_RESOURCE_CHECK_API(-EINVAL);

    SOL_VECTOR_FOREACH_REVERSE_IDX (&server->contexts, c, idx) {
        if (c->resource != resource)
            continue;

        destroy_context(c);
        sol_vector_del(&server->contexts, idx);

        return 0;
    }

    return -ENOENT;
}

SOL_API int
sol_coap_cancel_send_packet(struct sol_coap_server *server, struct sol_coap_packet *pkt, struct sol_network_link_addr *cliaddr)
{
    struct pending_reply *reply;
    uint16_t i, cancel = 0;
    struct outgoing *o;
    int r;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(pkt, -EINVAL);

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&server->outgoing, o, i) {
        uint16_t id;

        if (o->pkt != pkt)
            continue;

        sol_coap_header_get_id(pkt, &id);
        SOL_DBG("Packet with ID %d canceled", id);
        sol_ptr_vector_del(&server->outgoing, i);
        outgoing_free(o);
        cancel++;
    }

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&server->pending, reply, i) {
        /* match by pkt ref, here, once pkt may point to already freed
         * memory */
        if (reply->pkt != pkt)
            continue;

        if (reply->observing) {
            r = send_unobserve_packet(server, cliaddr, reply->path,
                reply->token, reply->tkl);
            if (r < 0)
                SOL_WRN("Could not unobserve packet.");
        }
        SOL_REENTRANT_FREE(reply->reentrant) {
            pending_reply_free(reply);
        }
        cancel++;
    }

    return cancel ? 0 : -ENOENT;
}

SOL_API int
sol_coap_unobserve_by_token(struct sol_coap_server *server, const struct sol_network_link_addr *cliaddr, uint8_t *token, uint8_t tkl)
{
    int r;
    uint16_t i;
    struct pending_reply *reply;

    SOL_NULL_CHECK(server, -EINVAL);

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&server->pending, reply, i) {
        if (!match_observe_reply(reply, token, tkl))
            continue;

        SOL_REENTRANT_CALL(reply->reentrant) {
            reply->cb((void *)reply->data, server, NULL, NULL);
        }

        r = send_unobserve_packet(server, cliaddr, reply->path, token, tkl);
        if (r < 0)
            SOL_WRN("Could not unobserve packet.");

        SOL_REENTRANT_FREE(reply->reentrant) {
            pending_reply_free(reply);
        }
        return r;
    }

    return -ENOENT;
}


SOL_API int
sol_coap_server_set_unknown_resource_handler(struct sol_coap_server *server,
    int (*handler)(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr),
    const void *data)
{
    SOL_NULL_CHECK(server, -EINVAL);

    server->unknown_handler = handler;
    server->unknown_handler_data = data;
    return 0;
}

#ifdef SOL_LOG_ENABLED
SOL_API void
sol_coap_packet_debug(struct sol_coap_packet *pkt)
{
    int r;
    uint8_t type, code;
    uint16_t query_len, id;
    char *path = NULL, *query;


    SOL_NULL_CHECK(pkt);

    if (sol_log_get_level() < SOL_LOG_LEVEL_DEBUG)
        return;

    query = (char *)sol_coap_find_first_option(pkt, SOL_COAP_OPTION_URI_QUERY, &query_len);
    if (!query)
        query_len = 0;

    r = packet_extract_path(pkt, &path);
    sol_coap_header_get_type(pkt, &type);
    sol_coap_header_get_id(pkt, &id);
    sol_coap_header_get_code(pkt, &code);
    SOL_DBG("{id: %d, href: '%s', type: %d, header_code: %d, query: '%.*s'}",
        id, r == 0 ? path : "", type, code, query_len, query);
    if (r == 0)
        free(path);
}
#endif
