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
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sol-coap.h"
#include "sol-json.h"
#include "sol-log.h"
#include "sol-oic-cbor.h"
#include "sol-oic-security.h"
#include "sol-oic-server.h"
#include "sol-platform.h"
#include "sol-random.h"
#include "sol-socket-dtls.h"
#include "sol-socket.h"
#include "sol-str-slice.h"
#include "sol-util-file.h"
#include "sol-util-internal.h"
#include "sol-vector.h"

#ifdef DTLS

enum sct {
    SCT_NO_SECURITY_MODE = 0,
    SCT_SYMMETRIC_PAIR_WISE_KEY = 1 << 0,
        SCT_SYMMETRIC_GROUP_KEY = 1 << 1,
        SCT_ASYMMETRIC_KEY = 1 << 2,
        SCT_SIGNED_ASYMMETRIC_KEY = 1 << 3,
        SCT_PIN_PASSWORD = 1 << 4,
        SCT_ASYMMETRIC_ENCRYPTION_KEY = 1 << 5,
};

struct doxm_data {
    /* Device Onwership Transfer Method */
    char owner_uuid[DTLS_PSK_ID_LEN];
    char device_uuid[DTLS_PSK_ID_LEN];
    enum sol_oic_pairing_method oxm_sel;
    struct sol_vector oxm;
    enum sct sct;
    bool owned;
};

enum provisioning_mode {
    DPM_NORMAL = 0,
    DPM_RESET = 1 << 0,
        DPM_TAKE_OWNER = 1 << 1,
        DPM_BOOTSTRAP_SERVICE = 1 << 2,
        DPM_SEC_MGMT_SERVICES = 1 << 3,
        DPM_PROVISION_CREDS = 1 << 4,
        DPM_PROVISION_ACLS = 1 << 5
};

enum provisioning_op_mode {
    DOP_MULTIPLE = 0,
    DOP_SERVER_DRIVEN  = 0,
    DOP_SINGLE = 1 << 0,
        DOP_CLIENT_DRIVEN = 1 << 1,
};

struct pstat_data {
    /* Provisioning Strategy */
    char device_id[DTLS_PSK_ID_LEN]; /* duplicate? */

    enum provisioning_mode cm;
    enum provisioning_mode tm;

    enum provisioning_op_mode operation_mode;
    struct sol_vector sm;

    uint16_t commit_hash;
    bool op;
};

enum method {
    METHOD_GET,
    METHOD_PUT
};

struct transfer_method {
    const char *oxm_string;

    sol_coap_responsecode_t (*handle_doxm)(struct sol_oic_security *security,
        const struct sol_network_link_addr *cliaddr,
        enum method method, const struct sol_oic_map_reader *input,
        struct sol_oic_map_writer *output);
    sol_coap_responsecode_t (*handle_pstat)(struct sol_oic_security *security,
        const struct sol_network_link_addr *cliaddr,
        enum method method, const struct sol_oic_map_reader *input,
        struct sol_oic_map_writer *output);
    sol_coap_responsecode_t (*handle_cred)(struct sol_oic_security *security,
        const struct sol_network_link_addr *cliaddr,
        enum method method, const struct sol_oic_map_reader *input,
        struct sol_oic_map_writer *output);
    sol_coap_responsecode_t (*handle_svc)(struct sol_oic_security *security,
        const struct sol_network_link_addr *cliaddr,
        enum method method, const struct sol_oic_map_reader *input,
        struct sol_oic_map_writer *output);

    int (*pair_request)(struct sol_oic_security *security,
        struct sol_oic_resource *resource,
        void (*paired_cb)(void *data, enum sol_oic_security_pair_result result), void *data);
};

struct sol_oic_security {
    struct sol_coap_server *server;
    struct sol_coap_server *server_dtls;
    const struct transfer_method *transfer_method;
    struct sol_socket_dtls_credential_cb callbacks;

    struct {
        struct sol_oic_server_resource *doxm;
        struct sol_oic_server_resource *pstat;
        struct sol_oic_server_resource *cred;
        struct sol_oic_server_resource *svc;
    } resources;

    struct doxm_data doxm;

    struct pstat_data pstat;
};

struct cred_item {
    struct {
        char *data;
        struct sol_str_slice slice;
    } id, psk;
    /* FIXME: Only symmetric pairwise keys supported at the moment */
};

struct creds {
    struct sol_vector items;
    const struct sol_oic_security *security;
};

struct pairing_ctx {
    void (*cb)(void *data, enum sol_oic_security_pair_result result);
    bool (*request_cb)(struct sol_coap_server *server, struct pairing_ctx *ctx, uint8_t *payload, uint16_t payload_len, sol_coap_responsecode_t code);
    void *cb_data;
    int64_t token;
    struct sol_oic_resource *resource;
    enum sol_oic_pairing_method pairing_method;
    char device_uuid[DTLS_PSK_ID_LEN];
    uint16_t port;
    struct sol_network_link_addr addr;
    struct sol_oic_security *security;
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
    const uint8_t *machine_id = get_machine_id();
    size_t len = DTLS_PSK_ID_LEN;

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

    item->id.data = strndup(id, id_len);
    SOL_NULL_CHECK_GOTO(item->id.data, no_id);

    item->psk.data = strndup(psk, psk_len);
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
        char id_buf_backing[DTLS_PSK_ID_LEN], psk_buf_backing[DTLS_PSK_KEY_LEN];
        bool result = false;

