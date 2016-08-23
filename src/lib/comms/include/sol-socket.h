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

#include <sol-network.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup Socket Socket
 * @ingroup Comms
 *
 * @brief API to create a communication endpoint using sockets.
 *
 * @{
 */

/**
 * @typedef sol_socket
 *
 * @brief Opaque handler for a socket
 */
struct sol_socket;
typedef struct sol_socket sol_socket;

/**
 * @brief Represents supported Cipher Suites for use with DTLS.
 */
enum sol_socket_dtls_cipher {
    SOL_SOCKET_DTLS_CIPHER_ECDH_ANON_AES128_CBC_SHA256,
    SOL_SOCKET_DTLS_CIPHER_PSK_AES128_CCM8,
    SOL_SOCKET_DTLS_CIPHER_ECDHE_ECDSA_AES128_CCM8,
};

/**
 * @brief Defines the behaviour of a socket instance
 */
typedef struct sol_socket_options {
#ifndef SOL_NO_API_VERSION
#define SOL_SOCKET_OPTIONS_API_VERSION (1)  /**< compile time API version to be checked during runtime */
    /**
     * must match #SOL_SOCKET_OPTIONS_API_VERSION at runtime.
     */
    uint16_t api_version;
    /**
     * To version each subclass
     */
    uint16_t sub_api;
#endif

    /**
     * @brief Register a function to be called when the socket has data to
     * be read.
     *
     * @li @c data the user's data given in this options.
     * @li @c s the socket that has data to be read.
     * @li returns @c true to keep being called, @c false otherwise.
     */
    bool (*on_can_read)(void *data, struct sol_socket *s);

    /**
     * @brief Register a function to be called when the socket is ready to
     * be written.
     *
     * @li @c data the user's data given in this options.
     * @li @c s the socket that is able to be written.
     * @li returns @c true to keep being called, @c false otherwise.
     */
    bool (*on_can_write)(void *data, struct sol_socket *s);

    /**
     * @brief User data data will be given in on_can_read() and
     * on_can_write()
     */
    const void *data;
} sol_socket_options;

/**
 * @brief Defines specific IP layer related behaviour of a socket
 * instance
 */
typedef struct sol_socket_ip_options {
    struct sol_socket_options base;
#ifndef SOL_NO_API_VERSION
#define SOL_SOCKET_IP_OPTIONS_SUB_API_VERSION (1)  /**< compile time API version to be checked during runtime */
#endif

    /**
     * @brief The family that should be used when creating the socket.
     * @see sol_network_family
     */
    enum sol_network_family family;

    /**
     * @brief If the socket's data should be encrypted or not.
     */
    bool secure;

    /**
     * @brief Allows multiple sockets to be bound to the same
     * socket address. It is used by sol_socket_bind()
     */
    bool reuse_port;

    /**
     * @brief Allow reuse of local addresses. It is used by
     * sol_socket_bind()
     */
    bool reuse_addr;

    /**
     * @brief If @c secure is true, this should be considered.
     * It indicates which DTLS cipher suites are supported and could
     * be used for communication.
     */
    enum sol_socket_dtls_cipher *cipher_suites;

    /**
     * @brief If @c secure is true, this should be considered.
     * It indicates the length of the @c cipher_suites array.
     */
    uint16_t cipher_suites_len;
} sol_socket_ip_options;

/**
 * @brief Structure to represent a socket class.
 *
 * This struct contains the necessary information do deal with a
 * socket. This contains the methods necessary to create a new socket
 * type.
 */
typedef struct sol_socket_type {
#ifndef SOL_NO_API_VERSION
#define SOL_SOCKET_TYPE_API_VERSION (1)  /**< compile time API version to be checked during runtime */
    /**
     * must match #SOL_SOCKET_TYPE_API_VERSION at runtime.
     */
    uint16_t api_version;
#endif

    /**
     * @brief Function to be called when the socket is deleted.
     *
     * @li @c s the socket pointer (this)
     */
    void (*del)(struct sol_socket *s);

    /**
     * @brief Starts or stops monitoring the socket for reading.
     *
     * @li @c s the socket pointer (this)
     * @li @c on @c true to be called when there is data to be read or
     *     @c false otherwise.
     * @li returns @c 0 in success, otherwise a negative (errno) value
     *     is returned.
     */
    int (*set_read_monitor)(struct sol_socket *s, bool on);

    /**
     * @brief Starts or stops monitoring the socket for writing.
     *
     * @li @c s the socket pointer (this)
     * @li @c on @c true to be called when there is possible to write or
     *     @c false otherwise.
     * @li returns @c 0 in success, otherwise a negative (errno) value
     *     is returned.
     */
    int (*set_write_monitor)(struct sol_socket *s, bool on);

    /**
     * @brief Function to be called to read data from socket.
     *
     * @li @c s the socket pointer (this)
     * @li @c buffer the data buffer that will be used to receive the data.
     *     If @a buffer does not has the flags SOL_BUFFER_FLAGS_FIXED_CAPACITY or
     *     SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED this functions will allocate/re-allocate
     *     the necessary memory. The @a buffer must be initialized.
     * @li @c cliaddr the source address of the message
     * @li returns @c the number of bytes received in success, otherwise a
     *     negative (errno) value is returned.
     *
     * @see sol_buffer_flags
     */
    ssize_t (*recvmsg)(struct sol_socket *s, struct sol_buffer *buffer,
        struct sol_network_link_addr *cliaddr);

    /**
     * @brief Function to be called to write data in the socket.
     *
     * @li @c s the socket pointer (this)
     * @li @c buffer the data to be transmitted
     * @li @c cliaddr the address which the data will be sent
     * @li returns @c the number of bytes written in success, otherwise a
     *     negative (errno) value is returned.
     */
    ssize_t (*sendmsg)(struct sol_socket *s, const struct sol_buffer *buffer,
        const struct sol_network_link_addr *cliaddr);

    /**
     * @brief Function to be called to join a multicast group.
     * Both IPv4 and IPv6 addresses are possible.
     *
     * @li @c s the socket pointer (this)
     * @li @c ifindex the index of the interface to be used.
     *     Interface index is available in @a sol_network_link.
     * @li @c group the address of the group to join.
     * @li returns @c 0 in success, otherwise a negative (errno) value
     *     is returned.
     */
    int (*join_group)(struct sol_socket *s, int ifindex,
        const struct sol_network_link_addr *group);

    /**
     * @brief Function to be called to bind the socket to a network address.
     *
     * @li @c s the socket pointer (this)
     * @li @c addr the address to bind.
     * @li returns @c 0 in success, otherwise a negative (errno) value
     *     is returned.
     */
    int (*bind)(struct sol_socket *s, const struct sol_network_link_addr *addr);
} sol_socket_type;

/**
 * @brief Structure to represent a socket.
 *
 * @see sol_socket_type
 */
typedef struct sol_socket {
    const struct sol_socket_type *type;
} sol_socket;

/**
 * @brief Creates an endpoint for communication.
 *
 * This function creates a socket using the system's default
 * implementation.
 *
 * @param options The socket's options.
 *
 * @return a handle to the socket on success, otherwise @c NULL is returned.
 *
 * @see sol_socket_del()
 * @see sol_socket_options
 */
struct sol_socket *sol_socket_ip_new(const struct sol_socket_options *options);

/**
 * @brief Destroy the @a socket instance.
 *
 * Destroy and release all the socket resources
 *
 * @param s The value got with @c sol_socket_ip_new()
 *
 * @see sol_socket_ip_new()
 */
void sol_socket_del(struct sol_socket *s);

/**
 * @brief Adds a function to be called when the socket had data
 * to be read.
 *
 * @param s The value got with @c sol_socket_ip_new()
 * @param on @c true to start to monitor the socket. When the socket is
 * available to read the callback set on @c sol_socket_options
 * will be called, if @c false it stops monitoring the socket to read.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_socket_ip_new()
 */
int sol_socket_set_read_monitor(struct sol_socket *s, bool on);

/**
 * @brief Adds a function to be called when the socket is able
 * to send data.
 *
 * @param s The value got with @c sol_socket_ip_new()
 * @param on @c true to start to monitor the socket. When the socket is
 * available to write the callback set on @c sol_socket_options
 * will be called, if @c false it stops monitoring the socket to write.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_socket_ip_new()
 */
int sol_socket_set_write_monitor(struct sol_socket *s, bool on);

/**
 * @brief Receive a message from a socket.
 *
 * If it's a datagram socket, @a buf may be @c NULL, and in this case
 * the function will only peek the incoming packet queue (not removing
 * data from it), returning the number of bytes needed to store the
 * next datagram and ignoring the cliaddr argument. This way, the user
 * may allocate the exact number of bytes to hold the message
 * contents.
 *
 * @param s The value got with @c sol_socket_ip_new()
 * @param buffer the data buffer that will be used to receive the data.
 * If @a buffer does not has the flags SOL_BUFFER_FLAGS_FIXED_CAPACITY or
 * SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED this functions will allocate/re-allocate.
 * The @a buffer must be initialized.
 * @param cliaddr The source address of the message.
 *
 * @return The number of bytes read in case of success, error code
 * (always negative) otherwise.
 *
 * @see sol_socket_sendmsg()
 * @see sol_buffer_flags
 */
ssize_t sol_socket_recvmsg(struct sol_socket *s, struct sol_buffer *buffer,
    struct sol_network_link_addr *cliaddr);

/**
 * @brief Transmits a message using the socket.
 *
 * @param s The value got with @c sol_socket_ip_new()
 * @param buffer The data to be transmitted.
 * @param cliaddr The address which the data will be sent.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_socket_recvmsg()
 */
ssize_t sol_socket_sendmsg(struct sol_socket *s, const struct sol_buffer *buffer,
    const struct sol_network_link_addr *cliaddr);

/**
 * @brief Joins a multicast group.
 * Both IPv4 and IPv6 addresses are possible.
 *
 * @param s The value got with @c sol_socket_ip_new()
 * @param ifindex The index of the interface to be used.
 * Interface index is available in @a sol_network_link.
 * @param group The address of the group to join.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_socket_bind()
 * @see sol_network_link_addr
 * @see sol_network_link
 */
int sol_socket_join_group(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group);

/**
 * @brief Binds the socket to a specific address.
 *
 * Assigns the  address specified by @a addr to the socket
 * referred to @a s
 *
 * @param s The value got with @c sol_socket_ip_new()
 * @param addr The address to associate.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_socket_join_group()
 * @see sol_network_link_addr
 */
int sol_socket_bind(struct sol_socket *s, const struct sol_network_link_addr *addr);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
