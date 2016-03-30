/*
 * This file is part of the Soletta Project
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

#include <sol-common-buildopts.h>
#include <sol-network.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup Socket Socket
 * @ingroup Comms
 *
 * @brief API to create an endpoint communication using sockets.
 *
 * @{
 */

struct sol_socket;

/**
 * @brief The defaults' socket types.
 */
enum sol_socket_default_type {
    SOL_SOCKET_UDP,
#ifdef SOL_DTLS_ENABLED
    SOL_SOCKET_DTLS,
#endif
};

/**
 * @brief Available socket options
 */
enum sol_socket_option {
    SOL_SOCKET_OPTION_REUSEADDR,
    SOL_SOCKET_OPTION_REUSEPORT
};

/**
 * @brief The socket option level
 */
enum sol_socket_level {
    SOL_SOCKET_LEVEL_SOCKET,
    SOL_SOCKET_LEVEL_IP,
    SOL_SOCKET_LEVEL_IPV6,
};

/**
 * @brief Structure to represent a socket class.
 *
 * This struct contains the necessary information do deal with
 * a socket. This contains the methods necessaries to create
 * a new type of socket.
 */
struct sol_socket_type {
#ifndef SOL_NO_API_VERSION
#define SOL_SOCKET_TYPE_API_VERSION (1)  /**< compile time API version to be checked during runtime */
    /**
     * must match #SOL_SOCKET_TYPE_API_VERSION at runtime.
     */
    uint16_t api_version;
#endif

    /**
     * @brief Function to be called when the socket is deleted.
     */
    void (*del)(struct sol_socket *s);

    /**
     * @brief Register a function to be called when the socket has data to
     * be read.
     */
    int (*set_on_read)(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s),
        const void *data);

    /**
     * @brief Register a function to be called when the socket is ready to
     * be written.
     */
    int (*set_on_write)(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s),
        const void *data);

    /**
     * @brief Function to be called to read data from socket.
     */
    ssize_t (*recvmsg)(struct sol_socket *s, void *buf, size_t len,
        struct sol_network_link_addr *cliaddr);

    /**
     * @brief Function to be called to write data in the socket.
     */
    int (*sendmsg)(struct sol_socket *s, const void *buf, size_t len,
        const struct sol_network_link_addr *cliaddr);

    /**
     * @brief Function to be called to join a multicast group.
     */
    int (*join_group)(struct sol_socket *s, int ifindex,
        const struct sol_network_link_addr *group);

    /**
     * @brief Function to be called to bind the socket to a network address.
     */
    int (*bind)(struct sol_socket *s, const struct sol_network_link_addr *addr);

    /**
     * @brief Function to be called to set a option in the socket.
     */
    int (*setsockopt)(struct sol_socket *s, enum sol_socket_level level,
        enum sol_socket_option optname, const void *optval, size_t optlen);

    /**
     * @brief Function to be called to get a socket option.
     */
    int (*getsockopt)(struct sol_socket *s, enum sol_socket_level level,
        enum sol_socket_option optname, void *optval, size_t *optlen);
};

#ifndef SOL_NO_API_VERSION
/**
 * @brief Macro used to check if a struct @ref sol_socket_type
 * has the expected API version.
 *
 * In case it's a wrong version, it'll return extra arguments passed
 * to the macro.
 */
#define SOL_SOCKET_TYPE_CHECK_API_VERSION(type_, ...) \
    if (SOL_UNLIKELY((type_)->api_version != \
        SOL_SOCKET_TYPE_API_VERSION)) { \
        SOL_ERR("Unexpected API version (response is %u, expected %u)", \
            (type_)->api_version, SOL_SOCKET_TYPE_API_VERSION); \
        return __VA_ARGS__; \
    }
#else
#define SOL_SOCKET_TYPE_CHECK_API_VERSION(type_, ...)
#endif

/**
 * @brief Structure to represent a socket.
 *
 * @see sol_socket_type
 */
struct sol_socket {
#ifndef SOL_NO_API_VERSION
#define SOL_SOCKET_API_VERSION (1)  /**< compile time API version to be checked during runtime */
    /**
     * must match #SOL_SOCKET_API_VERSION at runtime.
     */
    uint16_t api_version;
#endif
    const struct sol_socket_type *impl;
};

#ifndef SOL_NO_API_VERSION
/**
 * @brief Macro used to check if a struct @ref sol_socket
 * has the expected API version.
 *
 * In case it's a wrong version, it'll return extra arguments passed
 * to the macro.
 */
