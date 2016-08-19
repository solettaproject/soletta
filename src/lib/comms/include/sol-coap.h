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

#include <sol-socket.h>
#include <sol-network.h>
#include <sol-str-slice.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to handle CoAP protocol.
 *
 * The Constrained Application Protocol (CoAP) is a
 * specialized web transfer protocol for use with constrained
 * nodes and constrained (e.g., low-power, lossy) networks.
 */

/**
 * @defgroup CoAP CoAP
 * @ingroup Comms
 *
 * @brief API to handle Constrained Application Protocol (CoAP) protocol.
 *
 * The Constrained Application Protocol (CoAP) is a
 * specialized web transfer protocol for use with constrained
 * nodes and constrained (e.g., low-power, lossy) networks.
 * The nodes often have 8-bit microcontrollers with small
 * amounts of ROM and RAM, while constrained networks such as
 * IPv6 over Low-Power Wireless Personal Area Networks
 * (6LoWPANs) often have high packet error rates and a
 * typical throughput of 10s of kbit/s.  The protocol is
 * designed for machine-to-machine (M2M) applications such
 * as smart energy and building automation.
 *
 * CoAP provides a request/response interaction model between
 * application endpoints, supports built-in discovery of
 * services and resources, and includes key concepts of the
 * Web such as URIs and Internet media types. CoAP is
 * designed to easily interface with HTTP for integration
 * with the Web while meeting specialized requirements such
 * as multicast support, very low overhead, and simplicity
 * for constrained environments.
 *
 * Relevant RFCs:
 * - https://tools.ietf.org/html/rfc7252: The Constrained Application
 *   Protocol (CoAP)
 * - https://tools.ietf.org/html/rfc7641: Observing Resources in the
 *   Constrained Application Protocol (CoAP)
 *
 * @{
 */

/**
 * @brief Set of CoAP packet options we are aware of.
 *
 * Users may add options other than these to their packets, provided
 * they know how to format them correctly. The only restriction is
 * that all options must be added to a packet in numeric order.
 *
 * @see sol_coap_find_first_option()
 *
 * Refer to RFC 7252, section 12.2 for more information.
 */
enum sol_coap_option {
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
};

/**
 * @brief Available request methods.
 *
 * To be used with sol_coap_header_set_code() when crafting a request.
 */
enum sol_coap_method {
    /** @brief A GET request. */
    SOL_COAP_METHOD_GET = 1,
    /** @brief A POST request. */
    SOL_COAP_METHOD_POST = 2,
    /** @brief A PUT request. */
    SOL_COAP_METHOD_PUT = 3,
    /** @brief A DELETE request. */
    SOL_COAP_METHOD_DELETE = 4,
};

/**
 * @brief Mask of CoAP requests
 *
 * It may be used to identify, given a code, if it's a request or
 * a response.
 *
 * Example to identify a request:
 * @code
 * struct sol_coap_packet *req = X;
 * if (!(sol_coap_header_get_code(req) & ~SOL_COAP_REQUEST_MASK))
 *     {
 *         // do something
 *     }
 *@endcode
 */
#define SOL_COAP_REQUEST_MASK 0x07

/**
 * @brief CoAP packets may be of one of these types.
 */
enum sol_coap_message_type {
    /**
     * Confirmable messsage.
     *
     * Packet is a request or response that the destination end-point must
     * acknowledge. If received and processed properly, it will receive a
     * response of matching @c id and type #SOL_COAP_MESSAGE_TYPE_ACK.
     * If the recipient could not process the request, it will reply with a
     * matching @c id and type #SOL_COAP_MESSAGE_TYPE_RESET.
     */
    SOL_COAP_MESSAGE_TYPE_CON = 0,
    /**
     * Non-confirmable message.
     *
     * Packet is a request or response that does not need acknowledge.
     * Destinataries should not reply to them with an ACK, but may respond
     * with a message of type #SOL_COAP_MESSAGE_TYPE_RESET if the package could not
     * be processed due to being faulty.
     */
    SOL_COAP_MESSAGE_TYPE_NON_CON = 1,
    /**
     * Acknowledge.
     *
     * When a confirmable message is received, a response should be sent to
     * the source with the same @c id and this type.
     */
    SOL_COAP_MESSAGE_TYPE_ACK = 2,
    /**
     * Reset.
     *
     * Rejecting a packet for any reason is done by sending a message of this
     * type with the @c id of the corresponding source message.
     */
    SOL_COAP_MESSAGE_TYPE_RESET = 3
};

