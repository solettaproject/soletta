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

#include <systemd/sd-bus.h>

#include <sol-bus.h>
#include <sol-log.h>
#include <sol-network.h>
#include <sol-bluetooth.h>
#include <sol-util-internal.h>
#include <sol-mainloop.h>
#include <sol-monitors.h>
#include <sol-gatt.h>

#include "sol-bluetooth-impl-bluez.h"

struct sol_bt_scan_pending {
    sd_bus_slot *slot;
    void (*callback)(void *user_data, const struct sol_bt_device_info *device);
    const void *user_data;
};

struct sol_bt_session {
    void (*enabled)(void *data, bool powered);
    const void *user_data;
};

enum {
    ADAPTER_PROPERTY_POWERED = 0,
};

enum {
    DEVICE_PROPERTY_ADDRESS = 0,
    DEVICE_PROPERTY_NAME,
    DEVICE_PROPERTY_PAIRED,
    DEVICE_PROPERTY_CONNECTED,
    DEVICE_PROPERTY_UUIDS,
    DEVICE_PROPERTY_RSSI,
    DEVICE_PROPERTY_SERVICES_RESOLVED,
};

enum {
    SERVICE_PROPERTY_UUID = 0,
    SERVICE_PROPERTY_PRIMARY,
    SERVICE_PROPERTY_DEVICE,
};

enum {
    CHR_PROPERTY_UUID = 0,
    CHR_PROPERTY_VALUE,
    CHR_PROPERTY_FLAGS,
};

enum {
    DESC_PROPERTY_UUID = 0,
    DESC_PROPERTY_VALUE,
    DESC_PROPERTY_FLAGS,
};

struct subscription {
    struct sol_monitors_entry base;
    const struct sol_gatt_attr *attr;
    struct sol_bt_conn *conn;
    sd_bus_slot *slot;
};

static struct context context;

static void
subscription_cleanup(const struct sol_monitors *monitors,
    const struct sol_monitors_entry *entry)
{
    struct subscription *sub = (struct subscription *)entry;

    sub->slot = sd_bus_slot_unref(sub->slot);

    if (sub->conn)
        sol_bt_conn_unref(sub->conn);
}

struct sol_monitors subscriptions =
    SOL_MONITORS_INIT_CUSTOM(struct subscription, subscription_cleanup);

struct context *
bluetooth_get_context(void)
{
    return &context;
}

static bool
adapter_property_powered_set(void *data, const char *path, sd_bus_message *m)
{
    struct context *ctx = data;
    enum adapter_state new_state;
    bool powered, changed;
    int r;

    r = sd_bus_message_read_basic(m, SD_BUS_TYPE_BOOLEAN, &powered);
    SOL_INT_CHECK_GOTO(r, < 0, skip);

    if (ctx->original_state == ADAPTER_STATE_UNKNOWN)
        ctx->original_state = powered ? ADAPTER_STATE_ON : ADAPTER_STATE_OFF;

    new_state = powered ? ADAPTER_STATE_ON : ADAPTER_STATE_OFF;
    changed = ctx->current_state != new_state;

    ctx->current_state = new_state;

    return changed;

skip:
    r = sd_bus_message_skip(m, "b");
    SOL_INT_CHECK(r, < 0, false);

    return false;
}

static const struct sol_bus_properties adapter_properties[] = {
    [ADAPTER_PROPERTY_POWERED] = {
        .member = "Powered",
        .set = adapter_property_powered_set,
    },
    { NULL, NULL }
};

static void
notify_scan_device(const struct context *ctx, const struct device_info *d)
{
    const uint64_t min_info = (1 << DEVICE_PROPERTY_ADDRESS) |
        (1 << DEVICE_PROPERTY_NAME) |
        (1 << DEVICE_PROPERTY_PAIRED);
    const struct sol_bt_device_info *info = &d->info;
    const struct sol_bt_scan_pending *scan;
    uint16_t i;

    if ((d->mask & min_info) != min_info)
        return;

    SOL_PTR_VECTOR_FOREACH_IDX (&ctx->scans, scan, i)
        scan->callback((void *)scan->user_data, info);
}

static void
adapter_property_changed(void *data, const char *path, uint64_t mask)
{
    struct context *ctx = data;

    if (!ctx->adapter_path)
        return;

    if (mask & (1 << ADAPTER_PROPERTY_POWERED)) {
        struct sol_bt_session *s;
        uint16_t i;
        bool powered;

        powered = ctx->current_state == ADAPTER_STATE_ON;

        SOL_DBG("Adapter %s powered %s", ctx->adapter_path,
            powered ? "on" : "off");

        SOL_PTR_VECTOR_FOREACH_IDX (&ctx->sessions, s, i)
            s->enabled((void *)s->user_data, powered);

        /* Also notify about current devices */
        if (powered) {
            struct device_info *d;

            SOL_PTR_VECTOR_FOREACH_IDX (&ctx->devices, d, i)
                notify_scan_device(ctx, d);
        }
    }
}

static sd_bus_message *
create_property_set(sd_bus *bus, const char *service, const char *path,
    const char *interface, const char *member)
{
    sd_bus_message *m;
    int r;

    r = sd_bus_message_new_method_call(bus, &m, service, path,
        "org.freedesktop.DBus.Properties", "Set");
    SOL_INT_CHECK(r, < 0, NULL);

    r = sd_bus_message_append(m, "ss", interface, member);
    SOL_INT_CHECK_GOTO(r, < 0, error_append);

    return m;

error_append:
    sd_bus_message_unref(m);
    return NULL;
}