#define SOL_SOCKET_CHECK_API_VERSION(sock_, ...) \
    if (SOL_UNLIKELY((sock_)->api_version != \
        SOL_SOCKET_API_VERSION)) { \
        SOL_ERR("Unexpected API version (response is %u, expected %u)", \
            (sock_)->api_version, SOL_SOCKET_API_VERSION); \
        return __VA_ARGS__; \
    }
#else
#define SOL_SOCKET_CHECK_API_VERSION(sock_, ...)
#endif

/**
 * @brief Creates an endpoint for communication.
 *
 * This function creates a socket using the system default implementation.
 *
 * @param domain A communication domain.
 * @param type The socket's type.
 * @param protocol Specifies a particular protocol to be used.
 *
 * @return a handle to the socket on success, otherwise @c NULL is returned.
 *
 * @see sol_socket_del
 * @see sol_socket_type
 */
struct sol_socket *sol_socket_new(int domain, enum sol_socket_default_type type, int protocol);

/**
 * @brief Destroy the @a socket instance.
 *
 * Destroy and release all the socket resources
 *
 * @param s The value got with @c sol_socket_new
 *
 * @see sol_socket_new
 */
void sol_socket_del(struct sol_socket *s);

/**
 * @brief Adds a function to be called when the socket had data
 * to be read.
 *
 * @param s The value got with @c sol_socket_new.
 * @param cb The function to call when the socket has data.
 * @param data The user data pointer to pass to the function.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_socket_new
 */
int sol_socket_set_on_read(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), const void *data);

/**
 * @brief Adds a function to be called when the socket is able
 * to send data.
 *
 * @param s The value got with @c sol_socket_new.
 * @param cb The function to call when it is possible to write
 * in the socket.
 * @param data The user data pointer to pass to the function.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_socket_new
 */
int sol_socket_set_on_write(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), const void *data);

/**
 * @brief Receive a message from a socket.
 *
 * If the socket's type is SOL_SOCKET_UDP, @a buf may be @c NULL, and
 * in this case the function will only peek the incoming packet queue
 * (not removing data from it), returning the number of bytes needed
 * to store the next datagram and ignoring the cliaddr argument. This
 * way, the user may allocate the exact number of bytes to hold the
 * message contents.
 *
 * @param s The value got with @c sol_socket_new.
 * @param buf The data buffer that will be used to receive the data.
 * @param len The size of @a buf
 * @param cliaddr The address of who sent data.
 *
 * @return The number of bytes read in case of success, error code
 * (always negative) otherwise.
 *
 * @see sol_socket_sendmsg
 */
ssize_t sol_socket_recvmsg(struct sol_socket *s, void *buf, size_t len, struct sol_network_link_addr *cliaddr);

/**
 * @brief Transmits a message using the socket.
 *
 * @param s The value got with @c sol_socket_new.
 * @param buf The data to be transmitted.
 * @param len The size of @a buf
 * @param cliaddr The address which the data will be sent.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_socket_recvmsg
 */
int sol_socket_sendmsg(struct sol_socket *s, const void *buf, size_t len,
    const struct sol_network_link_addr *cliaddr);

/**
 * @brief Joins a multicast group.
 *
 * @param s The value got with @c sol_socket_new.
 * @param ifindex The index of the interface to be used.
 * @param group The address of the group to join.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_socket_bind
 * @see sol_network_link_addr
 */
int sol_socket_join_group(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group);

/**
 * @brief Binds the socket to a specific address.
 *
 * Assigns the  address specified by @a addr to the socket
 * referred to @a s
 *
 * @param s The value got with @c sol_socket_new
 * @param addr The address to associate.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_socket_join_group
 * @see sol_network_link_addr
 */
int sol_socket_bind(struct sol_socket *s, const struct sol_network_link_addr *addr);

/**
 * @brief Set options for the socket.
 *
 * @param s The value got with @c sol_socket_new.
 * @param level The level at which the option resides.
 * @param optname The option name.
 * @param optval The option value data.
 * @param optlen The size of the option.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_socket_getsockopt
 */
int sol_socket_setsockopt(struct sol_socket *s, enum sol_socket_level level, enum sol_socket_option optname,
    const void *optval, size_t optlen);

/**
 * @brief Get a socket option value.
 *
 * @param s The value got with @c sol_socket_new.
 * @param level The level at which the option resides.
 * @param optname The option name.
 * @param optval The value of the option.
 * @param optlen The size of the option.
 *
 * @return @c 0 on success, error code (always negative) otherwise.
 *
 * @see sol_socket_setsockopt
 */
int sol_socket_getsockopt(struct sol_socket *s, enum sol_socket_level level, enum sol_socket_option optname,
    void *optval, size_t *optlen);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
