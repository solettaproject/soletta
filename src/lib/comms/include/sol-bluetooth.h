/*
 * This file is part of the Soletta (TM) Project
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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to handle common Bluetooth communications.
 *
 * Bluetooth[1] is a standardised technology for short distance
 * communications. Its defined by an open standard, governed by the
 * Bluetooth Special Interest Group (SIG).
 *
 * Bluetooth defines two kinds of functionality, Bluetooth Smart, also
 * referred as Bluetooth Low Energy (BLE), and Bluetooth Basic Rate.
 * Bluetooth Basic Rate most popular application is wireless audio,
 * Bluetooth Low Energy is becoming popular for wearable devices.
 *
 * [1] https://www.bluetooth.com/
 */

/**
 * @defgroup Bluetooth Bluetooth
 * @ingroup Comms
 *
 * @brief API to handle Bluetooth technology communications.
 *
 * @warning Experimental API. Changes are expected in future releases.
 *
 * @{
 */

#include <sol-network.h>

/**
 * @brief Set of types of UUIDs
 *
 * Bluetooth services (and other entities) types are uniquely identified
 * by UUIDs. In Bluetooth, UUIDs come in different sizes, usually the 16-bit
 * type is reserved to be allocated by the Bluetooth SIG. The 32-bit type is
 * also reserved, but less used. The 128-bit is free to be used by applications.
 */
enum sol_bt_uuid_type {
    SOL_BT_UUID_TYPE_16 = 2,
    SOL_BT_UUID_TYPE_32 = 4,
    SOL_BT_UUID_TYPE_128 = 16,
};

/**
 * @brief Representation of a Bluetooth UUID.
 *
 * In Bluetooth, a UUID represents the type of an entity, for example,
 * if a service is encountered in a remote device with the 16-bit UUID '0x111F',
 * that service is a "HandsfreeAudioGateway".
 *
 * @see #sol_bt_uuid_type
 */
typedef struct sol_bt_uuid {
    enum sol_bt_uuid_type type;
    union {
        uint16_t val16;
        uint32_t val32;
        uint8_t val128[16];
        uint8_t val[0];
    };
} sol_bt_uuid;

/**
 * @brief Convert a string to a UUID.
 *
 * @param uuid The uuid in which to store the value.
 * @param str The string from which to convert.
 *
 * @return 0 on success, -errno on failure.
 */
int sol_bt_uuid_from_str(struct sol_bt_uuid *uuid, const struct sol_str_slice str);

/**
 * @brief Convert a string to a UUID.
 *
 * @param uuid The uuid to convert.
 * @param buffer The buffer in which to store the string representation.
 *
 * @return 0 on success, -errno on failure.
 */
int sol_bt_uuid_to_str(const struct sol_bt_uuid *uuid, struct sol_buffer *buffer);

/**
 * @brief Compare two UUIDs.
 *
 * The UUIDs are converted to their 128-bit representation, if they are of
 * different types, and compared returning if they are equal.
 *
 * @param u1 UUID to be compared.
 * @param u2 UUID to be compared.
 *
 * @return True if UUIDs are equal, False otherwise.
 */
bool sol_bt_uuid_eq(const struct sol_bt_uuid *u1, const struct sol_bt_uuid *u2);

/**
 * @typedef sol_bt_conn
 * @brief Represents an active connection to a Bluetooth device.
 *
 * The connection is established with sol_bt_connect(), and its lifetime is
 * managed by sol_bt_conn_ref()/sol_bt_conn_unref().
 */
struct sol_bt_conn;
typedef struct sol_bt_conn sol_bt_conn;

/**
 * @brief Increases the reference count of a connection.
 *
 * @param conn The reference to a connection.
 *
 * @return The same connection, with refcount increased, or NULL if invalid.
 */
struct sol_bt_conn *sol_bt_conn_ref(struct sol_bt_conn *conn);

/**
 * @brief Decreases the reference count of a connection.
 *
 * When the last reference is released, the connection and all the resources
 * associated with it are released.
 *
 * @param conn The reference to a connection.
 */
void sol_bt_conn_unref(struct sol_bt_conn *conn);

/**
 * @brief Returns the network address of the remote device.
 *
 * @param conn The reference to a connection.
 *
 * @return The network link address associated with the remote end of the
 *         connection.
 */
const struct sol_network_link_addr *sol_bt_conn_get_addr(
    const struct sol_bt_conn *conn);

/**
 * @brief Attempts to establish a connection with a remote device.
 *
 * @param addr The network link address of the remote device,
 *        @see sol_bt_start_scan().
 * @param on_connect The callback to be called when the connection
 *        is established successfully.
 * @param on_disconnect The callback to be called when the connection
 *        is terminated, after established.
 * @param on_error The callback to be called when the connection cannot
 *        be established.
 * @param user_data User data to be passed to the connection callbacks.
 *
 * @return a reference to a connection, so the connection attempt can be cancelled,
 *         @see sol_bt_disconnect().
 */
struct sol_bt_conn *sol_bt_connect(const struct sol_network_link_addr *addr,
    bool (*on_connect)(void *user_data, struct sol_bt_conn *conn),
    void (*on_disconnect)(void *user_data, struct sol_bt_conn *conn),
    void (*on_error)(void *user_data, int error),
    const void *user_data);

