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

#include <sol-common-buildopts.h>
#include <sol-vector.h>
#include <sol-str-slice.h>
#include <sol-buffer.h>
#include <sol-util.h>

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
 * It makes it possible to observe events, to inquire available links
 * and to set their states.
 *
 * @{
 */

/**
 * @brief String size of an IPv4/v6 address.
 */
#define SOL_NETWORK_INET_ADDR_STR_LEN 48

/**
 * @brief String size of a Bluetooth address.
 */
#define SOL_BLUETOOTH_ADDR_STRLEN 18

/**
 * @typedef sol_network_hostname_pending
 *
 * @brief A handle returned by sol_network_get_hostname_address_info()
 *
 * This handle can be used to cancel the work of unfinished
 * sol_network_get_hostname_address_info() calls, by calling
 * sol_network_hostname_pending_cancel().
 *
 * @see sol_network_get_hostname_address_info()
 * @see sol_network_hostname_pending_cancel()
 */
struct sol_network_hostname_pending;
typedef struct sol_network_hostname_pending sol_network_hostname_pending;

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
 * @brief Bitwise OR-ed flags to represent the status of a
 * #sol_network_link.
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
    SOL_NETWORK_FAMILY_INET6,
    /** @brief Bluetooth "raw" family */
    SOL_NETWORK_FAMILY_BLUETOOTH,
    /** @brief Bluetooth RFCOMM family */
    SOL_NETWORK_FAMILY_BLUETOOTH_RFCOMM,
    /** @brief Bluetooth L2CAP family */
    SOL_NETWORK_FAMILY_BLUETOOTH_L2CAP,
};

/**
 * @brief Type of a Bluetooth address
 *
 * With the increased privacy allowed by Bluetooth Low Energy,
 * a Bluetooth device may be identified by different types of
 * addresses.
 */
enum sol_network_bt_addr_type {
    SOL_NETWORK_BT_ADDR_BASIC_RATE,
    SOL_NETWORK_BT_ADDR_LE_PUBLIC,
    SOL_NETWORK_BT_ADDR_LE_RANDOM,
};

/**
 * @brief Structure to represent a network address, both IPv6 and IPv4 are valid.
 */
typedef struct sol_network_link_addr {
    enum sol_network_family family; /**< @brief IPv4 or IPv6 family */
    union {
        uint8_t in[4];
        uint8_t in6[16];
        struct {
            uint8_t bt_type;
            uint8_t bt_addr[6];
        };
    } addr; /**< @brief The address itself */
    uint16_t port; /**< @brief The port associed with the IP address */
} sol_network_link_addr;

/**
 * @brief Structure to represent a network link.
 *
 * This struct contains the necessary information do deal with a
 * network link. It has the state @ref sol_network_link_flags, the
 * index (the value used by the SO to identify the link) and its
 * address @ref sol_network_link_addr.
 */
