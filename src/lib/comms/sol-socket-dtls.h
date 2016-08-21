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

#pragma once

#include <stdbool.h>

#include "sol-buffer.h"
#include "sol-network.h"
#include "sol-socket.h"
#include "sol-str-slice.h"

#define SOL_DTLS_PSK_ID_LEN 16
#define SOL_DTLS_PSK_KEY_LEN 16

#define SOL_DTLS_ECDSA_PRIV_KEY_LEN 32
#define SOL_DTLS_ECDSA_PUB_KEY_X_LEN 32
#define SOL_DTLS_ECDSA_PUB_KEY_Y_LEN 32

struct sol_socket_dtls_credential_cb {
    const void *data;

    int (*init)(const void *data);
    void (*clear)(void *creds);

    ssize_t (*get_id)(const void *creds, struct sol_network_link_addr *addr,
        char *id, size_t id_len);
    ssize_t (*get_psk)(const void *creds, struct sol_str_slice id,
        char *psk, size_t psk_len);

    int (*get_ecdsa_priv_key)(const void *creds, struct sol_network_link_addr *addr,
        unsigned char *ecdsa_priv_key);
    int (*get_ecdsa_pub_key_x)(const void *creds, struct sol_network_link_addr *addr,
        unsigned char *ecdsa_pub_key_x);
    int (*get_ecdsa_pub_key_y)(const void *creds, struct sol_network_link_addr *addr,
        unsigned char *ecdsa_pub_key_y);
    int (*verify_ecdsa_key)(const void *creds, struct sol_network_link_addr *addr,
        const unsigned char *other_pub_x, const unsigned char *other_pub_y,
        size_t key_size);
};

struct sol_socket *sol_socket_dtls_wrap_socket(struct sol_socket *socket);

int sol_socket_dtls_set_handshake_cipher(struct sol_socket *s,
    enum sol_socket_dtls_cipher cipher);

int sol_socket_dtls_set_anon_ecdh_enabled(struct sol_socket *s, bool setting);

int sol_socket_dtls_prf_keyblock(struct sol_socket *s,
    const struct sol_network_link_addr *addr, struct sol_str_slice label,
    struct sol_str_slice random1, struct sol_str_slice random2,
    struct sol_buffer *buffer);

int sol_socket_dtls_set_credentials_callbacks(struct sol_socket *s,
    const struct sol_socket_dtls_credential_cb *cb);
