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

#include <sol-log.h>
#include <sol-bus.h>
#include <sol-bluetooth.h>
#include <sol-network.h>
#include <sol-util.h>
#include <sol-mainloop.h>
#include <sol-str-table.h>

#include <sol-gatt.h>

#include "sol-bluetooth-impl-bluez.h"

static const struct sol_str_table sol_gatt_chr_flags_table[] =  {
    SOL_STR_TABLE_ITEM("broadcast", SOL_GATT_CHR_FLAGS_BROADCAST),
    SOL_STR_TABLE_ITEM("read", SOL_GATT_CHR_FLAGS_READ),
    SOL_STR_TABLE_ITEM("write-without-response", SOL_GATT_CHR_FLAGS_WRITE_WITHOUT_RESPONSE),
    SOL_STR_TABLE_ITEM("write", SOL_GATT_CHR_FLAGS_WRITE),
    SOL_STR_TABLE_ITEM("notify", SOL_GATT_CHR_FLAGS_NOTIFY),
    SOL_STR_TABLE_ITEM("indicate", SOL_GATT_CHR_FLAGS_INDICATE),
    SOL_STR_TABLE_ITEM("authenticated-signed-writes", SOL_GATT_CHR_FLAGS_AUTHENTICATED_SIGNED_WRITES),
    SOL_STR_TABLE_ITEM("reliable-write", SOL_GATT_CHR_FLAGS_RELIABLE_WRITE),
    SOL_STR_TABLE_ITEM("writable-auxiliaries", SOL_GATT_CHR_FLAGS_WRITABLE_AUXILIARIES),
    SOL_STR_TABLE_ITEM("encrypt-read", SOL_GATT_CHR_FLAGS_ENCRYPT_WRITE),
    SOL_STR_TABLE_ITEM("encrypt-write", SOL_GATT_CHR_FLAGS_ENCRYPT_AUTHENTICATED_READ),
    SOL_STR_TABLE_ITEM("encrypt-authenticated-read", SOL_GATT_CHR_FLAGS_ENCRYPT_AUTHENTICATED_WRITE),
    SOL_STR_TABLE_ITEM("encrypt-authenticated-write", SOL_GATT_CHR_FLAGS_ENCRYPT_AUTHENTICATED_WRITE),
    { },
};

static const struct sol_str_table sol_gatt_desc_flags_table[] =  {
    SOL_STR_TABLE_ITEM("read", SOL_GATT_DESC_FLAGS_READ),
    SOL_STR_TABLE_ITEM("write", SOL_GATT_DESC_FLAGS_WRITE),
    SOL_STR_TABLE_ITEM("encrypt-read", SOL_GATT_DESC_FLAGS_ENCRYPT_READ),
    SOL_STR_TABLE_ITEM("encrypt-write", SOL_GATT_DESC_FLAGS_ENCRYPT_WRITE),
    SOL_STR_TABLE_ITEM("encrypt-authenticated-read", SOL_GATT_DESC_FLAGS_ENCRYPT_AUTHENTICATED_READ),
    SOL_STR_TABLE_ITEM("encrypt-authenticated-write", SOL_GATT_DESC_FLAGS_ENCRYPT_AUTHENTICATED_WRITE),
    { },
};

#define GATT_APPLICATION_PATH "/org/soletta/gatt"
/* A 32-bit number will be at most 10 digits long + 1 for the '\0' */
#define APP_PATH_SIZE_MAX (sizeof(GATT_APPLICATION_PATH) + 11)

struct application {
    uint32_t id;
    struct sol_gatt_attr *attrs;
    struct sol_vector slots;
    sd_bus_slot *register_slot;
};

struct sol_ptr_vector applications = SOL_PTR_VECTOR_INIT;

struct sol_ptr_vector pending_ops = SOL_PTR_VECTOR_INIT;

