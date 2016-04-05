/*
 * This file is part of the Soletta Project
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

struct sol_bt_conn {
    struct device_info *d;
    bool (*on_connect)(void *user_data, struct sol_bt_conn *conn);
    void (*on_disconnect)(void *user_data, struct sol_bt_conn *conn);
    void (*on_error)(void *user_data, int error);
    const void *user_data;
    sd_bus_slot *slot;
    int ref;
};

struct device_info {
    char *path;
    uint64_t mask;
    struct sol_bt_device_info info;
};

struct sol_bt_scan_pending {
    sd_bus_slot *slot;
    void (*callback)(void *user_data, const struct sol_bt_device_info *device);
    const void *user_data;
};

struct sol_bt_session {
    void (*enabled)(void *data, bool powered);
    const void *user_data;
};

enum adapter_state {
    ADAPTER_STATE_UNKNOWN,
    ADAPTER_STATE_OFF,
    ADAPTER_STATE_ON,
};

static struct context {
    sd_bus *system_bus;
    struct sol_bus_client *bluez;
    char *adapter_path;
    struct sol_ptr_vector devices;
    struct sol_ptr_vector sessions;
    struct sol_ptr_vector scans;
    struct sol_ptr_vector conns;
    enum adapter_state original_state;
    enum adapter_state current_state;
} context;

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
};

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
destroy_device(struct device_info *device)
{
    struct sol_bt_device_info *info;

    info = &device->info;
    free(info->name);
    sol_vector_clear(&info->uuids);

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
    { NULL, NULL }
};

static void
destroy_conn(struct sol_bt_conn *conn)
{
    if (conn->on_disconnect)
        conn->on_disconnect((void *)conn->user_data, conn);

    if (conn->slot)
        sd_bus_slot_unref(conn->slot);

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

static void
device_property_changed(void *data, const char *path, uint64_t mask)
{
    struct context *ctx = data;
    struct device_info *d;
    struct sol_bt_device_info *info;

    d = find_device_by_path(ctx, path);
    SOL_NULL_CHECK(d);

    info = &d->info;

    d->mask |= mask;

    /* If the device changed connection state */
    if (mask & (1 << DEVICE_PROPERTY_CONNECTED))
        trigger_bt_conn(ctx, d, info->connected);

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
    uint16_t idx;

    d = find_device_by_path(ctx, path);
    SOL_NULL_CHECK(d);

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

static const struct sol_bus_interfaces interfaces[] = {
    { .name = "org.bluez.Adapter1",
      .appeared = adapter_appeared,
      .removed = adapter_removed },
    { .name = "org.bluez.Device1",
      .appeared = device_appeared,
      .removed = device_removed },
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
    if (!conn)
        return;

    conn->ref--;

    if (conn->ref > 0)
        return;

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
    int err;

    conn->slot = sd_bus_slot_unref(conn->slot);

    sol_bus_log_callback(reply, userdata, ret_error);

    err = sd_bus_error_get_errno(ret_error);

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