/**
 * @brief Macro to create a response code.
 *
 * CoAP message code field is a 8-bit unsigned integer,
 * composed by 3 most significants bits representing class (0-7)
 * and 5 bits representing detail (00-31).
 *
 * It's usually represented as "c.dd". E.g.: 2.00 for "OK".
 */
#define sol_coap_make_response_code(clas, det) ((clas << 5) | (det))

/**
 * @brief Set of response codes available for a response packet.
 *
 * To be used with sol_coap_header_set_code() when crafting a reply.
 */
enum sol_coap_response_code {
    SOL_COAP_RESPONSE_CODE_OK = sol_coap_make_response_code(2, 0),
    SOL_COAP_RESPONSE_CODE_CREATED = sol_coap_make_response_code(2, 1),
    SOL_COAP_RESPONSE_CODE_DELETED = sol_coap_make_response_code(2, 2),
    SOL_COAP_RESPONSE_CODE_VALID = sol_coap_make_response_code(2, 3),
    SOL_COAP_RESPONSE_CODE_CHANGED = sol_coap_make_response_code(2, 4),
    SOL_COAP_RESPONSE_CODE_CONTENT = sol_coap_make_response_code(2, 5),
    SOL_COAP_RESPONSE_CODE_BAD_REQUEST = sol_coap_make_response_code(4, 0),
    SOL_COAP_RESPONSE_CODE_UNAUTHORIZED = sol_coap_make_response_code(4, 1),
    SOL_COAP_RESPONSE_CODE_BAD_OPTION = sol_coap_make_response_code(4, 2),
    SOL_COAP_RESPONSE_CODE_FORBIDDEN = sol_coap_make_response_code(4, 3),
    SOL_COAP_RESPONSE_CODE_NOT_FOUND = sol_coap_make_response_code(4, 4),
    SOL_COAP_RESPONSE_CODE_NOT_ALLOWED = sol_coap_make_response_code(4, 5),
    SOL_COAP_RESPONSE_CODE_NOT_ACCEPTABLE = sol_coap_make_response_code(4, 6),
    SOL_COAP_RESPONSE_CODE_PRECONDITION_FAILED = sol_coap_make_response_code(4, 12),
    SOL_COAP_RESPONSE_CODE_REQUEST_TOO_LARGE = sol_coap_make_response_code(4, 13),
    SOL_COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT = sol_coap_make_response_code(4, 15),
    SOL_COAP_RESPONSE_CODE_INTERNAL_ERROR = sol_coap_make_response_code(5, 0),
    SOL_COAP_RESPONSE_CODE_NOT_IMPLEMENTED = sol_coap_make_response_code(5, 1),
    SOL_COAP_RESPONSE_CODE_BAD_GATEWAY = sol_coap_make_response_code(5, 2),
    SOL_COAP_RESPONSE_CODE_SERVICE_UNAVAILABLE = sol_coap_make_response_code(5, 3),
    SOL_COAP_RESPONSE_CODE_GATEWAY_TIMEOUT = sol_coap_make_response_code(5, 4),
    SOL_COAP_RESPONSE_CODE_PROXYING_NOT_SUPPORTED = sol_coap_make_response_code(5, 5)
};

/**
 * @brief Macro to indicates that the header code was not set.
 *
 * To be used with sol_coap_header_set_code()
 */
#define SOL_COAP_CODE_EMPTY (0)

/**
 * @brief Some content-types available for use with the CONTENT_FORMAT option.
 *
 * Refer to RFC 7252, section 12.3 for more information.
 */
