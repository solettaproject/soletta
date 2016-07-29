/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
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

#define SOL_LOG_DOMAIN &_lwm2m_security_domain

#include "sol-log-internal.h"
#include "sol-util-internal.h"
#include "sol-coap.h"
#include "sol-lwm2m-security.h"
#include "sol-platform.h"
#include "sol-socket-dtls.h"
#include "sol-socket.h"
#include "sol-str-slice.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

SOL_LOG_INTERNAL_DECLARE(_lwm2m_security_domain, "lwm2m-security");

#ifdef DTLS

struct sol_socket *sol_coap_server_get_socket(const struct sol_coap_server *server);

static ssize_t
get_psk_from_server_or_bs_server(const void *data, struct sol_str_slice id,
    char *psk, size_t psk_len)
{
    const struct sol_lwm2m_security *ctx = data;
    struct sol_lwm2m_server *lwm2m_server;
    struct sol_lwm2m_bootstrap_server *lwm2m_bs_server;
    struct sol_vector *known_psks;
    struct sol_lwm2m_security_psk *stored_psk;
    uint16_t i;

    SOL_DBG("Looking for PSK with ID=%.*s", SOL_STR_SLICE_PRINT(id));

    //If the caller expects a PSK_LEN less than 16 bytes
    if (psk_len < SOL_DTLS_PSK_KEY_LEN)
        return -ENOBUFS;

    if (ctx->type == LWM2M_SERVER) {
        SOL_DBG("ctx->type = LWM2M_SERVER");
        lwm2m_server = ctx->entity;
        known_psks = &lwm2m_server->known_psks;
    } else {
        SOL_DBG("ctx->type = LWM2M_BOOTSTRAP_SERVER");
        lwm2m_bs_server = ctx->entity;
        known_psks = &lwm2m_bs_server->known_psks;
    }

    SOL_VECTOR_FOREACH_IDX (known_psks, stored_psk, i) {
        if (sol_str_slice_eq(sol_str_slice_from_blob(stored_psk->id), id)) {
            if (stored_psk->key->size != SOL_DTLS_PSK_KEY_LEN) {
                SOL_WRN("The PSK '%.*s' is %zu-bytes long; expecting a %d-bytes long PSK",
                    SOL_STR_SLICE_PRINT(sol_str_slice_from_blob(stored_psk->key)),
                    stored_psk->key->size, SOL_DTLS_PSK_KEY_LEN);
                return -EINVAL;
            }

            memcpy(psk, stored_psk->key->mem, stored_psk->key->size);
            return (ssize_t)SOL_DTLS_PSK_KEY_LEN;
        }
    }

    SOL_WRN("Could not find PSK with ID=%.*s", SOL_STR_SLICE_PRINT(id));

    return -ENOENT;
}

