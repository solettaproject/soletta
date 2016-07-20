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

#include <sol-bluetooth.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to handle Bluetooth GATT protocol
 *
 * The Bluetooth Generic Attribute Protocol (GATT) is a lightweight protocol
 * on top of another protocol named ATT (Attribute Protocol), that defines the
 * attribute has a type (UUID), a value, and is indentified by a handle.
 *
 * The API is based on the BlueZ D-Bus GATT API,
 * @see https://git.kernel.org/cgit/bluetooth/bluez.git/tree/doc/gatt-api.txt
 */

/**
 * @defgroup Bluetooth Bluetooth
 * @ingroup Comms
 *
 * @brief API to handle the GATT protocol.
 *
 * @warning Experimental API. Changes are expected in future releases.
 *
 * @{
 */

/**
 * @brief Set of types of Attributes
 *
 * GATT has the concept of different types of attributes, based on their UUIDs,
 * sol-gatt simplifies that, separating attributes into three types.
 */
enum sol_gatt_attr_type {
    SOL_GATT_ATTR_TYPE_INVALID,
    SOL_GATT_ATTR_TYPE_SERVICE,
    SOL_GATT_ATTR_TYPE_CHARACTERISTIC,
    SOL_GATT_ATTR_TYPE_DESCRIPTOR,
};

/**
 * @brief Set of flags for Charateristic attributes
 *
 * See the Bluetooth Core Specification, Table 3.5 and Table 3.8 for more details.
 */
enum sol_gatt_chr_flags {
    /** @brief When set allows the characteristic value to be broadcast. */
    SOL_GATT_CHR_FLAGS_BROADCAST = (1 << 0),
    /** @brief Allows the characteristic value to be read. */
    SOL_GATT_CHR_FLAGS_READ = (1 << 1),
    /** @brief Allows the write without response procedure against the characteristic value */
    SOL_GATT_CHR_FLAGS_WRITE_WITHOUT_RESPONSE = (1 << 2),
    /** @brief Allows the characteristic value to be written */
    SOL_GATT_CHR_FLAGS_WRITE = (1 << 3),
    /** @brief Allows notifications for the characteristic value */
    SOL_GATT_CHR_FLAGS_NOTIFY = (1 << 4),
    /** @brief Allows indications for the characteristic value */
    SOL_GATT_CHR_FLAGS_INDICATE = (1 << 5),
    /** @brief Allows the authenticated signed write procedure against the characteristic value */
    SOL_GATT_CHR_FLAGS_AUTHENTICATED_SIGNED_WRITES = (1 << 6),
    /** @brief Allows the reliable write procedure against the characteristic value */
    SOL_GATT_CHR_FLAGS_RELIABLE_WRITE = (1 << 7),
    /** @brief Allows write operation against the descriptors associated with this characteristic */
    SOL_GATT_CHR_FLAGS_WRITABLE_AUXILIARIES = (1 << 8),
    /** @brief Only allows encrypted read operations against the characteristic value */
    SOL_GATT_CHR_FLAGS_ENCRYPT_READ = (1 << 9),
    /** @brief Only allows encrypted write operations against the characteristic value */
    SOL_GATT_CHR_FLAGS_ENCRYPT_WRITE = (1 << 10),
    /** @brief Only allows encrypted and authenticated read operations against
     * the characteristic value
     */
    SOL_GATT_CHR_FLAGS_ENCRYPT_AUTHENTICATED_READ = (1 << 11),
    /** @brief Only allows encrypted and authenticated write operations against
     * the characteristic value
     */
    SOL_GATT_CHR_FLAGS_ENCRYPT_AUTHENTICATED_WRITE = (1 << 12),
};

/**
 * @brief Set of flags for Descriptor attributes
 */
enum sol_gatt_desc_flags {
    /** @brief Allows the descriptor value to be read. */
    SOL_GATT_DESC_FLAGS_READ = (1 << 0),
    /** @brief Allows the descriptor value to be written. */
    SOL_GATT_DESC_FLAGS_WRITE = (1 << 1),
    /** @brief Only allows encrypted read operations against the descriptor value. */
    SOL_GATT_DESC_FLAGS_ENCRYPT_READ = (1 << 2),
    /** @brief Only allows encrypted write operations against the descriptor value. */
    SOL_GATT_DESC_FLAGS_ENCRYPT_WRITE = (1 << 3),
    /**
     * @brief Only allows encrypted and authenticated read operations against
     * the descriptor value.
     */
    SOL_GATT_DESC_FLAGS_ENCRYPT_AUTHENTICATED_READ = (1 << 4),
    /**
     * @brief Only allows encrypted and authenticated write operations against
     * the descriptor value.
     */
    SOL_GATT_DESC_FLAGS_ENCRYPT_AUTHENTICATED_WRITE = (1 << 5),
};