typedef struct sol_network_link {
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
} sol_network_link;

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
        SOL_WRN("Unexpected API version (message is %u, expected %u)", \
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
 * @param buf The buffer where the converted string will be appended -
 * It must be already initialized.
 *
 * @return a string with the network link address on success, @c NULL on error.
 *
 * @see sol_network_link_addr_from_str()
 */
const char *sol_network_link_addr_to_str(const struct sol_network_link_addr *addr, struct sol_buffer *buf);

/**
 * @brief Converts a string address to @c sol_network_link_addr.
 *
 * @param addr A valid address with the same family of the address given in @c buf.
 * @param buf The string with the address.
 *
 * @return the network link address on success, @c NULL on error.
 *
 * @see sol_network_link_addr_to_str()
 */
const struct sol_network_link_addr *sol_network_link_addr_from_str(struct sol_network_link_addr *addr, const char *buf);

/**
 * @brief Checks if two address are equal - possibly including the port field.
 *
 * This function compares two addresses to see if they are the same.
 *
 * @param a The first address to be compared.
 * @param b The second address to be compared.
 * @param compare_ports Indicates if the port should be included in the comparison as well.
 *
 * @return @c true if they are equal, otherwise @c false.
 */
static inline bool
sol_network_link_addr_eq_full(const struct sol_network_link_addr *a,
    const struct sol_network_link_addr *b, bool compare_ports)
{
    size_t bytes;

    if (compare_ports && (a->port != b->port))
        return false;

    if (a->family == b->family) {
        const uint8_t *addr_a, *addr_b;

        if (a->family == SOL_NETWORK_FAMILY_INET) {
            addr_a = a->addr.in;
            addr_b = b->addr.in;
            bytes = sizeof(a->addr.in);
        } else if (a->family == SOL_NETWORK_FAMILY_INET6) {
            addr_a = a->addr.in6;
            addr_b = b->addr.in6;
            bytes = sizeof(a->addr.in6);
        } else if (a->family == SOL_NETWORK_FAMILY_BLUETOOTH) {
            if (a->addr.bt_type != b->addr.bt_type)
                return false;

            addr_a = a->addr.bt_addr;
            addr_b = b->addr.bt_addr;
            bytes = sizeof(a->addr.bt_addr);
        } else
            return false;
        return !memcmp(addr_a, addr_b, bytes);
    }

    if ((a->family == SOL_NETWORK_FAMILY_INET &&
        b->family == SOL_NETWORK_FAMILY_INET6) ||
        (a->family == SOL_NETWORK_FAMILY_INET6 &&
        b->family == SOL_NETWORK_FAMILY_INET)) {

        struct ipv6_map_prefix {
            const uint8_t zeroes[10];
            const uint16_t ones;
        } __attribute__ ((packed)) prefix = {
            { 0 }, sol_util_be16_to_cpu(0xffff)
        };
        const uint8_t *addr_ipv6, *addr_ipv4;


        if (a->family == SOL_NETWORK_FAMILY_INET6) {
            addr_ipv6 = a->addr.in6;
            addr_ipv4 = b->addr.in;
        } else {
            addr_ipv6 = b->addr.in6;
            addr_ipv4 = a->addr.in;
        }

        bytes = sizeof(a->addr.in);

        /**
         * An IPv6 is Mapped into v4 when:
         * First 80 bits are zero
         * The next 16 bits are 0xffff
         */
        if (!memcmp(addr_ipv6, &prefix, sizeof(struct ipv6_map_prefix)) &&
            !memcmp(addr_ipv6 + 12, addr_ipv4, bytes))
            return true;
    }

    return false;
}

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
static inline bool
sol_network_link_addr_eq(const struct sol_network_link_addr *a,
    const struct sol_network_link_addr *b)
{
    return sol_network_link_addr_eq_full(a, b, false);
}
/**
 * @brief Subscribes to receive network link events.
 *
 * This function register a callback given by the user that will be
 * called when a network event (@ref sol_network_event) occurrs in one
 * link (@ref sol_network_link).
 *
 * @param cb The callback used to notify the user.
 * @param data The user data given in the callback.
 *
 * @return @c true on success, @c -errno on error
 *
 * @see sol_network_unsubscribe_events()
 */
int sol_network_subscribe_events(void (*cb)(void *data, const struct sol_network_link *link,
    enum sol_network_event event),
    const void *data);

/**
 * @brief Stops receiving network link events.
 *
 * This function removes previous callbacks set (@ref
 * sol_network_subscribe_events()) to receive network events.
 *
 * @param cb The callback given on @ref sol_network_subscribe_events.
 * @param data The data given on @ref sol_network_subscribe_events.
 *
 * @return @c O on success, @c -errno on error
 *
 * @note It should be the same pair (callback/userdata) given on @ref
 * sol_network_subscribe_events()
 */
int sol_network_unsubscribe_events(void (*cb)(void *data, const struct sol_network_link *link,
    enum sol_network_event event),
    const void *data);

/**
 * @brief Retrieve the available network links on a system.
 *
 * @return A vector containing the available links @see sol_network_link
 *
 * @note This vector is updated as soon as the SO notifies about a
 * network link. This information is cached, so it's possible that at
 * the moment it is called the data is still not available. It's
 * recommended to first subscribe to receive network events (@see
 * sol_network_subscribe_events()) and then call it.
 */
const struct sol_vector *sol_network_get_available_links(void);

/**
 * @brief Gets the name of a network link.
 *
 * @param link The @ref sol_network_link structure which the name is desired.
 * @return The name of the interface on success, @c NULL on error.
 *
 * @note It @b must be freed by the user after usage.
 */
char *sol_network_link_get_name(const struct sol_network_link *link);

/**
 * @brief Sets a network link up.
 *
 * This function sets a network link up, after this a link will be
 * able to get a network address.
 *
 * @param link_index The index of a @ref sol_network_link structure.
 * @return @c true on success, @c -errno on error.
 *
 * @see sol_network_linke_down()
 */
int sol_network_link_up(uint16_t link_index);

/**
 * @brief Sets a network link down.
 *
 * This function sets a network link down, after this a link will not be
 * able to get a network address.
 *
 * @param link_index The index of a @ref sol_network_link structure.
 * @return @c 0 on success, @c -ENOSYS on error.
 *
 * @see sol_network_linke_up()
 */
int sol_network_link_down(uint16_t link_index);

/**
 * @brief Gets a hostname address info.
 *
 * This function will fetch the address of a given hostname. Since
 * this may take some time to complete, this will be an asynchronous
 * operation. When the address information is ready, @c host_info_cb
 * will be called with it. If an error happens or it was not possible
 * to fetch the host address information, @c addrs_list will be set to
 * @c NULL. The list @c addrs_list will contain a set of
 * #sol_network_link_addr.
 *
 * @note This operation may be cancelled by user with
 * sol_network_hostname_pending_cancel() while @a host_info_cb has not
 * been called yet.
 *
 * @param hostname The hostname to get the address info.
 * @param family The family the returned addresses should be, pass
 * #SOL_NETWORK_FAMILY_UNSPEC to match them all.
 * @param host_info_cb A callback to be called with the address list.
 * @param data Data to @c host_info_cb.
 * @return A handle to a hostname or @c NULL on error.
 * @see sol_network_hostname_pending_cancel()
 * @see #sol_network_family
 */
struct sol_network_hostname_pending *
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
int sol_network_hostname_pending_cancel(struct sol_network_hostname_pending *handle);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