static ssize_t
get_psk_from_client(const void *data, struct sol_str_slice id,
    char *psk, size_t psk_len)
{
    const struct sol_lwm2m_security *ctx = data;
    struct sol_lwm2m_client *lwm2m_client = ctx->entity;
    struct obj_ctx *obj_ctx;
    struct obj_instance *instance;
    struct sol_lwm2m_resource res[3] = { };
    uint16_t i;
    int r;

    SOL_DBG("Looking for PSK with ID=%.*s", SOL_STR_SLICE_PRINT(id));

    //If the caller expects a PSK_LEN less than 16 bytes
    if (psk_len < SOL_DTLS_PSK_KEY_LEN)
        return -ENOBUFS;

    obj_ctx = find_object_ctx_by_id(lwm2m_client, SECURITY_OBJECT_ID);
    if (!obj_ctx) {
        SOL_WRN("LWM2M Security object not provided!");
        return -ENOENT;
    }

    if (!obj_ctx->instances.len) {
        SOL_WRN("There are no Security Server instances");
        return -ENOENT;
    }

    SOL_VECTOR_FOREACH_IDX (&obj_ctx->instances, instance, i) {
        r = read_resources(lwm2m_client, obj_ctx, instance, res, 3,
            SECURITY_SECURITY_MODE,
            SECURITY_PUBLIC_KEY_OR_IDENTITY,
            SECURITY_SECRET_KEY);
        SOL_INT_CHECK_GOTO(r, < 0, err_clear);

        //If -ENOENT when reading SECURITY_SECRET_KEY or
        // SECURITY_SECURITY_MODE != SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY
        if (res[2].data_len == 0 ||
            res[0].data[0].content.integer != SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY) {
            clear_resource_array(res, sol_util_array_size(res));
            continue;
        }

        if (sol_str_slice_eq(sol_str_slice_from_blob(res[1].data[0].content.blob), id)) {
            if (res[2].data[0].content.blob->size != SOL_DTLS_PSK_KEY_LEN) {
                SOL_WRN("The PSK '%.*s' is %zu-bytes long; expecting a %d-bytes long PSK",
                    SOL_STR_SLICE_PRINT(sol_str_slice_from_blob(res[2].data[0].content.blob)),
                    res[2].data[0].content.blob->size, SOL_DTLS_PSK_KEY_LEN);
                r = -EINVAL;
                goto err_clear;
            }

            memcpy(psk, res[2].data[0].content.blob->mem, res[2].data[0].content.blob->size);
            clear_resource_array(res, sol_util_array_size(res));
            return (ssize_t)SOL_DTLS_PSK_KEY_LEN;
        }

        clear_resource_array(res, sol_util_array_size(res));
    }

    SOL_WRN("Could not find PSK with ID=%.*s", SOL_STR_SLICE_PRINT(id));

    return -ENOENT;

err_clear:
    clear_resource_array(res, sol_util_array_size(res));
    return r;
}

static ssize_t
get_id_from_server_or_bs_server(const void *data, struct sol_network_link_addr *addr,
    char *id, size_t id_len)
{
    const uint8_t *machine_id = sol_platform_get_machine_id_as_bytes();

    //If the caller expects a PSK_ID_LEN less than 16 bytes
    if (id_len < SOL_DTLS_PSK_ID_LEN)
        return -ENOBUFS;

    memcpy(id, machine_id, SOL_DTLS_PSK_ID_LEN);
    return (ssize_t)SOL_DTLS_PSK_ID_LEN;
}