SOL_API const struct sol_gatt_attr *
sol_gatt_pending_get_attr(const struct sol_gatt_pending *op)
{
    SOL_NULL_CHECK(op, NULL);

    return op->attr;
}

static void
destroy_pending(struct sol_gatt_pending *op)
{
    if (op->buf)
        sol_buffer_fini(op->buf);

    if (op->type == PENDING_REMOTE_READ && op->read)
        op->read((void *)op->user_data, false, NULL, NULL);

    if (op->type == PENDING_REMOTE_WRITE && op->write)
        op->write((void *)op->user_data, false, NULL);

    op->slot = sd_bus_slot_unref(op->slot);

    sd_bus_message_unref(op->m);
    free(op);
}

SOL_API int
sol_gatt_pending_reply(struct sol_gatt_pending *pending, int error,
    struct sol_buffer *buf)
{
    sd_bus_message *reply = NULL;
    struct context *ctx = bluetooth_get_context();
    const struct sol_gatt_attr *attr;
    const char *interface;
    int r;

    SOL_NULL_CHECK(pending, -EINVAL);

    attr = pending->attr;

    if (error) {
        r = error;
        goto done;
    }

    switch (pending->type) {
    case PENDING_READ:
    case PENDING_WRITE:
        r = sd_bus_message_new_method_return(pending->m, &reply);
        SOL_INT_CHECK(r, < 0, r);

        if (pending->type == PENDING_READ) {
            r = -EINVAL;
            SOL_NULL_CHECK_GOTO(buf, done);

            r = sd_bus_message_append_array(reply, 'y', buf->data, buf->used);
            SOL_INT_CHECK_GOTO(r, < 0, done);
        }
        break;

    case PENDING_INDICATE:
    case PENDING_NOTIFY:
        r = -EINVAL;
        pending->buf = buf;
        SOL_NULL_CHECK_GOTO(pending->buf, done);

        if (attr->type == SOL_GATT_ATTR_TYPE_DESCRIPTOR)
            interface = "org.bluez.GattDescriptor1";
        else
            interface = "org.bluez.GattCharacteristic1";

        r = sd_bus_emit_properties_changed(sol_bus_client_get_bus(ctx->bluez),
            attr->_priv, interface, "Value", NULL);
        SOL_INT_CHECK_GOTO(r, < 0, done);
        break;
    case PENDING_REMOTE_READ:
        pending->read((void *)pending->user_data, true, pending->attr, buf);
        pending->read = NULL;
        destroy_pending(pending);
        break;

    case PENDING_REMOTE_WRITE:
        pending->write((void *)pending->user_data, true, pending->attr);
        pending->write = NULL;
        destroy_pending(pending);
        break;
    }

    if (!reply)
        return 0;

    r = sd_bus_send(NULL, reply, NULL);
    sd_bus_message_unref(reply);
    SOL_INT_CHECK_GOTO(r, < 0, done);

    return 0;

done:
    if (r && pending->m) {
        r = sd_bus_message_new_method_errno(pending->m, &reply, r, NULL);
        SOL_INT_CHECK(r, < 0, r);

        r = sd_bus_send(NULL, reply, NULL);

        sd_bus_message_unref(reply);

        SOL_INT_CHECK(r, < 0, r);
    }

    return r;
}

