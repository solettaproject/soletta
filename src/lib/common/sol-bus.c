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
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#include "sol-bus.h"
#include "sol-mainloop.h"
#include "sol-platform-impl.h"
#include "sol-util.h"
#include "sol-vector.h"

#define SERVICE_NAME_OWNER_MATCH "type='signal',"                   \
    "sender='org.freedesktop.DBus',"                                \
    "path='/org/freedesktop/DBus',"                                 \
    "interface='org.freedesktop.DBus',"                             \
    "member='NameOwnerChanged',"                                    \
    "arg0='%s'"

#define INTERFACES_ADDED_MATCH "type='signal',"                 \
    "sender='%s',"                                              \
    "interface='org.freedesktop.DBus.ObjectManager',"           \
    "member='InterfacesAdded'"

struct property_table {
    const struct sol_bus_properties *properties;
    const void *data;
    void (*changed)(void *data, const char *path, uint64_t mask);
    sd_bus_slot *getall_slot;
    char *iface;
    char *path;
};

struct ctx {
    struct sol_mainloop_source *mainloop_source;
    sd_bus *bus;
    sd_event_source *ping;
    struct sol_ptr_vector clients;
    bool exiting;
};

struct sol_bus_client {
    sd_bus *bus;
    char *service;
    struct sol_ptr_vector property_tables;
    const struct sol_bus_interfaces *interfaces;
    const void *interfaces_data;
    sd_bus_slot *name_changed;
    sd_bus_slot *managed_objects;
    sd_bus_slot *interfaces_added;
    sd_bus_slot *properties_changed;
    sd_bus_slot *name_owner_slot;
    void (*connect)(void *data, const char *unique);
    void *connect_data;
    void (*disconnect)(void *data);
    void *disconnect_data;
};

static struct ctx _ctx;

struct source_ctx {
    struct sd_event *event;
    struct sol_fd *fd_handler;
};

static bool
source_prepare(void *data)
{
    struct source_ctx *s = data;

    return sd_event_prepare(s->event) > 0;
}

static bool
source_check(void *data)
{
    struct source_ctx *s = data;

    return sd_event_wait(s->event, 0) > 0;
}

static void
source_dispatch(void *data)
{
    struct source_ctx *s = data;

    sd_event_dispatch(s->event);
}

static void
source_dispose(void *data)
{
    struct source_ctx *s = data;

    sd_event_unref(s->event);
    sol_fd_del(s->fd_handler);

    free(s);
}

static const struct sol_mainloop_source_type source_type = {
    .api_version = SOL_MAINLOOP_SOURCE_TYPE_API_VERSION,
    .prepare = source_prepare,
    .check = source_check,
    .dispatch = source_dispatch,
    .dispose = source_dispose,
};

static bool
on_sd_event_fd(void *data, int fd, unsigned int active_flags)
{
    /* just used to wake up main loop */
    return true;
}

static struct sol_mainloop_source *
event_create_source(sd_event *event)
{
    struct sol_mainloop_source *source;
    struct source_ctx *ctx;

    ctx = malloc(sizeof(*ctx));
    SOL_NULL_CHECK(ctx, NULL);

    ctx->event = sd_event_ref(event);

    ctx->fd_handler = sol_fd_add(sd_event_get_fd(event),
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_ERR,
        on_sd_event_fd, ctx);
    SOL_NULL_CHECK_GOTO(ctx->fd_handler, error_fd);

    source = sol_mainloop_source_new(&source_type, ctx);
    SOL_NULL_CHECK_GOTO(source, error_source);

    return source;

error_source:
    sol_fd_del(ctx->fd_handler);
error_fd:
    sd_event_unref(ctx->event);
    free(ctx);
    return NULL;
}

static int
_event_mainloop_running(sd_event_source *event_source, void *userdata)
{
    struct ctx *ctx = userdata;

    SOL_DBG("systemd's mainloop running");
    sd_event_source_unref(ctx->ping);
    ctx->ping = NULL;

    return 0;
}

static int
event_attach_mainloop(void)
{
    int r;
    sd_event *e = NULL;

    if (_ctx.mainloop_source)
        return 0;

    r = sd_event_default(&e);
    if (r < 0)
        return r;

    _ctx.mainloop_source = event_create_source(e);
    if (!_ctx.mainloop_source)
        goto fail;

    sd_event_add_defer(e, &_ctx.ping, _event_mainloop_running, &_ctx);

    return 0;

fail:
    sd_event_unref(e);

    return -ENOMEM;
}