static int
adapter_set_powered(struct context *ctx, const char *path, bool powered)
{
    sd_bus *bus = sol_bus_client_get_bus(ctx->bluez);
    const char *service = sol_bus_client_get_service(ctx->bluez);
    sd_bus_message *m;
    int r;

    m = create_property_set(bus, service, path,
        "org.bluez.Adapter1", "Powered");
    SOL_NULL_CHECK(m, -ENOMEM);

    r = sd_bus_message_open_container(m, 'v', "b");
    SOL_INT_CHECK_GOTO(r, < 0, done);

    r = sd_bus_message_append(m, "b", &powered);
    SOL_INT_CHECK_GOTO(r, < 0, done);

    r = sd_bus_message_close_container(m);
    SOL_INT_CHECK_GOTO(r, < 0, done);

    r = sd_bus_call_async(bus, NULL, m, sol_bus_log_callback, NULL, 0);
    SOL_INT_CHECK_GOTO(r, < 0, done);

done:
    sd_bus_message_unref(m);

    return r;
}

static void
adapter_appeared(void *data, const char *path)
{
    struct context *ctx = data;
    int r;

    if (ctx->adapter_path)
        return;

    ctx->adapter_path = strdup(path);
    SOL_NULL_CHECK(ctx->adapter_path);

    r = sol_bus_map_cached_properties(ctx->bluez, path,
        "org.bluez.Adapter1",
        adapter_properties,
        adapter_property_changed, ctx);
    SOL_INT_CHECK_GOTO(r, < 0, error_map);

    if (sol_ptr_vector_get_len(&ctx->sessions) > 0) {
        r = adapter_set_powered(ctx, ctx->adapter_path, true);
        SOL_INT_CHECK(r, < 0);
    }

    return;

error_map:
    free(ctx->adapter_path);
    ctx->adapter_path = NULL;
}

static void
destroy_attr(struct sol_gatt_attr *attr)
{
    free(attr->_priv);
    free(attr);
}

static void
destroy_device(struct device_info *device)
{
    struct sol_bt_device_info *info;
    struct sol_gatt_attr *attr;
    uint16_t i;

    info = &device->info;
    free(info->name);
    sol_vector_clear(&info->uuids);

    SOL_PTR_VECTOR_FOREACH_IDX (&device->attrs, attr, i) {
        destroy_attr(attr);
    }
    sol_ptr_vector_clear(&device->attrs);

    free(device->path);
    free(device);
}

static struct device_info *
find_device_by_path(struct context *ctx, const char *path)
{
    struct device_info *d;
    uint16_t i;

    SOL_NULL_CHECK(path, NULL);

    SOL_PTR_VECTOR_FOREACH_IDX (&ctx->devices, d, i) {
        if (streq(d->path, path))
            return d;
    }
    return NULL;
}

static void
adapter_removed(void *data, const char *path)
{

}

static bool
device_property_address_set(void *data, const char *path, sd_bus_message *m)
{
    struct context *ctx = data;
    struct device_info *d;
    struct sol_bt_device_info *info;
    const char *address;
    int r;

    d = find_device_by_path(ctx, path);
    SOL_NULL_CHECK_GOTO(d, skip);

    info = &d->info;
    if (info->addr.family)
        goto skip;

    r = sd_bus_message_read_basic(m, 's', &address);
    SOL_INT_CHECK_GOTO(r, < 0, skip);

    sol_network_link_addr_from_str(&info->addr, address);

    return true;

skip:
    r = sd_bus_message_skip(m, "s");
    SOL_INT_CHECK(r, < 0, false);

    return false;
}

static bool
device_property_name_set(void *data, const char *path, sd_bus_message *m)
{
    struct context *ctx = data;
    struct device_info *d;
    struct sol_bt_device_info *info;
    const char *name;
    int r;

    d = find_device_by_path(ctx, path);
    SOL_NULL_CHECK_GOTO(d, skip);

    info = &d->info;

    r = sd_bus_message_read_basic(m, 's', &name);
    SOL_INT_CHECK_GOTO(r, < 0, skip);

    free(info->name);

    /* When we receive the property again we will try to allocate again */
    info->name = strdup(name);

    return true;

skip:
    r = sd_bus_message_skip(m, "s");
    SOL_INT_CHECK(r, < 0, false);

    return false;
}

static bool
device_property_paired_set(void *data, const char *path, sd_bus_message *m)
{
    struct context *ctx = data;
    struct device_info *d;
    struct sol_bt_device_info *info;
    bool paired;
    int r;

    d = find_device_by_path(ctx, path);
    SOL_NULL_CHECK_GOTO(d, skip);

    info = &d->info;

    r = sd_bus_message_read_basic(m, 'b', &paired);
    SOL_INT_CHECK_GOTO(r, < 0, skip);

    info->paired = paired;

    return true;

skip:
    r = sd_bus_message_skip(m, "b");
    SOL_INT_CHECK(r, < 0, false);

    return false;
}

static struct sol_gatt_attr *
find_attr(const struct device_info *d, const char *path)
{
    struct sol_gatt_attr *attr;
    uint16_t idx;

    SOL_PTR_VECTOR_FOREACH_IDX (&d->attrs, attr, idx) {
        if (!streq(attr->_priv, path))
            continue;

        return attr;
    }

    return NULL;
}

static bool
device_property_connected_set(void *data, const char *path, sd_bus_message *m)
{
    struct context *ctx = data;
    struct device_info *d;
    struct sol_bt_device_info *info;
    bool connected;
    int r;

    d = find_device_by_path(ctx, path);
    SOL_NULL_CHECK_GOTO(d, skip);

    info = &d->info;

    r = sd_bus_message_read_basic(m, 'b', &connected);
    SOL_INT_CHECK_GOTO(r, < 0, skip);

    info->connected = connected;
    if (connected == true)
        info->in_range = true;

    return true;

skip:
    r = sd_bus_message_skip(m, "b");
    SOL_INT_CHECK(r, < 0, false);

    return false;
}