static int
attr_method(enum pending_type type, sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct sol_gatt_attr *attr = userdata;
    sd_bus_message *reply = NULL;
    struct sol_gatt_pending *pending;
    const void *data;
    size_t len;
    struct sol_buffer buf;
    int r = -EINVAL;

    if (sol_bus_log_callback(m, userdata, ret_error))
        goto error;

    if (!(attr->flags & SOL_GATT_CHR_FLAGS_READ)) {
        r = -EPERM;
        goto error;
    }

    r = -ENOMEM;
    pending = calloc(1, sizeof(*pending));
    SOL_NULL_CHECK_GOTO(pending, error);

    pending->attr = attr;
    pending->m = sd_bus_message_ref(m);
    pending->type = type;

    r = sol_ptr_vector_append(&pending_ops, pending);
    SOL_INT_CHECK_GOTO(r, < 0, error_append);

    switch (type) {
    case PENDING_READ:
        r = attr->read(pending, 0);
        SOL_INT_CHECK_GOTO(r, < 0, error_op);
        break;
    case PENDING_WRITE:
        r = sd_bus_message_read_array(m, 'y', &data, &len);
        SOL_INT_CHECK_GOTO(r, < 0, error_op);

        sol_buffer_init_flags(&buf, (void *)data, len,
            SOL_BUFFER_FLAGS_MEMORY_NOT_OWNED | SOL_BUFFER_FLAGS_NO_NUL_BYTE);
        buf.used = len;

        r = attr->write(pending, &buf, 0);
        SOL_INT_CHECK_GOTO(r, < 0, error_op);
    /* fall through */
    default:
        r = -EINVAL;
        goto error_op;
    }

    return 0;

error_op:
    sol_ptr_vector_del_last(&pending_ops);

error_append:
    free(pending);

error:
    if (r < 0) {
        r = sd_bus_message_new_method_errno(m, &reply, r, ret_error);
        SOL_INT_CHECK(r, < 0, r);
    }

    if (reply) {
        r = sd_bus_send(NULL, reply, NULL);

        sd_bus_message_unref(reply);

        SOL_INT_CHECK(r, < 0, r);
    }

    return r;
}

static int
attr_read_value(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    return attr_method(PENDING_READ, m, userdata, ret_error);
}

static int
attr_write_value(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    return attr_method(PENDING_WRITE, m, userdata, ret_error);
}

static int
attr_prop_get_uuid(sd_bus *bus, const char *path,
    const char *interface, const char *property,
    sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct sol_gatt_attr *attr = userdata;
    struct sol_buffer buffer = SOL_BUFFER_INIT_EMPTY;
    char *s;
    int r;

    r = sol_bt_uuid_to_str(&attr->uuid, &buffer);
    SOL_INT_CHECK(r, < 0, r);

    r = sd_bus_message_append_string_space(reply, buffer.used, &s);
    SOL_INT_CHECK_GOTO(r, < 0, done);

    memcpy(s, buffer.data, buffer.used);

done:
    sol_buffer_fini(&buffer);

    return r;
}

static int
service_prop_get_primary(sd_bus *bus, const char *path,
    const char *interface, const char *property,
    sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    int r;
    int primary = true;

    r = sd_bus_message_append_basic(reply, 'b', &primary);
    SOL_INT_CHECK(r, < 0, r);


    return r;
}

static int
chr_prop_get_service(sd_bus *bus, const char *path,
    const char *interface, const char *property,
    sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    const char *end;
    char *p;
    int r, len;

    end = strstr(path, "/chr");
    SOL_NULL_CHECK(end, -EINVAL);

    len = end - path;

    p = strndupa(path, len);

    /* sd_bus_message_append_string_space() doesn't work for type 'object'. */
    r = sd_bus_message_append_basic(reply, 'o', p);
    SOL_INT_CHECK(r, < 0, r);

    return r;
}