        sol_buffer_init_flags(&id_buf, id_buf_backing, sizeof(id_buf_backing),
            SOL_BUFFER_FLAGS_CLEAR_MEMORY | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
        sol_buffer_init_flags(&psk_buf, psk_buf_backing, sizeof(psk_buf_backing),
            SOL_BUFFER_FLAGS_CLEAR_MEMORY | SOL_BUFFER_FLAGS_NO_NUL_BYTE);

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
    struct sol_str_slice id = SOL_STR_SLICE_STR((char *)get_machine_id(),
        MACHINE_ID_LEN);

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

static bool
creds_store(struct creds *creds)
{
    struct sol_buffer buf;
    struct cred_item *item;
    uint16_t idx;
    char contents[1024];
    char file_name[256];
    int r = -1;

    if (!generate_file_name("/tmp/oic-creds-", file_name, sizeof(file_name)))
        return false;

    sol_buffer_init_flags(&buf, contents, sizeof(contents),
        SOL_BUFFER_FLAGS_CLEAR_MEMORY);

    if (sol_buffer_append_char(&buf, '[') < 0)
        goto failure;

    SOL_VECTOR_FOREACH_IDX (&creds->items, item, idx) {
        char id_buf_backing[64], psk_buf_backing[64];
        struct sol_buffer id_buf, psk_buf;
        bool iter_failed = true;

        sol_buffer_init_flags(&id_buf, id_buf_backing, sizeof(id_buf_backing), SOL_BUFFER_FLAGS_CLEAR_MEMORY);
        sol_buffer_init_flags(&psk_buf, psk_buf_backing, sizeof(psk_buf_backing), SOL_BUFFER_FLAGS_CLEAR_MEMORY);

        if (sol_buffer_append_as_base64(&id_buf, item->id.slice, SOL_BASE64_MAP) < 0)
            goto finish_bufs;
        if (sol_buffer_append_as_base64(&psk_buf, item->psk.slice, SOL_BASE64_MAP) < 0)
            goto finish_bufs;

        if (sol_buffer_append_printf(&buf, "{\"id\":\"%.*s\",", (int)id_buf.used, (const char *)id_buf.data) < 0)
            goto finish_bufs;
        if (sol_buffer_append_printf(&buf, "\"psk\":\"%.*s\"},", (int)psk_buf.used, (const char *)psk_buf.data) < 0)
            goto finish_bufs;

        iter_failed = false;

finish_bufs:
        sol_buffer_fini(&psk_buf);
        sol_buffer_fini(&id_buf);

        if (iter_failed)
            goto failure;
    }
    if (idx && buf.used) {
        /* Remove trailing ',' */
        buf.used--;
    }

    if (sol_buffer_append_char(&buf, ']') < 0)
        goto failure;

    //FIXME: Save file in appropriate location
    r = sol_util_write_file(file_name, "%.*s", (int)buf.used,
        (const char *)buf.data);
failure:
    sol_buffer_fini(&buf);

    return r >= 0;
}

static bool
decode_base64(struct sol_json_token *token, char *output, size_t output_len)
{
    struct sol_buffer buf;
    bool success = true;
    struct sol_buffer buf_value = SOL_BUFFER_INIT_EMPTY;
    int r;

    r = sol_json_token_get_unescaped_string(token, &buf_value);
    SOL_INT_CHECK(r, < 0, false);

    sol_buffer_init_flags(&buf, output, output_len,
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);

    if (sol_buffer_append_from_base64(&buf,
        sol_buffer_get_slice(&buf_value), SOL_BASE64_MAP) < 0) {
        SOL_WRN("Could not decode Base 64 value");
        success = false;
    }

    sol_buffer_fini(&buf);
    sol_buffer_fini(&buf_value);
    return success;
}

static void
doxm_clear(struct doxm_data *doxm)
{
    sol_vector_clear(&doxm->oxm);
}

static void
pstat_clear(struct pstat_data *pstat)
{
    sol_vector_clear(&pstat->sm);
}

static bool
validate_sct(int32_t val)
{
    if (val < 0)
        return false;

    return val < SCT_ASYMMETRIC_ENCRYPTION_KEY << 1;
}

static bool
validate_oxm(int32_t val)
{
    return val == SOL_OIC_PAIR_JUST_WORKS;
}

static bool
parse_oxm_vector(struct sol_json_token *current, struct sol_vector *vector)
{
    enum sol_json_loop_reason reason;
    struct sol_json_scanner scanner;
    struct sol_json_token token;
    enum sol_oic_pairing_method *item;
    int32_t v;

    sol_json_scanner_init(&scanner, current->start,
        current->end - current->start);
    SOL_JSON_SCANNER_ARRAY_LOOP (&scanner, &token,
        SOL_JSON_TYPE_NUMBER, reason) {
        if (sol_json_token_get_int32(&token, &v) < 0)
            return false;

        if (!validate_oxm(v))
            return false;

        item = sol_vector_append(vector);
        SOL_NULL_CHECK(item, false);

        *item = (enum sol_oic_pairing_method)v;
    }

    return reason == SOL_JSON_LOOP_REASON_OK;
}

static bool
parse_doxm_json(struct doxm_data *doxm, const char *payload, size_t len)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;
    int32_t inval;
    bool set_oxmsel = false, set_owned = false, set_deviceid = false,
        set_owner = false, set_sct = false, set_oxm = false;

    sol_util_secure_clear_memory(doxm, sizeof(*doxm));
    sol_vector_init(&doxm->oxm, sizeof(enum sol_oic_pairing_method));

    sol_json_scanner_init(&scanner, payload, len);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (!set_oxmsel && SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "oxmsel")) {
            if (sol_json_token_get_int32(&value, &inval) < 0) {
                SOL_WRN("Could not convert `oxmsel` field to integer");
                goto failure;
            }
            if (!validate_oxm(inval)) {
                SOL_WRN("Invalid `oxmsel` value");
                goto failure;
            }
            doxm->oxm_sel = (enum sol_oic_pairing_method)inval;
            set_oxmsel = true;
        } else if (!set_sct && SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "sct")) {
            if (sol_json_token_get_int32(&value, &inval) < 0) {
                SOL_WRN("Could not convert `sct` field to integer");
                goto failure;
            }
            if (!validate_sct(inval)) {
                SOL_WRN("Invalid `sct` value");
                goto failure;
            }
            doxm->sct = (enum sct)inval;
            set_sct = true;
        } else if (!set_sct && SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "oxm")) {
            if (sol_json_token_get_type(&value) != SOL_JSON_TYPE_ARRAY_START) {
                SOL_WRN("Field `oxm` has an unexpected value");
                goto failure;
            }
            if (!parse_oxm_vector(&value, &doxm->oxm)) {
                SOL_WRN("Could not parse `oxm` vector");
                goto failure;
            }
            set_oxm = true;
        } else if (!set_owned && SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "owned")) {
            if (sol_json_token_get_type(&value) == SOL_JSON_TYPE_TRUE) {
                doxm->owned = true;
            } else if (sol_json_token_get_type(&value) == SOL_JSON_TYPE_FALSE) {
                doxm->owned = false;
            } else {
                SOL_WRN("Invalid type for field `owned`");
                goto failure;
            }

            set_owned = true;
        } else if (!set_deviceid && SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "deviceid") && sol_json_token_get_type(&value) == SOL_JSON_TYPE_STRING) {
            if (!decode_base64(&value, doxm->device_uuid, sizeof(doxm->device_uuid)))
                goto failure;

            set_deviceid = true;
        } else if (!set_owner && SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "ownr") && sol_json_token_get_type(&value) == SOL_JSON_TYPE_STRING) {
            if (!decode_base64(&value, doxm->owner_uuid, sizeof(doxm->owner_uuid)))
                goto failure;

            set_owner = true;
        }
    }

    if (reason != SOL_JSON_LOOP_REASON_OK)
        goto failure;

    return set_oxmsel && set_owned && set_deviceid && set_sct && set_oxm;

failure:
    doxm_clear(doxm);
    return false;
}

static bool
parse_doxm_json_payload(struct doxm_data *doxm, const char *payload, size_t len)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;

    sol_json_scanner_init(&scanner, payload, len);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "doxm")) {
            struct sol_str_slice slice = sol_json_token_to_slice(&value);
            return parse_doxm_json(doxm, slice.data, slice.len);
        }
    }

    return false;
}

static bool
serialize_doxm_json(const struct doxm_data *doxm, struct sol_buffer *buf)
{
    struct sol_str_slice device_id_slice = SOL_STR_SLICE_STR(doxm->device_uuid,
        sizeof(doxm->device_uuid));
    struct sol_str_slice owner_id_slice = SOL_STR_SLICE_STR(doxm->owner_uuid,
        sizeof(doxm->owner_uuid));
    int r;
    enum sol_oic_pairing_method *oxm;
    uint16_t idx;

    r = sol_buffer_append_printf(buf, "\"doxm\":{\"oxm\":[");
    SOL_INT_CHECK(r, < 0, false);

    SOL_VECTOR_FOREACH_IDX (&doxm->oxm, oxm, idx) {
        r = sol_buffer_append_printf(buf, "%d,", *oxm);
        SOL_INT_CHECK(r, < 0, false);
    }
    if (idx && buf->used) {
        /* Remove trailing ',' */
        buf->used--;
    }

    r = sol_buffer_append_printf(buf,
        "],\"oxmsel\":%d,\"sct\":%d,\"owned\":%s,\"deviceid\":\"",
        doxm->oxm_sel, doxm->sct, doxm->owned ? "true" : "false");
    SOL_INT_CHECK(r, < 0, false);

    r = sol_buffer_append_as_base64(buf, device_id_slice, SOL_BASE64_MAP);
    SOL_INT_CHECK(r, < 0, false);
    r = sol_buffer_append_printf(buf, "\",\"ownr\":\"");
    SOL_INT_CHECK(r, < 0, false);
    r = sol_buffer_append_as_base64(buf, owner_id_slice, SOL_BASE64_MAP);
    SOL_INT_CHECK(r, < 0, false);
    r = sol_buffer_append_printf(buf, "\"}");
    SOL_INT_CHECK(r, < 0, false);

    return true;
}

static bool
validate_provisioning_mode(int32_t val)
{
    if (val < 0)
        return false;

    if (val > (DPM_NORMAL | DPM_RESET | DPM_TAKE_OWNER | DPM_BOOTSTRAP_SERVICE | DPM_SEC_MGMT_SERVICES | DPM_PROVISION_CREDS | DPM_PROVISION_ACLS))
        return false;

    return true;
}

static bool
validate_provisioning_op_mode(int32_t val)
{
    if (val < 0)
        return false;

    if (val > (DOP_MULTIPLE | DOP_SINGLE | DOP_CLIENT_DRIVEN |
        DOP_SERVER_DRIVEN))
        return false;

    return true;
}