static int
_match_disconnected(sd_bus_message *m, void *userdata,
    sd_bus_error *error)
{
    struct ctx *ctx = userdata;

    if (!ctx->exiting) {
        SOL_WRN("D-Bus connection terminated: %s. Exiting.",
            error && error->message ? error->message : "(unknown reason)");
        sol_quit();
    }

    return 0;
}

static int
connect_bus(void)
{
    int r;
    sd_bus *bus = NULL;
    struct source_ctx *s;

    r = sd_bus_default_system(&bus);
    SOL_INT_CHECK(r, < 0, r);

    s = sol_mainloop_source_get_data(_ctx.mainloop_source);

    r = sd_bus_attach_event(bus, s->event,
        SD_EVENT_PRIORITY_NORMAL);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = sd_bus_add_match(bus, NULL,
        "type='signal',"
        "sender='org.freedesktop.DBus.Local',"
        "interface='org.freedesktop.DBus.Local',"
        "member='Disconnected'",
        _match_disconnected, &_ctx);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    _ctx.bus = bus;

    return 0;

fail:
    sd_bus_unref(bus);
    return r;
}

/* We attach libsystemd's mainloop to Soletta's on the first time we need to
 * connect to the bus. Any fail on getting connected to the bus means the
 * mainloop terminates.
 */
SOL_API sd_bus *
sol_bus_get(void (*bus_initialized)(sd_bus *bus))
{
    int r;

    if (_ctx.bus)
        return _ctx.bus;

    if (!_ctx.mainloop_source) {
        r = event_attach_mainloop();
        SOL_INT_CHECK_GOTO(r, < 0, fail);
    }

    r = connect_bus();
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    sol_ptr_vector_init(&_ctx.clients);

    if (bus_initialized)
        bus_initialized(_ctx.bus);

    return _ctx.bus;

fail:
    SOL_WRN("D-Bus requested but connection could not be made");
    sol_quit();

    return NULL;
}

static void
destroy_property_table(struct property_table *table)
{
    sd_bus_slot_unref(table->getall_slot);
    free(table->path);
    free(table->iface);
    free(table);
}

static void
destroy_client(struct sol_bus_client *client)
{
    struct property_table *t;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&client->property_tables, t, i) {
        destroy_property_table(t);
    }
    sol_ptr_vector_clear(&client->property_tables);

    client->name_changed = sd_bus_slot_unref(client->name_changed);
    client->managed_objects = sd_bus_slot_unref(client->managed_objects);
    client->interfaces_added = sd_bus_slot_unref(client->interfaces_added);

    client->bus = sd_bus_unref(client->bus);
    free(client->service);
}

SOL_API void
sol_bus_close(void)
{
    _ctx.exiting = true;

    if (_ctx.bus) {
        struct sol_bus_client *c;
        uint16_t i;

        SOL_PTR_VECTOR_FOREACH_IDX (&_ctx.clients, c, i) {
            destroy_client(c);
        }
        sol_ptr_vector_clear(&_ctx.clients);

        sd_bus_flush(_ctx.bus);
        sd_bus_close(_ctx.bus);
        sd_bus_unref(_ctx.bus);
        _ctx.bus = NULL;
    }

    if (_ctx.mainloop_source) {
        struct source_ctx *s;
        sd_event_source_unref(_ctx.ping);

        s = sol_mainloop_source_get_data(_ctx.mainloop_source);
        sd_event_unref(s->event);
        sol_mainloop_source_del(_ctx.mainloop_source);
        _ctx.mainloop_source = NULL;
    }
}

SOL_API struct sol_bus_client *
sol_bus_client_new(sd_bus *bus, const char *service)
{
    struct sol_bus_client *client;
    int r;

    SOL_NULL_CHECK(bus, NULL);
    SOL_NULL_CHECK(service, NULL);

    client = calloc(1, sizeof(struct sol_bus_client));
    SOL_NULL_CHECK(client, NULL);

    client->bus = sd_bus_ref(bus);

    client->service = strdup(service);
    SOL_NULL_CHECK_GOTO(client->service, fail);

    sol_ptr_vector_init(&client->property_tables);

    r = sol_ptr_vector_append(&_ctx.clients, client);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    return client;

fail:
    destroy_client(client);
    return NULL;
}

SOL_API void
sol_bus_client_free(struct sol_bus_client *client)
{
    if (!client)
        return;

    destroy_client(client);

    sol_ptr_vector_remove(&_ctx.clients, client);

    free(client);
}

SOL_API const char *
sol_bus_client_get_service(struct sol_bus_client *client)
{
    SOL_NULL_CHECK(client, NULL);

    return client->service;
}