static int
flags_to_dbus_string_array(uint16_t flags, const struct sol_str_table *table, sd_bus_message *m)
{
    const struct sol_str_table *elem;
    int r;

    r = sd_bus_message_open_container(m, 'a', "s");
    SOL_INT_CHECK(r, < 0, r);

    for (elem = table; elem && elem->key; elem++) {
        char *s;

        if (!(flags & elem->val))
            continue;

        r = sd_bus_message_append_string_space(m, elem->len, &s);
        SOL_INT_CHECK(r, < 0, r);

        memcpy(s, elem->key, elem->len);
    }

    r = sd_bus_message_close_container(m);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

uint16_t
dbus_string_array_to_flags(enum sol_gatt_attr_type type, sd_bus_message *m)
{
    const struct sol_str_table *table = NULL;
    const char *str;
    uint16_t flags = 0;
    int r;

    if (type == SOL_GATT_ATTR_TYPE_CHARACTERISTIC)
        table = sol_gatt_chr_flags_table;

    if (type == SOL_GATT_ATTR_TYPE_DESCRIPTOR)
        table = sol_gatt_desc_flags_table;

    r = sd_bus_message_enter_container(m, 'a', "s");
    SOL_INT_CHECK(r, < 0, 0);

    if (!table)
        goto done;

    while (sd_bus_message_read_basic(m, 's', &str) > 0) {
        flags |= sol_str_table_lookup_fallback(table,
            sol_str_slice_from_str(str), 0);
    }

done:
    r = sd_bus_message_exit_container(m);
    SOL_INT_CHECK(r, < 0, 0);

    return flags;
}

static int
chr_prop_get_flags(sd_bus *bus, const char *path,
    const char *interface, const char *property,
    sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct sol_gatt_attr *attr = userdata;

    return flags_to_dbus_string_array(attr->flags,
        sol_gatt_chr_flags_table, reply);
}

static int
desc_prop_get_flags(sd_bus *bus, const char *path,
    const char *interface, const char *property,
    sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct sol_gatt_attr *attr = userdata;

    return flags_to_dbus_string_array(attr->flags,
        sol_gatt_desc_flags_table, reply);
}

static int
desc_prop_get_characteristic(sd_bus *bus, const char *path,
    const char *interface, const char *property,
    sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    const char *end;
    char *p;
    int r, len;

    end = strstr(path, "/desc");
    SOL_NULL_CHECK(end, -EINVAL);

    len = end - path;

    p = strndupa(path, len);

    r = sd_bus_message_append_basic(reply, 'o', p);
    SOL_INT_CHECK(r, < 0, r);

    return 0;
}

static struct sol_gatt_pending *
find_pending(struct sol_gatt_attr *attr)
{
    struct sol_gatt_pending *pending;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&pending_ops, pending, i) {
        if (pending->attr == attr)
            return pending;
    }

    return NULL;
}

static int
cached_prop_value(sd_bus *bus, const char *path,
    const char *interface, const char *property,
    sd_bus_message *reply, void *userdata, sd_bus_error *ret_error)
{
    struct sol_gatt_attr *attr = userdata;
    struct sol_gatt_pending *pending;
    const uint8_t *buf;
    size_t len;
    int r;

    buf = NULL;
    len = 0;

    pending = find_pending(attr);
    if (pending && pending->buf) {
        buf = pending->buf->data;
        len = pending->buf->used;
    }

    r = sd_bus_message_append_array(reply, 'y', buf, len);
    SOL_INT_CHECK(r, < 0, r);

    if (pending) {
        sol_ptr_vector_remove(&pending_ops, pending);
        destroy_pending(pending);
    }

    return r;
}

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", attr_prop_get_uuid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Primary", "b", service_prop_get_primary, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