static bool
parse_sm_vector(struct sol_json_token *current, struct sol_vector *vector)
{
    enum sol_json_loop_reason reason;
    struct sol_json_scanner scanner;
    struct sol_json_token token;
    enum provisioning_op_mode *item;
    int32_t v;

    sol_json_scanner_init(&scanner, current->start,
        current->end - current->start);
    SOL_JSON_SCANNER_ARRAY_LOOP (&scanner, &token,
        SOL_JSON_TYPE_NUMBER, reason) {
        if (sol_json_token_get_int32(&token, &v) < 0)
            return false;

        if (!validate_provisioning_op_mode(v))
            return false;

        item = sol_vector_append(vector);
        SOL_NULL_CHECK(item, false);

        *item = (enum provisioning_op_mode)v;
    }

    return reason == SOL_JSON_LOOP_REASON_OK;
}

enum pstat_fields {
    PF_ERROR = 0,
    PF_CM = 1 << 0,
        PF_TM = 1 << 1,
        PF_OM = 1 << 2,
        PF_CH = 1 << 3,
        PF_ISOP = 1 << 4,
        PF_SM = 1 << 5,
        PF_DEVICEID = 1 << 6
};

static enum pstat_fields
parse_pstat_json(struct pstat_data *pstat, const char *payload, size_t len)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;
    enum pstat_fields fields = 0;

    sol_util_secure_clear_memory(pstat, sizeof(*pstat));
    sol_vector_init(&pstat->sm, sizeof(enum provisioning_op_mode));

    sol_json_scanner_init(&scanner, payload, len);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (!(fields & PF_CM) && SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "cm")) {
            if (sol_json_token_get_int32(&value, (int32_t *)&pstat->cm) < 0) {
                SOL_WRN("Could not convert `cm` field to integer");
                goto failure;
            }
            if (!validate_provisioning_mode(pstat->cm)) {
                SOL_WRN("Invalid value for field `cm`");
                goto failure;
            }
            fields |= PF_CM;
        } else if (!(fields & PF_TM) && SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "tm")) {
            if (sol_json_token_get_int32(&value, (int32_t *)&pstat->tm) < 0) {
                SOL_WRN("Could not convert `tm` field to integer");
                goto failure;
            }
            if (!validate_provisioning_mode(pstat->tm)) {
                SOL_WRN("Invalid value for field `tm`");
                goto failure;
            }
            fields |= PF_TM;
        } else if (!(fields & PF_OM) && SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "om")) {
            if (sol_json_token_get_int32(&value, (int32_t *)&pstat->operation_mode) < 0) {
                SOL_WRN("Could not convert `om` field to integer");
                goto failure;
            }
            if (!validate_provisioning_op_mode(pstat->operation_mode)) {
                SOL_WRN("Invalid value for field `om`");
                goto failure;
            }
            fields |= PF_OM;
        } else if (!(fields & PF_CH) && SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "ch")) {
            int32_t v;

            if (sol_json_token_get_int32(&value, &v) < 0) {
                SOL_WRN("Could not convert `ch` field to integer");
                goto failure;
            }

            if (v < 0 || v > UINT16_MAX) {
                SOL_WRN("Field `ch` has value out of bounds");
                goto failure;
            }

            pstat->commit_hash = (uint16_t)v;
            fields |= PF_CH;
        } else if (!(fields & PF_ISOP) && SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "isop")) {
            if (sol_json_token_get_type(&value) == SOL_JSON_TYPE_TRUE) {
                pstat->op = true;
            } else if (sol_json_token_get_type(&value) == SOL_JSON_TYPE_FALSE) {
                pstat->op = false;
            } else {
                SOL_WRN("Invalid type for field `isop`");
                goto failure;
            }
            fields |= PF_ISOP;
        } else if (!(fields & PF_SM) && SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "sm")) {
            if (sol_json_token_get_type(&value) != SOL_JSON_TYPE_ARRAY_START) {
                SOL_WRN("Field `sm` has an unexpected value");
                goto failure;
            }
            if (!parse_sm_vector(&value, &pstat->sm)) {
                SOL_WRN("Could not parse `sm` vector");
                goto failure;
            }
            fields |= PF_SM;
        } else if (!(fields & PF_DEVICEID) && SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "deviceid") && sol_json_token_get_type(&value) == SOL_JSON_TYPE_STRING) {

            if (!decode_base64(&value, pstat->device_id, sizeof(pstat->device_id)))
                goto failure;
            fields |= PF_DEVICEID;
        }
    }

    if (reason != SOL_JSON_LOOP_REASON_OK)
        goto failure;

    if (fields & (PF_ISOP | PF_DEVICEID | PF_CH | PF_CM | PF_OM | PF_SM))
        return fields;

failure:
    pstat_clear(pstat);
    return PF_ERROR;
}

static bool
parse_pstat_json_payload(struct pstat_data *pstat, const char *payload, size_t len)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;

    sol_json_scanner_init(&scanner, payload, len);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "pstat")) {
            struct sol_str_slice slice = sol_json_token_to_slice(&value);
            return parse_pstat_json(pstat, slice.data, slice.len);
        }
    }

    return false;
}

static bool
serialize_pstat_json(const struct pstat_data *pstat, struct sol_buffer *buf)
{
    struct sol_str_slice device_id_slice = SOL_STR_SLICE_STR(pstat->device_id,
        sizeof(pstat->device_id));
    enum provisioning_op_mode *sm;
    uint16_t idx;

    if (sol_buffer_append_printf(buf, "\"pstat\":{") < 0)
        return false;
    if (sol_buffer_append_printf(buf, "\"isop\":%s,", pstat->op ? "true" : "false") < 0)
        return false;
    if (sol_buffer_append_printf(buf, "\"deviceid\":\"") < 0)
        return false;
    if (sol_buffer_append_as_base64(buf, device_id_slice, SOL_BASE64_MAP) < 0)
        return false;
    if (sol_buffer_append_printf(buf, "\",") < 0)
        return false;
    if (sol_buffer_append_printf(buf, "\"ch\":%d,", pstat->commit_hash) < 0)
        return false;
    if (sol_buffer_append_printf(buf, "\"cm\":%d,", pstat->cm) < 0)
        return false;
    if (sol_buffer_append_printf(buf, "\"tm\":%d,", pstat->tm) < 0)
        return false;
    if (sol_buffer_append_printf(buf, "\"om\":%d,", pstat->operation_mode) < 0)
        return false;

    if (sol_buffer_append_printf(buf, "\"sm\":[") < 0)
        return false;

    SOL_VECTOR_FOREACH_IDX (&pstat->sm, sm, idx) {
        if (sol_buffer_append_printf(buf, "%d,", *sm) < 0)
            return false;
    }
    if (idx && buf->used) {
        /* Remove trailing ',' */
        buf->used--;
    }

    if (sol_buffer_append_printf(buf, "]}") < 0)
        return false;

    return true;
}

static int
security_register_owner_psk(struct sol_oic_security *security,
    const struct sol_network_link_addr *cliaddr,
    struct sol_str_slice uuid,
    struct sol_str_slice owner_id, struct sol_str_slice device_id)
{
    struct sol_str_slice label = sol_str_slice_from_str(security->transfer_method->oxm_string);
    struct sol_buffer psk;
    struct sol_socket *socket_dtls;
    struct creds *creds;
    uint8_t psk_data[DTLS_PSK_KEY_LEN];
    int r;

    socket_dtls = sol_coap_server_get_socket(security->server_dtls);
    SOL_NULL_CHECK(socket_dtls, -EINVAL);

    sol_buffer_init_flags(&psk, psk_data, sizeof(psk_data),
        SOL_BUFFER_FLAGS_NO_NUL_BYTE
        | SOL_BUFFER_FLAGS_CLEAR_MEMORY);

    r = sol_socket_dtls_prf_keyblock(socket_dtls, cliaddr, label, owner_id,
        device_id, &psk);
    if (r < 0) {
        SOL_WRN("Could not generate PSK from DTLS handshake");
        goto inval;
    }

