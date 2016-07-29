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

#include "sol-coap.h"
#include "sol-json.h"
#include "sol-log.h"
#include "sol-oic-cbor.h"
#include "sol-oic-security.h"
#include "sol-oic-server.h"
#include "sol-platform.h"
#include "sol-socket-dtls.h"
#include "sol-socket.h"
#include "sol-str-slice.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

#ifdef DTLS
#include "sol-certificate.h"

#define OIC_CRED_FILE_PREFIX "oic-"
#define OIC_CRED_FILE_SUFIX ".psk"

struct sol_oic_security {
    struct sol_coap_server *server;
    struct sol_coap_server *server_dtls;
    struct sol_socket_dtls_credential_cb callbacks;
};

struct cred_item {
    struct {
        char *data;
        struct sol_str_slice slice;
    } id, psk;
    /* FIXME: Only symmetric pairwise keys supported at the moment. */
};

struct sol_socket *sol_coap_server_get_socket(const struct sol_coap_server *server);

static ssize_t
creds_get_psk(const void *data, struct sol_str_slice id,
    char *psk, size_t psk_len)
{
    SOL_BUFFER_DECLARE_STATIC(path, NAME_MAX);
    struct sol_cert *cert;
    struct sol_blob *contents;
    int r;

    SOL_DBG("Looking for PSK with ID=%.*s", SOL_STR_SLICE_PRINT(id));

    if (psk_len < SOL_DTLS_PSK_KEY_LEN)
        return -ENOBUFS;

    r = sol_buffer_append_slice(&path,
        sol_str_slice_from_str(OIC_CRED_FILE_PREFIX));
    SOL_INT_CHECK(r, < 0, r);

    r = sol_util_file_encode_filename(&path, id);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_buffer_append_slice(&path,
        sol_str_slice_from_str(OIC_CRED_FILE_SUFIX));
    SOL_INT_CHECK(r, < 0, r);

    cert = sol_cert_load_from_id(path.data);
    if (!cert)
        return -ENOENT;

    contents = sol_cert_get_contents(cert);
    sol_cert_unref(cert);

    if (contents->size < SOL_DTLS_PSK_KEY_LEN) {
        SOL_WRN("PSK found is invalid.");
        sol_blob_unref(contents);
        return -ENOENT;
    }

    memcpy(psk, contents->mem, SOL_DTLS_PSK_KEY_LEN);
    sol_blob_unref(contents);

    return SOL_DTLS_PSK_KEY_LEN;
}

static ssize_t
creds_get_id(const void *data, struct sol_network_link_addr *addr,
    char *id, size_t id_len)
{
    const uint8_t *machine_id = sol_platform_get_machine_id_as_bytes();
    size_t len = SOL_DTLS_PSK_ID_LEN;

    if (len > id_len)
        return -ENOBUFS;

    memcpy(id, machine_id, len);
    return (ssize_t)len;
}

static void
sol_oic_security_del_full(struct sol_oic_security *security, bool is_server)
{
    SOL_NULL_CHECK(security);

    sol_coap_server_unref(security->server);
    sol_coap_server_unref(security->server_dtls);

    sol_util_clear_memory_secure(security, sizeof(*security));
    free(security);
}

static struct sol_oic_security *
sol_oic_security_add_full(struct sol_coap_server *server,
    struct sol_coap_server *server_dtls, bool is_server)
{
    struct sol_oic_security *security;
    struct sol_socket *socket_dtls;
    int r = 0;

    security = malloc(sizeof(*security));
    SOL_NULL_CHECK_ERRNO(security, ENOMEM, NULL);

    socket_dtls = sol_coap_server_get_socket(server_dtls);
    if (!socket_dtls) {
        r = -errno;
        goto error_sec;
    }

    security->callbacks = (struct sol_socket_dtls_credential_cb) {
        .data = security,
        .get_id = creds_get_id,
        .get_psk = creds_get_psk
    };
    r = sol_socket_dtls_set_credentials_callbacks(socket_dtls,
        &security->callbacks);
    if (r < 0) {
        SOL_WRN("Passed DTLS socket is not a valid sol_socket_dtls");
        goto error_sec;
    }

    security->server = sol_coap_server_ref(server);
    security->server_dtls = sol_coap_server_ref(server_dtls);

    return security;

error_sec:
    free(security);
    errno = -r;
    return NULL;
}
#endif

struct sol_oic_security *
sol_oic_server_security_add(struct sol_coap_server *server,
    struct sol_coap_server *server_dtls)
{
#ifdef DTLS
    return sol_oic_security_add_full(server, server_dtls, true);
#else
    return NULL;
#endif
}

void
sol_oic_server_security_del(struct sol_oic_security *security)
{
#ifdef DTLS
    sol_oic_security_del_full(security, true);
#endif
}

struct sol_oic_security *
sol_oic_client_security_add(struct sol_coap_server *server,
    struct sol_coap_server *server_dtls)
{
#ifdef DTLS
    return sol_oic_security_add_full(server, server_dtls, false);
#else
    return NULL;
#endif
}

void
sol_oic_client_security_del(struct sol_oic_security *security)
{
#ifdef DTLS
    sol_oic_security_del_full(security, false);
#endif
}