enum sol_coap_content_type {
    SOL_COAP_CONTENT_TYPE_NONE = -1,
    SOL_COAP_CONTENT_TYPE_TEXT_PLAIN = 0,
    SOL_COAP_CONTENT_TYPE_APPLICATION_LINK_FORMAT = 40,
    SOL_COAP_CONTENT_TYPE_APPLICATION_CBOR = 60, /* RFC7049 */
    SOL_COAP_CONTENT_TYPE_APPLICATION_JSON = 50,
};

/**
 * @brief Flags accepted by a #sol_coap_resource.
 */
enum sol_coap_flags {
    SOL_COAP_FLAGS_NONE       = 0,
    /** If the resource should be exported in the CoRE well-known registry. */
    SOL_COAP_FLAGS_WELL_KNOWN = (1 << 1)
};

/**
 * @typedef sol_coap_packet
 *
 * @brief Opaque handler for a CoAP packet.
 */
struct sol_coap_packet;
typedef struct sol_coap_packet sol_coap_packet;

/**
 * @typedef sol_coap_server
 *
 * @brief Opaque handler for a CoAP server.
 */
struct sol_coap_server;
typedef struct sol_coap_server sol_coap_server;

/**
 * @brief Description for a CoAP resource.
 *
 * CoAP servers will want to register resources, so that clients can act on
 * them, by fetching their state or requesting updates to them. These resources
 * are registered using this struct and the sol_coap_server_register_resource()
 * function.
 */
typedef struct sol_coap_resource {
#ifndef SOL_NO_API_VERSION
#define SOL_COAP_RESOURCE_API_VERSION (1)
    /** @brief API version */
    uint16_t api_version;
#endif
    /**
     * @brief GET request.
     *
     * @param data User data pointer provided to sol_coap_server_register_resource().
     * @param server The server through which the request was made.
     * @param resource The resource the request mas made on.
     * @param req Packet containing the request data. It's not safe to keep a
     *            reference to it after this function returns.
     * @param cliaddr The source address of the request.
     *
     * @return 0 on success, -errno on failure.
     */
    int (*get)(void *data, struct sol_coap_server *server,
        const struct sol_coap_resource *resource,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr);
    /**
     * @brief POST request.
     *
     * @param data User data pointer provided to sol_coap_server_register_resource().
     * @param server The server through which the request was made.
     * @param resource The resource the request mas made on.
     * @param req Packet containing the request data. It's not safe to keep a
     *            reference to it after this function returns.
     * @param cliaddr The source address of the request.
     *
     * @return 0 on success, -errno on failure.
     */
    int (*post)(void *data, struct sol_coap_server *server,
        const struct sol_coap_resource *resource,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr);
    /**
     * @brief PUT request.
     *
     * @param data User data pointer provided to sol_coap_server_register_resource().
     * @param server The server through which the request was made.
     * @param resource The resource the request mas made on.
     * @param req Packet containing the request data. It's not safe to keep a
     *            reference to it after this function returns.
     * @param cliaddr The source address of the request.
     *
     * @return 0 on success, -errno on failure.
     */
    int (*put)(void *data, struct sol_coap_server *server,
        const struct sol_coap_resource *resource,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr);
    /**
     * @brief DELETE request.
     *
     * @param data User data pointer provided to sol_coap_server_register_resource().
     * @param server The server through which the request was made.
     * @param resource The resource the request mas made on.
     * @param req Packet containing the request data. It's not safe to keep a
     *            reference to it after this function returns.
     * @param cliaddr The source address of the request.
     *
     * @return 0 on success, -errno on failure.
     */
    int (*del)(void *data, struct sol_coap_server *server,
        const struct sol_coap_resource *resource,
        struct sol_coap_packet *req,
        const struct sol_network_link_addr *cliaddr);
    /**
     * @brief Bitwise OR-ed flags from #sol_coap_flags, if any is necessary.
     */
    enum sol_coap_flags flags;
    /**
     * @brief Path representing the resource.
     *
     * An array of #sol_str_slice, each being a component of the path without
     * any separators. Last slice should be empty.
     */
    struct sol_str_slice path[];
} sol_coap_resource;