    creds = creds_init(security);
    if (!creds) {
        SOL_WRN("Could not load credentials database");
        goto inval;
    }

    if (!creds_add(creds, uuid.data, uuid.len, psk.data, psk.used)) {
        SOL_WRN("Could not register PSK in credentials database");
        goto inval;
    }

    r = creds_store(creds);
    creds_clear(creds);
    if (!r) {
        SOL_WRN("Could not store credentials database");
        goto inval;
    }

    sol_buffer_fini(&psk);
    return 0;

inval:
    sol_buffer_fini(&psk);
    return -EINVAL;
}

static int
security_store_context(struct sol_oic_security *security)
{
    struct sol_buffer buf;
    char contents[1024];
    int r;
    char file_name[256];

    if (!generate_file_name("/tmp/oic-security-context-", file_name,
        sizeof(file_name)))
        return -EINVAL;

    sol_buffer_init_flags(&buf, contents, sizeof(contents),
        SOL_BUFFER_FLAGS_CLEAR_MEMORY);

    r = sol_buffer_append_printf(&buf, "{");
    SOL_INT_CHECK_GOTO(r, < 0, failure);

    if (!serialize_doxm_json(&security->doxm, &buf))
        goto failure;

    r = sol_buffer_append_printf(&buf, ", ");
    SOL_INT_CHECK_GOTO(r, < 0, failure);

    if (!serialize_pstat_json(&security->pstat, &buf))
        goto failure;

    r = sol_buffer_append_char(&buf, '}');
    SOL_INT_CHECK_GOTO(r, < 0, failure);

    //FIXME: Save file in appropriate location
    if (sol_util_write_file(file_name, "%.*s",
        (int)buf.used, (const char *)buf.data) < 0)
        goto failure;

    return 0;

failure:
    sol_buffer_fini(&buf);
    return r;
}

static int
security_load_default_context(struct sol_oic_security *security)
{
    enum sol_oic_pairing_method *oxm;
    enum provisioning_op_mode *sm;

    sol_util_secure_clear_memory(&security->doxm, sizeof(security->doxm));

    sol_vector_init(&security->doxm.oxm, sizeof(enum sol_oic_pairing_method));
    security->doxm.sct = SCT_SYMMETRIC_PAIR_WISE_KEY;
    oxm = sol_vector_append(&security->doxm.oxm);
    SOL_NULL_CHECK(oxm, -ENOMEM);
    *oxm = SOL_OIC_PAIR_JUST_WORKS;
    security->doxm.oxm_sel = SOL_OIC_PAIR_JUST_WORKS;
    memcpy(security->doxm.device_uuid, get_machine_id(),
        sizeof(security->doxm.device_uuid));

    sol_util_secure_clear_memory(&security->pstat, sizeof(security->pstat));
    sol_vector_init(&security->pstat.sm, sizeof(enum provisioning_op_mode));
    security->pstat.cm = DPM_TAKE_OWNER | DPM_BOOTSTRAP_SERVICE |
        DPM_SEC_MGMT_SERVICES | DPM_PROVISION_CREDS | DPM_PROVISION_ACLS;
    security->pstat.tm = security->pstat.cm;
    security->pstat.operation_mode = DOP_SINGLE | DOP_CLIENT_DRIVEN;
    sm = sol_vector_append(&security->pstat.sm);
    SOL_NULL_CHECK_GOTO(sm, error);
    *sm = security->pstat.operation_mode;

    return 0;
error:
    sol_vector_clear(&security->doxm.oxm);
    return -ENOMEM;
}

static int
security_load_context(struct sol_oic_security *security)
{
    struct sol_json_scanner scanner;
    struct sol_json_token token, key, value;
    enum sol_json_loop_reason reason;
    size_t length;
    char *contents = NULL;
    char file_name[256];

    if (generate_file_name("/tmp/oic-security-context-", file_name,
        sizeof(file_name)))
        contents = sol_util_load_file_string(file_name, &length);

    if (!contents)
        return security_load_default_context(security);

    sol_json_scanner_init(&scanner, contents, length);
    SOL_JSON_SCANNER_OBJECT_LOOP (&scanner, &token, &key, &value, reason) {
        if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "doxm")) {
            struct sol_str_slice slice = sol_json_token_to_slice(&value);

            if (!parse_doxm_json(&security->doxm, slice.data, slice.len)) {
                reason = SOL_JSON_LOOP_REASON_INVALID;
                break;
            }
        } else if (SOL_JSON_TOKEN_STR_LITERAL_EQ(&key, "pstat")) {
            struct sol_str_slice slice = sol_json_token_to_slice(&value);

            if (parse_pstat_json(&security->pstat, slice.data, slice.len) ==
                PF_ERROR) {
                reason = SOL_JSON_LOOP_REASON_INVALID;
                break;
            }
        }
    }

    free(contents);

    return reason == SOL_JSON_LOOP_REASON_OK ? 0 : -EINVAL;
}

static sol_coap_responsecode_t
handle_doxm_jw(struct sol_oic_security *security,
    const struct sol_network_link_addr *cliaddr, enum method method,
    const struct sol_oic_map_reader *input, struct sol_oic_map_writer *output)
{
    static const char unowned_uuid[DTLS_PSK_ID_LEN] = { 0 };

    bool ret;
    int r;
    CborError err;

    switch (method) {
    case METHOD_GET: {
        struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;

        r = sol_buffer_append_printf(&buf, "{");
        SOL_INT_CHECK_GOTO(r, < 0, error_get);
        if (!serialize_doxm_json(&security->doxm, &buf)) {
            SOL_WRN("Could not serialize transfer method data");
            goto error_get;
        }
        r = sol_buffer_append_printf(&buf, "}");
        SOL_INT_CHECK_GOTO(r, < 0, error_get);

        ret = sol_oic_map_append(output, &SOL_OIC_REPR_TEXT_STRING(NULL,
            buf.data, buf.used));
        sol_buffer_fini(&buf);
        SOL_EXP_CHECK(!ret, SOL_COAP_RSPCODE_BAD_REQUEST);

        return SOL_COAP_RSPCODE_CONTENT;

error_get:
        sol_buffer_fini(&buf);
        return SOL_COAP_RSPCODE_INTERNAL_ERROR;
    }
    case METHOD_PUT: {
        struct doxm_data new_doxm;
        CborValue str, *map = (CborValue *)input;
        struct sol_str_slice doxm_str;
        sol_coap_responsecode_t return_code = SOL_COAP_RSPCODE_INTERNAL_ERROR;

        if (!cbor_value_is_map(map)) {
            SOL_WRN("Transfer method has an invalid security payload");
            return SOL_COAP_RSPCODE_BAD_REQUEST;
        }

        err = cbor_value_enter_container(map, &str);
        SOL_INT_CHECK(err, != CborNoError, SOL_COAP_RSPCODE_BAD_REQUEST);
        if (!cbor_value_is_text_string(&str))
            return SOL_COAP_RSPCODE_BAD_REQUEST;

        err = cbor_value_dup_text_string(&str, (char **)&doxm_str.data,
            &doxm_str.len, NULL);
        SOL_INT_CHECK(err, != CborNoError, SOL_COAP_RSPCODE_BAD_REQUEST);

        ret = parse_doxm_json_payload(&new_doxm, doxm_str.data, doxm_str.len);
        free((char *)doxm_str.data);
        if (!ret) {
            SOL_WRN("Could not parse security payload");
            return_code = SOL_COAP_RSPCODE_BAD_REQUEST;
            goto exit;
        }

        if (new_doxm.oxm_sel != SOL_OIC_PAIR_JUST_WORKS) {
            SOL_WRN("Ownership transfer method invalid, expecting just works "
                "(%d), got %d instead", SOL_OIC_PAIR_JUST_WORKS,
                new_doxm.oxm_sel);
            return_code = SOL_COAP_RSPCODE_NOT_IMPLEMENTED;
            goto exit;
        }

        if (!security->doxm.owned && !new_doxm.owned) {
            struct sol_socket *socket;

            socket = sol_coap_server_get_socket(security->server_dtls);
            SOL_NULL_CHECK_GOTO(socket, exit);

            SOL_INF("Device is unowned, enabling anonymous ECDH for initial "
                "handshake");
            sol_socket_dtls_set_anon_ecdh_enabled(socket, true);
            return_code = SOL_COAP_RSPCODE_CHANGED;
            goto exit;
        }

        if (!security->doxm.owned && new_doxm.owned &&
            memcmp(new_doxm.owner_uuid, unowned_uuid, sizeof(unowned_uuid))) {
            struct sol_str_slice owner_id, device_id;
            owner_id.data = new_doxm.owner_uuid;
            owner_id.len = sizeof(new_doxm.owner_uuid);
            device_id.data = new_doxm.device_uuid;
            device_id.len = sizeof(new_doxm.device_uuid);

            r = security_register_owner_psk(security, cliaddr, owner_id,
                owner_id, device_id);
            if (!r) {
                security->doxm.owned = true;
                security->doxm.oxm_sel = new_doxm.oxm_sel;
                memcpy(security->doxm.owner_uuid, new_doxm.owner_uuid,
                    sizeof(security->doxm.owner_uuid));

                SOL_INF("Owner PSK has been added, storing on disk");
                r = security_store_context(security);
            }
            if (!r) {
                struct sol_socket *socket;

                socket = sol_coap_server_get_socket(security->server_dtls);
                SOL_NULL_CHECK_GOTO(socket, exit);

                SOL_INF("Anonymous ECDH not needed anymore, disabling");
                sol_socket_dtls_set_anon_ecdh_enabled(socket, false);
                return_code = SOL_COAP_RSPCODE_CHANGED;
                goto exit;
            }

            SOL_INF("Some error happened while trying to register owner PSK");
            return_code = SOL_COAP_RSPCODE_UNAUTHORIZED;
        }

exit:
        doxm_clear(&new_doxm);
        return return_code;
    }
    default:
        return SOL_COAP_RSPCODE_BAD_REQUEST;
    }

}

