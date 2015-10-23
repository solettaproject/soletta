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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_sol_coap_log_domain
#include "sol-log-internal.h"
#include "sol-mainloop.h"
#include "sol-network.h"
#include "sol-socket.h"
#include "sol-str-slice.h"
#include "sol-util.h"
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

#define COAP_RESOURCE_CHECK_API(...) \
    do { \
        if (unlikely(resource->api_version != \
            SOL_COAP_RESOURCE_API_VERSION)) { \
            SOL_WRN("Couldn't handle resource that has unsupported version " \
                "'%u', expected version is '%u'", \
                resource->api_version, SOL_COAP_RESOURCE_API_VERSION); \
            return __VA_ARGS__; \
        } \
    } while (0)

struct sol_coap_server {
    struct sol_vector contexts;
    struct sol_ptr_vector pending; /* waiting pending replies */
    struct sol_ptr_vector outgoing; /* in case we need to retransmit */
    struct sol_socket *socket;
    int refcnt;
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
    int (*cb)(struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr, void *data);
    const void *data;
    bool observing;
    uint16_t id;
    uint8_t tkl;
    uint8_t token[0];
};

struct outgoing {
    struct sol_coap_server *server;
    struct sol_coap_packet *pkt;
    struct sol_timeout *timeout;
    struct sol_network_link_addr cliaddr;
    int counter; /* How many times this packet was retransmited. */
};

static bool on_can_write(void *data, struct sol_socket *s);

SOL_API uint8_t
sol_coap_header_get_ver(const struct sol_coap_packet *pkt)
{
    struct coap_header *hdr = (struct coap_header *)pkt->buf;

    return hdr->ver;
}

SOL_API uint8_t
sol_coap_header_get_type(const struct sol_coap_packet *pkt)
{
    struct coap_header *hdr = (struct coap_header *)pkt->buf;

    return hdr->type;
}

SOL_API uint8_t *
sol_coap_header_get_token(const struct sol_coap_packet *pkt, uint8_t *len)
{
    struct coap_header *hdr = (struct coap_header *)pkt->buf;

    *len = hdr->tkl;
    if (hdr->tkl == 0)
        return NULL;

    return (uint8_t *)pkt->buf + sizeof(*hdr);
}

SOL_API void
sol_coap_header_set_ver(struct sol_coap_packet *pkt, uint8_t ver)
{
    struct coap_header *hdr = (struct coap_header *)pkt->buf;

    hdr->ver = ver;
}

SOL_API uint16_t
sol_coap_header_get_id(const struct sol_coap_packet *pkt)
{
    struct coap_header *hdr = (struct coap_header *)pkt->buf;

    return ntohs(hdr->id);
}

SOL_API uint8_t
sol_coap_header_get_code(const struct sol_coap_packet *pkt)
{
    struct coap_header *hdr = (struct coap_header *)pkt->buf;
    uint8_t code = hdr->code;

    switch (code) {
    /* Methods are encoded in the code field too */
    case SOL_COAP_METHOD_GET:
    case SOL_COAP_METHOD_POST:
    case SOL_COAP_METHOD_PUT:
    case SOL_COAP_METHOD_DELETE:

    /* All the defined response codes */
    case SOL_COAP_RSPCODE_OK:
    case SOL_COAP_RSPCODE_CREATED:
    case SOL_COAP_RSPCODE_DELETED:
    case SOL_COAP_RSPCODE_VALID:
    case SOL_COAP_RSPCODE_CHANGED:
    case SOL_COAP_RSPCODE_CONTENT:
    case SOL_COAP_RSPCODE_BAD_REQUEST:
    case SOL_COAP_RSPCODE_UNAUTHORIZED:
    case SOL_COAP_RSPCODE_BAD_OPTION:
    case SOL_COAP_RSPCODE_FORBIDDEN:
    case SOL_COAP_RSPCODE_NOT_FOUND:
    case SOL_COAP_RSPCODE_NOT_ALLOWED:
    case SOL_COAP_RSPCODE_NOT_ACCEPTABLE:
    case SOL_COAP_RSPCODE_PRECONDITION_FAILED:
    case SOL_COAP_RSPCODE_REQUEST_TOO_LARGE:
    case SOL_COAP_RSPCODE_INTERNAL_ERROR:
    case SOL_COAP_RSPCODE_NOT_IMPLEMENTED:
    case SOL_COAP_RSPCODE_BAD_GATEWAY:
    case SOL_COAP_RSPCODE_SERVICE_UNAVAILABLE:
    case SOL_COAP_RSPCODE_GATEWAY_TIMEOUT:
    case SOL_COAP_RSPCODE_PROXYING_NOT_SUPPORTED:
        return code;
    default:
        SOL_WRN("Invalid code (%d)", code);
        return 0;
    }
}