static const sd_bus_vtable characteristic_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", attr_prop_get_uuid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Service", "o", chr_prop_get_service, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Value", "ay", cached_prop_value, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Flags", "as", chr_prop_get_flags, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("ReadValue", NULL, "ay",
        attr_read_value, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("WriteValue", "ay", NULL,
        attr_write_value, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

static const sd_bus_vtable descriptor_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", attr_prop_get_uuid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Characteristic", "o",
        desc_prop_get_characteristic, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Value", "ay", cached_prop_value, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Flags", "as", desc_prop_get_flags, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("ReadValue", NULL, "ay",
        attr_read_value, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("WriteValue", "ay", NULL,
        attr_write_value, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

static struct application *
find_application(struct sol_gatt_attr *attrs)
{
    struct application *a;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&applications, a, i) {
        if (a->attrs == attrs)
            return a;
    }

    return NULL;
}

static int
register_app_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct application *app = userdata;

    app->register_slot = sd_bus_slot_unref(app->register_slot);

    return sol_bus_log_callback(m, userdata, ret_error);
}

SOL_API int
sol_gatt_register_attributes(struct sol_gatt_attr *attrs)
{
    char app_path[64];
    struct context *ctx = bluetooth_get_context();
    struct sol_gatt_attr *attr;
    struct application *app;
    const char *service, *service_path = NULL, *chr_path = NULL;
    sd_bus_message *m = NULL;
    sd_bus *bus;
    sd_bus_slot **s;
    enum sol_gatt_attr_type previous = SOL_GATT_ATTR_TYPE_INVALID;
    static unsigned int app_id;
    unsigned int i;
    int r;

    SOL_NULL_CHECK(attrs, -EINVAL);

    app = find_application(attrs);
    if (app)
        return -EALREADY;

    SOL_NULL_CHECK(ctx->bluez, -EINVAL);

    bus = sol_bus_client_get_bus(ctx->bluez);
    service = sol_bus_client_get_service(ctx->bluez);

    app = calloc(1, sizeof(*app));
    SOL_NULL_CHECK(app, -ENOMEM);

    r = sol_ptr_vector_append(&applications, app);
    SOL_INT_CHECK_GOTO(r, < 0, error_append);

    app->attrs = attrs;
    app->id = ++app_id;
    sol_vector_init(&app->slots, sizeof(sd_bus_slot * *));

    r = snprintf(app_path, sizeof(app_path), GATT_APPLICATION_PATH "%u", app->id);
    SOL_INT_CHECK_GOTO(r, < 0, error_print);

    for (attr = attrs; attr && attr->type != SOL_GATT_ATTR_TYPE_INVALID;
        attr++) {
        unsigned int id = attr - attrs;
        char *path;

        s = sol_vector_append(&app->slots);
        SOL_INT_CHECK_GOTO(r, < 0, error_slot);

        switch (attr->type) {
        case SOL_GATT_ATTR_TYPE_SERVICE:
            r = asprintf(&path, "%s/service%u", app_path, id);
            SOL_INT_CHECK_GOTO(r, < 0, error_vtable);

            r = sd_bus_add_object_vtable(bus, s, path, "org.bluez.GattService1",
                service_vtable, attr);

            service_path = path;
            attr->_priv = path;
            break;


        case SOL_GATT_ATTR_TYPE_CHARACTERISTIC:
            if (previous == SOL_GATT_ATTR_TYPE_INVALID || !service_path) {
                SOL_WRN("invalid type sequence %d -> %d", previous, attr->type);
                r = -EINVAL;
                goto error_vtable;
            }

            r = asprintf(&path, "%s/chr%u", service_path, id);
            SOL_INT_CHECK_GOTO(r, < 0, error_vtable);

            r = sd_bus_add_object_vtable(bus, s, path, "org.bluez.GattCharacteristic1",
                characteristic_vtable, attr);
            SOL_INT_CHECK_GOTO(r, < 0, error_vtable);

            chr_path = path;
            attr->_priv = path;
            break;

        case SOL_GATT_ATTR_TYPE_DESCRIPTOR:
            if (previous == SOL_GATT_ATTR_TYPE_INVALID
                || previous == SOL_GATT_ATTR_TYPE_SERVICE || !chr_path) {
                SOL_WRN("invalid type sequence %d -> %d", previous, attr->type);
                r = -EINVAL;
                goto error_vtable;
            }

            r = asprintf(&path, "%s/desc%u", chr_path, id);
            SOL_INT_CHECK_GOTO(r, < 0, error_vtable);

            r = sd_bus_add_object_vtable(bus, s, path, "org.bluez.GattDescriptor1",
                descriptor_vtable, attr);
            SOL_INT_CHECK_GOTO(r, < 0, error_vtable);

            attr->_priv = path;
            break;
        default:
            SOL_WRN("Invalid attribute type %d", attr->type);
            r = -EINVAL;
            goto error_vtable;
        }

        previous = attr->type;
    }

    r = -ENOMEM;

    s = sol_vector_append(&app->slots);
    SOL_NULL_CHECK_GOTO(s, error_vtable);

    r = sd_bus_add_object_manager(bus, s, app_path);
    SOL_INT_CHECK_GOTO(r, < 0, error_vtable);

    r = sd_bus_message_new_method_call(bus, &m, service, ctx->adapter_path,
        "org.bluez.GattManager1", "RegisterApplication");
    SOL_INT_CHECK_GOTO(r, < 0, error_slot);

    r = sd_bus_message_append(m, "o", app_path);
    SOL_INT_CHECK_GOTO(r, < 0, error_slot);

    r = sd_bus_message_open_container(m, 'a', "{sv}");
    SOL_INT_CHECK_GOTO(r, < 0, error_slot);

    r = sd_bus_message_close_container(m);
    SOL_INT_CHECK_GOTO(r, < 0, error_slot);

    r = sd_bus_call_async(bus, &app->register_slot, m, register_app_reply, app, 0);
    SOL_INT_CHECK_GOTO(r, < 0, error_slot);

    sd_bus_message_unref(m);

    return 0;

error_vtable:
    sol_vector_del_last(&app->slots);

error_slot:
    sd_bus_message_unref(m);

    SOL_VECTOR_FOREACH_IDX (&app->slots, s, i) {
        sd_bus_slot_unref(*s);
    }
    sol_vector_clear(&app->slots);

    for (attr = attrs; attr && attr->type != SOL_GATT_ATTR_TYPE_INVALID;
        attr++) {
        free(attr->_priv);
    }

error_print:
    sol_ptr_vector_del_last(&applications);

error_append:
    free(app);

    return r;
}

static void
destroy_application(struct application *app)
{
    struct sd_bus_slot **s;
    struct sol_gatt_attr *attr;
    uint16_t idx;

    app->register_slot = sd_bus_slot_unref(app->register_slot);

    SOL_VECTOR_FOREACH_IDX (&app->slots, s, idx) {
        sd_bus_slot_unref(*s);
    }
    sol_vector_clear(&app->slots);

    for (attr = app->attrs; attr && attr->type != SOL_GATT_ATTR_TYPE_INVALID; attr++) {
        struct sol_gatt_pending *pending = find_pending(attr);

        if (!pending)
            continue;

        sol_ptr_vector_remove(&pending_ops, pending);
        destroy_pending(pending);
    }

    sol_ptr_vector_remove(&applications, app);
    free(app);
}

SOL_API int
sol_gatt_unregister_attributes(struct sol_gatt_attr *attrs)
{
    struct application *app;

    app = find_application(attrs);
    SOL_NULL_CHECK(app, -EINVAL);

    destroy_application(app);

    return 0;
}

void
clear_applications(void)
{
    struct application *app;
    uint16_t idx;

    SOL_PTR_VECTOR_FOREACH_IDX (&applications, app, idx) {
        destroy_application(app);
    }
    sol_ptr_vector_clear(&applications);
}

static int
prepare_update(enum pending_type type, const struct sol_gatt_attr *attr)
{
    struct sol_gatt_pending *pending;
    int r;

    SOL_NULL_CHECK(attr, -EINVAL);
    SOL_NULL_CHECK(attr->read, -EINVAL);

    pending = calloc(1, sizeof(*pending));
    SOL_NULL_CHECK(pending, -ENOMEM);

    pending->attr = attr;
    pending->type = type;

    r = sol_ptr_vector_append(&pending_ops, pending);
    SOL_INT_CHECK_GOTO(r, < 0, error_append);

    r = attr->read(pending, 0);
    SOL_INT_CHECK_GOTO(r, < 0, error_read);

    return 0;

error_read:
    sol_ptr_vector_del_last(&pending_ops);

error_append:
    free(pending);
    return r;
}

SOL_API int
sol_gatt_indicate(struct sol_bt_conn *conn, const struct sol_gatt_attr *attr)
{
    return prepare_update(PENDING_INDICATE, attr);
}

SOL_API int
sol_gatt_notify(struct sol_bt_conn *conn, const struct sol_gatt_attr *attr)
{
    return prepare_update(PENDING_NOTIFY, attr);
}

SOL_API int
sol_gatt_discover(struct sol_bt_conn *conn, enum sol_gatt_attr_type type,
    const struct sol_gatt_attr *parent,
    const struct sol_bt_uuid *uuid,
    bool (*func)(void *user_data, struct sol_bt_conn *conn,
    const struct sol_gatt_attr *attr),
    const void *user_data)
{
    struct device_info *d;
    struct pending_discovery *disc;
    int r;

    SOL_NULL_CHECK(conn, -EINVAL);
    SOL_NULL_CHECK(func, -EINVAL);

    d = conn->d;

    disc = calloc(1, sizeof(*disc));
    SOL_NULL_CHECK(disc, -ENOMEM);

    disc->conn = sol_bt_conn_ref(conn);
    disc->type = type;
    disc->parent = parent;
    disc->uuid = uuid;
    disc->func = func;
    disc->user_data = user_data;

    if (!d->resolved) {
        r = sol_ptr_vector_append(&d->pending_discoveries, disc);
        SOL_INT_CHECK_GOTO(r, < 0, done);

        return 0;
    }

    trigger_gatt_discover(disc);

    r = 0;

done:
    destroy_pending_discovery(disc);

    return r;
}

SOL_API int
sol_gatt_read_attr(struct sol_bt_conn *conn, struct sol_gatt_attr *attr,
    void (*cb)(void *user_data, bool success,
    const struct sol_gatt_attr *attr,
    const struct sol_buffer *buf),
    const void *user_data)
{
    struct sol_gatt_pending *pending;
    int r;

    SOL_NULL_CHECK(attr, -EINVAL);
    SOL_NULL_CHECK(attr->read, -EINVAL);

    pending = calloc(1, sizeof(*pending));
    SOL_NULL_CHECK(pending, -ENOMEM);

    pending->attr = attr;
    pending->type = PENDING_REMOTE_READ;
    pending->read = cb;
    pending->user_data = user_data;

    r = sol_ptr_vector_append(&pending_ops, pending);
    SOL_INT_CHECK_GOTO(r, < 0, error_append);

    r = attr->read(pending, 0);
    SOL_INT_CHECK_GOTO(r, < 0, error_read);

    return 0;

error_read:
    sol_ptr_vector_del_last(&pending_ops);

error_append:
    free(pending);
    return r;
}

SOL_API int
sol_gatt_write_attr(struct sol_bt_conn *conn, struct sol_gatt_attr *attr,
    struct sol_buffer *buf,
    void (*cb)(void *user_data, bool success,
    const struct sol_gatt_attr *attr),
    const void *user_data)
{
    struct sol_gatt_pending *pending;
    int r;

    SOL_NULL_CHECK(attr, -EINVAL);
    SOL_NULL_CHECK(attr->write, -EINVAL);

    pending = calloc(1, sizeof(*pending));
    SOL_NULL_CHECK(pending, -ENOMEM);

    pending->attr = attr;
    pending->type = PENDING_REMOTE_WRITE;
    pending->write = cb;
    pending->user_data = user_data;

    r = sol_ptr_vector_append(&pending_ops, pending);
    SOL_INT_CHECK_GOTO(r, < 0, error_append);

    r = attr->write(pending, buf, 0);
    SOL_INT_CHECK_GOTO(r, < 0, error_read);

    return 0;

error_read:
    sol_ptr_vector_del_last(&pending_ops);

error_append:
    free(pending);
    return r;
}