static bool
device_property_uuids_set(void *data, const char *path, sd_bus_message *m)
{
    struct context *ctx = data;
    struct device_info *d;
    struct sol_bt_device_info *info;
    struct sol_bt_uuid *u = NULL;
    const char *uuid = NULL;
    int r;

    d = find_device_by_path(ctx, path);
    SOL_NULL_CHECK_GOTO(d, skip);

    info = &d->info;

    sol_vector_clear(&info->uuids);

    r = sd_bus_message_enter_container(m, 'a', "s");
    SOL_INT_CHECK_GOTO(r, < 0, done);

    while (sd_bus_message_read_basic(m, 's', &uuid) > 0) {
        u = sol_vector_append(&info->uuids);
        SOL_NULL_CHECK_GOTO(u, done);

        /* In practice, all UUIDs will be 36 bytes long, just being careful. */
        r = sol_bt_uuid_from_str(u, sol_str_slice_from_str(uuid));
        SOL_INT_CHECK_GOTO(r, < 0, done);
    }

    r = sd_bus_message_exit_container(m);

done:
    if (!u || r < 0)
        sol_vector_clear(&info->uuids);

    return true;

skip:
    r = sd_bus_message_skip(m, "as");
    SOL_INT_CHECK(r, < 0, false);

    return false;
}

static bool
device_property_rssi_set(void *data, const char *path, sd_bus_message *m)
{
    struct context *ctx = data;
    struct device_info *d;
    struct sol_bt_device_info *info;
    int16_t rssi;
    int r;

    d = find_device_by_path(ctx, path);
    SOL_NULL_CHECK_GOTO(d, skip);

    info = &d->info;

    r = sd_bus_message_read_basic(m, 'n', &rssi);
    SOL_INT_CHECK_GOTO(r, < 0, skip);

    info->rssi = rssi;
    info->in_range = true;

    return true;

skip:
    r = sd_bus_message_skip(m, "n");
    SOL_INT_CHECK(r, < 0, false);

    return false;
}

static bool
device_property_services_resolved_set(void *data, const char *path, sd_bus_message *m)
{
    struct context *ctx = data;
    struct device_info *d;
    bool resolved;
    int r;

    d = find_device_by_path(ctx, path);
    SOL_NULL_CHECK_GOTO(d, skip);

    r = sd_bus_message_read_basic(m, 'b', &resolved);
    SOL_INT_CHECK_GOTO(r, < 0, skip);

    r = d->resolved != resolved;

    d->resolved = resolved;

    return r;

skip:
    r = sd_bus_message_skip(m, "b");
    SOL_INT_CHECK(r, < 0, false);

    return false;
}

static const struct sol_bus_properties device_properties[] = {
    [DEVICE_PROPERTY_ADDRESS] = {
        .member = "Address",
        .set = device_property_address_set,
    },
    [DEVICE_PROPERTY_NAME] = {
        .member = "Name",
        .set = device_property_name_set,
    },
    [DEVICE_PROPERTY_PAIRED] = {
        .member = "Paired",
        .set = device_property_paired_set,
    },
    [DEVICE_PROPERTY_CONNECTED] = {
        .member = "Connected",
        .set = device_property_connected_set,
    },
    [DEVICE_PROPERTY_UUIDS] = {
        .member = "UUIDs",
        .set = device_property_uuids_set,
    },
    [DEVICE_PROPERTY_RSSI] = {
        .member = "RSSI",
        .set = device_property_rssi_set,
    },
    [DEVICE_PROPERTY_SERVICES_RESOLVED] = {
        .member = "ServicesResolved",
        .set = device_property_services_resolved_set,
    },
    { NULL, NULL }
};

static bool
attr_property_uuid_set(void *data, const char *path, sd_bus_message *m)
{
    struct sol_gatt_attr *attr = data;
    const char *str;
    int r;

    r = sd_bus_message_read_basic(m, 's', &str);
    SOL_INT_CHECK_GOTO(r, < 0, skip);

    r = sol_bt_uuid_from_str(&attr->uuid, sol_str_slice_from_str(str));
    SOL_INT_CHECK_GOTO(r, < 0, skip);

    return false;

skip:
    r = sd_bus_message_skip(m, "s");
    SOL_INT_CHECK(r, < 0, false);

    return false;
}

static const struct sol_bus_properties service_properties[] = {
    [SERVICE_PROPERTY_UUID] = {
        .member = "UUID",
        .set = attr_property_uuid_set,
    },
    { NULL, NULL }
};

static bool
attr_property_value_set(void *data, const char *path, sd_bus_message *m)
{
    const struct sol_gatt_attr *attr = data;
    struct subscription *sub;
    struct sol_buffer buf;
    const void *buf_data;
    size_t len;
    uint16_t idx;
    int r;

    r = sd_bus_message_read_array(m, 'y', &buf_data, &len);
    SOL_INT_CHECK_GOTO(r, < 0, skip);

    sol_buffer_init_flags(&buf, (void *)buf_data, len,
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    buf.used = len;

    SOL_MONITORS_WALK (&subscriptions, sub, idx) {
        if (sub->attr != attr)
            continue;

        if (!(((bool (*)(void *, const struct sol_gatt_attr *, const struct sol_buffer *))
            sub->base.cb)((void *)sub->base.data, attr, &buf)))
            sol_monitors_del(&subscriptions, idx);
    }

    return false;

skip:
    r = sd_bus_message_skip(m, "ay");
    SOL_INT_CHECK(r, < 0, false);

    return false;
}

static bool
attr_property_flags_set(void *data, const char *path, sd_bus_message *m)
{
    struct sol_gatt_attr *attr = data;

    attr->flags = dbus_string_array_to_flags(attr->type, m);

    return false;
}

static const struct sol_bus_properties attr_properties[] = {
    [CHR_PROPERTY_UUID] = {
        .member = "UUID",
        .set = attr_property_uuid_set,
    },
    [CHR_PROPERTY_VALUE] = {
        .member = "Value",
        .set = attr_property_value_set,
    },
    [CHR_PROPERTY_FLAGS] = {
        .member = "Flags",
        .set = attr_property_flags_set,
    },
    { NULL, NULL }
};

static void
destroy_conn(struct sol_bt_conn *conn)
{
    struct subscription *sub;
    uint16_t idx;

    if (conn->on_disconnect)
        conn->on_disconnect((void *)conn->user_data, conn);

    if (conn->slot)
        sd_bus_slot_unref(conn->slot);

    SOL_MONITORS_WALK (&subscriptions, sub, idx) {
        if (sub->conn == conn)
            sol_monitors_del(&subscriptions, idx);
    }

    free(conn);
}

static void
trigger_bt_conn(struct context *ctx, struct device_info *d, bool connected)
{
    struct sol_bt_conn *conn;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&ctx->conns, conn, i) {
        if (conn->d != d)
            continue;

        if (connected && conn->on_connect)
            conn->on_connect((void *)conn->user_data, conn);

        if (!connected) {
            sol_ptr_vector_del(&ctx->conns, i);
            destroy_conn(conn);
        }
    }
}