static sol_coap_responsecode_t
handle_pstat_jw(struct sol_oic_security *security,
    const struct sol_network_link_addr *cliaddr, enum method method,
    const struct sol_oic_map_reader *input, struct sol_oic_map_writer *output)
{
    bool ret;

    switch (method) {
    case METHOD_GET: {
        struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
        int r;

        r = sol_buffer_append_printf(&buf, "{");
        SOL_INT_CHECK_GOTO(r, < 0, error_get);
        if (!serialize_pstat_json(&security->pstat, &buf)) {
            SOL_WRN("Could not serialize provisioning strategy data");
            goto error_get;
        }
        r = sol_buffer_append_printf(&buf, "}");
        SOL_INT_CHECK_GOTO(r, < 0, error_get);

        ret = sol_oic_map_append(output, &SOL_OIC_REPR_TEXT_STRING(NULL,
            buf.data, buf.used));
        sol_buffer_fini(&buf);
        SOL_EXP_CHECK(!ret, SOL_COAP_RSPCODE_BAD_REQUEST);

        return SOL_COAP_RSPCODE_CONTENT;
error_get:
        sol_buffer_fini(&buf);
        return SOL_COAP_RSPCODE_INTERNAL_ERROR;
    }
    case METHOD_PUT: {
        struct pstat_data new_pstat;
        enum pstat_fields fields;
        uint16_t commit_hash = 0;
        CborValue str, *map = (CborValue *)input;
        CborError err;
        struct sol_str_slice pstat_str;

        if (!cbor_value_is_map(map)) {
            SOL_WRN("Transfer method has an invalid security payload");
            return SOL_COAP_RSPCODE_BAD_REQUEST;
        }

        err = cbor_value_enter_container(map, &str);
        SOL_INT_CHECK(err, != CborNoError, SOL_COAP_RSPCODE_BAD_REQUEST);
        if (!cbor_value_is_text_string(&str))
            return SOL_COAP_RSPCODE_BAD_REQUEST;

        err = cbor_value_dup_text_string(&str, (char **)&pstat_str.data,
            &pstat_str.len, NULL);
        SOL_INT_CHECK(err, != CborNoError, SOL_COAP_RSPCODE_BAD_REQUEST);
        fields = parse_pstat_json_payload(&new_pstat, pstat_str.data,
            pstat_str.len);
        free((char *)pstat_str.data);
        pstat_clear(&new_pstat);
        if (fields == PF_ERROR) {
            SOL_WRN("Could not parse security payload");
            return SOL_COAP_RSPCODE_BAD_REQUEST;
        }

        if (fields & PF_CH)
            commit_hash = new_pstat.commit_hash;

        if (fields & PF_TM) {
            if (new_pstat.tm == 0 && security->pstat.commit_hash == commit_hash) {
                security->pstat.op = true;
                security->pstat.cm = DPM_NORMAL;
            }
        }

        if (fields & PF_OM) {
            enum provisioning_op_mode *pom;
            uint16_t idx;

            SOL_VECTOR_FOREACH_IDX (&security->pstat.sm, pom, idx) {
                if (*pom == new_pstat.operation_mode) {
                    security->pstat.operation_mode = new_pstat.operation_mode;
                    break;
                }
            }
        }

        if (security_store_context(security) == 0)
            return SOL_COAP_RSPCODE_CHANGED;

        SOL_WRN("Could not store security context");

        /* Fallthrough */
    }
    default:
        return SOL_COAP_RSPCODE_BAD_REQUEST;
    }
}

static int
create_request(sol_coap_method_t method, sol_coap_msgtype_t type,
    const char *href, const char *query, size_t query_len,
    int64_t *token, struct sol_coap_packet **req_ptr)
{
    int r;
    struct sol_coap_packet *req;

    req = sol_coap_packet_request_new(method, type);
    SOL_NULL_CHECK(req, -ENOMEM);

    if (!sol_oic_set_token_and_mid(req, token)) {
        SOL_WRN("Could not set token and mid");
        r = -EINVAL;
        goto out;
    }

    r = sol_coap_packet_add_uri_path_option(req, href);
    SOL_INT_CHECK_GOTO(r, < 0, out);

    if (query && query_len) {
        r = sol_coap_add_option(req, SOL_COAP_OPTION_URI_QUERY, query, query_len);
        SOL_INT_CHECK_GOTO(r, < 0, out);
    }

    *req_ptr = req;
    return 0;

out:
    sol_coap_packet_unref(req);
    return r;
}

static void
clear_pairing_ctx(struct pairing_ctx *ctx)
{
    sol_oic_resource_unref(ctx->resource);
}

static bool
request_cb(struct sol_coap_server *server, struct sol_coap_packet *req, const struct sol_network_link_addr *addr, void *data)
{
    struct pairing_ctx *ctx = data;
    uint8_t *payload;
    uint16_t payload_len;

    if (!req || !addr)
        goto error;

    if (!sol_oic_pkt_has_same_token(req, ctx->token)) {
        SOL_WRN("Packet received has not the same token");
        goto error;
    }

    if (!sol_oic_pkt_has_cbor_content(req) ||
        sol_coap_packet_get_payload(req, &payload, &payload_len) < 0) {
        payload = NULL;
        payload_len = 0;
    }

    if (!ctx->request_cb(server, ctx, payload, payload_len,
        sol_coap_header_get_code(req)))
        goto error;

    return false;

error:
    ctx->cb(ctx->cb_data, SOL_OIC_PAIR_ERROR_PAIR_FAILURE);
    clear_pairing_ctx(ctx);
    free(ctx);
    return false;
}