/**
 * @brief Gets the CoAP protocol version of the packet.
 *
 * @param pkt The packet to get version from.
 * @param version The CoAP protocol version the packet implements.
 *
 * @return 0 on success, negative number on error.
 */
int sol_coap_header_get_version(const struct sol_coap_packet *pkt, uint8_t *version);

/**
 * @brief Gets the type of the message container in the packet.
 *
 * @param pkt The packet containing the message.
 * @param type The type of the message, one of #sol_coap_message_type.
 *
 * @return 0 on success, negative number on error.
 */
int sol_coap_header_get_type(const struct sol_coap_packet *pkt, uint8_t *type);

/**
 * @brief Gets the token of the packet, if any.
 *
 * If the packet contains a token, this function can retrieve it.
 *
 * @param pkt The packet with the token.
 * @param len Pointer where to store the length in bytes of the token.
 *
 * @return @c NULL in case of error (when @c errno will be set to @c
 * EINVAL), otherwise a pointer to an internal buffer containint the
 * token. It must not be modified.
 */
uint8_t *sol_coap_header_get_token(const struct sol_coap_packet *pkt, uint8_t *len);

/**
 * @brief Gets the request/response code in the packet.
 *
 * If the packet is a request, the code returned is one of #sol_coap_method.
 * If it's a response, it will be one of #sol_coap_response_code.
 * If the code was not set, the returned code will be #SOL_COAP_CODE_EMPTY.
 *
 * @param pkt The packet to get the code from.
 * @param code The request/response code.
 *
 * @return 0 on success, negative number on error.
 */
int sol_coap_header_get_code(const struct sol_coap_packet *pkt, uint8_t *code);

/**
 * @brief Gets the message ID.
 *
 * @param pkt The packet from which to get the message ID.
 * @param id The message ID.
 *
 * @return 0 on success, negative number on error.
 */
int sol_coap_header_get_id(const struct sol_coap_packet *pkt, uint16_t *id);

/**
 * @brief Sets the CoAP protocol version in the packet's header.
 *
 * Usually not needed, as packets created with sol_coap_packet_new() will
 * already have the version set.
 *
 * @param pkt The packet to set the version.
 * @param ver The version to set.
 *
 * @return 0 on success, negative error code on failure.
 */
int sol_coap_header_set_version(struct sol_coap_packet *pkt, uint8_t ver);

/**
 * @brief Sets the message type in the packet's header.
 *
 * Must be one of #sol_coap_message_type.
 *
 * @param pkt The packet to set the type to.
 * @param type The message type to set.
 *
 * @return 0 on success, negative error code on failure.
 */
int sol_coap_header_set_type(struct sol_coap_packet *pkt, uint8_t type);

/**
 * @brief Sets a token in the packet.
 *
 * Tokens can be used, besides the ID, to identify a specific request. For
 * @c OBSERVE requests, the server will send notifications with the same
 * @a token used to make the request.
 *
 * @param pkt The packet on which to set the token.
 * @param token The token to set, can be any array of bytes.
 * @param tokenlen The length in bytes of @a token.
 *
 * @return 0 on success, negative error code on failure.
 *
 * @warning This function is meant to be used only @b once per packet,
 * more than that will lead to undefined behavior
 */
int sol_coap_header_set_token(struct sol_coap_packet *pkt, uint8_t *token, uint8_t tokenlen);

/**
 * @brief Sets the code of the message.
 *
 * For requests, it must be one of #sol_coap_method.
 * For responses, it must be one of #sol_coap_response_code.
 *
 * @param pkt The packet on which to set the code.
 * @param code The request/response code.
 *
 * @return 0 on success, negative error code on failure.
 */
int sol_coap_header_set_code(struct sol_coap_packet *pkt, uint8_t code);

