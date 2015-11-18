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

/**
 * Set of CoAP packet options we are aware of.
 *
 * Users may add options other than these to their packets, provided they
 * know how to format them correctly. The only restriction is that all options
 * must be added to a packet in numeric order.
 *
 * Refer to RFC 7252, section 12.2 for more information.
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

/**
 * Available request methods.
 *
 * To be used with sol_coap_header_set_code() when crafting a request.
 */
typedef enum {
    SOL_COAP_METHOD_GET = 1,
    SOL_COAP_METHOD_POST = 2,
    SOL_COAP_METHOD_PUT = 3,
    SOL_COAP_METHOD_DELETE = 4,
} sol_coap_method_t;

#define SOL_COAP_REQUEST_MASK 0x07

/**
 * CoAP packets may be of one of these types.
 */
typedef enum {
    /**
     * Confirmable messsage.
     *
     * Packet is a request or response that the destination end-point must
     * acknowledge. If received and processed properly, it will receive a
     * response of matching @c id and type #SOL_COAP_TYPE_ACK.
     * If the recipient could not process the request, it will reply with a
     * matching @c id and type #SOL_COAP_TYPE_RESET.
     */
    SOL_COAP_TYPE_CON = 0,
    /**
     * Non-confirmable message.
     *
     * Packet is a request or response that does not need acknowledge.
     * Destinataries should not reply to them with an ACK, but may respond
     * with a message of type #SOL_COAP_TYPE_RESET if the package could not
     * be processed due to being faulty.
     */
    SOL_COAP_TYPE_NONCON = 1,
    /**
     * Acknowledge.
     *
     * When a confirmable message is received, a response should be sent to
     * the source with the same @c id and this type.
     */
    SOL_COAP_TYPE_ACK = 2,
    /**
     * Reset.
     *
     * Rejecting a packet for any reason is done by sending a message of this
     * type with the @c id of the corresponding source message.
     */
    SOL_COAP_TYPE_RESET = 3
} sol_coap_msgtype_t;

#define MAKE_RSPCODE(clas, det) ((clas << 5) | (det))

/**
 * Set of response codes available for a response packet.
 *
 * To be used with sol_coap_header_set_code() when crafting a reply.
 */
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

/**
 * Some content-types available for use with the CONTENT_FORMAT option.
 *
 * Refer to RFC 7252, section 12.3 for more information.
 */
typedef enum {
    SOL_COAP_CONTENTTYPE_NONE = -1,
    SOL_COAP_CONTENTTYPE_TEXT_PLAIN = 0,
    SOL_COAP_CONTENTTYPE_APPLICATION_LINKFORMAT = 40,
    SOL_COAP_CONTENTTYPE_APPLICATION_CBOR = 60, /* RFC7049 */
    SOL_COAP_CONTENTTYPE_APPLICATION_JSON = 50,
} sol_coap_content_type_t;

/**
 * Flags accepted by a #sol_coap_resource.
 */
enum sol_coap_flags {
    SOL_COAP_FLAGS_NONE       = 0,
    /** If the resource should be exported in the CoRE well-known registry. */
    SOL_COAP_FLAGS_WELL_KNOWN = (1 << 1)
};

/**
 * Opaque handler for a CoAP packet.
 */
struct sol_coap_packet;

/**
 * Opaque handler for a CoAP server.
 */
struct sol_coap_server;

/**
 * Description for a CoAP resource.
 *
 * CoAP servers will want to register resources, so that clients can act on
 * them, by fetching their state or requesting updates to them. These resources
 * are registered using this struct and the sol_coap_server_register_resource()
 * function.
 */