SOL_API void
sol_coap_header_set_id(struct sol_coap_packet *pkt, uint16_t id)
{
    struct coap_header *hdr = (struct coap_header *)pkt->buf;

    hdr->id = htons(id);
}

SOL_API void
sol_coap_header_set_type(struct sol_coap_packet *pkt, uint8_t type)
{
    struct coap_header *hdr = (struct coap_header *)pkt->buf;

    hdr->type = type;
}

SOL_API void
sol_coap_header_set_code(struct sol_coap_packet *pkt, uint8_t code)
{
    struct coap_header *hdr = (struct coap_header *)pkt->buf;

    hdr->code = code;
}

SOL_API bool
sol_coap_header_set_token(struct sol_coap_packet *pkt, uint8_t *token, uint8_t tokenlen)
{
    struct coap_header *hdr = (struct coap_header *)pkt->buf;

    if (pkt->payload.size < sizeof(*hdr) + tokenlen)
        return false;

    pkt->payload.used += tokenlen;

    hdr->tkl = tokenlen;
    memcpy(pkt->buf + sizeof(*hdr), token, tokenlen);

    return true;
}

static bool
uri_path_eq(const struct sol_coap_packet *req, const struct sol_str_slice path[])
{
    struct sol_coap_option_value options[16];
    unsigned int i;
    int r;
    uint16_t count = 16;

    SOL_NULL_CHECK(req, false);
    SOL_NULL_CHECK(path, false);

    r = coap_find_options(req, SOL_COAP_OPTION_URI_PATH, options, count);
    if (r < 0)
        return false;

    count = r;

    for (i = 0; path[i].len && i < count; i++) {
        const struct sol_coap_option_value *v = &options[i];
        const struct sol_str_slice s = SOL_STR_SLICE_STR((char *)v->value, v->len);

        if (!sol_str_slice_eq(s, path[i]))
            return false;
    }

    return path[i].len == 0 && i == count;
}

SOL_API int
sol_coap_uri_path_to_buf(const struct sol_str_slice path[],
    uint8_t *buf, size_t buflen)
{
    size_t cur;
    unsigned int i;

    SOL_NULL_CHECK(path, 0);
    SOL_NULL_CHECK(buf, 0);

    cur = 0;

    for (i = 0; path[i].len && cur < buflen; i++) {
        buf[cur] = '/';
        buflen--;
        cur++;

        if (path[i].len >= buflen)
            return cur;

        memcpy(buf + cur, path[i].data, path[i].len);
        cur += path[i].len;
        buflen -= path[i].len;
    }

    return cur;
}