SOL_API sd_bus *
sol_bus_client_get_bus(struct sol_bus_client *client)
{
    SOL_NULL_CHECK(client, NULL);

    return client->bus;
}

static int
_message_map_all_properties(sd_bus_message *m,
    const struct property_table *t, sd_bus_error *ret_error)
{
    uint64_t mask = 0;
    int r;

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    SOL_INT_CHECK(r, < 0, r);

    do {
        const struct sol_bus_properties *iter;
        const char *member;

        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
        if (r <= 0) {
            r = 0;
            break;
        }

        r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &member);
        SOL_INT_CHECK_GOTO(r, < 0, end);

        for (iter = t->properties; iter->member; iter++) {
            if (streq(iter->member, member))
                break;
        }

        if (iter->member) {
            bool changed;

            changed = iter->set((void *)t->data, t->path, m);
            if (changed)
                mask |= 1 << (iter - t->properties);
        } else {
            r = sd_bus_message_skip(m, "v");
            SOL_INT_CHECK_GOTO(r, < 0, end);
        }

        r = sd_bus_message_exit_container(m);
        SOL_INT_CHECK_GOTO(r, < 0, end);
    } while (1);

end:
    if (mask > 0)
        t->changed((void *)t->data, t->path, mask);

    if (r == 0)
        r = sd_bus_message_exit_container(m);

    return r;
}

static const struct property_table *
find_property_table(struct sol_bus_client *client,
    const char *iface, const char *path)
{
    struct property_table *t;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&client->property_tables, t, i) {
        if (streq(t->iface, iface) && streq(t->path, path))
            return t;
    }

    return NULL;
}

static int
_match_properties_changed(sd_bus_message *m, void *userdata,
    sd_bus_error *ret_error)
{
    struct sol_bus_client *client = userdata;
    const struct property_table *t;
    const char *path;
    const char *iface;
    int r;

    path = sd_bus_message_get_path(m);
    SOL_NULL_CHECK(path, -EINVAL);

    r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &iface);
    SOL_INT_CHECK(r, < 0, r);

    t = find_property_table(client, iface, path);
    SOL_NULL_CHECK(t, -ENOENT);

    /* Ignore PropertiesChanged signals until the GetAll() method returns */
    if (t->getall_slot)
        return 0;

    r = _message_map_all_properties(m, t, ret_error);
    SOL_INT_CHECK(r, < 0, r);

    /* Ignore invalidated properties */

    return 0;
}

static int
_getall_properties(sd_bus_message *reply, void *userdata,
    sd_bus_error *ret_error)
{
    struct property_table *t = userdata;

    t->getall_slot = sd_bus_slot_unref(t->getall_slot);

    if (sol_bus_log_callback(reply, userdata, ret_error) < 0)
        return 0;

    return _message_map_all_properties(reply, t, ret_error);
}

SOL_API int
sol_bus_map_cached_properties(struct sol_bus_client *client,
    const char *path, const char *iface,
    const struct sol_bus_properties property_table[],
    void (*changed)(void *data, const char *path, uint64_t mask),
    const void *data)
{
    sd_bus_message *m = NULL;
    const struct sol_bus_properties *iter_desc;
    struct property_table *t;
    char matchstr[4096];
    int r;

    SOL_NULL_CHECK(client, -EINVAL);

    /* Make sure uint64_t is sufficient to notify state changes - we only
     * support at most 64 properties */
    for (iter_desc = property_table; iter_desc->member != NULL;)
        iter_desc++;

    SOL_INT_CHECK(iter_desc - property_table, >= (int)sizeof(uint64_t) * CHAR_BIT, -ENOBUFS);

    t = calloc(1, sizeof(*t));
    SOL_NULL_CHECK(t, -ENOMEM);

    t->iface = strdup(iface);
    SOL_NULL_CHECK_GOTO(t->iface, fail);

    t->path = strdup(path);
    SOL_NULL_CHECK_GOTO(t->path, fail);

    t->properties = property_table;
    t->data = data;
    t->changed = changed;

    if (!client->properties_changed) {
        r = snprintf(matchstr, sizeof(matchstr),
            "type='signal',"
            "sender='%s',"
            "interface='org.freedesktop.DBus.Properties',"
            "member='PropertiesChanged'",
            client->service);
        SOL_INT_CHECK(r, >= (int)sizeof(matchstr), -ENOBUFS);

        r = sd_bus_add_match(client->bus, &client->properties_changed, matchstr,
            _match_properties_changed, client);
        SOL_INT_CHECK_GOTO(r, < 0, fail);
    }

    r = sol_ptr_vector_append(&client->property_tables, t);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    r = sd_bus_message_new_method_call(client->bus, &m, client->service, path,
        "org.freedesktop.DBus.Properties",
        "GetAll");
    SOL_INT_CHECK_GOTO(r, < 0, fail_getall);

    r = sd_bus_message_append(m, "s", iface);
    SOL_INT_CHECK_GOTO(r, < 0, fail_getall);

    r = sd_bus_call_async(client->bus, &t->getall_slot, m,
        _getall_properties, t, 0);
    SOL_INT_CHECK_GOTO(r, < 0, fail_getall);

    sd_bus_message_unref(m);

    return 0;

fail_getall:
    sd_bus_message_unref(m);
    sol_ptr_vector_del(&client->property_tables,
        sol_ptr_vector_get_len(&client->property_tables) - 1);

fail:
    destroy_property_table(t);

    return r;
}