struct sol_coap_resource {
#ifndef SOL_NO_API_VERSION
#define SOL_COAP_RESOURCE_API_VERSION (1)
    uint16_t api_version;
    uint16_t reserved; /* save this hole for a future field */
#endif
    /**
     * GET request.
     *
     * @param server The server through which the request was made.
     * @param resource The resource the request mas made on.
     * @param req Packet containing the request data. It's not safe to keep a
     *            reference to it after this function returns.
     * @param cliaddr The source address of the request.
     * @param data User data pointer provided to sol_coap_server_register_resource().
     *
     * @return 0 on success, -errno on failure.
     */
    int (*get)(struct sol_coap_server *server,
        const struct sol_coap_resource *resource,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr, void *data);
    /**
     * POST request.
     *
     * @param server The server through which the request was made.
     * @param resource The resource the request mas made on.
     * @param req Packet containing the request data. It's not safe to keep a
     *            reference to it after this function returns.
     * @param cliaddr The source address of the request.
     * @param data User data pointer provided to sol_coap_server_register_resource().
     *
     * @return 0 on success, -errno on failure.
     */
    int (*post)(struct sol_coap_server *server,
        const struct sol_coap_resource *resource,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr, void *data);
    /**
     * PUT request.
     *
     * @param server The server through which the request was made.
     * @param resource The resource the request mas made on.
     * @param req Packet containing the request data. It's not safe to keep a
     *            reference to it after this function returns.
     * @param cliaddr The source address of the request.
     * @param data User data pointer provided to sol_coap_server_register_resource().
     *
     * @return 0 on success, -errno on failure.
     */
    int (*put)(struct sol_coap_server *server,
        const struct sol_coap_resource *resource,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr, void *data);
    /**
     * DELETE request.
     *
     * @param server The server through which the request was made.
     * @param resource The resource the request mas made on.
     * @param req Packet containing the request data. It's not safe to keep a
     *            reference to it after this function returns.
     * @param cliaddr The source address of the request.
     * @param data User data pointer provided to sol_coap_server_register_resource().
     *
     * @return 0 on success, -errno on failure.
     */
    int (*delete)(struct sol_coap_server *server,
        const struct sol_coap_resource *resource,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr, void *data);
    /**
     * Bitwise OR-ed flags from #sol_coap_flags, if any is necessary.
     */
    enum sol_coap_flags flags;
    struct sol_str_slice iface;
    struct sol_str_slice resource_type;
    /**
     * Path representing the resource.
     *
     * An array of #sol_str_slice, each being a component of the path without
     * any separators. Last slice should be empty.
     */
    struct sol_str_slice path[];
};

/**
 * Gets the CoAP protocol version of the packet.
 *
 * @param pkt The packet to get version from.
 *
 * @return The CoAP protocol version the packet implements.
 */
uint8_t sol_coap_header_get_ver(const struct sol_coap_packet *pkt);

/**
 * Gets the type of the message container in the packet.
 *
 * @param pkt The packet containing the message.
 *
 * @return The type of the message, one of #sol_coap_msgtype_t.
 */
uint8_t sol_coap_header_get_type(const struct sol_coap_packet *pkt);

/**
 * Gets the token of the packet, if any.
 *
 * If the packet contains a token, this function can retrieve it.
 *
 * @param pkt The packet with the token.
 * @param len Pointer where to store the length in bytes of the token.
 *
 * @return Pointer to an internal buffer containint the token. It must not be modified.
 */
uint8_t *sol_coap_header_get_token(const struct sol_coap_packet *pkt, uint8_t *len);

/**
 * Gets the request/response code in the packet.
 *
 * If the packet is a request, the code returned is one of #sol_coap_method_t.
 * If it's a response, it will be one of #sol_coap_responsecode_t.
 *
 * @param pkt The packet to get the code from.
 *
 * @return The request/response code.
 */
uint8_t sol_coap_header_get_code(const struct sol_coap_packet *pkt);

/**
 * Gets the message ID.
 *
 * @param pkt The packet from which to get the message ID.
 *
 * @return The message ID.
 */
uint16_t sol_coap_header_get_id(const struct sol_coap_packet *pkt);

/**
 * Sets the CoAP protocol version in the packet's header.
 *
 * Usually not needed, as packets created with sol_coap_packet_new() will
 * already have the version set.
 *
 * @param pkt The packet to set the version.
 * @param ver The version to set.
 */
void sol_coap_header_set_ver(struct sol_coap_packet *pkt, uint8_t ver);

/**
 * Sets the message type in the packet's header.
 *
 * Must be one of #sol_coap_msgtype_t.
 *
 * @param pkt The packet to set the type to.
 * @param type The message type to set.
 */
void sol_coap_header_set_type(struct sol_coap_packet *pkt, uint8_t type);

/**
 * Sets a token in the packet.
 *
 * Tokens can be used, besides the id, to identify a specific request. For
 * @c OBSERVE requests, the server will send notifications with the same
 * @a token used to make the request.
 *
 * @param pkt The packet on which to set the token.
 * @param token The token to set, can be any array of bytes.
 * @param tokenlen The length in bytes of @a token.
 *
 * @return True on success, false if there's not enough space in the packet for the token.
 */