static int(*find_resource_cb(const struct sol_coap_packet *req,
    const struct sol_coap_resource *resource)) (
    const struct sol_coap_resource *resource,
    struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr, void *data){
    uint8_t opcode;

    SOL_NULL_CHECK(resource, NULL);

    if (!uri_path_eq(req, resource->path))
        return NULL;

    opcode = sol_coap_header_get_code(req);

    switch (opcode) {
    case SOL_COAP_METHOD_GET:
        return resource->get;
    case SOL_COAP_METHOD_POST:
        return resource->post;
    case SOL_COAP_METHOD_PUT:
        return resource->put;
    case SOL_COAP_METHOD_DELETE:
        return resource->delete;
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

static void
coap_packet_free(struct sol_coap_packet *pkt)
{
    free(pkt);
}

SOL_API void
sol_coap_packet_unref(struct sol_coap_packet *pkt)
{
    SOL_NULL_CHECK(pkt);

    if (pkt->refcnt > 1) {
        pkt->refcnt--;
        return;
    }

    coap_packet_free(pkt);
}

SOL_API struct sol_coap_packet *
sol_coap_packet_new(struct sol_coap_packet *old)
{
    struct sol_coap_packet *pkt;

    pkt = calloc(1, sizeof(struct sol_coap_packet));
    SOL_NULL_CHECK(pkt, NULL); /* It may possible that in the next round there is enough memory. */

    pkt->refcnt = 1;

    sol_coap_header_set_ver(pkt, COAP_VERSION);

    pkt->payload.used = sizeof(struct coap_header);
    pkt->payload.size = COAP_UDP_MTU;

    if (old) {
        uint8_t tkl;
        uint8_t *token = sol_coap_header_get_token(old, &tkl);
        sol_coap_header_set_id(pkt, sol_coap_header_get_id(old));
        sol_coap_header_set_token(pkt, token, tkl);
    }

    return pkt;
}

static void
outgoing_free(struct outgoing *outgoing)
{
    if (outgoing->timeout)
        sol_timeout_del(outgoing->timeout);

    sol_coap_packet_unref(outgoing->pkt);
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
    struct outgoing *outgoing = data;
    struct sol_coap_server *server = outgoing->server;
    char addr[SOL_INET_ADDR_STRLEN];

    outgoing->timeout = NULL;

    sol_socket_set_on_write(server->socket, on_can_write, server);

    sol_network_addr_to_str(&outgoing->cliaddr, addr, sizeof(addr));

    SOL_DBG("server %p retrying packet id %d to client %s",
        server, sol_coap_header_get_id(outgoing->pkt), addr);

    return false;
}

static void
setup_timeout(struct sol_coap_server *server, struct outgoing *outgoing)
{
    struct outgoing *o;
    int timeout;
    uint16_t i;

    if (outgoing->counter >= MAX_RETRANSMIT) {
        SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&server->outgoing, o, i) {
            if (o == outgoing) {
                SOL_DBG("packet id %d dropped, after %d retransmissions",
                    sol_coap_header_get_id(outgoing->pkt), outgoing->counter);
                sol_ptr_vector_del(&server->outgoing, i);
                outgoing_free(o);
                return;
            }
        }
    }

    timeout = ACK_TIMEOUT_MS << outgoing->counter++;
    outgoing->timeout = sol_timeout_add(timeout, timeout_cb, outgoing);

    SOL_DBG("waiting %d ms to re-try packet id %d", timeout, sol_coap_header_get_id(outgoing->pkt));
}

static bool
on_can_write(void *data, struct sol_socket *s)
{
    struct sol_coap_server *server = data;
    struct outgoing *outgoing;
    int err;
    int idx;

    if (sol_ptr_vector_get_len(&server->outgoing) == 0)
        return false;

    outgoing = next_in_queue(server, &idx);
    if (!outgoing)
        return false;

    err = sol_socket_sendmsg(s, outgoing->pkt->buf,
        outgoing->pkt->payload.used, &outgoing->cliaddr);
    /* Eventually we are going to re-send it. */
    if (err == -EAGAIN)
        return true;

    if (err < 0) {
        char addr[SOL_INET_ADDR_STRLEN];
        sol_network_addr_to_str(&outgoing->cliaddr, addr, sizeof(addr));
        SOL_WRN("Could not send packet %d to %s (%d): %s", sol_coap_header_get_id(outgoing->pkt),
            addr, errno, sol_util_strerrora(errno));
        return false;
    }

    if (sol_coap_header_get_type(outgoing->pkt) != SOL_COAP_TYPE_CON) {
        sol_ptr_vector_del(&server->outgoing, idx);
        outgoing_free(outgoing);
    } else {
        setup_timeout(server, outgoing);
    }

    if (sol_ptr_vector_get_len(&server->outgoing) == 0)
        return false;

    return true;
}

static int
enqueue_packet(struct sol_coap_server *server, struct sol_coap_packet *pkt,
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

    sol_socket_set_on_write(server->socket, on_can_write, server);

    return 0;
}

SOL_API int
sol_coap_send_packet_with_reply(struct sol_coap_server *server, struct sol_coap_packet *pkt,
    const struct sol_network_link_addr *cliaddr,
    int (*reply_cb)(struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr,
    void *data),
    void *data)
{
    struct sol_coap_option_value option = {};
    struct pending_reply *reply = NULL;
    uint8_t tkl, *token;
    int err, count;
    bool observing = false;

    count = coap_find_options(pkt, SOL_COAP_OPTION_OBSERVE, &option, 1);
    if (count < 0) {
        sol_coap_packet_unref(pkt);
        return -EINVAL;
    }

    /* Observing is enabled. */
    if (count == 1 && option.len == 1 && option.value[0] == 0)
        observing = true;

    if (!reply_cb) {
        if (observing) {
            SOL_WRN("Observing a resource without a callback.");
            sol_coap_packet_unref(pkt);
            return -EINVAL;
        }
        goto done;
    }

    token = sol_coap_header_get_token(pkt, &tkl);

    reply = calloc(1, sizeof(*reply) + tkl);
    if (!reply) {
        sol_coap_packet_unref(pkt);
        return -ENOMEM;
    }

    reply->id = sol_coap_header_get_id(pkt);
    reply->cb = reply_cb;
    reply->data = data;
    reply->observing = observing;
    reply->tkl = tkl;
    if (token)
        memcpy(reply->token, token, tkl);

done:
    err = enqueue_packet(server, pkt, cliaddr);
    if (err < 0) {
        char addr[SOL_INET_ADDR_STRLEN];
        sol_network_addr_to_str(cliaddr, addr, sizeof(addr));
        SOL_WRN("Could not enqueue packet %p to %s (%d): %s", pkt, addr, errno,
            sol_util_strerrora(errno));
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
    free(reply);
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

SOL_API int
sol_coap_packet_send_notification(struct sol_coap_server *server,
    struct sol_coap_resource *resource, struct sol_coap_packet *pkt)
{
    struct resource_context *c;
    struct resource_observer *o;
    int err = 0;
    uint8_t tkl;
    uint16_t i;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(resource, -EINVAL);
    SOL_NULL_CHECK(pkt, -EINVAL);

    COAP_RESOURCE_CHECK_API(-EINVAL);

    c = find_context(server, resource);
    SOL_NULL_CHECK(c, -ENOENT);

    sol_coap_header_get_token(pkt, &tkl);

    SOL_PTR_VECTOR_FOREACH_IDX (&c->observers, o, i) {
        struct sol_coap_packet *p;
        int h;

        p = sol_coap_packet_new(NULL);
        SOL_NULL_CHECK(p, -ENOMEM);

        sol_coap_header_set_code(p, sol_coap_header_get_code(pkt));
        sol_coap_header_set_type(p, sol_coap_header_get_type(pkt));
        sol_coap_header_set_token(p, o->token, o->tkl);

        h = o->tkl + sizeof(struct coap_header);

        /*
         * Copying the options + payload from the notification packet to
         * every packet that will be sent.
         */
        memcpy(p->buf + h,
            pkt->buf + sizeof(struct coap_header),
            pkt->payload.used - sizeof(struct coap_header));

        /* As the user may have added a token (which was unused), we need to fix the size. */
        p->payload.used = pkt->payload.used + o->tkl - tkl;

        err = enqueue_packet(server, p, &o->cliaddr);
        if (err < 0) {
            char addr[SOL_INET_ADDR_STRLEN];
            sol_network_addr_to_str(&o->cliaddr, addr, sizeof(addr));
            SOL_WRN("Failed to enqueue packet %p to %s", p, addr);
            goto done;
        }

        sol_coap_packet_unref(p);
    }

done:
    sol_coap_packet_unref(pkt);
    return err;
}

SOL_API struct sol_coap_packet *
sol_coap_packet_notification_new(struct sol_coap_server *server, struct sol_coap_resource *resource)
{
    struct resource_context *c;
    struct sol_coap_packet *pkt;
    uint16_t id;

    SOL_NULL_CHECK(resource, NULL);
    COAP_RESOURCE_CHECK_API(NULL);

    c = find_context(server, resource);
    SOL_NULL_CHECK(c, NULL);

    if (++c->age == UINT16_MAX)
        c->age = 2;

    id = htons(c->age);

    pkt = sol_coap_packet_new(NULL);
    SOL_NULL_CHECK(pkt, NULL);

    sol_coap_header_set_type(pkt, SOL_COAP_TYPE_NONCON);
    sol_coap_add_option(pkt, SOL_COAP_OPTION_OBSERVE, &id, sizeof(id));

    return pkt;
}

SOL_API int
sol_coap_send_packet(struct sol_coap_server *server,
    struct sol_coap_packet *pkt,
    const struct sol_network_link_addr *cliaddr)
{
    return sol_coap_send_packet_with_reply(server, pkt, cliaddr, NULL, NULL);
}

SOL_API struct sol_coap_packet *
sol_coap_packet_request_new(sol_coap_method_t method, sol_coap_msgtype_t type)
{
    static uint16_t request_id;
    struct sol_coap_packet *pkt;

    pkt = sol_coap_packet_new(NULL);
    SOL_NULL_CHECK(pkt, NULL);

    sol_coap_header_set_code(pkt, method);
    sol_coap_header_set_id(pkt, ++request_id);
    sol_coap_header_set_type(pkt, type);

    return pkt;
}

SOL_API int
sol_coap_add_option(struct sol_coap_packet *pkt, uint16_t code, const void *value, uint16_t len)
{
    struct option_context context = { .delta = 0,
                                      .used = 0 };
    int r, offset;

    SOL_NULL_CHECK(pkt, -EINVAL);
    SOL_NULL_CHECK(value, -EINVAL);

    if (pkt->payload.start) {
        SOL_WRN("packet %p has a payload, would overwrite it", pkt);
        return -EINVAL;
    }

    offset = coap_get_header_len(pkt);
    if (offset < 0) {
        SOL_WRN("Failed to get header len from packet %p", pkt);
        return -EINVAL;
    }

    /* We check for options in all the 'used' space. */
    context.buflen = pkt->payload.used - offset;
    context.buf = pkt->buf + offset;

    while (context.delta <= code) {
        r = coap_parse_option(pkt, &context, NULL, NULL);
        if (r < 0)
            return -ENOENT;

        if (r == 0)
            break;

        /* If the new option code is out of order. */
        if (code < context.delta)
            return -EINVAL;
    }

    /* We can now add options using all the available space. */
    context.buflen = pkt->payload.size - context.used;

    r = coap_option_encode(&context, code, value, len);
    if (r < 0)
        return -EINVAL;

    pkt->payload.used += r;

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

    for (uri++; *uri; uri = slash + 1) {
        int r;

        slash = strchr(uri, '/');
        if (!slash)
            return sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_PATH, uri, strchr(uri, '\0') - uri);

        r = sol_coap_add_option(pkt, SOL_COAP_OPTION_URI_PATH, uri, slash - uri);
        if (r < 0)
            return r;
    }

    return -EINVAL;
}

SOL_API const void *
sol_coap_find_first_option(const struct sol_coap_packet *pkt, uint16_t code, uint16_t *len)
{
    struct sol_coap_option_value option = {};
    uint16_t count = 1;

    SOL_NULL_CHECK(pkt, NULL);
    SOL_NULL_CHECK(len, NULL);

    if (coap_find_options(pkt, code, &option, count) <= 0)
        return NULL;

    *len = option.len;

    return option.value;
}

static int
well_known_get(const struct sol_coap_resource *resource, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr, void *data)
{
    struct sol_coap_server *server = data;
    struct resource_context *c;
    struct sol_coap_packet *resp;
    uint8_t *payload;
    uint16_t i, size, len;
    int err;

    resp = sol_coap_packet_new(req);
    if (!resp) {
        SOL_WRN("Could not build response packet");
        return -EINVAL;
    }
    sol_coap_header_set_type(resp, SOL_COAP_TYPE_ACK);
    sol_coap_header_set_code(resp, SOL_COAP_RSPCODE_CONTENT);

    err = sol_coap_packet_get_payload(resp, &payload, &size);
    if (err < 0) {
        sol_coap_packet_unref(resp);
        return err;
    }

    len = 0;
    SOL_VECTOR_FOREACH_IDX (&server->contexts, c, i) {
        const struct sol_coap_resource *r = c->resource;

        if (!(r->flags & SOL_COAP_FLAGS_WELL_KNOWN))
            continue;

        if (len + 1 < size)
            goto error;

        payload[len] = '<';
        len++;

        len += sol_coap_uri_path_to_buf(r->path, payload + len, size - len);
        if (len >= size - 2)
            goto error;

        payload[len] = '>';
        len++;

        if (i < server->contexts.len) {
            payload[len] = ',';
            len += 1;
        }
    }

    sol_coap_packet_set_payload_used(resp, len);
    return sol_coap_send_packet(server, resp, cliaddr);

error:
    sol_coap_header_set_code(resp, SOL_COAP_RSPCODE_INTERNAL_ERROR);
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
    struct sol_coap_option_value option = {};
    int r;
    uint16_t count = 1;

    SOL_NULL_CHECK(pkt, -EINVAL);

    r = coap_find_options(pkt, SOL_COAP_OPTION_OBSERVE, &option, count);
    if (r <= 0)
        return -ENOENT;

    /* The value is in the network order, and has at max 3 bytes. */
    switch (option.len) {
    case 1:
        return option.value[0];
    case 2:
        return (option.value[0] << 0) | (option.value[1] << 8);
    case 3:
        return (option.value[0] << 0) | (option.value[1] << 8) | (option.value[2] << 16);
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
    SOL_INT_CHECK(r, < 0, -ENOMEM);

    return 0;
}

static bool
match_reply(struct pending_reply *reply, struct sol_coap_packet *pkt)
{
    const uint16_t id = sol_coap_header_get_id(pkt);

    /* When observing the match is made using the token. */
    if (reply->observing) {
        uint8_t tkl, *token;
        token = sol_coap_header_get_token(pkt, &tkl);
        return tkl == reply->tkl && !memcmp(token, reply->token, tkl);
    }

    return reply->id == id;
}

static int
resource_not_found(struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr,
    struct sol_coap_server *server)
{
    struct sol_coap_packet *resp;

    resp = sol_coap_packet_new(req);
    SOL_NULL_CHECK(resp, -ENOMEM);

    sol_coap_header_set_type(resp, SOL_COAP_TYPE_ACK);
    sol_coap_header_set_code(resp, SOL_COAP_RSPCODE_NOT_FOUND);

    return sol_coap_send_packet(server, resp, cliaddr);
}

static int
respond_packet(struct sol_coap_server *server, struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr)
{
    int (*cb)(const struct sol_coap_resource *resource,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr,
        void *data);
    struct pending_reply *reply;
    struct resource_context *c;
    struct outgoing *o;
    int observe;
    uint16_t i, id;
    uint8_t code;

    id = sol_coap_header_get_id(req);
    code = sol_coap_header_get_code(req);

    observe = get_observe_option(req);

    /* If it has the same 'id' as a packet that we are trying to send we will stop now. */
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&server->outgoing, o, i) {
        if (id != sol_coap_header_get_id(o->pkt)) {
            continue;
        }

        SOL_DBG("Received ACK for packet id %d", id);

        sol_ptr_vector_del(&server->outgoing, i);
        outgoing_free(o);
        break;
    }

    /* If it isn't a request. */
    if (code & ~SOL_COAP_REQUEST_MASK) {
        SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&server->pending, reply, i) {
            if (!match_reply(reply, req))
                continue;

            reply->cb(req, cliaddr, (void *)reply->data);

            /* Keeps calling observing is enabled. */
            if (!reply->observing) {
                sol_ptr_vector_del(&server->pending, i);
                free(reply);
            }
        }
        return 0;
    }

    /* /.well-known/core well known resource */
    cb = find_resource_cb(req, &well_known);
    if (cb)
        return cb(&well_known, req, cliaddr, server);

    SOL_VECTOR_FOREACH_IDX (&server->contexts, c, i) {
        const struct sol_coap_resource *resource = c->resource;

        cb = find_resource_cb(req, resource);
        if (!cb)
            continue;

        if (observe >= 0)
            register_observer(c, req, cliaddr, observe);

        return cb(resource, req, cliaddr, (void *)c->data);
    }

    return resource_not_found(req, cliaddr, server);
}

static bool
on_can_read(void *data, struct sol_socket *s)
{
    struct sol_coap_server *server = data;
    struct sol_network_link_addr cliaddr;
    struct sol_coap_packet *pkt;
    ssize_t len;
    int err;

    pkt = sol_coap_packet_new(NULL);
    SOL_NULL_CHECK(pkt, true); /* It may possible that in the next round there is enough memory. */

    len = sol_socket_recvmsg(s, pkt->buf, pkt->payload.size, &cliaddr);
    if (len < 0) {
        SOL_WRN("Could not read from socket (%d): %s", errno, sol_util_strerrora(errno));
        coap_packet_free(pkt);
        return true;
    }

    pkt->payload.size = len;

    err = coap_packet_parse(pkt);
    if (err < 0) {
        SOL_WRN("Failure parsing coap packet");
        coap_packet_free(pkt);
        return true;
    }

    err = respond_packet(server, pkt, &cliaddr);
    if (err < 0) {
        errno = -err;
        SOL_WRN("Couldn't respond to packet (%d): %s", -err, sol_util_strerrora(errno));
    }

    coap_packet_free(pkt);

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
        sol_ptr_vector_del(&server->pending, i);
        free(reply);
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

    if (!(link->flags & SOL_NETWORK_LINK_RUNNING) && !(link->flags & SOL_NETWORK_LINK_MULTICAST))
        return 0;

    SOL_VECTOR_FOREACH_IDX (&link->addrs, addr, i) {
        groupaddr.family = addr->family;

        if (addr->family == AF_INET) {
            sol_network_addr_from_str(&groupaddr, IPV4_ALL_COAP_NODES_GROUP);
            if (sol_socket_join_group(s, link->index, &groupaddr) < 0)
                return -errno;

            continue;
        }

        sol_network_addr_from_str(&groupaddr, IPV6_ALL_COAP_NODES_SCOPE_LOCAL);
        if (sol_socket_join_group(s, link->index, &groupaddr) < 0)
            return -errno;

        sol_network_addr_from_str(&groupaddr, IPV6_ALL_COAP_NODES_SCOPE_SITE);
        if (sol_socket_join_group(s, link->index, &groupaddr) < 0)
            return -errno;
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
sol_coap_server_new_full(enum sol_socket_type type, int port)
{
    struct sol_network_link_addr servaddr = { .family = AF_INET6,
                                              .port = port };
    const struct sol_vector *links;
    struct sol_network_link *link;
    struct sol_coap_server *server;
    struct sol_socket *s;
    uint16_t i;

    SOL_LOG_INTERNAL_INIT_ONCE;

    s = sol_socket_new(servaddr.family, type, 0);
    if (!s) {
        SOL_WRN("Could not create socket (%d): %s", errno, sol_util_strerrora(errno));
        return NULL;
    }

    if (sol_socket_bind(s, &servaddr) < 0) {
        SOL_WRN("Could not bind socket (%d): %s", errno, sol_util_strerrora(errno));
        sol_socket_del(s);
        return NULL;
    }

    server = calloc(1, sizeof(*server));
    if (!server) {
        sol_socket_del(s);
        return NULL;
    }

    server->refcnt = 1;

    sol_vector_init(&server->contexts, sizeof(struct resource_context));

    sol_ptr_vector_init(&server->pending);
    sol_ptr_vector_init(&server->outgoing);

    server->socket = s;
    if (sol_socket_set_on_read(s, on_can_read, server) < 0) {
        free(server);
        sol_socket_del(s);
        return NULL;
    }

    /* If type is SOL_SOCKET_DTLS, then it's only a unicast server. */
    if (type == SOL_SOCKET_UDP) {
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
                if (join_mcast_groups(s, link) < 0) {
                    char *name = sol_network_link_get_name(link);
                    SOL_WRN("Could not join multicast group, iface %s (%d): %s",
                        name, errno, sol_util_strerrora(errno));
                    free(name);
                }
            }
        }
    }

    sol_network_subscribe_events(network_event, server);

    SOL_DBG("New server %p on port %d%s", server, port,
        type == SOL_SOCKET_UDP ? "" : " (secure)");

    return server;
}

SOL_API struct sol_coap_server *
sol_coap_server_new(int port)
{
    return sol_coap_server_new_full(SOL_SOCKET_UDP, port);
}

SOL_API struct sol_coap_server *
sol_coap_secure_server_new(int port)
{
#ifdef DTLS
    return sol_coap_server_new_full(SOL_SOCKET_DTLS, port);
#else
    errno = ENOSYS;
    return NULL;
#endif
}

SOL_API int
sol_coap_packet_get_payload(struct sol_coap_packet *pkt, uint8_t **buf, uint16_t *len)
{
    SOL_NULL_CHECK(pkt, -EINVAL);
    SOL_NULL_CHECK(buf, -EINVAL);
    SOL_NULL_CHECK(len, -EINVAL);

    *buf = NULL;
    *len = 0;

    if (!pkt->payload.start) {
        SOL_INT_CHECK(pkt->payload.used + 1, > pkt->payload.size, -ENOMEM);

        /* Have payload, adding marker. */
        pkt->buf[pkt->payload.used] = COAP_MARKER;
        pkt->payload.used += 1;

        pkt->payload.start = pkt->buf + pkt->payload.used;
    }

    *buf = pkt->payload.start;
    *len = pkt->payload.size - pkt->payload.used;
    return 0;
}

SOL_API int
sol_coap_packet_set_payload_used(struct sol_coap_packet *pkt, uint16_t len)
{
    SOL_NULL_CHECK(pkt, -EINVAL);
    SOL_NULL_CHECK(pkt->payload.start, -EBADF);
    SOL_INT_CHECK(pkt->payload.used + len, > pkt->payload.size, -ENOMEM);

    pkt->payload.used += len;
    return 0;
}

SOL_API bool
sol_coap_packet_has_payload(struct sol_coap_packet *pkt)
{
    SOL_NULL_CHECK(pkt, false);
    return pkt->payload.start || pkt->payload.used != pkt->payload.size;
}

SOL_API bool
sol_coap_server_register_resource(struct sol_coap_server *server,
    const struct sol_coap_resource *resource, void *data)
{
    struct resource_context *c;

    SOL_NULL_CHECK(server, false);
    SOL_NULL_CHECK(resource, false);

    COAP_RESOURCE_CHECK_API(false);

    if (find_context(server, resource)) {
        SOL_WRN("Attempting to register duplicate resource in CoAP server");
        return false;
    }

    c = sol_vector_append(&server->contexts);
    SOL_NULL_CHECK(c, false);

    c->resource = resource;
    c->data = data;
    c->age = 2;

    sol_ptr_vector_init(&c->observers);

    return true;
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