void
destroy_pending_discovery(struct pending_discovery *disc)
{
    sol_bt_conn_unref(disc->conn);
    free(disc);
}

void
trigger_gatt_discover(struct pending_discovery *disc)
{
    const struct sol_gatt_attr *attr, *parent = disc->parent;
    const struct device_info *d = disc->conn->d;
    const struct sol_bt_uuid *uuid = disc->uuid;
    enum sol_gatt_attr_type type = disc->type;
    bool found = false, finished = false;
    uint16_t idx;

    SOL_PTR_VECTOR_FOREACH_IDX (&d->attrs, attr, idx) {
        if (parent && !found) {
            if (attr != parent)
                continue;

            found = true;
        }

        if (found && attr != parent)
            if (attr->type == parent->type)
                break;

        if (type != SOL_GATT_ATTR_TYPE_INVALID && attr->type != type)
            continue;

        if (uuid && !sol_bt_uuid_eq(&attr->uuid, uuid))
            continue;

        if (!disc->func((void *)disc->user_data, disc->conn, attr)) {
            /* The user terminated the discover procedure. */
            finished = true;
            break;
        }
    }

    /* we may want to inform the user that there are no more attributes. */
    if (!finished)
        disc->func((void *)disc->user_data, disc->conn, NULL);
}

static void
device_property_changed(void *data, const char *path, uint64_t mask)
{
    struct context *ctx = data;
    struct device_info *d;
    struct sol_bt_device_info *info;
    struct pending_discovery *disc;
    uint16_t idx;

    d = find_device_by_path(ctx, path);
    SOL_NULL_CHECK(d);

    info = &d->info;

    d->mask |= mask;

    /* If the device changed connection state */
    if (mask & (1 << DEVICE_PROPERTY_CONNECTED))
        trigger_bt_conn(ctx, d, info->connected);

    if (info->connected && d->resolved) {
        SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&d->pending_discoveries, disc, idx) {
            trigger_gatt_discover(disc);
            destroy_pending_discovery(disc);
            sol_ptr_vector_del(&d->pending_discoveries, idx);
        }
    }

    notify_scan_device(ctx, d);
}

static void
device_appeared(void *data, const char *path)
{
    struct context *ctx = data;
    struct device_info *d;
    struct sol_bt_device_info *info;
    int r;

    d = find_device_by_path(ctx, path);
    if (d)
        return;

    d = calloc(1, sizeof(*d));
    SOL_NULL_CHECK(d);

    info = &d->info;

    sol_ptr_vector_init(&d->attrs);
    sol_ptr_vector_init(&d->pending_discoveries);

    sol_vector_init(&info->uuids, sizeof(struct sol_bt_uuid));
    d->path = strdup(path);
    SOL_NULL_CHECK_GOTO(d->path, error_dup);

    r = sol_bus_map_cached_properties(ctx->bluez, path, "org.bluez.Device1",
        device_properties, device_property_changed, ctx);
    SOL_INT_CHECK_GOTO(r, < 0, error_map);

    r = sol_ptr_vector_append(&ctx->devices, d);
    SOL_INT_CHECK_GOTO(r, < 0, error_append);

    return;

error_append:
    sol_bus_unmap_cached_properties(ctx->bluez, device_properties, ctx);
error_map:
    free(d->path);
error_dup:
    free(d);
}

static void
device_removed(void *data, const char *path)
{
    struct context *ctx = data;
    struct sol_bt_conn *conn;
    struct device_info *d;
    struct pending_discovery *disc;
    uint16_t idx;

    d = find_device_by_path(ctx, path);
    SOL_NULL_CHECK(d);

    SOL_PTR_VECTOR_FOREACH_IDX (&d->pending_discoveries, disc, idx) {
        disc->func((void *)disc->user_data, NULL, NULL);
        destroy_pending_discovery(disc);
    }
    sol_ptr_vector_clear(&d->pending_discoveries);

    /* Also remove the connections that this device may have still. */
    SOL_PTR_VECTOR_FOREACH_REVERSE_IDX (&ctx->conns, conn, idx) {
        if (conn->d != d)
            continue;

        destroy_conn(conn);
        sol_ptr_vector_del(&ctx->conns, idx);
    }

    destroy_device(d);

    sol_ptr_vector_remove(&ctx->devices, d);
}

static struct device_info *
match_device_by_prefix(struct context *ctx, const char *path)
{
    struct device_info *d;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&ctx->devices, d, i) {
        if (strstartswith(path, d->path))
            return d;
    }
    return NULL;
}

static void
service_property_changed(void *data, const char *path, uint64_t mask)
{

}