static bool
put_doxm_owned_cb(struct sol_coap_server *server, struct pairing_ctx *ctx, uint8_t *payload, uint16_t payload_len, sol_coap_responsecode_t code)
{
    struct sol_socket *socket;
    struct sol_str_slice owner_id, device_id;
    int r;

    socket = sol_coap_server_get_socket(server);
    SOL_NULL_CHECK(socket, false);
    r = sol_socket_dtls_close(socket, &ctx->addr);
    SOL_INT_CHECK(r, < 0, false);


    owner_id = SOL_STR_SLICE_STR((const char *)get_machine_id(),
        MACHINE_ID_LEN);
    device_id = SOL_STR_SLICE_STR(ctx->device_uuid, sizeof(ctx->device_uuid));
    r = security_register_owner_psk(ctx->security, &ctx->addr, device_id,
        owner_id, device_id);
    SOL_INT_CHECK(r, < 0, false);

    r = sol_socket_dtls_set_handshake_cipher(socket,
        SOL_SOCKET_DTLS_CIPHER_NULL_NULL_NULL);
    SOL_INT_CHECK(r, < 0, false);

    ctx->cb(ctx->cb_data, SOL_OIC_PAIR_SUCCESS);
    clear_pairing_ctx(ctx);
    free(ctx);
    return true;
}

static bool
put_pstat_cb(struct sol_coap_server *server, struct pairing_ctx *ctx, uint8_t *payload, uint16_t payload_len, sol_coap_responsecode_t code)
{
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    enum sol_oic_pairing_method *oxm;
    struct sol_oic_map_writer map_encoder;
    CborError err;
    int r;
    bool ret;
    struct sol_coap_packet *req = NULL;
    struct sol_socket *socket;
    struct doxm_data doxm = {
        .oxm_sel = ctx->pairing_method,
        .owned = true,
        .sct = SCT_SYMMETRIC_PAIR_WISE_KEY,
    };

    if (code != SOL_COAP_RSPCODE_CHANGED || !ctx->security->server_dtls)
        return false;

    //Enable anonymous ECDH in socket
    socket = sol_coap_server_get_socket(ctx->security->server_dtls);
    SOL_NULL_CHECK(socket, false);

    SOL_INF("Enabling anonymous ECDH for initial handshake");
    r = sol_socket_dtls_set_anon_ecdh_enabled(socket, true);
    SOL_INT_CHECK(r, < 0, false);

    //Initialize doxm
    sol_vector_init(&doxm.oxm, sizeof(enum sol_oic_pairing_method));
    oxm = sol_vector_append(&doxm.oxm);
    SOL_NULL_CHECK_GOTO(oxm, error);
    *oxm = doxm.oxm_sel;
    memcpy(doxm.device_uuid, ctx->device_uuid, sizeof(doxm.device_uuid));
    memcpy(doxm.owner_uuid, get_machine_id(), sizeof(doxm.owner_uuid));

    //Serialize doxm
    r = sol_buffer_append_printf(&buf, "{");
    SOL_INT_CHECK_GOTO(r, < 0, error);
    if (!serialize_doxm_json(&doxm, &buf)) {
        SOL_WRN("Could not serialize transfer method data");
        goto error;
    }
    r = sol_buffer_append_printf(&buf, "}");
    SOL_INT_CHECK_GOTO(r, < 0, error);

    //send doxm
    r = create_request(SOL_COAP_METHOD_PUT, SOL_COAP_TYPE_NONCON,
        "/oic/sec/doxm", NULL, 0, &ctx->token, &req);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    sol_oic_packet_cbor_create(req, &map_encoder);
    ret = sol_oic_map_append(&map_encoder, &SOL_OIC_REPR_TEXT_STRING(NULL,
        buf.data, buf.used));
    SOL_EXP_CHECK_GOTO(!ret, error);

    err = sol_oic_packet_cbor_close(req, &map_encoder);
    SOL_INT_CHECK_GOTO(err, != CborNoError, error);

    ctx->request_cb = put_doxm_owned_cb;
    ctx->addr.port = ctx->resource->secure_port;
    r = sol_coap_send_packet_with_reply(ctx->security->server_dtls, req,
        &ctx->addr, request_cb, ctx);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    doxm_clear(&doxm);
    sol_buffer_fini(&buf);
    return true;

error:
    doxm_clear(&doxm);
    sol_buffer_fini(&buf);
    sol_coap_packet_unref(req);
    return false;

}

static bool
get_pstat_cb(struct sol_coap_server *server, struct pairing_ctx *ctx, uint8_t *payload, uint16_t payload_len, sol_coap_responsecode_t code)
{
    CborParser parser;
    CborError err;
    CborValue root, str;
    struct sol_str_slice pstat_str;
    struct pstat_data pstat;
    struct sol_coap_packet *req = NULL;
    struct sol_oic_map_writer map_encoder;
    bool parsed;
    int r;

    if (!payload || !payload_len)
        return false;

    if (code != SOL_COAP_RSPCODE_CONTENT)
        return false;

    err = cbor_parser_init(payload, payload_len, 0, &parser, &root);
    SOL_INT_CHECK(err, != CborNoError, false);
    if (!cbor_value_is_map(&root))
        return false;

    err = cbor_value_enter_container(&root, &str);
    SOL_INT_CHECK(err, != CborNoError, false);
    if (!cbor_value_is_text_string(&str))
        return false;

    err = cbor_value_dup_text_string(&str, (char **)&pstat_str.data,
        &pstat_str.len, NULL);
    SOL_INT_CHECK(err, != CborNoError, false);
    parsed = parse_pstat_json_payload(&pstat, pstat_str.data, pstat_str.len);
    pstat_clear(&pstat);
    if (!parsed || pstat.operation_mode != (DOP_SINGLE | DOP_CLIENT_DRIVEN))
        goto fail;

    r = create_request(SOL_COAP_METHOD_PUT, SOL_COAP_TYPE_NONCON,
        "/oic/sec/pstat", NULL, 0, &ctx->token, &req);
    SOL_INT_CHECK(r, < 0, false);

    sol_oic_packet_cbor_create(req, &map_encoder);
    if (!sol_oic_map_append(&map_encoder, &SOL_OIC_REPR_TEXT_STRING(NULL,
        pstat_str.data, pstat_str.len)))
        goto fail;

    err = sol_oic_packet_cbor_close(req, &map_encoder);
    SOL_INT_CHECK_GOTO(err, != CborNoError, fail);

    ctx->request_cb = put_pstat_cb;
    r = sol_coap_send_packet_with_reply(server, req,
        &ctx->addr, request_cb, ctx);
    SOL_INT_CHECK_GOTO(r, < 0, fail);
    free((char *)pstat_str.data);
    return true;

fail:
    free((char *)pstat_str.data);
    sol_coap_packet_unref(req);
    return false;
}

static bool
put_doxm_cb(struct sol_coap_server *server, struct pairing_ctx *ctx, uint8_t *payload, uint16_t payload_len, sol_coap_responsecode_t code)
{
    int r;
    struct sol_coap_packet *req;

    if (code != SOL_COAP_RSPCODE_CHANGED)
        return false;

    r = create_request(SOL_COAP_METHOD_GET, SOL_COAP_TYPE_NONCON,
        "/oic/sec/pstat", NULL, 0, &ctx->token, &req);
    SOL_INT_CHECK(r, < 0, false);

    ctx->request_cb = get_pstat_cb;
    r = sol_coap_send_packet_with_reply(server, req,
        &ctx->addr, request_cb, ctx);
    SOL_INT_CHECK(r, < 0, false);

    return true;
}

static bool
doxm_supports_pairing_method(struct doxm_data *doxm, enum sol_oic_pairing_method pm)
{
    enum sol_oic_pairing_method *oxm;
    uint16_t idx;

    SOL_VECTOR_FOREACH_IDX (&doxm->oxm, oxm, idx) {
        if (*oxm == pm)
            return true;
    }

    return false;
}

