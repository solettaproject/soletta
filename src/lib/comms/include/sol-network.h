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

#include <stdbool.h>

#include <sol-common-buildopts.h>
#include <sol-vector.h>
#include <sol-str-slice.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These are routines that Soletta provides for handling network
 * link interfaces, making it possible to observe events,
 * to inquire available links and to set their states.
 */

/**
 * @defgroup Comms Communication Modules
 *
 * @brief Comms consists on a few communication modules.
 *
 * It provides ways to deal with network, CoAP protocol and
 * OIC protocol (server and client sides).
 *
 * @defgroup Network Network
 * @ingroup Comms
 *
 * @brief Network module provides a way to handle network link interfaces.
 *
 * It makes possible to observe events, to inquire available links
 * and to set their states.
 *
 * @{
 */

/**
 * @brief String size of an IPv4/v6 address.
 */
#define SOL_INET_ADDR_STRLEN 48


/**
 * @struct sol_network_hostname_handle
 *
 * @brief A handle to sol_network_get_hostname_address_info()
 *
 * This handle can be used to cancel get sol_network_get_hostname_address_info()
 * by calling sol_network_cancel_get_hostname_address_info()
 *
 * @see sol_network_get_hostname_address_info()
 * @see sol_network_cancel_get_hostname_address_info()
 */
struct sol_network_hostname_handle;

/**
 * @brief Type of events generated for a network link.
 *
 * @see sol_network_subscribe_events()
 */
enum sol_network_event {
    SOL_NETWORK_LINK_ADDED,
    SOL_NETWORK_LINK_REMOVED,
    SOL_NETWORK_LINK_CHANGED,
};

/**
 * @brief Bitwise OR-ed flags to represents the status of #sol_network_link.
 *
 * @see sol_network_link
 */
enum sol_network_link_flags {
    SOL_NETWORK_LINK_UP            = (1 << 0),
    SOL_NETWORK_LINK_BROADCAST     = (1 << 1),
    SOL_NETWORK_LINK_LOOPBACK      = (1 << 2),
    SOL_NETWORK_LINK_MULTICAST     = (1 << 3),
    SOL_NETWORK_LINK_RUNNING       = (1 << 4),
};

/**
 * @brief Type of a network address
 *
 * Tells how an address should be interpreted.
 */
enum sol_network_family {
    /** @brief Unspecified address type */
    SOL_NETWORK_FAMILY_UNSPEC,
    /** @brief IPv4 family. */
    SOL_NETWORK_FAMILY_INET,
    /** @brief IPv6 family. */
    SOL_NETWORK_FAMILY_INET6
};

/**
 * @brief Structure to represent a network address, both IPv6 and IPv4 are valid.
 */
struct sol_network_link_addr {
    enum sol_network_family family; /**< @brief IPv4 or IPv6 family */
    union {
        uint8_t in[4];
        uint8_t in6[16];
    } addr; /**< @brief The address itself */
    uint16_t port; /**< @brief The port associed with the IP address */
};

/**
 * @brief Structure to represent a network link.
 *
 * This struct contains the necessary information do deal with a
 * network link. It has the state @ref sol_network_link_flags, the
 * index (the value used by the SO to identify the link) and its
 * address @ref sol_network_link_addr.
 */
struct sol_network_link {
#ifndef SOL_NO_API_VERSION
#define SOL_NETWORK_LINK_API_VERSION (1)
    uint16_t api_version; /**< @brief API version */
#endif
    uint16_t index; /**< @brief the index of this link given by SO  */
    enum sol_network_link_flags flags; /**< @brief  The status of the link */
    /**
     * @brief List of network addresses.
     * @see sol_network_link_addr
     **/
    struct sol_vector addrs;
};

#ifndef SOL_NO_API_VERSION
/**
 * @brief Macro used to check if a struct @c struct sol_network_link has
 * the expected API version.
 *
 * In case it has wrong version, it'll return extra arguments passed
 * to the macro.
 */
#define SOL_NETWORK_LINK_CHECK_VERSION(link_, ...) \
    if (SOL_UNLIKELY((link_)->api_version != \
        SOL_NETWORK_LINK_API_VERSION)) { \
        SOL_WRN("Unexpected API version (message is %" PRIu16 ", expected %" PRIu16 ")", \
            (link_)->api_version, SOL_NETWORK_LINK_API_VERSION); \
        return __VA_ARGS__; \
    }
#else
#define SOL_NETWORK_LINK_CHECK_VERSION(link_, ...)
#endif

/**
 * @brief Converts a @c sol_network_link_addr to a string.
 *
 * @param addr The address to be converted.
 * @param buf The buffer where the converted string will be stored.
 * @param len The size of buf.
 *
 * @return a string with the network link address on success, @c NULL on error.
 *
 * @see sol_network_addr_from_str()
 */
const char *sol_network_addr_to_str(const struct sol_network_link_addr *addr,
    char *buf, uint32_t len);