static int
remote_attr_read_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct sol_gatt_pending *op = userdata;
    struct sol_buffer buf = SOL_BUFFER_INIT_EMPTY;
    const void *data = NULL;
    size_t len = 0;
    int r;

    op->slot = sd_bus_slot_unref(op->slot);

    if (sol_bus_log_callback(m, userdata, ret_error)) {
        r = -EINVAL;
        goto done;
    }

    r = sd_bus_message_read_array(m, 'y', &data, &len);
    SOL_INT_CHECK_GOTO(r, < 0, done);

    sol_buffer_init_flags(&buf, (void *)data, len,
        SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
    buf.used = len;

done:
    sol_gatt_pending_reply(op, r, &buf);

    return r;
}

static int
remote_attr_read(struct sol_gatt_pending *op,
    uint16_t offset)
{
    struct context *ctx = bluetooth_get_context();
    sd_bus *bus = sol_bus_client_get_bus(ctx->bluez);
    const char *service = sol_bus_client_get_service(ctx->bluez);
    const struct sol_gatt_attr *attr = op->attr;
    const char *interface, *path = attr->_priv;
    int r;

    if (attr->type == SOL_GATT_ATTR_TYPE_DESCRIPTOR)
        interface = "org.bluez.GattDescriptor1";
    else
        interface = "org.bluez.GattCharacteristic1";

    r = sd_bus_call_method_async(bus, &op->slot, service, path,
        interface, "ReadValue", remote_attr_read_reply, op, NULL);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
remote_attr_write_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct sol_gatt_pending *op = userdata;
    int r = 0;

    op->slot = sd_bus_slot_unref(op->slot);

    if (sol_bus_log_callback(m, userdata, ret_error))
        r = -EINVAL;

    sol_gatt_pending_reply(op, r, NULL);

    return r;
}

static int
remote_attr_write(struct sol_gatt_pending *op,
    struct sol_buffer *buf,
    uint16_t offset)
{
    struct context *ctx = bluetooth_get_context();
    sd_bus *bus = sol_bus_client_get_bus(ctx->bluez);
    const char *service = sol_bus_client_get_service(ctx->bluez);
    const struct sol_gatt_attr *attr = op->attr;
    const char *interface, *path = attr->_priv;
    sd_bus_message *m;
    int r;

    if (attr->type == SOL_GATT_ATTR_TYPE_DESCRIPTOR)
        interface = "org.bluez.GattDescriptor1";
    else
        interface = "org.bluez.GattCharacteristic1";

    r = sd_bus_message_new_method_call(bus, &m, service, path,
        interface, "WriteValue");
    SOL_INT_CHECK_GOTO(r, < 0, done);

    r = sd_bus_message_append_array(m, 'y', buf->data, buf->used);
    SOL_INT_CHECK_GOTO(r, < 0, done);

    r = sd_bus_call_async(bus, &op->slot, m, remote_attr_write_reply, op, 0);
    SOL_INT_CHECK_GOTO(r, < 0, done);

done:
    sd_bus_message_unref(m);

    return r;
}

static struct sol_gatt_attr *
new_attr(enum sol_gatt_attr_type type, const char *path)
{
    struct sol_gatt_attr *attr;

    attr = calloc(1, sizeof(*attr));
    SOL_NULL_CHECK(attr, NULL);

    attr->type = type;
    attr->read = remote_attr_read;
    attr->write = remote_attr_write;

    attr->_priv = strdup(path);
    SOL_NULL_CHECK_GOTO(attr->_priv, error_dup);

    return attr;

error_dup:
    free(attr);
    return NULL;
}

static void
service_appeared(void *data, const char *path)
{
    struct context *ctx = data;
    struct device_info *d;
    struct sol_gatt_attr *attr;
    int r;

    d = match_device_by_prefix(ctx, path);
    SOL_NULL_CHECK(d);

    attr = new_attr(SOL_GATT_ATTR_TYPE_SERVICE, path);
    SOL_NULL_CHECK(attr);

    r = sol_ptr_vector_append(&d->attrs, attr);
    SOL_INT_CHECK_GOTO(r, < 0, error_append);

    r = sol_bus_map_cached_properties(ctx->bluez, path,
        "org.bluez.GattService1",
        service_properties,
        service_property_changed, attr);
    SOL_INT_CHECK_GOTO(r, < 0, error_map);

    return;

error_map:
    sol_ptr_vector_del_last(&d->attrs);

error_append:
    destroy_attr(attr);
}

static void
attr_property_changed(void *data, const char *path, uint64_t mask)
{

}

static void
chr_appeared(void *data, const char *path)
{
    struct context *ctx = data;
    struct device_info *d;
    struct sol_gatt_attr *attr;
    int r;

    d = match_device_by_prefix(ctx, path);
    SOL_NULL_CHECK(d);

    attr = new_attr(SOL_GATT_ATTR_TYPE_CHARACTERISTIC, path);
    SOL_NULL_CHECK(attr);

    r = sol_ptr_vector_append(&d->attrs, attr);
    SOL_INT_CHECK_GOTO(r, < 0, error_append);

    r = sol_bus_map_cached_properties(ctx->bluez, path,
        "org.bluez.GattCharacteristic1",
        attr_properties,
        attr_property_changed, attr);
    SOL_INT_CHECK_GOTO(r, < 0, error_map);

    return;

error_map:
    sol_ptr_vector_del_last(&d->attrs);

error_append:
    destroy_attr(attr);
}

static void
attr_removed(void *data, const char *path)
{
    struct context *ctx = data;
    struct device_info *d;
    struct sol_gatt_attr *attr;
    int r;

    d = match_device_by_prefix(ctx, path);
    SOL_NULL_CHECK(d);

    attr = find_attr(d, path);
    SOL_NULL_CHECK(attr);

    r = sol_bus_unmap_cached_properties(ctx->bluez, attr_properties, attr);
    SOL_INT_CHECK(r, < 0);

    sol_ptr_vector_remove(&d->attrs, attr);
    destroy_attr(attr);
}