/**
 * @typedef sol_gatt_pending
 * @brief Represents a pending request
 *
 * So a response to a GATT operation can be returned asynchronously,
 * operation callbacks, @see sol_gatt_attr, passes a pending operation
 * to the user which calls sol_gatt_pending_reply() when the operation
 * is complete.
 */
struct sol_gatt_pending;
typedef struct sol_gatt_pending sol_gatt_pending;

/**
 * @brief Returns the attribute referenced by a pending operation
 *
 * @param op The pending operation
 *
 * @return reference to an attribute
 */
const struct sol_gatt_attr *sol_gatt_pending_get_attr(
    const struct sol_gatt_pending *op);

/**
 * @brief Representation of a GATT Attribute
 */
typedef struct sol_gatt_attr {
    struct sol_bt_uuid uuid;
    enum sol_gatt_attr_type type;
    uint16_t flags;
    /**
     * The response for this operation will be returned by calling
     * #sol_gatt_pending_reply() passing a valid buffer, or an error.
     */
    int (*read)(struct sol_gatt_pending *op,
        uint16_t offset);
    /**
     * The response for this operation will be returned by calling
     * #sol_gatt_pending_reply(), the @a buf parameter will be ignored.
     *
     * @param op The pending operation.
     * @param buf The buffer that will be written into the attribute,
     *        If the function returns success, it takes the ownership
     *        of the buffer.
     * @param offset The offset into which the write operation was made.
     *
     * @return 0 on success, -errno on failure.
     */
    int (*write)(struct sol_gatt_pending *op,
        struct sol_buffer *buf,
        uint16_t offset);

    void *user_data;

    void *_priv;
} sol_gatt_attr;

/**
 * @brief Returns a response to an asynchronous operation
 *
 * When a operation is performed in a attribute, the response to the operation
 * is only returned when this function is called.
 *
 * @param pending Pending operation to be responded.
 * @param error Error to be returned, 0 on success.
 * @param buf Payload (can be NULL) to be returned, on success the payload
 *        ownership passes to the function.
 *
 * @return 0 on success, -errno on failure.
 */
int sol_gatt_pending_reply(struct sol_gatt_pending *pending,
    int error,
    struct sol_buffer *buf);

/**
 * @brief Helper to the construction of a GATT Service with 16-bit UUID.
 *
 * @see sol_gatt_attr
 */
#define SOL_GATT_SERVICE_UUID_16(service_uuid) \
    { .type = SOL_GATT_ATTR_TYPE_SERVICE, \
      .uuid = { .type = SOL_BT_UUID_TYPE_16, \
                .val16 = (service_uuid) } }

/**
 * @brief Helper to the construction of a GATT Characteristic with 16-bit UUID.
 *
 * @see sol_gatt_attr
 */
#define SOL_GATT_CHARACTERISTIC_UUID_16(_uuid, _flags, ...) \
    { .type = SOL_GATT_ATTR_TYPE_CHARACTERISTIC, \
      .uuid = { .type = SOL_BT_UUID_TYPE_16, \
                .val16 = (_uuid) }, \
      .flags = (_flags), \
      __VA_ARGS__ }

/**
 * @brief Helper to the construction of a GATT Descriptor with 16-bit UUID.
 *
 * @see sol_gatt_attr
 */
#define SOL_GATT_DESCRIPTOR_UUID_16(_uuid, _flags, ...) \
    { .type = SOL_GATT_ATTR_TYPE_DESCRIPTOR, \
      .uuid = { .type = SOL_BT_UUID_TYPE_16, \
                .val16 = (_uuid) }, \
      .flags = (_flags), \
      __VA_ARGS__ }

/**
 * @brief Terminates the list of attributes.
 *
 * @see sol_gatt_register_attributes()
 */
#define SOL_GATT_ATTR_INVALID { .type = SOL_GATT_ATTR_TYPE_INVALID }

/**
 * @brief Registers attributes into the GATT database
 *
 * @param attrs Array of the attributes to be registered
 *
 * @return 0 on success, -errno on failure.
 */
int sol_gatt_register_attributes(struct sol_gatt_attr *attrs);

/**
 * @brief Unregisters attributes from the GATT database
 *
 * @param attrs Array of the attributes to be unregistered
 *
 * @return 0 on success, -errno on failure.
 */
int sol_gatt_unregister_attributes(struct sol_gatt_attr *attrs);

