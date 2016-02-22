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

#include <errno.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>
#include <bluetooth/uuid.h>
#include <bluetooth/l2cap.h>
#include <misc/byteorder.h>
#include <misc/util.h>

#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-mainloop-zephyr.h"
#include "sol-network.h"
#include "sol-vector.h"
#include "sol-util.h"

#include "sol-socket.h"
#include "sol-socket-impl.h"

#define DEVICE_NAME "Soletta"

static struct sol_ptr_vector ble_bound_sockets = SOL_PTR_VECTOR_INIT;
static struct bt_gatt_discover_params discover_params;

#define SOCK_BUF_SIZE 64

struct sol_socket_ble {
    struct sol_socket base;

    struct {
        bool (*cb)(void *data, struct sol_socket *s);
        const void *data;
    } read, write;

    struct sol_timeout *write_timeout;
    struct bt_conn *connection;
    uint16_t charc_handle;
    uint8_t receive_buf[SOCK_BUF_SIZE];
    uint8_t send_buf[SOCK_BUF_SIZE];
    int receive_size;
    int send_size;
};

// Data to be used in scan response packets
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, sizeof(DEVICE_NAME)),
};

static const uint8_t ad_flags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;

// Data to be used in advertisement packets
static const struct bt_data ad[] = {
    BT_DATA(BT_DATA_FLAGS, &ad_flags, sizeof(ad_flags)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL,
        0x16, 0xe8, 0x0E, 0xf7, 0x69, 0xeb, 0x87, 0xa9,
        0x63, 0x4f, 0x84, 0xc7, 0x29, 0xd5, 0xe3, 0xad),
};

static struct bt_uuid_128 iotivity_service = BT_UUID_INIT_128(
    0x16, 0xe8, 0x0E, 0xf7, 0x69, 0xeb, 0x87, 0xa9,
    0x63, 0x4f, 0x84, 0xc7, 0x29, 0xd5, 0xe3, 0xad);

static struct bt_uuid_128 response = BT_UUID_INIT_128(
    0x56, 0xb2, 0x16, 0x82, 0x04, 0x95, 0x31, 0x88,
    0xc4, 0x42, 0x80, 0x45, 0x82, 0x19, 0x24, 0xe9);

static struct bt_uuid_128 request = BT_UUID_INIT_128(
    0x18, 0xd2, 0x03, 0x7f, 0x78, 0x9d, 0xb6, 0x90,
    0x86, 0x4b, 0x37, 0x46, 0x4f, 0x33, 0x7b, 0xad);

static int
read_string(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
            uint16_t len, uint16_t offset)
{
    const char *str = attr->user_data;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, str, strlen(str));
}

static struct sol_socket_ble *
sol_socket_get(struct bt_conn *conn)
{
    struct sol_socket_ble *socket;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&ble_bound_sockets, socket, i) {
        if (socket->connection == conn) {
            return socket;
        }
    }

    return NULL;
}

static void
exchange_mtu_cb(struct bt_conn *conn, uint8_t err)
{
    if (err != 0) {
        SOL_WRN("MTU exchange error: %u", err);
    }
}

static uint8_t
request_uuid_discover(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                      struct bt_gatt_discover_params *params)
{
    int err;
    struct sol_socket_ble *s;

    SOL_NULL_CHECK_GOTO(attr, stop);

    s = sol_socket_get(conn);
    SOL_NULL_CHECK_GOTO(s, stop);

    s->charc_handle = params->start_handle + 1;

    err = bt_gatt_write_without_response(s->connection, s->charc_handle,
                                         s->send_buf, s->send_size, false);

    memset(s->send_buf, 0, s->send_size);
    s->send_size = 0;
    SOL_INT_CHECK_GOTO(err, < 0, stop);

stop:
    return BT_GATT_ITER_STOP;
}

static uint8_t
primary_service_discover(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         struct bt_gatt_discover_params *params)
{

    SOL_NULL_CHECK_GOTO(attr, stop);

    int err;
    struct sol_socket_ble *s = sol_socket_get(conn);
    SOL_NULL_CHECK_GOTO(s, stop);

    discover_params.uuid = &request;
    discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
    discover_params.func = request_uuid_discover;
    discover_params.start_handle = attr->handle + 1;

    err = bt_gatt_discover(s->connection, &discover_params);
    SOL_INT_CHECK_GOTO(err, < 0, stop);

stop:
    return BT_GATT_ITER_STOP;
}