static void
desc_appeared(void *data, const char *path)
{
    struct context *ctx = data;
    struct device_info *d;
    struct sol_gatt_attr *attr;
    int r;

    d = match_device_by_prefix(ctx, path);
    SOL_NULL_CHECK(d);

    attr = new_attr(SOL_GATT_ATTR_TYPE_DESCRIPTOR, path);
    SOL_NULL_CHECK(attr);

    r = sol_ptr_vector_append(&d->attrs, attr);
    SOL_INT_CHECK_GOTO(r, < 0, error_append);

    r = sol_bus_map_cached_properties(ctx->bluez, path,
        "org.bluez.GattDescriptor1",
        attr_properties,
        attr_property_changed, attr);
    SOL_INT_CHECK_GOTO(r, < 0, error_map);

    return;

error_map:
    sol_ptr_vector_del_last(&d->attrs);

error_append:
    destroy_attr(attr);
}

static const struct sol_bus_interfaces interfaces[] = {
    { .name = "org.bluez.Adapter1",
      .appeared = adapter_appeared,
      .removed = adapter_removed },
    { .name = "org.bluez.Device1",
      .appeared = device_appeared,
      .removed = device_removed },
    { .name = "org.bluez.GattService1",
      .appeared = service_appeared,
      .removed = attr_removed },
    { .name = "org.bluez.GattCharacteristic1",
      .appeared = chr_appeared,
      .removed = attr_removed },
    { .name = "org.bluez.GattDescriptor1",
      .appeared = desc_appeared,
      .removed = attr_removed },
    { NULL }
};

SOL_API struct sol_bt_conn *
sol_bt_conn_ref(struct sol_bt_conn *conn)
{
    SOL_NULL_CHECK(conn, NULL);

    conn->ref++;

    return conn;
}

SOL_API void
sol_bt_conn_unref(struct sol_bt_conn *conn)
{
    struct context *ctx;

    if (!conn)
        return;

    conn->ref--;

    if (conn->ref > 0)
        return;

    ctx = bluetooth_get_context();

    sol_ptr_vector_remove(&ctx->conns, conn);
    destroy_conn(conn);
}

SOL_API const struct sol_network_link_addr *
sol_bt_conn_get_addr(const struct sol_bt_conn *conn)
{
    SOL_NULL_CHECK(conn, NULL);

    return &conn->d->info.addr;
}

static void
bluez_service_connected(void *data, const char *unique)
{
    struct context *ctx = data;
    const char *mine;
    int r;

    SOL_DBG("BlueZ service connected (%s)", unique);

    r = sd_bus_get_unique_name(ctx->system_bus, &mine);
    SOL_INT_CHECK(r, < 0);

    SOL_DBG("Connected to system bus as %s", mine);

    sol_ptr_vector_init(&ctx->devices);

    sol_bus_watch_interfaces(ctx->bluez, interfaces, ctx);
}

static void
bluez_service_disconnected(void *data)
{
    struct context *ctx = data;
    struct device_info *d;
    struct sol_bt_session *s;
    struct sol_bt_scan_pending *p;
    struct sol_bt_conn *conn;
    uint16_t idx;

    ctx->original_state = ADAPTER_STATE_UNKNOWN;
    ctx->current_state = ADAPTER_STATE_UNKNOWN;

    sol_monitors_clear(&subscriptions);

    sol_bus_remove_interfaces_watch(ctx->bluez, interfaces, ctx);

    SOL_PTR_VECTOR_FOREACH_IDX (&ctx->scans, p, idx)
        free(p);
    sol_ptr_vector_clear(&ctx->scans);

    SOL_PTR_VECTOR_FOREACH_IDX (&ctx->conns, conn, idx)
        destroy_conn(conn);
    sol_ptr_vector_clear(&ctx->conns);

    SOL_PTR_VECTOR_FOREACH_IDX (&ctx->devices, d, idx)
        destroy_device(d);
    sol_ptr_vector_clear(&ctx->devices);

    SOL_PTR_VECTOR_FOREACH_IDX (&ctx->sessions, s, idx) {
        s->enabled((void *)s->user_data, false);
        free(s);
    }
    sol_ptr_vector_clear(&ctx->sessions);

    clear_applications();

    free(ctx->adapter_path);
    ctx->adapter_path = NULL;
}

static int
watch_bluez(void)
{
    int r;

    if (context.bluez)
        goto watch;

    sol_ptr_vector_init(&context.sessions);
    sol_ptr_vector_init(&context.scans);
    sol_ptr_vector_init(&context.conns);

    context.system_bus = sol_bus_get(NULL);
    SOL_NULL_CHECK(context.system_bus, -EINVAL);

    context.bluez = sol_bus_client_new(context.system_bus, "org.bluez");
    SOL_NULL_CHECK(context.bluez, -EINVAL);

watch:
    r = sol_bus_client_set_connect_handler(context.bluez,
        bluez_service_connected, &context);
    SOL_INT_CHECK(r, < 0, -EINVAL);

    r = sol_bus_client_set_disconnect_handler(context.bluez,
        bluez_service_disconnected,
        &context);
    SOL_INT_CHECK(r, < 0, -EINVAL);

    return 0;
}

static struct device_info *
find_device_by_addr(struct context *ctx,
    const struct sol_network_link_addr *addr)
{
    struct device_info *d;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&ctx->devices, d, i) {
        const struct sol_bt_device_info *info = &d->info;

        if (sol_network_link_addr_eq(addr, &info->addr))
            return d;
    }

    return NULL;
}

static int
connect_reply(sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct context *ctx = &context;
    struct sol_bt_conn *conn = userdata;
    int err, r;

    conn->slot = sd_bus_slot_unref(conn->slot);

    r = sol_bus_log_callback(reply, userdata, ret_error);

    err = sd_bus_error_get_errno(ret_error);

    if (!err)
        err = r;

    if (err) {
        conn->on_error((void *)conn->user_data, err);
        sol_ptr_vector_remove(&ctx->conns, conn);

        /* Don't call on_disconnect on errors */
        conn->on_disconnect = NULL;
        destroy_conn(conn);
        return -err;
    }

    /* Will notify the 'conn' when the property changes */

    return 0;
}