SOL_API int
sol_bus_unmap_cached_properties(struct sol_bus_client *client,
    const struct sol_bus_properties property_table[],
    const void *data)
{
    struct property_table *t, *found = NULL;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&client->property_tables, t, i) {
        if (t->properties == property_table && t->data == data) {
            found = t;
            break;
        }
    }
    SOL_NULL_CHECK(found, -ENOENT);

    sol_ptr_vector_del(&client->property_tables, i);
    destroy_property_table(found);

    return 0;
}

static int
name_owner_changed(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct sol_bus_client *client = userdata;
    const char *name, *old, *new;

    if (sd_bus_message_read(m, "sss", &name, &old, &new) < 0)
        return 0;

    if (new && client->connect) {
        /* Assuming that when a name is replaced, calling 'connected()' is
         * the right thing to do.
         */
        client->connect(client->connect_data, new);
        return 0;
    }

    if (client->disconnect)
        client->disconnect(client->disconnect_data);

    return 0;
}

static const struct sol_bus_interfaces *
find_interface(const struct sol_bus_client *client, const char *iface)
{
    const struct sol_bus_interfaces *s;

    for (s = client->interfaces; s && s->name; s++) {
        if (streq(iface, s->name))
            return s;
    }
    return NULL;
}

static bool
filter_device_properties(sd_bus_message *m, const char *iface, const char *path,
    struct sol_bus_client *client, sd_bus_error *ret_error)
{
    const struct property_table *t;

    t = find_property_table(client, iface, path);
    if (!t) {
        sd_bus_message_skip(m, "a{sv}");
        return false;
    }

    _message_map_all_properties(m, t, ret_error);

    return true;
}

static void
filter_interfaces(struct sol_bus_client *client, sd_bus_message *m, sd_bus_error *ret_error)
{
    const char *path;

    if (sd_bus_message_read(m, "o", &path) < 0)
        return;

    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sa{sv}}") < 0)
        return;

    do {
        const struct sol_bus_interfaces *s;
        const char *iface;

        if (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}") < 0)
            break;

        if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &iface) < 0)
            return;

        s = find_interface(client, iface);
        if (s && s->appeared)
            s->appeared((void *)client->interfaces_data, path);

        filter_device_properties(m, iface, path, client, ret_error);

        if (sd_bus_message_exit_container(m) < 0)
            return;

    } while (1);
}

static int
interfaces_added_cb(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct sol_bus_client *client = userdata;

    if (sol_bus_log_callback(m, userdata, ret_error))
        return -EINVAL;

    filter_interfaces(client, m, ret_error);

    return 0;
}

static int
managed_objects_cb(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct sol_bus_client *client = userdata;
    int err;

    if (sol_bus_log_callback(m, userdata, ret_error)) {
        err = -EINVAL;
        goto end;
    }

    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{oa{sa{sv}}}") < 0) {
        err = -EINVAL;
        goto end;
    }

    while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "oa{sa{sv}}") > 0) {

        filter_interfaces(client, m, ret_error);

        if (sd_bus_message_exit_container(m) < 0) {
            err = -EINVAL;
            goto end;
        }
    }

    if (sd_bus_message_exit_container(m) < 0) {
        err = -EINVAL;
        goto end;
    }

    err = 0;

end:
    client->managed_objects = sd_bus_slot_unref(client->managed_objects);
    return err;
}