/**
 * @brief Sets the message ID.
 *
 * CoAP packets require an ID to identify a request, so that their response
 * can be matched by it.
 * It's not usually necessary to call this function, as packets created by
 * sol_coap_packet_new() will have generated an ID already,
 *
 * @param pkt The packet on which to set the ID.
 * @param id The ID to set.
 *
 * @return 0 on success, negative error code on failure.
 */
int sol_coap_header_set_id(struct sol_coap_packet *pkt, uint16_t id);

/**
 * @brief Check if a given CoAP server instance is running over DTLS or not.
 *
 * @param server The server to be checked.
 *
 * @return True if this server uses DTLS to encrypt communication with its
 * endpoints; false otherwise.
 *
 */
bool sol_coap_server_is_secure(const struct sol_coap_server *server);

/**
 * @brief Creates a new CoAP server instance.
 *
 * Creates a new, CoAP server instance listening on address
 * @a addr. If the server cannot be created, NULL will be returned and
 * @c errno will be set to indicate the reason.
 *
 * @param addr The address where the server will listen on.
 * @param secure Set to @c true to create a server that will encrypt communication with its
 * endpoints using DTLS or @c false otherwise.
 *
 * @note If @c secure is @c true, this function will create a @c sol_coap_server
 * over a DTLS Socket supporting @b only the #SOL_SOCKET_DTLS_CIPHER_PSK_AES128_CCM8
 * Cipher Suite. The recommended function for creating secure @c sol_coap_server is
 * @c sol_coap_server_new_by_cipher_suites.
 *
 * @return A new server instance, or NULL in case of failure.
 *
 * @see sol_coap_server_new_by_cipher_suites
 *
 */
struct sol_coap_server *sol_coap_server_new(const struct sol_network_link_addr *addr, bool secure);

/**
 * @brief Creates a new DTLS-Secured CoAP server instance.
 *
 * Creates a new, DTLS-Secured CoAP server instance listening on address
 * @a addr. If the server cannot be created, NULL will be returned and
 * @c errno will be set to indicate the reason.
 *
 * @param addr The address where the server will listen on.
 * @param cipher_suites Indicates the desired cipher suites to use.
 * @param cipher_suites_len Indicates the length of the @c cipher_suites array.
 *
 * @return A new server instance, or NULL in case of failure.
 *
 */
struct sol_coap_server *sol_coap_server_new_by_cipher_suites(
    const struct sol_network_link_addr *addr,
    enum sol_socket_dtls_cipher *cipher_suites,
    uint16_t cipher_suites_len);

/**
 * @brief Take a reference of the given server.
 *
 * Increment the reference count of the server, if it's still valid.
 *
 * @param server The server to reference.
 *
 * @return The same server, with refcount increased, or NULL if invalid.
 */
struct sol_coap_server *sol_coap_server_ref(struct sol_coap_server *server);

/**
 * @brief Release a reference from the given server.
 *
 * When the last reference is released, the server and all resources associated
 * with it will be freed.
 *
 * @param server The server to release.
 */
void sol_coap_server_unref(struct sol_coap_server *server);

/**
 * @brief Creates a new CoAP packet.
 *
 * Creates a packet to send as a request or response.
 * If @a old is not @c NULL, its @c ID and @c token (if any) will be copied to
 * the new packet. This is useful when crafting a new packet as a response
 * to @a old. It's also important to note that if @a old has #sol_coap_message_type
 * equal to #SOL_COAP_MESSAGE_TYPE_CON, the new packet message type will be set to
 * #SOL_COAP_MESSAGE_TYPE_ACK. If it has it equal to #SOL_COAP_MESSAGE_TYPE_NON_CON,
 * the new packet message type will be set to #SOL_COAP_MESSAGE_TYPE_NON_CON.
 *
 * @param old An optional packet to use as basis.
 *
 * @return A new packet, or @c NULL on failure.
 */
struct sol_coap_packet *sol_coap_packet_new(struct sol_coap_packet *old);

/**
 * @brief Convenience function to create a new packet for a request.
 *
 * Creates a new packet, automatically setting a new @c id, and setting the
 * request method to @a method and message type to @a type.
 *
 * @param method The method to use for the request.
 * @param type The message type.
 *
 * @return A new packet, with @c id, @c method and @c type already set.
 */