/**
 * @brief Terminates a connection, or connection attempt.
 *
 * In case the connection is not already established, the connection
 * attempt will be cancelled after this is called, otherwise the @c
 * on_disconnect() function provided on the call to sol_bt_connect()
 * will be called.
 *
 * @param conn The reference to a connection.
 *
 * @return 0 on success, -errno otherwise.
 */
int sol_bt_disconnect(struct sol_bt_conn *conn);

/**
 * @typedef sol_bt_session
 * @brief Represents a Bluetooth usage session
 *
 * Because Bluetooth usage may increase the power comsuption, there's
 * a need to keep track of what is using Bluetooth in the system, and
 * keep Bluetooth turned off if it's not used.
 */
struct sol_bt_session;
typedef struct sol_bt_session sol_bt_session;

/**
 * @brief Enables the local Bluetooth controller.
 *
 * Before using any other functionality from this module, this function
 * should be called.
 *
 * In case the Bluetooth controller is already enabled, the enabled() callback
 * will be called before this function returns.
 *
 * @param on_enabled Function to be called when the controller changes state.
 * @param user_data User data to be provided to the @a on_enabled callback.
 *
 * @return a reference to a session, used for returning the system to its
 * previous state, @see sol_bt_disable().
 */
struct sol_bt_session *sol_bt_enable(
    void (*on_enabled)(void *data, bool powered),
    const void *user_data);

/**
 * @brief Disables a session, returning the controller to its previous state.
 *
 * In case the session is not already enabled, the enabling attempt
 * will be cancelled after this is called, otherwise the @c
 * on_enabled() function provided on the call to sol_bt_enable() will
 * be called with @c false @c powered argument value.
 *
 * @param session Reference to a session.
 *
 * @return 0 on success, -errno otherwise.
 */
int sol_bt_disable(struct sol_bt_session *session);

/**
 * @brief Represents a information about a remote device.
 */
typedef struct sol_bt_device_info {
    /**
     * @brief Network address of the remote device.
     */
    struct sol_network_link_addr addr;
    /**
     * @brief Vector of service UUIDs discovered, may be empty.
     */
    struct sol_vector uuids;
    /**
     * @brief Friendly name of the device.
     */
    char *name;
    /**
     * @brief Received signal strength, measured in dBm.
     */
    int16_t rssi;
    /**
     * @brief Whether the device is paired.
     */
    bool paired;
    /**
     * @brief Whether the device is connected.
     */
    bool connected;
    /**
     * @brief Whether the devices is currently in range.
     */
    bool in_range;
} sol_bt_device_info;

/**
 * @brief Over which transport should a scan be performed.
 */
enum sol_bt_transport {
    /** @brief Discover devices over the Bluetooth Low Energy transport */
    SOL_BT_TRANSPORT_LE = 1,
    /** @brief Discover devices over the Bluetooth Basic Rate transport */
    SOL_BT_TRANSPORT_BREDR,
    /** @brief Discover devices over All transports */
    SOL_BT_TRANSPORT_ALL = SOL_BT_TRANSPORT_LE | SOL_BT_TRANSPORT_BREDR,
};

/**
 * @brief Converts a transport to a string, @see sol_bt_transport.
 *
 * @param transport A transport.
 *
 * @return a pointer to a string representing the transport on success,
 *         NULL otherwise.
 */
const char *sol_bt_transport_to_str(enum sol_bt_transport transport);

/**
 * @brief Converts string to a transport, @see sol_bt_transport.
 *
 * @param str A with the transport representation.
 *
 * @return a pointer to a string representing the transport on success,
 *         NULL otherwise.
 */
enum sol_bt_transport sol_bt_transport_from_str(const char *str);

/**
 * @typedef sol_bt_scan_pending
 * @brief Represents a pending scan session
 *
 * Represents a pending scan session, @see sol_bt_start_scan().
 */
struct sol_bt_scan_pending;
typedef struct sol_bt_scan_pending sol_bt_scan_pending;

/**
 * @brief Start scanning for devices.
 *
 * If there are already known devices, even if not visible, they will be
 * notified. This function is safe to be called multiple times, the
 * discovery will be stopped when the last user calls sol_bt_stop_scan().
 *
 * @param transport The transport in which to discover devices.
 * @param on_found The callback to be called for each found device. It
 *        may be called multiple times, when the information about the
 *        device changes.
 * @param user_data User data to be passed to the callback.
 *
 * @return pointer to a pending scan session on success, NULL on error,
 *         @see sol_bt_stop_scan().
 */
struct sol_bt_scan_pending *sol_bt_start_scan(
    enum sol_bt_transport transport,
    void (*on_found)(void *user_data, const struct sol_bt_device_info *device),
    const void *user_data);

/**
 * @brief Stop a scanning session.
 *
 * @param handle Pointer of an ongoing scanning session.
 *
 * @return 0 on success, -errno otherwise.
 */
int sol_bt_stop_scan(struct sol_bt_scan_pending *handle);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