/**
 * @brief Reads the value from a attribute
 *
 * This only really makes sense for Characteristics and Descriptors,
 * the Read Long attribute procedure will be performed if it's supported.
 *
 * @param conn Connection to a remote device
 * @param attr Attribute to be read
 * @param cb Callback to be called when the operation is finished
 * @param user_data User pointer to be passed to the callback
 *
 * @return 0 on success, -errno on failure.
 */
int sol_gatt_read_attr(struct sol_bt_conn *conn, struct sol_gatt_attr *attr,
    void (*cb)(void *user_data, bool success,
    const struct sol_gatt_attr *attr,
    const struct sol_buffer *buf),
    const void *user_data);

/**
 * @brief Writes the value to a attribute
 *
 * This only really makes sense for Characteristics and Descriptors,
 * the Write Long attribute procedure will be performed if it's supported.
 *
 * @param conn Connection to a remote device
 * @param attr Attribute to be written
 * @param buf Buffer to be written, on success the ownership passes to the function.
 * @param cb Callback to be called when the operation is finished
 * @param user_data User pointer to be passed to the callback
 *
 * @return 0 on success, -errno on failure.
 */
int sol_gatt_write_attr(struct sol_bt_conn *conn, struct sol_gatt_attr *attr,
    struct sol_buffer *buf,
    void (*cb)(void *user_data, bool success,
    const struct sol_gatt_attr *attr),
    const void *user_data);

/**
 * @brief Discover attributes by type, restricted by UUID and parent attribute.
 *
 * Discover attributes belonging to a parent attribute, for example,
 * discover all the characteristics under a service or all the
 * descriptors under a characteristic, matching uuid (if provided).
 *
 * @param conn Connection to a remote device
 * @param type If different from #SOL_GATT_ATTR_TYPE_INVALID, only consider attributes
 *        of @a type, @see sol_gatt_attr_type.
 * @param parent If different from NULL, only consider attributes belonging
 *        to @a parent attribute.
 * @param uuid If different from NULL, only consider attributes matching @a uuid.
 * @param cb Callback to be called when the conditions above are met.
 * @param user_data User pointer to be passed to the callback
 *
 * @return 0 on success, -errno on failure.
 */
int sol_gatt_discover(struct sol_bt_conn *conn, enum sol_gatt_attr_type type,
    const struct sol_gatt_attr *parent,
    const struct sol_bt_uuid *uuid,
    bool (*cb)(void *user_data, struct sol_bt_conn *conn,
    const struct sol_gatt_attr *attr),
    const void *user_data);

/**
 * Registers a callback to be called when a notification/indication is received.
 *
 * It will also try to enable indications/nofications writing to the
 * CCC attribute if that descriptor exists.
 *
 * @param conn Connection to a remote device
 * @param attr Attribute which to enable Notifications/Indications
 * @param cb Callback to be called for each update, if you want the notification
 *        to be cancelled, return @a false.
 * @param user_data User pointer to be passed to the callback
 *
 * @return 0 on success, -errno on failure.
 */
int sol_gatt_subscribe(struct sol_bt_conn *conn, const struct sol_gatt_attr *attr,
    bool (*cb)(void *user_data, const struct sol_gatt_attr *attr,
    const struct sol_buffer *buffer),
    const void *user_data);

/**
 * Unregisters from receiving notifications indications
 *
 * @param cb Callback previously set
 * @param user_data User pointer
 *
 * @return 0 on success, -errno on failure.
 */
int sol_gatt_unsubscribe(bool (*cb)(void *user_data, const struct sol_gatt_attr *attr,
    const struct sol_buffer *buffer),
    const void *user_data);

/**
 * Sends an indication to the device represented by @a conn. If @a
 * conn is @c NULL, it sends to all devices that have registered
 * themselves via the CCC attribute.
 *
 * The value to be indicated is the value retrieved by calling the @a
 * read callback of the attribute.
 *
 * @param conn Connection in which to send notificatiions/indications.
 * @param attr Attribute to update.
 *
 * @return 0 on success, -errno on failure.
 */
int sol_gatt_indicate(struct sol_bt_conn *conn, const struct sol_gatt_attr *attr);

/**
 * Sends a notificatiion to the device represented by @a conn. If @a
 * conn is @c NULL, it sends to all devices that have registered
 * themselves via the CCC attribute.
 *
 * The value to be notified is the value retrieved by calling the @a
 * read callback of the attribute.
 *
 * @param conn Connection in which to send notificatiions/indications.
 * @param attr Attribute to update.
 *
 * @return 0 on success, -errno on failure.
 */
int sol_gatt_notify(struct sol_bt_conn *conn, const struct sol_gatt_attr *attr);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