bool sol_coap_header_set_token(struct sol_coap_packet *pkt, uint8_t *token, uint8_t tokenlen);

/**
 * Sets the code of the message.
 *
 * For requests, it must be one of #sol_coap_method_t.
 * For responses, it must be one of #sol_coap_responsecode_t.
 *
 * @param pkt The packet on which to set the code.
 * @param code The request/response code.
 */
void sol_coap_header_set_code(struct sol_coap_packet *pkt, uint8_t code);

/**
 * Sets the message ID.
 *
 * CoAP packets require an ID to identify a request, so that their response
 * can be matched by it.
 * It's not usually necessary to call this function, as packets created by
 * sol_coap_packet_new() will have generated an ID already,
 *
 * @param pkt The packet on which to set the ID.
 * @param id The ID to set.
 */
void sol_coap_header_set_id(struct sol_coap_packet *pkt, uint16_t id);

/**
 * Creates a new CoAP server instance.
 *
 * Creates a new, unsecured, CoAP server instance listening on @a port. Clients
 * may use 0 for @a port to let the system pick an available one.
 * If the server cannot be created, NULL will be returned and errno will be
 * set to indicate the reason.
 *
 * @param port The port the server will listen on.
 *
 * @return A new server instance, or NULL in case of failure.
 *
 * @see sol_coap_secure_server_new()
 */
struct sol_coap_server *sol_coap_server_new(uint16_t port);

/**
 * Creates a new secure CoAP server instance.
 *
 * Creates a new CoAP server instance listening on @a port. Clients
 * may use 0 for @a port to let the system pick an available one.
 * This server will encrypt communication with its endpoints using DTLS.
 * If the server cannot be created, NULL will be returned and errno will be
 * set to indicate the reason.
 *
 * @param port The port the server will listen on.
 *
 * @return A new server instance, or NULL in case of failure.
 *
 * @see sol_coap_secure_server_new()
 */
struct sol_coap_server *sol_coap_secure_server_new(uint16_t port);

/**
 * Take a reference of the given server.
 *
 * Increment the reference count of the server, if it's still valid.
 *
 * @param server The server to reference.
 *
 * @return The same server, with refcount increased, or NULL if invalid.
 */
struct sol_coap_server *sol_coap_server_ref(struct sol_coap_server *server);

/**
 * Release a reference from the given server.
 *
 * When the last reference is released, the server and all resources associated
 * with it will be freed.
 *
 * @param server The server to release.
 */
void sol_coap_server_unref(struct sol_coap_server *server);

/**
 * Creates a new CoAP packet.
 *
 * Creates a packet to send as a request or response.
 * If @a old is not NULL, its @c id and @c token (if any) will be copied to
 * the new packet. This is useful when crafting a new packet as a response
 * to @a old.
 *
 * @param old An optional packet to use as basis.
 *
 * @return A new packet, or NULL on failure.
 */
struct sol_coap_packet *sol_coap_packet_new(struct sol_coap_packet *old);

/**
 * Convenience function to create a new packet for a request.
 *
 * Creates a new packet, automatically setting a new @c id, and setting the
 * request method to @a method and message type to @a type.
 *
 * @param method The method to use for the request.
 * @param type The message type.
 *
 * @return A new packet, with @c id, @c method and @c type already set.
 */
struct sol_coap_packet *sol_coap_packet_request_new(sol_coap_method_t method, sol_coap_msgtype_t type);

/**
 * Convenience function to create a packet suitable to send as a notification.
 *
 * When notifying observing clients of changes in a resource, this function
 * simplifies the creation of the notification packet by handling the management
 * of the resource age (and setting the id) and type of the packet.
 *
 * It should be used along sol_coap_packet_send_notification() to ensure that
 * the correct token is added to the packet sent to the clients.
 *
 * @param server The server holding the resource that changed.
 * @param resource The resource the notification is about.
 *
 * @return A new packet, with @c id and @c type set accordingly, or NULL on failure.
 *
 * @see sol_coap_packet_send_notification()
 */
struct sol_coap_packet *sol_coap_packet_notification_new(struct sol_coap_server *server,
    struct sol_coap_resource *resource);

struct sol_coap_packet *sol_coap_packet_ref(struct sol_coap_packet *pkt);
void sol_coap_packet_unref(struct sol_coap_packet *pkt);