static bool
already_connected(void *data)
{
    struct context *ctx = &context;
    struct sol_bt_conn *conn = data;
    struct device_info *d = conn->d;

    trigger_bt_conn(ctx, d, true);

    return false;
}

SOL_API struct sol_bt_conn *
sol_bt_connect(const struct sol_network_link_addr *addr,
    bool (*on_connect)(void *user_data, struct sol_bt_conn *conn),
    void (*on_disconnect)(void *user_data, struct sol_bt_conn *conn),
    void (*on_error)(void *user_data, int error),
    const void *user_data)
{
    struct context *ctx = &context;
    sd_bus *bus = sol_bus_client_get_bus(ctx->bluez);
    const char *service = sol_bus_client_get_service(ctx->bluez);
    struct device_info *d;
    struct sol_bt_conn *conn;
    int r;

    SOL_NULL_CHECK(addr, NULL);
    SOL_NULL_CHECK(on_connect, NULL);
    SOL_NULL_CHECK(on_disconnect, NULL);
    SOL_NULL_CHECK(on_error, NULL);
    SOL_INT_CHECK((int)ctx->current_state, != ADAPTER_STATE_ON, NULL);

    d = find_device_by_addr(ctx, addr);
    SOL_NULL_CHECK(d, NULL);

    conn = calloc(1, sizeof(*conn));
    SOL_NULL_CHECK(conn, NULL);

    conn->d = d;
    conn->on_connect = on_connect;
    conn->on_disconnect = on_disconnect;
    conn->on_error = on_error;
    conn->user_data = user_data;
    conn->ref = 1;

    r = sol_ptr_vector_append(&ctx->conns, conn);
    SOL_INT_CHECK_GOTO(r, < 0, error_append);

    if (d->info.connected) {
        sol_timeout_add(0, already_connected, conn);
        return conn;
    }

    r = sd_bus_call_method_async(bus, &conn->slot, service, d->path,
        "org.bluez.Device1", "Connect", connect_reply, conn, NULL);
    SOL_INT_CHECK_GOTO(r, < 0, error_call);

    return conn;

error_call:
    sol_ptr_vector_remove(&ctx->conns, conn);

error_append:
    free(conn);
    return NULL;
}

SOL_API int
sol_bt_disconnect(struct sol_bt_conn *conn)
{
    struct context *ctx = &context;
    const char *service = sol_bus_client_get_service(ctx->bluez);
    sd_bus *bus = sol_bus_client_get_bus(ctx->bluez);
    struct device_info *d;
    int r;

    SOL_NULL_CHECK(conn, -EINVAL);
    SOL_INT_CHECK((int)ctx->current_state, != ADAPTER_STATE_ON, -EINVAL);

    r = sol_ptr_vector_remove(&ctx->conns, conn);
    SOL_INT_CHECK(r, < 0, -ENOENT);

    d = conn->d;

    /* Don't want to trigger on_disconnect() when actively called. */
    conn->on_disconnect = NULL;
    destroy_conn(conn);

    r = sd_bus_call_method_async(bus, NULL, service, d->path,
        "org.bluez.Device1", "Disconnect", sol_bus_log_callback, NULL, NULL);
    SOL_INT_CHECK(r, < 0, -EINVAL);

    return 0;
}

SOL_API struct sol_bt_session *
sol_bt_enable(void (*enabled)(void *data, bool powered), const void *user_data)
{
    struct sol_bt_session *session;
    int r;

    r = watch_bluez();
    SOL_INT_CHECK(r, < 0, NULL);

    session = calloc(1, sizeof(*session));
    SOL_NULL_CHECK(session, NULL);

    r = sol_ptr_vector_append(&context.sessions, session);
    SOL_INT_CHECK_GOTO(r, < 0, error_append);

    if (sol_ptr_vector_get_len(&context.sessions) == 1
        && context.adapter_path) {
        r = adapter_set_powered(&context, context.adapter_path, true);
        SOL_INT_CHECK_GOTO(r, < 0, error_set_powered);
    }

    session->enabled = enabled;
    session->user_data = user_data;

    if (context.current_state == ADAPTER_STATE_ON)
        session->enabled((void *)session->user_data, true);

    return session;

error_set_powered:
    sol_ptr_vector_remove(&context.sessions, session);

error_append:
    free(session);
    return NULL;
}

SOL_API int
sol_bt_disable(struct sol_bt_session *session)
{
    int r;
    bool powered;

    r = watch_bluez();
    SOL_INT_CHECK(r, < 0, r);

    if (!context.adapter_path)
        return -ENOTCONN; /* FIXME: Not ready? */

    r = sol_ptr_vector_remove(&context.sessions, session);
    SOL_INT_CHECK(r, < 0, -ENOENT);

    free(session);

    if (sol_ptr_vector_get_len(&context.sessions) > 0)
        return 0;

    /* Return the controller to its original state. */
    powered = context.original_state == ADAPTER_STATE_ON;

    r = adapter_set_powered(&context, context.adapter_path, powered);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static int
start_discovery_reply(sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct sol_bt_scan_pending *scan = userdata;

    scan->slot = sd_bus_slot_unref(scan->slot);

    sol_bus_log_callback(reply, userdata, ret_error);

    return 0;
}

SOL_API struct sol_bt_scan_pending *
sol_bt_start_scan(enum sol_bt_transport transport,
    void (*cb)(void *user_data, const struct sol_bt_device_info *device),
    const void *user_data)
{
    struct context *ctx = &context;
    sd_bus *bus = sol_bus_client_get_bus(ctx->bluez);
    const char *service = sol_bus_client_get_service(ctx->bluez);
    struct sol_bt_scan_pending *scan;
    int r;

    SOL_NULL_CHECK(cb, NULL);
    SOL_NULL_CHECK(ctx->adapter_path, NULL);
    SOL_INT_CHECK((int)ctx->current_state, != ADAPTER_STATE_ON, NULL);

    scan = calloc(1, sizeof(*scan));
    SOL_NULL_CHECK(scan, NULL);

    r = sol_ptr_vector_append(&ctx->scans, scan);
    SOL_INT_CHECK_GOTO(r, < 0, error_append);

    scan->callback = cb;
    scan->user_data = (void *)user_data;

    if (sol_ptr_vector_get_len(&ctx->scans) > 1)
        return scan;

    r = sd_bus_call_method_async(bus, &scan->slot, service, ctx->adapter_path,
        "org.bluez.Adapter1", "StartDiscovery", start_discovery_reply, scan, NULL);
    SOL_INT_CHECK_GOTO(r, < 0, error_call);

    return scan;

error_call:
    sol_ptr_vector_remove(&ctx->scans, scan);

error_append:
    free(scan);

    return NULL;
}

static void
reset_devices_in_range(struct context *ctx)
{
    struct device_info *d;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&ctx->devices, d, i) {
        d->info.in_range = false;
    }
}