struct sol_coap_packet *sol_coap_packet_new_request(enum sol_coap_method method, enum sol_coap_message_type type);

/**
 * @brief Convenience function to create a packet suitable to send as a notification.
 *
 * When notifying observing clients of changes in a resource, this function
 * simplifies the creation of the notification packet by handling the management
 * of the resource age (and setting the id) and type of the packet.
 *
 * It should be used along sol_coap_notify() to ensure that
 * the correct token is added to the packet sent to the clients.
 *
 * @param server The server holding the resource that changed.
 * @param resource The resource the notification is about.
 *
 * @return A new packet, with @c id and @c type set accordingly, or NULL on failure.
 *
 * @see sol_coap_notify()
 */
struct sol_coap_packet *sol_coap_packet_new_notification(struct sol_coap_server *server,
    struct sol_coap_resource *resource);

/**
 * @brief Take a reference of the given packet.
 *
 * Increment the reference count of the packet, if it's still valid.
 *
 * @param pkt The packet to reference.
 *
 * @return The same packet, with refcount increased, or @c NULL if invalid.
 */
struct sol_coap_packet *sol_coap_packet_ref(struct sol_coap_packet *pkt);

/**
 * @brief Release a reference from the given packet.
 *
 * When the last reference is released, the packet will be freed.
 *
 * @param pkt The packet to release.
 */
void sol_coap_packet_unref(struct sol_coap_packet *pkt);

/**
 * @brief Gets a pointer to the packet's payload.
 *
 * When creating a packet, first set all the packet header parameters
 * and options, then use this function to get a pointer to the
 * packet's payload buffer. One may then append content to the
 * payload buffer (all @c sol-buffer appending functions will do).
 * Note that @b only @b appending is permitted, otherwise the packet
 * headers/options -- that are also payload -- will get corrupted.
 *
 * Getting the payload pointer marks the end of the header and options in the
 * packet, so after that point, it's no longer possible to set any of those.
 *
 * When receiving a packet, first check if it contains a payload with
 * sol_coap_packet_has_payload(), and if so, this function will return
 * in @a buf the packet's buffer and in @a offset, where in that
 * buffer the user's payload actually begins.
 *
 * @param pkt The packet to fetch the payload of.
 * @param buf Where to store the address of the payload buffer.
 * @param offset Where to store the offset, in @a buf, where the
 * actual payload starts.
 *
 * @return 0 on success, a negative error code on failure.
 */
int sol_coap_packet_get_payload(struct sol_coap_packet *pkt, struct sol_buffer **buf, size_t *offset);

/**
 * @brief Checks if the given packet contains a payload.
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
 * @brief Adds an option to the CoAP packet.
 *
 * Options must be added in order, according to their numeric definitions.
 *
 * @param pkt The packet to which the option will be added.
 * @param code The option code, one of #sol_coap_option.
 * @param value Pointer to the value of the option, or NULL if none.
 * @param len Length in bytes of @a value, or 0 if @a value is NULL.
 *
 * @return 0 on success, -errno on failure.
 */
int sol_coap_add_option(struct sol_coap_packet *pkt, uint16_t code, const void *value, uint16_t len);

/**
 * @brief Convenience function to add the the #SOL_COAP_OPTION_URI_PATH from a string.
 *
 * @param pkt The packet to the add the path to.
 * @param uri The path to add, must start with '/'.
 *
 * @return 0 on success, -errno on failure.
 */
int sol_coap_packet_add_uri_path_option(struct sol_coap_packet *pkt, const char *uri);

/**
 * @brief Finds the first apeearence of the specified option in a packet.
 *
 * @param pkt The packet holding the options.
 * @param code The option code to look for. Pass one of the
 * #sol_coap_option values or any custom option code.
 * @param len The length in bytes of the option's value.
 *
 * @return Pointer to the option's value, or NULL if not found.
 */