/**
 * Gets a pointer to the packet's payload.
 *
 * When creating a packet, first set all the packet header parameters and options,
 * then use this function to get a pointer to the beginning of the available
 * space for the payload within the packet @a pkt. The pointer to the buffer
 * will be stored in @a buf, and the available size in @a len.
 * If there's no enough space left in the packet, -ENOMEM will be returned.
 * After calling this function and writing any contents to the payload,
 * the function sol_coap_packet_set_payload_used() should be called to inform
 * how much of the payload was used.
 *
 * Getting the payload pointer marks the end of the header and options in the
 * packet, so after that point, it's no longer possible to set any of those.
 *
 * When receiving a packet, first check if it contains a payload with
 * sol_coap_packet_has_payload(), and if so, this function will return in @a buf
 * the address within the packet @a pkt where the payload begins, with its
 * length in @a len.
 *
 * @param pkt The packet to fetch the payload of.
 * @param buf Where to store the address of the beginning of the payload.
 * @param len Where to store the length of the payload.
 *
 * @return 0 on success, -errno on failure.
 */
int sol_coap_packet_get_payload(struct sol_coap_packet *pkt, uint8_t **buf, uint16_t *len);

/**
 * Sets the amount of space used from the payload.
 *
 * This function makes no sense for packets received from other end points,
 * only when creating a packet locally to be sent to remote clients or servers.
 *
 * For each call to sol_coap_packet_get_payload(), there should be a matching
 * call to sol_coap_packet_set_payload_used(). After writing any content to
 * the payload retrieved before, this function will increase the total amount
 * of payload used by @a len.
 *
 * @param pkt The packet the payload used belongs to.
 * @param len How much of the retrieved payload was used.
 *
 * @return 0 on success, -errno on failure.
 */
int sol_coap_packet_set_payload_used(struct sol_coap_packet *pkt, uint16_t len);

/**
 * Checks if the given packet contains a payload.
 *
 * For packets received as a request or response, this function checks if
 * they contain a payload. It should be used before calling sol_coap_packet_get_payload().
 *
 * @param pkt The packet to check.
 *
 * @return True if the packet contains a payload, false otherwise.
 */
bool sol_coap_packet_has_payload(struct sol_coap_packet *pkt);

/**
 * Adds an option to the CoAP packet.
 *
 * Options must be added in order, according to their numeric definitions.
 *
 * @param pkt The packet to which the option will be added.
 * @param code The option code, one of #sol_coap_option_num_t.
 * @param value Pointer to the value of the option, or NULL if none.
 * @param len Length in bytes of @a value, or 0 if @a value is NULL.
 *
 * @return 0 on success, -errno on failure.
 */
int sol_coap_add_option(struct sol_coap_packet *pkt, uint16_t code, const void *value, uint16_t len);

/**
 * Convenience function to add the the #SOL_COAP_OPTION_URI_PATH from a string.
 *
 * @param pkt The packet to the add the path to.
 * @param uri The path to add, must start with '/'.
 *
 * @return 0 on success, -errno on failure.
 */
int sol_coap_packet_add_uri_path_option(struct sol_coap_packet *pkt, const char *uri);

/**
 * Finds the first apeearence of the specified option in a packet.
 *
 * @param pkt The packet holding the options.
 * @param code The option code to look for.
 * @param len The length in bytes of the option's value.
 *
 * @return Pointer to the option's value, or NULL if not found.
 */
const void *sol_coap_find_first_option(const struct sol_coap_packet *pkt, uint16_t code, uint16_t *len);

/**
 * Sends a packet to the given address.
 *
 * Sends the packet @a pkt to the destination address especified by @a cliaddr,
 * which may be a multicast address for discovery purposes.
 *
 * If a response is expected, then sol_coap_send_packet_with_reply() should
 * be used instead.
 *
 * @note This function will take the reference of the given @a pkt.
 *
 * @param server The server through which the packet will be sent.
 * @param pkt The packet to send.
 * @param cliaddr The recipient address.
 *
 * @return 0 on success, -errno otherwise.
 *
 * @see sol_coap_send_packet_with_reply()
 */
int sol_coap_send_packet(struct sol_coap_server *server, struct sol_coap_packet *pkt,
    const struct sol_network_link_addr *cliaddr);