static ssize_t
get_id_from_client(const void *data, struct sol_network_link_addr *addr, char *id, size_t id_len)
{
    const struct sol_lwm2m_security *ctx = data;
    struct sol_lwm2m_client *lwm2m_client = ctx->entity;
    struct obj_ctx *obj_ctx;
    struct obj_instance *instance;
    struct sol_lwm2m_resource res[4] = { };
    int64_t server_id;
    uint16_t i;
    int r;

    //If the caller expects a PSK_ID_LEN less than 16 bytes
    if (id_len < SOL_DTLS_PSK_ID_LEN)
        return -ENOBUFS;

    SOL_BUFFER_DECLARE_STATIC(addr_str, SOL_NETWORK_INET_ADDR_STR_LEN);

    r = get_server_id_by_link_addr(&lwm2m_client->connections, addr, &server_id);
    SOL_INT_CHECK(r, < 0, r);

    if (!sol_network_link_addr_to_str(addr, &addr_str))
        SOL_WRN("Could not convert the server address to string");
    else
        SOL_DBG("Looking for PSK ID for communication with server_id=%" PRId64
            " and server_addr=%.*s", server_id,
            SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr_str)));

    obj_ctx = find_object_ctx_by_id(lwm2m_client, SECURITY_OBJECT_ID);
    if (!obj_ctx) {
        SOL_WRN("LWM2M Security Object not provided!");
        return -ENOENT;
    }

    if (!obj_ctx->instances.len) {
        SOL_WRN("There are no Security Object instances");
        return -ENOENT;
    }

    SOL_VECTOR_FOREACH_IDX (&obj_ctx->instances, instance, i) {
        r = read_resources(lwm2m_client, obj_ctx, instance, res, 4,
            SECURITY_SECURITY_MODE,
            SECURITY_SERVER_ID,
            SECURITY_PUBLIC_KEY_OR_IDENTITY,
            SECURITY_IS_BOOTSTRAP);
        SOL_INT_CHECK_GOTO(r, < 0, err_clear);

        //If -ENOENT when reading SECURITY_PUBLIC_KEY_OR_IDENTITY or
        // SECURITY_SECURITY_MODE != SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY
        if (res[2].data_len == 0 ||
            res[0].data[0].content.integer != SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY) {
            clear_resource_array(res, sol_util_array_size(res));
            continue;
        }

        if (res[1].data[0].content.integer == server_id ||
            (res[3].data[0].content.b && (UINT16_MAX == server_id))) {
            size_t psk_id_len;

            if (res[2].data[0].content.blob->size > SOL_DTLS_PSK_ID_LEN) {
                SOL_WRN("The PSK ID '%.*s' is %zu-bytes long;"
                    " expecting a PSK ID at most %d-bytes long",
                    SOL_STR_SLICE_PRINT(sol_str_slice_from_blob(res[2].data[0].content.blob)),
                    res[2].data[0].content.blob->size, SOL_DTLS_PSK_ID_LEN);
                r = -EINVAL;
                goto err_clear;
            }

            psk_id_len = res[2].data[0].content.blob->size;

            memcpy(id, res[2].data[0].content.blob->mem, psk_id_len);
            clear_resource_array(res, sol_util_array_size(res));

            return (ssize_t)psk_id_len;
        }

        clear_resource_array(res, sol_util_array_size(res));
    }

    SOL_WRN("Could not find PSK ID for communication with server_id=%" PRId64
        " and server_addr=%.*s", server_id,
        SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&addr_str)));

    return -ENOENT;

err_clear:
    clear_resource_array(res, sol_util_array_size(res));
    return r;
}

static void
sol_lwm2m_security_del_full(struct sol_lwm2m_security *security,
    enum lwm2m_entity_type type)
{
    SOL_NULL_CHECK(security);

    sol_util_clear_memory_secure(security, sizeof(*security));
    free(security);
}

static struct sol_lwm2m_security *
sol_lwm2m_security_add_full(void *entity, enum lwm2m_entity_type type,
    enum sol_lwm2m_security_mode sec_mode)
{
    struct sol_lwm2m_security *security;
    struct sol_socket *socket_dtls = NULL;
    int r = 0;

    ssize_t (*get_psk_cb)(const void *creds, struct sol_str_slice id,
        char *psk, size_t psk_len) = NULL;
    ssize_t (*get_id_cb)(const void *creds, struct sol_network_link_addr *addr,
        char *id, size_t id_len) = NULL;
    bool has_security = false;

    SOL_LOG_INTERNAL_INIT_ONCE;

    switch (type) {
    case LWM2M_CLIENT:
        get_psk_cb = get_psk_from_client;
        get_id_cb = get_id_from_client;
        socket_dtls = sol_coap_server_get_socket(
            ((struct sol_lwm2m_client *)entity)->dtls_server);
        if (((struct sol_lwm2m_client *)entity)->security) {
            security = ((struct sol_lwm2m_client *)entity)->security;
            has_security = true;
        }
        break;
    case LWM2M_SERVER:
        get_psk_cb = get_psk_from_server_or_bs_server;
        get_id_cb = get_id_from_server_or_bs_server;
        socket_dtls = sol_coap_server_get_socket(
            ((struct sol_lwm2m_server *)entity)->dtls_server);
        if (((struct sol_lwm2m_server *)entity)->security) {
            security = ((struct sol_lwm2m_server *)entity)->security;
            has_security = true;
        }
        break;
    case LWM2M_BOOTSTRAP_SERVER:
        get_psk_cb = get_psk_from_server_or_bs_server;
        get_id_cb = get_id_from_server_or_bs_server;
        socket_dtls = sol_coap_server_get_socket(
            ((struct sol_lwm2m_bootstrap_server *)entity)->coap);
        if (((struct sol_lwm2m_bootstrap_server *)entity)->security) {
            security = ((struct sol_lwm2m_bootstrap_server *)entity)->security;
            has_security = true;
        }
        break;
    default:
        r = -EINVAL;
        SOL_WRN("Invalid Entity Type");
        goto err_socket_dtls;
    }
    if (!socket_dtls) {
        r = -errno;
        goto err_socket_dtls;
    }