const void *sol_coap_find_first_option(const struct sol_coap_packet *pkt, uint16_t code, uint16_t *len);

/**
 * @brief Gets a number of specified option in a packet.
 *
 * @param pkt The packet holding the options.
 * @param code The option code to look for. Pass one of the
 * #sol_coap_option values or any custom option code.
 * @param vec An vector of struct sol_str_slice to hold the options.
 * @param veclen The length of @a vec.
 *
 * @return The number of options found, negative errno otherwise.
 */
int sol_coap_find_options(const struct sol_coap_packet *pkt, uint16_t code, struct sol_str_slice *vec, uint16_t veclen);

/**
 * @brief Sends a packet to the given address.
 *
 * Sends the packet @a pkt to the destination address especified by @a cliaddr,
 * which may be a multicast address for discovery purposes.
 *
 * If a response is expected, then sol_coap_send_packet_with_reply() should
 * be used instead.
 *
 * @note This function will take the reference of the given @a pkt and do a
 * release its memory even on errors.
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
 * @brief Sends a packet to the given address.
 *
 * Sends the packet @a pkt to the destination address especified by @a
 * cliaddr, which may be a multicast address for discovery purposes,
 * and issues a given callback when a response arrives.
 *
 * If @a reply_cb is @c NULL, this function will behave exactly like
 * sol_coap_send_packet(). If a valid function is passed, when a
 * response is received, the function @a reply_cb will be called. As
 * long as this function returns @c true, @a server will continue
 * waiting for more responses. When it returns @c false, the internal
 * response handler will be freed and any new reply that may arrive
 * for this request will be ignored. For unobserving packets, @a
 * server will also be notified using an unobserve packet.
 *
 * After an internal timeout is reached, @a reply_cb will be called
 * with @c NULL @c req and @c cliaddr. The same behavior is expected
 * for its return: if @c true, @a server will issue a new timeout and
 * continue waiting responses until it ends, otherwise @a server will
 * terminate response waiting.
 *
 * @warning If @a pkt has the #SOL_COAP_OPTION_OBSERVE option and at
 * least one response arrives before the internal timeout and @a
 * reply_cb returns @c true, that will be interpreted as if the user
 * wishes to wait for responses indefinetely and no timeout will apply
 * anymore. The user is then responsible for cancelling the request
 * with sol_coap_cancel_send_packet().
 *
 * @note This function will take the reference of the given @a pkt.
 *
 * @param server The server through which the packet will be sent.
 * @param pkt The packet to send.
 * @param cliaddr The recipient address.
 * @param reply_cb The function to call when a response is received.
 * @param data The user data pointer to pass to the @a reply_cb function.
 *
 * @return 0 on success, a negative error code otherwise.
 */
int sol_coap_send_packet_with_reply(struct sol_coap_server *server, struct sol_coap_packet *pkt,
    const struct sol_network_link_addr *cliaddr,
    bool (*reply_cb)(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr), const void *data);

/**
 * @brief Sends the notification packet to all registered observers.
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
 * @note This function will take the reference of the given @a pkt and do a
 * release its memory even on errors.
 *
 * @see sol_coap_send_packet()
 * @see sol_coap_send_packet_with_reply()
 */
int sol_coap_notify(struct sol_coap_server *server,
    struct sol_coap_resource *resource, struct sol_coap_packet *pkt);

/**
 * @brief Sends the notification packet returned by a callback
 * to all registered observers.
 *
 * Sends the notification @c pkt to all the registered observers for the
 * resource @a resource, setting the correct token for each instance. The @c pkt
 * will be filled by a callback @c cb given to this function, probably based on the
 * observer's address and/or any other used data.
 *
 * @param server The server through which the notifications will be sent.
 * @param resource The resource the notification is about.
 * @param cb A callback that is used to create the @c pkt - @c data User data; @c server The server through which the notifications will be sent; @c resource The resource the notification is about; @c addr The address of the observer; @c pkt The pkt to be filled.
 * @param cb_data The user data to @c cb.
 *
 * @return 0 on success, -errno on failure.
 *
 * @see sol_coap_notify()
 * @see sol_coap_send_packet()
 * @see sol_coap_send_packet_with_reply()
 */