/**
 * Sends a packet to the given address.
 *
 * Sends the packet @a pkt to the destination address especified by @a cliaddr,
 * which may be a multicast address for discovery purposes.
 *
 * When a response is received, the function @a reply_cb will be called.
 * As long as this function returns @c true, @a server will continue waiting
 * for more responses. When the function returns @c false, the internal response
 * handler will be freed and any new replies that arrive for this request
 * will be ignored. For unobserving packets server will also be notified using
 * an unobserve packet.
 * After internal timeout is reached reply_cb will be called with @c NULL
 * req and cliaddr. The same behavior is expected for reply_cb return, if
 * reply_cb returns @c true, @a server will continue waiting responses until
 * next timeout. If reply_cb returns @c false, @a server will terminate
 * response waiting.
 *
 * @note This function will take the reference of the given @a pkt.
 *
 * @param server The server through which the packet will be sent.
 * @param pkt The packet to send.
 * @param cliaddr The recipient address.
 * @param reply_cb The function to call when a response is received.
 * @param data The user data pointer to pass to the @a reply_cb function.
 *
 * @return 0 on success, -errno otherwise.
 */
int sol_coap_send_packet_with_reply(struct sol_coap_server *server, struct sol_coap_packet *pkt,
    const struct sol_network_link_addr *cliaddr,
    bool (*reply_cb)(struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr,
    void *data), void *data);

/**
 * Sends the notification packet to all registered observers.
 *
 * Sends the notification @a pkt to all the registered observers for the
 * resource @a resource, setting the correct token for each instance.
 *
 * @param server The server through which the notifications will be sent.
 * @param resource The resource the notification is about.
 * @param pkt The notification packet to send.
 *
 * @return 0 on success, -errno on failure.
 *
 * @see sol_coap_send_packet()
 * @see sol_coap_send_packet_with_reply()
 */
int sol_coap_packet_send_notification(struct sol_coap_server *server,
    struct sol_coap_resource *resource, struct sol_coap_packet *pkt);

/**
 * Register a #sol_coap_resource with the server.
 *
 * Registers the given resource with the server, in order to be able to
 * handle requests related to it made by clients.
 * The same resource can be registered on multiple servers.
 *
 * @param server The server on which the resource wil be registered.
 * @param resource The resource to register.
 * @param data User data pointer that will be passed to the requests callbacks.
 *
 * @return True on success, false on failure.
 */
bool sol_coap_server_register_resource(struct sol_coap_server *server,
    const struct sol_coap_resource *resource, void *data);

/**
 * Unregisters a resource from the server.
 *
 * Unregisters a resource previously registered with sol_coap_server_register_resource().
 * This specific server will no longer handle requests made to the given resource,
 * but if its registered in other servers, it may still be found by clients.
 *
 * @param server The server from which to unregister the resource.
 * @param resource The resource to unregister.
 *
 * @return 0 on success, -ENOENT if the resource was no registered to this server.
 *         -EINVAL if some parameter is invalid.
 */
int sol_coap_server_unregister_resource(struct sol_coap_server *server,
    const struct sol_coap_resource *resource);

/**
 * Converts a path from slices to a string.
 *
 * Takes a path, as respresented by #sol_coap_resource and writes all its
 * components, separated by '/' to @a buf.
 *
 * @param path Array of slices, last one empty, representing the path.
 * @param buf Buffer where to store the string version of the path.
 * @param buflen Size of the buffer.
 * @param size Pointer where to store the number of bytes written to @a buf
 *        If NULL, it will be ignored.
 *
 * @return 0 on success
 *         -EOVERFLOW if buflen is smaller than the number of bytes needed to
 *         store all strings from @a path in @a buf.
 *         -EINVAL if some parameter is invalid.
 */
int sol_coap_uri_path_to_buf(const struct sol_str_slice path[],
    uint8_t *buf, size_t buflen, size_t *size);

/*
 * Cancel a packet sent using sol_coap_send_packet() or
 * sol_coap_send_packet_with_reply() functions.
 * For observating packets, an unobserve packet will be sent to server and
 * no more replies will be processed.
 *
 * @param server The server through which the packet was sent.
 * @param pkt The packet sent.
 * @param cliaddr The source address of the sent packet.
 *
 * @return 0 on success
 *         -ENOENT if the packet was already canceled
 *         -EINVAL if some parameter is invalid.
 */
int sol_coap_cancel_send_packet(struct sol_coap_server *server, struct sol_coap_packet *pkt, struct sol_network_link_addr *cliaddr);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