static bool
get_doxm_cb(struct sol_coap_server *server, struct pairing_ctx *ctx, uint8_t *payload, uint16_t payload_len, sol_coap_responsecode_t code)
{
    CborParser parser;
    CborError err;
    CborValue root, str;
    int r;
    struct sol_oic_map_writer map_encoder;
    struct sol_str_slice doxm_str = { .len = 0, .data = NULL };
    struct doxm_data doxm;
    struct sol_coap_packet *req = NULL;
    enum sol_oic_security_pair_result result = SOL_OIC_PAIR_ERROR_PAIR_FAILURE;

    if (!payload || !payload_len)
        goto end;

    err = cbor_parser_init(payload, payload_len, 0, &parser, &root);
    SOL_INT_CHECK_GOTO(err, != CborNoError, end);
    if (!cbor_value_is_map(&root))
        goto end;

    err = cbor_value_enter_container(&root, &str);
    SOL_INT_CHECK_GOTO(err, != CborNoError, end);
    if (!cbor_value_is_text_string(&str)) {
        result = SOL_OIC_PAIR_ERROR_ALREADY_OWNED;
        goto end;
    }

    err = cbor_value_dup_text_string(&str, (char **)&doxm_str.data,
        &doxm_str.len, NULL);
    SOL_INT_CHECK_GOTO(err, != CborNoError, end);
    if (!parse_doxm_json_payload(&doxm, doxm_str.data, doxm_str.len))
        goto end;

    if (doxm.owned) {
        result = SOL_OIC_PAIR_ERROR_ALREADY_OWNED;
        goto end_doxm;
    }

    if (!doxm_supports_pairing_method(&doxm, ctx->pairing_method)) {
        result = SOL_OIC_PAIR_ERROR_UNSUPPORTED_PAIRING_METHOD;
        goto end_doxm;
    }

    if (!(doxm.sct | SCT_SYMMETRIC_PAIR_WISE_KEY)) {
        result = SOL_OIC_PAIR_ERROR_UNSUPPORTED_CREDENTIAL_TYPE;
        goto end_doxm;
    }

    r = create_request(SOL_COAP_METHOD_PUT, SOL_COAP_TYPE_NONCON,
        "/oic/sec/doxm", NULL, 0, &ctx->token, &req);
    SOL_INT_CHECK_GOTO(r, < 0, end_doxm);

    sol_oic_packet_cbor_create(req, &map_encoder);
    if (!sol_oic_map_append(&map_encoder,
        &SOL_OIC_REPR_TEXT_STRING(NULL, doxm_str.data, doxm_str.len)))
        goto end_doxm;
    err = sol_oic_packet_cbor_close(req, &map_encoder);
    SOL_INT_CHECK_GOTO(err, != CborNoError, end_doxm);

    ctx->request_cb = put_doxm_cb;
    memcpy(ctx->device_uuid, doxm.device_uuid, sizeof(doxm.device_uuid));
    r = sol_coap_send_packet_with_reply(server, req,
        &ctx->addr, request_cb, ctx);
    SOL_INT_CHECK_GOTO(r, < 0, end_doxm);

    doxm_clear(&doxm);
    goto success;

end_doxm:
    doxm_clear(&doxm);
end:
    ctx->cb(ctx->cb_data, result);
    clear_pairing_ctx(ctx);
    free(ctx);
success:
    free((char *)doxm_str.data);
    return true;
}

#define QUERY_OWNED_FALSE "Owned=FALSE"
static int
get_doxm(struct sol_oic_security *security, struct pairing_ctx *ctx)
{
    int r;
    struct sol_coap_packet *req;

    r = create_request(SOL_COAP_METHOD_GET, SOL_COAP_TYPE_NONCON,
        "/oic/sec/doxm", QUERY_OWNED_FALSE, sizeof(QUERY_OWNED_FALSE),
        &ctx->token, &req);
    SOL_INT_CHECK(r, < 0, r);

    ctx->request_cb = get_doxm_cb;
    r = sol_coap_send_packet_with_reply(security->server, req,
        &ctx->addr, request_cb, ctx);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}
#undef QUERY_OWNED_FALSE

static int
pair_request_jw(struct sol_oic_security *security,
    struct sol_oic_resource *resource,
    void (*paired_cb)(void *data, enum sol_oic_security_pair_result result), void *data)
{
    struct pairing_ctx *ctx = NULL;
    int r;

    ctx = sol_util_memdup(&(struct pairing_ctx) {
            .cb = paired_cb,
            .cb_data = data,
        }, sizeof(*ctx));
    SOL_NULL_CHECK(ctx, -ENOMEM);
    ctx->pairing_method = SOL_OIC_PAIR_JUST_WORKS;
    ctx->resource = sol_oic_resource_ref(resource);
    ctx->addr = ctx->resource->addr;
    ctx->security = security;
    if (!ctx->resource) {
        free(ctx);
        return -EINVAL;
    }

    r = get_doxm(security, ctx);
    if (r < 0) {
        clear_pairing_ctx(ctx);
        free(ctx);
    }

    return r;
}

static const struct transfer_method transfer_method_just_works = {
    .oxm_string = "oic.sec.doxm.jw",
    .handle_doxm = handle_doxm_jw,
    .handle_pstat = handle_pstat_jw,
    .pair_request = pair_request_jw,
};

static sol_coap_responsecode_t
handle_get_doxm_thunk(const struct sol_network_link_addr *cliaddr,
    const void *data, const struct sol_oic_map_reader *input, struct sol_oic_map_writer *output)
{
    struct sol_oic_security *security = (struct sol_oic_security *)data;

    if (!security->transfer_method->handle_doxm)
        return -ENOENT;

    return security->transfer_method->handle_doxm(security, cliaddr, METHOD_GET,
        input, output);
}

static sol_coap_responsecode_t
handle_put_doxm_thunk(const struct sol_network_link_addr *cliaddr,
    const void *data, const struct sol_oic_map_reader *input, struct sol_oic_map_writer *output)
{
    struct sol_oic_security *security = (struct sol_oic_security *)data;

    if (!security->transfer_method->handle_doxm)
        return SOL_COAP_RSPCODE_NOT_IMPLEMENTED;

    return security->transfer_method->handle_doxm(security, cliaddr, METHOD_PUT,
        input, output);
}

static sol_coap_responsecode_t
handle_get_pstat_thunk(const struct sol_network_link_addr *cliaddr,
    const void *data, const struct sol_oic_map_reader *input, struct sol_oic_map_writer *output)
{
    struct sol_oic_security *security = (struct sol_oic_security *)data;

    if (!security->transfer_method->handle_pstat)
        return SOL_COAP_RSPCODE_NOT_IMPLEMENTED;

    return security->transfer_method->handle_pstat(security, cliaddr,
        METHOD_GET, input, output);
}

static sol_coap_responsecode_t
handle_put_pstat_thunk(const struct sol_network_link_addr *cliaddr,
    const void *data, const struct sol_oic_map_reader *input, struct sol_oic_map_writer *output)
{
    struct sol_oic_security *security = (struct sol_oic_security *)data;

    if (!security->transfer_method->handle_pstat)
        return SOL_COAP_RSPCODE_NOT_IMPLEMENTED;

    return security->transfer_method->handle_pstat(security, cliaddr,
        METHOD_PUT, input, output);
}

static sol_coap_responsecode_t
handle_put_cred_thunk(const struct sol_network_link_addr *cliaddr,
    const void *data, const struct sol_oic_map_reader *input, struct sol_oic_map_writer *output)
{
    struct sol_oic_security *security = (struct sol_oic_security *)data;

    if (!security->transfer_method->handle_cred)
        return SOL_COAP_RSPCODE_NOT_IMPLEMENTED;

    return security->transfer_method->handle_cred(security, cliaddr,
        METHOD_PUT, input, output);
}

static sol_coap_responsecode_t
handle_put_svc_thunk(const struct sol_network_link_addr *cliaddr,
    const void *data, const struct sol_oic_map_reader *input, struct sol_oic_map_writer *output)
{
    struct sol_oic_security *security = (struct sol_oic_security *)data;

    if (!security->transfer_method->handle_svc)
        return SOL_COAP_RSPCODE_NOT_IMPLEMENTED;

    return security->transfer_method->handle_svc(security, cliaddr,
        METHOD_PUT, input, output);
}