static void
bt_connected(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];
    int ret;

    uint16_t i;
    struct sol_socket_ble *s;

    ret = bt_gatt_exchange_mtu(conn, exchange_mtu_cb);
    SOL_INT_CHECK(ret, != 0, NULL);

    ret = bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (ret < 0 || ret > BT_ADDR_LE_STR_LEN) {
        SOL_WRN("Failed to parse Bluetooth address");
        return NULL;
    }

    SOL_DBG("Connected: %s", addr);

    SOL_PTR_VECTOR_FOREACH_IDX (&ble_bound_sockets, s, i) {
        if (!s->connection) {
            s->connection = bt_conn_ref(conn);
            break;
        }
    }
}

static int
bt_advertise(void)
{
    int err;
    struct bt_le_adv_param param;

    param.interval_min = BT_GAP_ADV_FAST_INT_MIN_2;
    param.interval_max = BT_GAP_ADV_FAST_INT_MAX_2;
    param.type = BT_LE_ADV_IND;
    param.addr_type = BT_LE_ADV_ADDR_IDENTITY;

    err = bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

    return err;
}

static void
bt_disconnected(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];
    int ret;
    uint16_t i;
    struct sol_socket_ble *socket;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    SOL_DBG("Disconnected: %s", addr);

    socket = sol_socket_get(conn);

    if (socket) {
        bt_conn_unref(socket->connection);
        sol_ptr_vector_remove(&ble_bound_sockets, socket);
        free(socket);
    }
}

static int
write_cb_request(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                 const void *buf, uint16_t len, uint16_t offset)
{
    char addr[BT_ADDR_LE_STR_LEN];
    int ret;
    struct sol_socket_ble *s = sol_socket_get(conn);

    SOL_NULL_CHECK(s, -EINVAL);

    if ((offset + len - 2) > sizeof(s->receive_buf))
        return -EINVAL;

    memcpy(s->receive_buf + offset, buf + 2, len - 2);

    s->receive_size += len - 2;

    SOL_DBG("Write request conn %p offset %u len %u", conn, offset, len);

    return len;
}

static int
flush_cb_request(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                 uint8_t flags)
{
    struct sol_socket_ble *s = sol_socket_get(conn);

    SOL_DBG("Flush conn %p", conn);

    s->read.cb((void *)s->read.data, &s->base);
    s->receive_size = 0;

    return 0;
}

static struct bt_gatt_attr attrs [] = {
        BT_GATT_PRIMARY_SERVICE(BT_UUID_GAP),
        BT_GATT_CHARACTERISTIC(BT_UUID_GAP_DEVICE_NAME, BT_GATT_CHRC_READ),
        BT_GATT_DESCRIPTOR(BT_UUID_GAP_DEVICE_NAME, BT_GATT_PERM_READ,
                           read_string, NULL, DEVICE_NAME),
        BT_GATT_PRIMARY_SERVICE(&iotivity_service),
        BT_GATT_CHARACTERISTIC(&request, BT_GATT_CHRC_WRITE),
        BT_GATT_LONG_DESCRIPTOR(&request, BT_GATT_PERM_WRITE, NULL,
                                write_cb_request, flush_cb_request, NULL),
        BT_GATT_CHARACTERISTIC(&response, BT_GATT_CHRC_NOTIFY),
        BT_GATT_DESCRIPTOR(&response, BT_GATT_PERM_READ, NULL, NULL,
                           BT_GATT_CCC_NOTIFY),
};

static struct bt_conn_cb conn_callbacks = {
        .connected = bt_connected,
        .disconnected = bt_disconnected,
};

static struct sol_socket_ble *
sol_socket_zephyr_ble_new(int domain, enum sol_socket_type type, int protocol)
{
    struct sol_socket_ble *socket;

    SOL_INT_CHECK_GOTO(domain, != AF_BT_IOTIVITY, unsupported_family);

    socket = calloc(1, sizeof(*socket));
    SOL_NULL_CHECK_GOTO(socket, socket_error);

    return &socket->base;

socket_error:
    errno = ENOMEM;
    return NULL;

unsupported_family:
    errno = EAFNOSUPPORT;
    return NULL;
}

static void
sol_socket_zephyr_ble_del(struct sol_socket *s)
{
    struct sol_socket_ble *socket = (struct sol_socket_ble *)s;

    if (socket->connection) {
        bt_disconnected(socket->connection);
    }

    free(socket);
}

static int
sol_socket_zephyr_ble_set_on_read(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), void *data)
{
    struct sol_socket_ble *socket = (struct sol_socket_ble *)s;

    SOL_NULL_CHECK(socket, -EINVAL);

    socket->read.cb = cb;
    socket->read.data = data;

    return 0;
}

static bool
write_timeout_cb(void *data)
{
    struct sol_socket_ble *s = data;

    if (s->write.cb((void *)s->write.data, &s->base))
        return true;

    s->write_timeout = NULL;
    return false;
}

