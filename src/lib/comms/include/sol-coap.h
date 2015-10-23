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

#pragma once

#include <sol-network.h>
#include <sol-str-slice.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to handle CoAP protocol.
 * The Constrained Application Protocol (CoAP) is a
 * specialized web transfer protocol for use with constrained
 * nodes and constrained (e.g., low-power, lossy) networks.
 */

/**
 * @defgroup CoAP CoAP
 * @ingroup Comms
 *
 * The Constrained Application Protocol (CoAP) is a
 * specialized web transfer protocol for use with constrained
 * nodes and constrained (e.g., low-power, lossy) networks.
 * The nodes often have 8-bit microcontrollers with small
 * amounts of ROM and RAM, while constrained networks such as
 * IPv6 over Low-Power Wireless Personal Area Networks
 * (6LoWPANs) often have high packet error rates and a
 * typical throughput of 10s of kbit/s.  The protocol is
 * designed for machine- to-machine (M2M) applications such
 * as smart energy and building automation.
 *
 * CoAP provides a request/response interaction model between
 * application endpoints, supports built-in discovery of
 * services and resources, and includes key concepts of the
 * Web such as URIs and Internet media types.  CoAP is
 * designed to easily interface with HTTP for integration
 * with the Web while meeting specialized requirements such
 * as multicast support, very low overhead, and simplicity
 * for constrained environments.
 *
 * @{
 */

typedef enum {
    SOL_COAP_OPTION_IF_MATCH = 1,
    SOL_COAP_OPTION_URI_HOST = 3,
    SOL_COAP_OPTION_ETAG = 4,
    SOL_COAP_OPTION_IF_NONE_MATCH = 5,
    SOL_COAP_OPTION_OBSERVE = 6,
    SOL_COAP_OPTION_URI_PORT = 7,
    SOL_COAP_OPTION_LOCATION_PATH = 8,
    SOL_COAP_OPTION_URI_PATH = 11,
    SOL_COAP_OPTION_CONTENT_FORMAT = 12,
    SOL_COAP_OPTION_MAX_AGE = 14,
    SOL_COAP_OPTION_URI_QUERY = 15,
    SOL_COAP_OPTION_ACCEPT = 17,
    SOL_COAP_OPTION_LOCATION_QUERY = 20,
    SOL_COAP_OPTION_PROXY_URI = 35,
    SOL_COAP_OPTION_PROXY_SCHEME = 39
} sol_coap_option_num_t;

typedef enum {
    SOL_COAP_METHOD_GET = 1,
    SOL_COAP_METHOD_POST = 2,
    SOL_COAP_METHOD_PUT = 3,
    SOL_COAP_METHOD_DELETE = 4,
} sol_coap_method_t;

#define SOL_COAP_REQUEST_MASK 0x07

typedef enum {
    SOL_COAP_TYPE_CON = 0,
    SOL_COAP_TYPE_NONCON = 1,
    SOL_COAP_TYPE_ACK = 2,
    SOL_COAP_TYPE_RESET = 3
} sol_coap_msgtype_t;

#define MAKE_RSPCODE(clas, det) ((clas << 5) | (det))
typedef enum {
    SOL_COAP_RSPCODE_OK = MAKE_RSPCODE(2, 0),
    SOL_COAP_RSPCODE_CREATED = MAKE_RSPCODE(2, 1),
    SOL_COAP_RSPCODE_DELETED = MAKE_RSPCODE(2, 2),
    SOL_COAP_RSPCODE_VALID = MAKE_RSPCODE(2, 3),
    SOL_COAP_RSPCODE_CHANGED = MAKE_RSPCODE(2, 4),
    SOL_COAP_RSPCODE_CONTENT = MAKE_RSPCODE(2, 5),
    SOL_COAP_RSPCODE_BAD_REQUEST = MAKE_RSPCODE(4, 0),
    SOL_COAP_RSPCODE_UNAUTHORIZED = MAKE_RSPCODE(4, 1),
    SOL_COAP_RSPCODE_BAD_OPTION = MAKE_RSPCODE(4, 2),
    SOL_COAP_RSPCODE_FORBIDDEN = MAKE_RSPCODE(4, 3),
    SOL_COAP_RSPCODE_NOT_FOUND = MAKE_RSPCODE(4, 4),
    SOL_COAP_RSPCODE_NOT_ALLOWED = MAKE_RSPCODE(4, 5),
    SOL_COAP_RSPCODE_NOT_ACCEPTABLE = MAKE_RSPCODE(4, 6),
    SOL_COAP_RSPCODE_PRECONDITION_FAILED = MAKE_RSPCODE(4, 12),
    SOL_COAP_RSPCODE_REQUEST_TOO_LARGE = MAKE_RSPCODE(4, 13),
    SOL_COAP_RSPCODE_INTERNAL_ERROR = MAKE_RSPCODE(5, 0),
    SOL_COAP_RSPCODE_NOT_IMPLEMENTED = MAKE_RSPCODE(5, 1),
    SOL_COAP_RSPCODE_BAD_GATEWAY = MAKE_RSPCODE(5, 2),
    SOL_COAP_RSPCODE_SERVICE_UNAVAILABLE = MAKE_RSPCODE(5, 3),
    SOL_COAP_RSPCODE_GATEWAY_TIMEOUT = MAKE_RSPCODE(5, 4),
    SOL_COAP_RSPCODE_PROXYING_NOT_SUPPORTED = MAKE_RSPCODE(5, 5)
} sol_coap_responsecode_t;