SOL_API int
sol_bus_watch_interfaces(struct sol_bus_client *client,
    const struct sol_bus_interfaces interfaces[],
    const void *data)
{
    sd_bus_message *m = NULL;
    const struct sol_bus_interfaces *iter;
    char matchstr[512];
    int r;

    /* One set of 'interfaces' per client. Too limited? */
    if (client->interfaces)
        return -EALREADY;

    for (iter = interfaces; iter->name;)
        iter++;

    SOL_INT_CHECK(iter - interfaces, >= (int)sizeof(uint64_t) * CHAR_BIT, -ENOBUFS);

    r = snprintf(matchstr, sizeof(matchstr), INTERFACES_ADDED_MATCH, client->service);
    SOL_INT_CHECK(r, < 0, -ENOMEM);

    client->interfaces = interfaces;
    client->interfaces_data = data;

    if (client->interfaces_added)
        return 0;

    r = sd_bus_add_match(client->bus, &client->interfaces_added,
        matchstr, interfaces_added_cb, client);
    SOL_INT_CHECK(r, < 0, -ENOMEM);

    if (client->managed_objects)
        return 0;

    r = sd_bus_message_new_method_call(client->bus, &m, client->service, "/",
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    SOL_INT_CHECK_GOTO(r, < 0, error_message);

    r = sd_bus_call_async(client->bus, &client->managed_objects,
        m, managed_objects_cb, client, 0);
    SOL_INT_CHECK_GOTO(r, < 0, error_call);

    sd_bus_message_unref(m);

    return 0;

error_call:
    sd_bus_message_unref(m);

error_message:
    client->interfaces_added = sd_bus_slot_unref(client->interfaces_added);

    return -EINVAL;
}

SOL_API int
sol_bus_remove_interfaces_watch(struct sol_bus_client *client,
    const struct sol_bus_interfaces interfaces[],
    const void *data)
{
    if (client->interfaces != interfaces || client->interfaces_data != data)
        return -ENODATA;

    client->interfaces = NULL;
    client->interfaces_data = NULL;

    return 0;
}

static sd_bus_slot *
add_name_owner_watch(struct sol_bus_client *client,
    sd_bus_message_handler_t cb, void *userdata)
{
    sd_bus_slot *slot = NULL;
    char matchstr[512];
    int r;

    r = snprintf(matchstr, sizeof(matchstr), SERVICE_NAME_OWNER_MATCH, client->service);
    SOL_INT_CHECK(r, < 0, NULL);

    r = sd_bus_add_match(client->bus, &slot, matchstr, cb, userdata);
    SOL_INT_CHECK(r, < 0, NULL);

    return slot;
}

static int
get_name_owner_reply_cb(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    struct sol_bus_client *client = userdata;
    const char *unique;
    int r;

    client->name_owner_slot = sd_bus_slot_unref(client->name_owner_slot);

    /* Do not error because the service may not exist yet. */
    if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *error = sd_bus_message_get_error(m);
        SOL_DBG("Failed method call: %s: %s", error->name, error->message);
        return 0;
    }

    r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &unique);
    SOL_INT_CHECK(r, < 0, -EINVAL);

    if (client->connect)
        client->connect(client->connect_data, unique);

    return 0;
}

SOL_API int
sol_bus_client_set_connect_handler(struct sol_bus_client *client,
    void (*connect)(void *data, const char *unique),
    void *data)
{
    SOL_NULL_CHECK(client, -EINVAL);

    client->connect = connect;
    client->connect_data = data;

    if (client->name_changed)
        return 0;

    client->name_changed = add_name_owner_watch(client, name_owner_changed, client);
    SOL_NULL_CHECK(client->name_changed, -ENOMEM);

    /* In case the name is already present in the bus. */
    sd_bus_call_method_async(sol_bus_client_get_bus(client),
        &client->name_owner_slot, "org.freedesktop.DBus",
        "/", "org.freedesktop.DBus", "GetNameOwner",
        get_name_owner_reply_cb, client, "s", sol_bus_client_get_service(client));

    return 0;
}

SOL_API int
sol_bus_client_set_disconnect_handler(struct sol_bus_client *client,
    void (*disconnect)(void *data),
    void *data)
{
    SOL_NULL_CHECK(client, -EINVAL);

    client->disconnect = disconnect;
    client->disconnect_data = data;

    if (client->name_changed)
        return 0;

    client->name_changed = add_name_owner_watch(client, name_owner_changed, client);
    SOL_NULL_CHECK(client->name_changed, -ENOMEM);

    return 0;
}

SOL_API int
sol_bus_log_callback(sd_bus_message *reply, void *userdata,
    sd_bus_error *ret_error)
{
    const sd_bus_error *error;

    if (!sd_bus_message_is_method_error(reply, NULL))
        return 0;

    error = sd_bus_message_get_error(reply);
    SOL_WRN("Failed method call: %s: %s", error->name, error->message);

    return -1;
}