int sol_coap_notify_by_callback(struct sol_coap_server *server,
    struct sol_coap_resource *resource,
    int (*cb)(void *cb_data, struct sol_coap_server *server,
    struct sol_coap_resource *resource, struct sol_network_link_addr *addr,
    struct sol_coap_packet **pkt),
    const void *cb_data);

/**
 * @brief Register a #sol_coap_resource with the server.
 *
 * Registers the given resource with the server, in order to be able to
 * handle requests related to it made by clients.
 * The same resource can be registered on multiple servers.
 *
 * @param server The server on which the resource wil be registered.
 * @param resource The resource to register.
 * @param data User data pointer that will be passed to the requests callbacks.
 *
 * @return 0 on success, negative @c errno on error.
 */
int sol_coap_server_register_resource(struct sol_coap_server *server,
    const struct sol_coap_resource *resource, const void *data);

/**
 * @brief Unregisters a resource from the server.
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
 * @brief Inserts a path given in slices form to a @c sol-buffer, at a
 * given offset.
 *
 * Takes a path, as respresented by #sol_coap_resource and inserts all
 * its components, in order, separated by '/', to @a buf's @a offset
 * position.
 *
 * @param path Array of slices, last one empty, representing the path.
 * @param buf Buffer where to append the string version of the path.
 * @param offset Where to insert the paths string into @a buf.
 * @param size Pointer where to store the number of bytes written to @a buf
 *        If @c NULL, it will be ignored.
 *
 * @return 0 on success, negative error code on failure
 *
 * @warning @a buf has be initialized before this call and must be
 * free/finished afterwards
 */
int sol_coap_path_to_buffer(const struct sol_str_slice path[],
    struct sol_buffer *buf, size_t offset, size_t *size);

/**
 * @brief Cancel a packet sent using sol_coap_send_packet() or
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
 * @brief Remove observation identified by @a token from server.
 *
 * Send to server an unobserve packet so client identified by @a token will be
 * removed from the server's observation list. We are suppose to stop receiving
 * new notifications.
 *
 * If observation was added using sol_coap_send_packet_with_reply() function
 * we will have no more calls to reply_cb.
 *
 * @param server The server through which the observation packet was sent.
 * @param cliaddr The source address of the observation packet sent.
 * @param token The observation token
 * @param tkl The observation token length
 *
 * @return 0 on success.
 *         -ENOENT if there is no observe packet registered with @a token and
 *         no unobserve packet was sent.
 *         -ENOMEM Out of memory
 *         -EINVAL if some parameter is invalid.
 */
int sol_coap_unobserve_by_token(struct sol_coap_server *server, const struct sol_network_link_addr *cliaddr, uint8_t *token, uint8_t tkl);


/**
 * @brief Register a unknown handler callback.
 *
 * Every time the @a server receives a request for a #sol_coap_resource that was not
 * registered with sol_coap_server_register_resource() the @a handler will be called.
 *
 * @param server The server to register the unknown resource handler.
 * @param handler The unknown handler callback or @c NULL to unregister.
 * @param data The data to @a handler
 * @return 0 on success
 *         -EINVAL if server is @c NULL.
 */
int sol_coap_server_set_unknown_resource_handler(struct sol_coap_server *server,
    int (*handler)(void *data, struct sol_coap_server *server,
    struct sol_coap_packet *req, const struct sol_network_link_addr *cliaddr),
    const void *data);

/**
 * @brief Print information about the packet @a pkt.
 *
 * Used for debug purposes.
 *
 * @param pkt The packet to be debuged.
 */
#ifdef SOL_LOG_ENABLED
void sol_coap_packet_debug(struct sol_coap_packet *pkt);
#else
static inline void
sol_coap_packet_debug(struct sol_coap_packet *pkt)
{
}
#endif


/**
 * @}
 */

#ifdef __cplusplus
}
#endif
