/*
 * This file is part of the Soletta Project
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
#ifdef DTLS
#include "sol-util-file.h"
#endif
#include "sol-util-internal.h"
#include "sol-vector.h"

#ifdef DTLS

enum method {
    METHOD_GET,
    METHOD_PUT
};

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

struct creds {
    struct sol_vector items;
    const struct sol_oic_security *security;
};

struct sol_socket *sol_coap_server_get_socket(const struct sol_coap_server *server);

static ssize_t
creds_get_psk(const void *data, struct sol_str_slice id,
    char *psk, size_t psk_len)
{
    const struct creds *creds = data;
    struct cred_item *iter;
    uint16_t idx;

    SOL_DBG("Looking for PSK with ID=%.*s", (int)id.len, id.data);

    SOL_VECTOR_FOREACH_IDX (&creds->items, iter, idx) {
        if (sol_str_slice_eq(id, iter->id.slice)) {
            if (iter->id.slice.len > psk_len)
                return -ENOBUFS;

            memcpy(psk, iter->psk.data, iter->id.slice.len);
            return (ssize_t)iter->psk.slice.len;
        }
    }

    return -ENOENT;
}

static ssize_t
creds_get_id(const void *data, char *id, size_t id_len)
{
    const uint8_t *machine_id = sol_platform_get_machine_id_as_bytes();
    size_t len = SOL_DTLS_PSK_ID_LEN;

    if (len > id_len)
        return -ENOBUFS;

    memcpy(id, machine_id, len);
    return (ssize_t)len;
}

static bool
creds_add(struct creds *creds, const char *id, size_t id_len,
    const char *psk, size_t psk_len)
{
    struct cred_item *item;
    char psk_stored[64];
    ssize_t r;

    r = creds_get_psk(creds, SOL_STR_SLICE_STR(id, id_len),
        psk_stored, sizeof(psk_stored));
    if (r > 0) {
        struct sol_str_slice stored = SOL_STR_SLICE_STR(psk_stored, r);
        struct sol_str_slice passed = SOL_STR_SLICE_STR(psk, psk_len);

        if (sol_str_slice_eq(stored, passed))
            return true;

        SOL_WRN("Attempting to add PSK for ID=%.*s, but it's already"
            " registered and different from the supplied key",
            (int)id_len, id);
        return false;
    } else if (r < 0 && r != -ENOENT) {
        SOL_WRN("Error while adding credentials: %s", sol_util_strerrora(-r));
        return false;
    }

    item = sol_vector_append(&creds->items);
    SOL_NULL_CHECK(item, false);

    item->id.data = sol_util_memdup(id, id_len);
    SOL_NULL_CHECK_GOTO(item->id.data, no_id);

    item->psk.data = sol_util_memdup(psk, psk_len);
    SOL_NULL_CHECK_GOTO(item->psk.data, no_psk);

    item->id.slice = SOL_STR_SLICE_STR(item->id.data, id_len);
    item->psk.slice = SOL_STR_SLICE_STR(item->psk.data, psk_len);

    return true;

no_psk:
    sol_util_secure_clear_memory(item->id.data, id_len);
    free(item->id.data);
no_id:
    sol_util_secure_clear_memory(item, sizeof(*item));
    sol_vector_del(&creds->items, creds->items.len - 1);

    return false;
}

static void
creds_clear(void *data)
{
    struct creds *creds = data;
    struct cred_item *iter;
    uint16_t idx;

    SOL_VECTOR_FOREACH_IDX (&creds->items, iter, idx) {
        sol_util_secure_clear_memory(iter->id.data, iter->id.slice.len);
        sol_util_secure_clear_memory(iter->psk.data, iter->psk.slice.len);

        free(iter->id.data);
        free(iter->psk.data);
    }
    sol_vector_clear(&creds->items);

    sol_util_secure_clear_memory(creds, sizeof(*creds));
    free(creds);
}

static struct sol_str_slice
remove_quotes(struct sol_str_slice slice)
{
    if (slice.len < 2)
        return SOL_STR_SLICE_STR(NULL, 0);

    slice.len -= 2;
    slice.data++;

    return slice;
}

static bool
creds_add_json_token(struct creds *creds, struct sol_json_scanner *scanner,
    struct sol_json_token *token)
{
    struct sol_json_token key, value;
    enum sol_json_loop_reason reason;
    struct sol_str_slice psk = SOL_STR_SLICE_EMPTY;
    struct sol_str_slice id = SOL_STR_SLICE_EMPTY;
    struct sol_buffer id_buf, psk_buf;

    SOL_JSON_SCANNER_OBJECT_LOOP_NEST (scanner, token, &key, &value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "id")) {
            id = remove_quotes(sol_json_token_to_slice(&value));
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "psk")) {
            psk = remove_quotes(sol_json_token_to_slice(&value));
        }
    }

    if (id.len && psk.len) {
        char id_buf_backing[SOL_DTLS_PSK_ID_LEN],
            psk_buf_backing[SOL_DTLS_PSK_KEY_LEN];
        bool result = false;

        sol_buffer_init_flags(&id_buf, id_buf_backing, sizeof(id_buf_backing),
            SOL_BUFFER_FLAGS_CLEAR_MEMORY | SOL_BUFFER_FLAGS_NO_NUL_BYTE |
            SOL_BUFFER_FLAGS_NO_FREE);
        sol_buffer_init_flags(&psk_buf, psk_buf_backing, sizeof(psk_buf_backing),
            SOL_BUFFER_FLAGS_CLEAR_MEMORY | SOL_BUFFER_FLAGS_NO_NUL_BYTE |
            SOL_BUFFER_FLAGS_NO_FREE);

        if (sol_buffer_append_from_base64(&id_buf, id, SOL_BASE64_MAP) < 0)
            goto finish_bufs;
        if (sol_buffer_append_from_base64(&psk_buf, psk, SOL_BASE64_MAP) < 0)
            goto finish_bufs;

        result = creds_add(creds, id_buf.data, id_buf.used,
            psk_buf.data, psk_buf.used);

finish_bufs:
        sol_buffer_fini(&psk_buf);
        sol_buffer_fini(&id_buf);

        return result;
    }

    return false;
}

static bool
generate_file_name(const char *prefix, char *filename, size_t len)
{
    struct sol_buffer buf;
    int r;
    struct sol_str_slice id = SOL_STR_SLICE_STR((char *)sol_platform_get_machine_id_as_bytes(), 16);

    sol_buffer_init_flags(&buf, filename, len,
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED);

    r = sol_buffer_append_printf(&buf, "%s", prefix);
    SOL_INT_CHECK(r, < 0, false);
    r = sol_buffer_append_as_base64(&buf, id, SOL_BASE64_MAP);
    SOL_INT_CHECK(r, < 0, false);
    r = sol_buffer_append_printf(&buf, "%s", ".json");
    SOL_INT_CHECK(r, < 0, false);

    return true;
}

static void *
creds_init(const void *data)
{
    struct creds *creds;
    struct sol_json_scanner scanner;
    struct sol_json_token token;
    enum sol_json_loop_reason reason;
    char *file_data;
    size_t length;
    char file_name[256];

    if (!generate_file_name("/tmp/oic-creds-", file_name, sizeof(file_name)))
        return NULL;

    creds = calloc(1, sizeof(*creds));
    SOL_NULL_CHECK(creds, NULL);

    creds->security = data;
    sol_vector_init(&creds->items, sizeof(struct cred_item));

    file_data = sol_util_load_file_string(file_name, &length);
    if (!file_data)
        return creds;

    sol_json_scanner_init(&scanner, file_data, length);
    SOL_JSON_SCANNER_ARRAY_LOOP (&scanner, &token, SOL_JSON_TYPE_OBJECT_START, reason) {
        if (!creds_add_json_token(creds, &scanner, &token)) {
            creds_clear(creds);
            creds = NULL;
            goto out;
        }
    }

    if (reason != SOL_JSON_LOOP_REASON_OK) {
        creds_clear(creds);
        creds = NULL;
    }

out:
    sol_util_secure_clear_memory(&scanner, sizeof(scanner));
    sol_util_secure_clear_memory(&token, sizeof(token));
    sol_util_secure_clear_memory(&reason, sizeof(reason));

    sol_util_secure_clear_memory(file_data, length);
    free(file_data);

    return creds;
}

static void
sol_oic_security_del_full(struct sol_oic_security *security, bool is_server)
{
    SOL_NULL_CHECK(security);

    sol_coap_server_unref(security->server);
    sol_coap_server_unref(security->server_dtls);

    sol_util_secure_clear_memory(security, sizeof(*security));
    free(security);
}

static struct sol_oic_security *
sol_oic_security_add_full(struct sol_coap_server *server,
    struct sol_coap_server *server_dtls, bool is_server)
{
    struct sol_oic_security *security;
    struct sol_socket *socket_dtls;

    security = malloc(sizeof(*security));
    SOL_NULL_CHECK(security, NULL);

    socket_dtls = sol_coap_server_get_socket(server_dtls);
    SOL_NULL_CHECK_GOTO(socket_dtls, error_sec);

    security->callbacks = (struct sol_socket_dtls_credential_cb) {
        .data = security,
        .init = creds_init,
        .clear = creds_clear,
        .get_id = creds_get_id,
        .get_psk = creds_get_psk
    };
    if (sol_socket_dtls_set_credentials_callbacks(socket_dtls, &security->callbacks) < 0) {
        SOL_WRN("Passed DTLS socket is not a valid sol_socket_dtls");
        goto error_sec;
    }

    security->server = sol_coap_server_ref(server);
    security->server_dtls = sol_coap_server_ref(server_dtls);

    return security;

error_sec:
    free(security);
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