static bool
register_server_bits(struct sol_oic_security *security)
{
    static const struct sol_oic_resource_type sec_doxm = {
        SOL_SET_API_VERSION(.api_version = SOL_OIC_RESOURCE_TYPE_API_VERSION, )
        .get.handle = handle_get_doxm_thunk,
        .put.handle = handle_put_doxm_thunk,
        .path = SOL_STR_SLICE_LITERAL("/oic/sec/doxm"),
        .resource_type = SOL_STR_SLICE_LITERAL("oic.sec.doxm"),
        .interface = SOL_STR_SLICE_LITERAL("oic.mi.def"),
    };
    static const struct sol_oic_resource_type sec_pstat = {
        SOL_SET_API_VERSION(.api_version = SOL_OIC_RESOURCE_TYPE_API_VERSION, )
        .get.handle = handle_get_pstat_thunk,
        .put.handle = handle_put_pstat_thunk,
        .path = SOL_STR_SLICE_LITERAL("/oic/sec/pstat"),
    };
    static const struct sol_oic_resource_type sec_cred = {
        SOL_SET_API_VERSION(.api_version = SOL_OIC_RESOURCE_TYPE_API_VERSION, )
        .put.handle = handle_put_cred_thunk,
        .path = SOL_STR_SLICE_LITERAL("/oic/sec/cred"),
    };
    static const struct sol_oic_resource_type sec_svc = {
        SOL_SET_API_VERSION(.api_version = SOL_OIC_RESOURCE_TYPE_API_VERSION, )
        .put.handle = handle_put_svc_thunk,
        .path = SOL_STR_SLICE_LITERAL("/oic/sec/svc"),
    };
    struct sol_oic_server_resource *doxm, *pstat, *cred, *svc;

    doxm = sol_oic_server_add_resource(&sec_doxm, security,
        SOL_OIC_FLAG_DISCOVERABLE_EXPLICIT | SOL_OIC_FLAG_OBSERVABLE |
        SOL_OIC_FLAG_SECURE | SOL_OIC_FLAG_ACTIVE);
    if (!doxm)
        return false;

    pstat = sol_oic_server_add_resource(&sec_pstat, security,
        SOL_OIC_FLAG_ACTIVE);
    if (!pstat)
        goto free_doxm;

    cred = sol_oic_server_add_resource(&sec_cred, security,
        SOL_OIC_FLAG_ACTIVE);
    if (!cred)
        goto free_pstat;

    svc = sol_oic_server_add_resource(&sec_svc, security,
        SOL_OIC_FLAG_ACTIVE);
    if (!svc)
        goto free_cred;

    security->resources.doxm = doxm;
    security->resources.pstat = pstat;
    security->resources.cred = cred;
    security->resources.svc = svc;
    return true;

free_cred:
    sol_oic_server_del_resource(svc);
free_pstat:
    sol_oic_server_del_resource(pstat);
free_doxm:
    sol_oic_server_del_resource(doxm);

    return false;
}

static void
unregister_server_bits(struct sol_oic_security *security)
{
    if (security->resources.doxm)
        sol_oic_server_del_resource(security->resources.doxm);

    if (security->resources.pstat)
        sol_oic_server_del_resource(security->resources.pstat);

    if (security->resources.cred)
        sol_oic_server_del_resource(security->resources.cred);

    if (security->resources.svc)
        sol_oic_server_del_resource(security->resources.svc);
}

static void
sol_oic_security_del_full(struct sol_oic_security *security, bool is_server)
{
    SOL_NULL_CHECK(security);

    if (is_server)
        unregister_server_bits(security);

    sol_coap_server_unref(security->server);
    sol_coap_server_unref(security->server_dtls);
    sol_vector_clear(&security->pstat.sm);
    sol_vector_clear(&security->doxm.oxm);

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
    SOL_NULL_CHECK(socket_dtls, NULL);

    security->callbacks = (struct sol_socket_dtls_credential_cb) {
        .data = security,
        .init = creds_init,
        .clear = creds_clear,
        .get_id = creds_get_id,
        .get_psk = creds_get_psk
    };
    if (sol_socket_dtls_set_credentials_callbacks(socket_dtls, &security->callbacks) < 0) {
        SOL_WRN("Passed DTLS socket is not a valid sol_socket_dtls");
        return NULL;
    }

    security->server = sol_coap_server_ref(server);
    security->server_dtls = sol_coap_server_ref(server_dtls);

    /* FIXME: More methods may be added in the future, so this might
     * have to change to a vector of supported methods. */
    security->transfer_method = &transfer_method_just_works;

    if (security_load_context(security) < 0) {
        SOL_WRN("Could not load security context");
        sol_oic_security_del_full(security, false);
        return NULL;
    }

    if (is_server) {
        if (register_server_bits(security))
            return security;

        sol_oic_security_del_full(security, false);
        return NULL;
    }

    return security;
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

bool
sol_oic_security_get_is_paired(const struct sol_oic_security *security,
    struct sol_str_slice device_id)
{
#ifdef DTLS
    struct creds *creds;
    char psk[DTLS_PSK_KEY_LEN];
    ssize_t r;

    creds = creds_init(security);
    SOL_NULL_CHECK(creds, false);

    r = creds_get_psk(creds, device_id, psk, sizeof(psk));
    creds_clear(creds);
    sol_util_secure_clear_memory(psk, sizeof(psk));

    return r > 0;
#else
    return false;
#endif
}

int
sol_oic_security_pair_request(struct sol_oic_security *security,
    struct sol_oic_resource *resource, enum sol_oic_pairing_method pm,
    void (*paired_cb)(void *data, enum sol_oic_security_pair_result result), void *data)
{
#ifdef DTLS
    SOL_NULL_CHECK(security, -EINVAL);
    SOL_NULL_CHECK(security->transfer_method, -EINVAL);
    SOL_NULL_CHECK(security->transfer_method->pair_request, -EINVAL);
    SOL_NULL_CHECK(resource, -EINVAL);
    SOL_NULL_CHECK(paired_cb, -EINVAL);

    if (pm != SOL_OIC_PAIR_JUST_WORKS) {
        SOL_WRN("Pairing method(%d) not supported. Use SOL_OIC_PAIR_JUST_WORKS",
            pm);
        return -EINVAL;
    }

    return security->transfer_method->pair_request(security, resource,
        paired_cb, data);
#else
    return -EINVAL;
#endif
}

bool
sol_oic_set_token_and_mid(struct sol_coap_packet *pkt, int64_t *token)
{
    static struct sol_random *random = NULL;
    int32_t mid;

    if (SOL_UNLIKELY(!random)) {
        random = sol_random_new(SOL_RANDOM_DEFAULT, 0);
        SOL_NULL_CHECK(random, false);
    }

    if (!sol_random_get_int64(random, token)) {
        SOL_WRN("Could not generate CoAP token");
        return false;
    }
    if (!sol_random_get_int32(random, &mid)) {
        SOL_WRN("Could not generate CoAP message id");
        return false;
    }

    if (!sol_coap_header_set_token(pkt, (uint8_t *)token, (uint8_t)sizeof(*token))) {
        SOL_WRN("Could not set CoAP packet token");
        return false;
    }

    sol_coap_header_set_id(pkt, (int16_t)mid);

    return true;
}

static unsigned int
as_nibble(const char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    SOL_WRN("Invalid hex character: %d", c);
    return 0;
}

const uint8_t *
get_machine_id(void)
{
    static uint8_t machine_id[MACHINE_ID_LEN] = { 0 };
    static bool machine_id_set = false;
    const char *machine_id_buf;

    if (SOL_UNLIKELY(!machine_id_set)) {
        machine_id_buf = sol_platform_get_machine_id();
        if (!machine_id_buf) {
            SOL_WRN("Could not get machine ID");
            memset(machine_id, 0xFF, sizeof(machine_id));
        } else {
            const char *p;
            size_t i;

            for (p = machine_id_buf, i = 0; i < MACHINE_ID_LEN; i++, p += 2)
                machine_id[i] = as_nibble(*p) << 4 | as_nibble(*(p + 1));
        }

        machine_id_set = true;
    }

    return machine_id;
}

bool
sol_oic_pkt_has_same_token(const struct sol_coap_packet *pkt, int64_t token)
{
    uint8_t *token_data, token_len;

    token_data = sol_coap_header_get_token(pkt, &token_len);
    if (SOL_UNLIKELY(!token_data))
        return false;

    if (SOL_UNLIKELY(token_len != sizeof(token)))
        return false;

    return SOL_LIKELY(memcmp(token_data, &token, sizeof(token)) == 0);
}