/**
 * @brief Converts a string address to @c sol_network_link_addr.
 *
 * @param addr A valid address with the same family of the address given in @c buf.
 * @param buf The string with the address.
 *
 * @return the network link address on success, @c NULL on error.
 *
 * @see sol_network_addr_to_str()
 */
const struct sol_network_link_addr *sol_network_addr_from_str(struct sol_network_link_addr *addr, const char *buf);

/**
 * @brief Checks if two address are equal.
 *
 * This function compares two addresses to see if they are the same.
 *
 * @param a The first address to be compared.
 * @param b The second address to be compared.
 *
 * @return @c true if they are equal, otherwise @c false.
 */
bool sol_network_link_addr_eq(const struct sol_network_link_addr *a,
    const struct sol_network_link_addr *b);

/**
 * @brief Initialize the support to network.
 *
 * This function sets up all the internal code to monitor and deal
 * with network events.
 *
 * When the network support is not necessary anymore, call @ref
 * sol_network_shutdown() to release all the resources allocated.
 *
 * @return @c 1 or greater on success, @c 0 on error.
 *
 * @see sol_network_shutdown()
 */
int sol_network_init(void);

/**
 * @brief Shut down the support to network.
 *
 * This function shuts down the network support, it should be called
 * the same same number of times that @ref sol_network_init().
 *
 * @see sol_network_init()
 */
void sol_network_shutdown(void);

/**
 * @brief Subscribes on to receive network link events.
 *
 * This function register a callback given by the user that will be
 * called when a network event (@ref sol_network_event) occurrs in one
 * link (@ref sol_network_link).
 *
 * @param cb The callback used to notify the user.
 * @param data The user data given in the callback.
 *
 * @return @c true on success, @c false on error
 *
 * @see sol_network_unsubscribe_events()
 */
bool sol_network_subscribe_events(void (*cb)(void *data, const struct sol_network_link *link,
    enum sol_network_event event),
    const void *data);

/**
 * @brief Stops receive the network events.
 *
 * This function removes previous callbacks set (@ref
 * sol_network_subscribe_events()) to receive network events.
 *
 * @param cb The callback given on @ref sol_network_subscribe_events.
 * @param data The data given on @ref sol_network_subscribe_events.
 *
 * @return @c true on success, @c false on error
 *
 * @note It should be the same pair (callback/userdata) given on @ref
 * sol_network_subscribe_events()
 */
bool sol_network_unsubscribe_events(void (*cb)(void *data, const struct sol_network_link *link,
    enum sol_network_event event),
    const void *data);
/**
 * @brief Retrieve the available network links on system.
 *
 * This function gets the availables links in the system, it should
 * not be called if the network was not initialized @see
 * sol_network_init().
 *
 * @return A vector containing the available links @see sol_network_link
 *
 * @note This vector is updated as soon as the SO notifies about a
 * network link. This is information is cached, so it's possible that
 * at the moment it is called the data is still not available. It's
 * recommended first subscribe to receive network events @see
 * sol_network_subscribe_events() and then call it.
 */
const struct sol_vector *sol_network_get_available_links(void);

/**
 * @brief Gets the name of a network link.
 *
 * @param link The @ref sol_network_link structure which the name is desired.
 * @return The name of the interface on success, @c NULL on error.
 */
char *sol_network_link_get_name(const struct sol_network_link *link);

/**
 * @brief Sets a network link up.
 *
 * This function sets a network link up, after this a link will be
 * able to get a network address.
 *
 * @param link_index The index of a @ref sol_network_link structure.
 * @return @c true on success, @c false on error.
 */
bool sol_network_link_up(uint16_t link_index);

/**
 * @brief Gets a hostname address info.
 *
 * This function will fetch the address of a given hostname, since this may
 * take some time, this will be an async operation. When the address info
 * is ready the @c host_info_cb will called with the host's address info.
 * If an error happens or it was not possible to fetch the host address
 * information, @c addrs_list will be set to @c NULL.
 * The list @c addrs_list will contains a set of #sol_network_link_addr.
 *
 * @param hostname The hostname to get the address info.
 * @param family The family the returned addresses should be, pass SOL_NETWORK_FAMILY_UNSPEC
 * to match them all.
 * @param host_info_cb A callback to be called with the address list.
 * @param data Data to @c host_info_cb.
 * @return A handle to a hostname or @c NULL on error.
 * @see sol_network_cancel_get_hostname_address_info()
 * @see #sol_network_family
 */
struct sol_network_hostname_handle *
sol_network_get_hostname_address_info(const struct sol_str_slice hostname,
    enum sol_network_family family, void (*host_info_cb)(void *data,
    const struct sol_str_slice hostname, const struct sol_vector *addrs_list),
    const void *data);

/**
 * @brief Cancels a request to get the hostname info.
 *
 * @param handle The handle returned by #sol_network_get_hostname_address_info
 * @return 0 on success, -errno on error.
 * @see sol_network_get_hostname_address_info()
 */
int sol_network_cancel_get_hostname_address_info(struct sol_network_hostname_handle *handle);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