typedef enum {
    SOL_COAP_CONTENTTYPE_NONE = -1,
    SOL_COAP_CONTENTTYPE_TEXT_PLAIN = 0,
    SOL_COAP_CONTENTTYPE_APPLICATION_LINKFORMAT = 40,
    SOL_COAP_CONTENTTYPE_APPLICATION_CBOR = 60, /* RFC7049 */
    SOL_COAP_CONTENTTYPE_APPLICATION_JSON = 50,
} sol_coap_content_type_t;

enum sol_coap_flags {
    SOL_COAP_FLAGS_NONE       = 0,
    /* If the resource should be exported in the CoRE well-known registry. */
    SOL_COAP_FLAGS_WELL_KNOWN = (1 << 1)
};

struct sol_coap_packet;

struct sol_coap_server;

struct sol_coap_resource {
#define SOL_COAP_RESOURCE_API_VERSION (1)
    uint16_t api_version;
    uint16_t reserved; /* save this hole for a future field */
    /*
     * handlers for the CoAP defined methods.
     */
    int (*get)(const struct sol_coap_resource *resource,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr, void *data);
    int (*post)(const struct sol_coap_resource *resource,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr, void *data);
    int (*put)(const struct sol_coap_resource *resource,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr, void *data);
    int (*delete)(const struct sol_coap_resource *resource,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr, void *data);
    enum sol_coap_flags flags;
    struct sol_str_slice iface;
    struct sol_str_slice resource_type;
    struct sol_str_slice path[];
};

uint8_t sol_coap_header_get_ver(const struct sol_coap_packet *pkt);
uint8_t sol_coap_header_get_type(const struct sol_coap_packet *pkt);
uint8_t *sol_coap_header_get_token(const struct sol_coap_packet *pkt, uint8_t *len);
uint8_t sol_coap_header_get_code(const struct sol_coap_packet *pkt);
uint16_t sol_coap_header_get_id(const struct sol_coap_packet *pkt);
void sol_coap_header_set_ver(struct sol_coap_packet *pkt, uint8_t ver);
void sol_coap_header_set_type(struct sol_coap_packet *pkt, uint8_t type);
bool sol_coap_header_set_token(struct sol_coap_packet *pkt, uint8_t *token, uint8_t tokenlen);
void sol_coap_header_set_code(struct sol_coap_packet *pkt, uint8_t code);
void sol_coap_header_set_id(struct sol_coap_packet *pkt, uint16_t id);

struct sol_coap_server *sol_coap_server_new(int port);
struct sol_coap_server *sol_coap_secure_server_new(int port);
struct sol_coap_server *sol_coap_server_ref(struct sol_coap_server *server);
void sol_coap_server_unref(struct sol_coap_server *server);

/* Creates a new packet using the old as basis, copying 'token' and 'id'.  */
struct sol_coap_packet *sol_coap_packet_new(struct sol_coap_packet *old);

/* Creates a new request, adding a proper 'id' value. */
struct sol_coap_packet *sol_coap_packet_request_new(sol_coap_method_t method, sol_coap_msgtype_t type);

/* Creates a notification packet. The 'token' and 'id' will be dealt internally. */
struct sol_coap_packet *sol_coap_packet_notification_new(struct sol_coap_server *server,
    struct sol_coap_resource *resource);

struct sol_coap_packet *sol_coap_packet_ref(struct sol_coap_packet *pkt);
void sol_coap_packet_unref(struct sol_coap_packet *pkt);

int sol_coap_packet_get_payload(struct sol_coap_packet *pkt, uint8_t **buf, uint16_t *len);
int sol_coap_packet_set_payload_used(struct sol_coap_packet *pkt, uint16_t len);
bool sol_coap_packet_has_payload(struct sol_coap_packet *pkt);

int sol_coap_add_option(struct sol_coap_packet *pkt, uint16_t code, const void *value, uint16_t len);
int sol_coap_packet_add_uri_path_option(struct sol_coap_packet *pkt, const char *uri);

const void *sol_coap_find_first_option(const struct sol_coap_packet *pkt, uint16_t code, uint16_t *len);

int sol_coap_send_packet(struct sol_coap_server *server, struct sol_coap_packet *pkt,
    const struct sol_network_link_addr *cliaddr);

int sol_coap_send_packet_with_reply(struct sol_coap_server *server, struct sol_coap_packet *pkt,
    const struct sol_network_link_addr *cliaddr,
    int (*reply_cb)(struct sol_coap_packet *req,
    const struct sol_network_link_addr *cliaddr, void *data),
    void *data);

int sol_coap_packet_send_notification(struct sol_coap_server *server,
    struct sol_coap_resource *resource, struct sol_coap_packet *pkt);

bool sol_coap_server_register_resource(struct sol_coap_server *server,
    const struct sol_coap_resource *resource, void *data);
int sol_coap_server_unregister_resource(struct sol_coap_server *server,
    const struct sol_coap_resource *resource);

int sol_coap_uri_path_to_buf(const struct sol_str_slice path[],
    uint8_t *buf, size_t buflen);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