    if (!has_security) {
        security = calloc(1, sizeof(struct sol_lwm2m_security));
        SOL_NULL_CHECK_ERRNO(security, ENOMEM, NULL);

        security->callbacks = (struct sol_socket_dtls_credential_cb) {
            .data = security,
            .get_id = get_id_cb
        };
        r = sol_socket_dtls_set_credentials_callbacks(socket_dtls,
            &security->callbacks);
        if (r < 0) {
            SOL_WRN("Passed DTLS socket is not a valid sol_socket_dtls");
            goto err_sec;
        }

        security->type = type;
        security->entity = entity;
    }

    switch (sec_mode) {
    case SOL_LWM2M_SECURITY_MODE_PRE_SHARED_KEY:
        security->callbacks.get_psk = get_psk_cb;
        break;
    case SOL_LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY:
        SOL_WRN("Raw Public Key security mode is not supported yet.");
        r = -ENOTSUP;
        goto err_sec;
    case SOL_LWM2M_SECURITY_MODE_CERTIFICATE:
        SOL_WRN("Certificate security mode is not supported yet.");
        r = -ENOTSUP;
        goto err_sec;
    case SOL_LWM2M_SECURITY_MODE_NO_SEC:
        SOL_WRN("NoSec Security Mode does not use DTLS.");
        r = -EINVAL;
        goto err_sec;
    default:
        SOL_WRN("Unknown DTLS [Security Mode] Resource from Security Object: %d", sec_mode);
        r = -EINVAL;
        goto err_sec;
    }

    return security;

err_sec:
    free(security);
err_socket_dtls:
    errno = -r;
    return NULL;
}
#endif

struct sol_lwm2m_security *
sol_lwm2m_client_security_add(struct sol_lwm2m_client *lwm2m_client,
    enum sol_lwm2m_security_mode sec_mode)
{
#ifdef DTLS
    return sol_lwm2m_security_add_full(lwm2m_client, LWM2M_CLIENT, sec_mode);
#else
    return NULL;
#endif
}

void
sol_lwm2m_client_security_del(struct sol_lwm2m_security *security)
{
#ifdef DTLS
    sol_lwm2m_security_del_full(security, LWM2M_CLIENT);
#endif
}

struct sol_lwm2m_security *
sol_lwm2m_server_security_add(struct sol_lwm2m_server *lwm2m_server,
    enum sol_lwm2m_security_mode sec_mode)
{
#ifdef DTLS
    return sol_lwm2m_security_add_full(lwm2m_server, LWM2M_SERVER, sec_mode);
#else
    return NULL;
#endif
}

void
sol_lwm2m_server_security_del(struct sol_lwm2m_security *security)
{
#ifdef DTLS
    sol_lwm2m_security_del_full(security, LWM2M_SERVER);
#endif
}

struct sol_lwm2m_security *
sol_lwm2m_bootstrap_server_security_add(struct sol_lwm2m_bootstrap_server *lwm2m_bs_server,
    enum sol_lwm2m_security_mode sec_mode)
{
#ifdef DTLS
    return sol_lwm2m_security_add_full(lwm2m_bs_server, LWM2M_BOOTSTRAP_SERVER, sec_mode);
#else
    return NULL;
#endif
}

void
sol_lwm2m_bootstrap_server_security_del(struct sol_lwm2m_security *security)
{
#ifdef DTLS
    sol_lwm2m_security_del_full(security, LWM2M_BOOTSTRAP_SERVER);
#endif
}