SOL_API int
sol_bt_stop_scan(struct sol_bt_scan_pending *scan)
{
    struct context *ctx = &context;
    sd_bus *bus = sol_bus_client_get_bus(ctx->bluez);
    const char *service = sol_bus_client_get_service(ctx->bluez);
    int r;

    SOL_NULL_CHECK(scan, -EINVAL);

    r = sol_ptr_vector_remove(&ctx->scans, scan);
    SOL_INT_CHECK(r, < 0, -ENOENT);

    scan->slot = sd_bus_slot_unref(scan->slot);
    free(scan);

    if (sol_ptr_vector_get_len(&ctx->scans) > 0)
        return 0;

    /* We stopped the scan, set all devices to out of range */
    reset_devices_in_range(ctx);

    r = sd_bus_call_method_async(bus, NULL, service, ctx->adapter_path,
        "org.bluez.Adapter1", "StopDiscovery", sol_bus_log_callback, NULL, NULL);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static uint16_t
find_subscription_by_attr(const struct sol_gatt_attr *attr)
{
    struct subscription *sub;
    uint16_t idx;

    SOL_MONITORS_WALK (&subscriptions, sub, idx) {
        if (sub->attr == attr) {
            return idx;
        }
    }

    return UINT16_MAX;
}

static int
start_notify_reply(sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    const struct sol_gatt_attr *attr = userdata;
    struct subscription *sub;
    uint16_t idx;

    SOL_MONITORS_WALK (&subscriptions, sub, idx) {
        if (sub->attr == attr)
            sub->slot = sd_bus_slot_unref(sub->slot);
    }

    return sol_bus_log_callback(reply, userdata, ret_error);
}

SOL_API int
sol_gatt_subscribe(struct sol_bt_conn *conn, const struct sol_gatt_attr *attr,
    bool (*cb)(void *user_data, const struct sol_gatt_attr *attr,
    const struct sol_buffer *buffer),
    const void *user_data)
{
    struct context *ctx = &context;
    sd_bus *bus = sol_bus_client_get_bus(ctx->bluez);
    const char *service = sol_bus_client_get_service(ctx->bluez);
    struct subscription *sub;
    int r;
    uint16_t idx;

    SOL_NULL_CHECK(attr, -EINVAL);
    SOL_NULL_CHECK(cb, -EINVAL);
    SOL_NULL_CHECK(conn, -EINVAL);
    SOL_INT_CHECK(attr->type, != SOL_GATT_ATTR_TYPE_CHARACTERISTIC, -EINVAL);

    if (!(attr->flags & (SOL_GATT_CHR_FLAGS_NOTIFY | SOL_GATT_CHR_FLAGS_INDICATE))) {
        SOL_WRN("Attribute doesn't support Notifications/Indications");
        return -EINVAL;
    }

    idx = find_subscription_by_attr(attr);

    sub = sol_monitors_append(&subscriptions, (sol_monitors_cb_t)cb, user_data);
    SOL_NULL_CHECK(sub, -ENOMEM);

    sub->conn = sol_bt_conn_ref(conn);
    sub->attr = attr;

    /* There's another subscription for this attribute. */
    if (idx != UINT16_MAX)
        return 0;

    r = sd_bus_call_method_async(bus, &sub->slot, service, attr->_priv,
        "org.bluez.GattCharacteristic1", "StartNotify", start_notify_reply, (void *)attr, NULL);
    SOL_INT_CHECK_GOTO(r, < 0, error);

    return 0;

error:
    sol_monitors_del(&subscriptions, idx);

    return r;
}

SOL_API int
sol_gatt_unsubscribe(bool (*cb)(void *user_data, const struct sol_gatt_attr *attr,
    const struct sol_buffer *buffer),
    const void *user_data)
{
    struct context *ctx = &context;
    sd_bus *bus = sol_bus_client_get_bus(ctx->bluez);
    const char *service = sol_bus_client_get_service(ctx->bluez);
    const struct sol_gatt_attr *attr;
    struct subscription *sub;
    int r;

    r = sol_monitors_find(&subscriptions, (sol_monitors_cb_t)cb, user_data);
    SOL_INT_CHECK(r, < 0, r);

    sub = sol_monitors_get(&subscriptions, r);

    sub->slot = sd_bus_slot_unref(sub->slot);

    attr = sub->attr;

    r = sol_monitors_del(&subscriptions, r);
    SOL_INT_CHECK(r, < 0, r);

    r = find_subscription_by_attr(attr);

    if (r == UINT16_MAX)
        return 0;

    r = sd_bus_call_method_async(bus, NULL, service, attr->_priv,
        "org.bluez.GattCharacteristic1", "StopNotify", sol_bus_log_callback, NULL, NULL);
    SOL_INT_CHECK(r, < 0, r);

    return r;
}