static int
sol_socket_zephyr_ble_set_on_write(struct sol_socket *s, bool (*cb)(void *data, struct sol_socket *s), void *data)
{
    struct sol_socket_ble *socket = (struct sol_socket_ble *)s;

    SOL_NULL_CHECK(socket, -EINVAL);
    SOL_NULL_CHECK(cb, -EINVAL);

    if (cb && !socket->write_timeout) {
        socket->write_timeout = sol_timeout_add(0, write_timeout_cb, socket);
        SOL_NULL_CHECK(socket->write_timeout, -ENOMEM);
    } else if (!cb && socket->write_timeout) {
        sol_timeout_del(socket->write_timeout);
        socket->write_timeout = NULL;
    }

    socket->write.cb = cb;
    socket->write.data = data;

    return 0;
}

static int
sol_socket_zephyr_ble_recvmsg(struct sol_socket *s, void *buf, size_t len, struct sol_network_link_addr *cliaddr)
{
    bt_addr_le_t *dst;
    struct sol_socket_ble *socket = (struct sol_socket_ble *)s;

    dst = bt_conn_get_dst(socket->connection);

    memcpy(cliaddr->addr.in_ble, dst->val, sizeof(cliaddr->addr.in_ble));
    cliaddr->addr.in_ble[6] = dst->type;
    cliaddr->family = AF_BT_IOTIVITY;

    memcpy(buf, socket->receive_buf, socket->receive_size);

    return socket->receive_size;
}

static int
sol_socket_zephyr_ble_sendmsg(struct sol_socket *s, const void *buf, size_t len, const struct sol_network_link_addr *cliaddr)
{

    /*
        TODO: If there is no connection in the socket, it should create one and
              connect to an available device with the iotivitiy service UUID.
    */
    int err;
    uint8_t header[2];
    struct sol_socket_ble *socket = (struct sol_socket_ble *)s;

    if (!socket->connection) {
        SOL_WRN("Socket does not have any connection");
        return 1;
    }

    header[1] = len & 0xFF;
    header[0] = len & 0x0F;
    header[0] = len | 0x40;

    memcpy(socket->send_buf, header, 2);
    memcpy(socket->send_buf + 2, buf, len);
    socket->send_size = len + 2;

    if (socket->charc_handle)
        goto write;

    /*
        FIXME:  The discover should be enqueued when the connection has been
                established instead of when it is sending the message.
    */
    discover_params.uuid = &iotivity_service;
    discover_params.type = BT_GATT_DISCOVER_PRIMARY;
    discover_params.start_handle = 1;
    discover_params.end_handle = 0xffff;
    discover_params.func = primary_service_discover;

    err = bt_gatt_discover(socket->connection, &discover_params);
    if (err) {
        SOL_WRN("GATT Discover failed error %d", err);
        return err;
     }

    return 1;

write:
    err = bt_gatt_write_without_response(socket->connection,
                                         socket->charc_handle,
                                         socket->send_buf, socket->send_size,
                                         false);
    memset(socket->send_buf, 0, socket->send_size);
    socket->send_size = 0;

    return err;
}

static int
sol_socket_zephyr_ble_join_group(struct sol_socket *s, int ifindex, const struct sol_network_link_addr *group)
{
    SOL_WRN("Not implemented");
    return 0;
}

static int
sol_socket_zephyr_ble_bind(struct sol_socket *s, const struct sol_network_link_addr *addr)
{
    int err;
    struct sol_socket_ble *socket = (struct sol_socket_ble *)s;

    err = bt_gatt_register(attrs, ARRAY_SIZE(attrs));
    SOL_INT_CHECK(err, < 0, NULL);

    err = bt_enable(NULL);
    SOL_INT_CHECK(err, < 0, err);

    err = bt_advertise();
    SOL_INT_CHECK(err, < 0, err);

    err = sol_ptr_vector_append(&ble_bound_sockets, socket);
    SOL_INT_CHECK_GOTO(err, < 0, append_failed);

    bt_conn_cb_register(&conn_callbacks);

    return 0;

append_failed:
    bt_le_adv_stop();
    return err;
}

const struct sol_socket_impl *
sol_socket_zephyr_ble_get_impl(void)
{
    static const struct sol_socket_impl impl = {
        .bind = sol_socket_zephyr_ble_bind,
        .join_group = sol_socket_zephyr_ble_join_group,
        .sendmsg = sol_socket_zephyr_ble_sendmsg,
        .recvmsg = sol_socket_zephyr_ble_recvmsg,
        .set_on_write = sol_socket_zephyr_ble_set_on_write,
        .set_on_read = sol_socket_zephyr_ble_set_on_read,
        .del = sol_socket_zephyr_ble_del,
        .new = sol_socket_zephyr_ble_new
    };

    return &impl;
}
