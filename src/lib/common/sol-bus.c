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
    SOL_SET_API_VERSION(.api_version = SOL_MAINLOOP_SOURCE_TYPE_API_VERSION, )
    .prepare = source_prepare,
    .check = source_check,
    .dispatch = source_dispatch,
    .dispose = source_dispose,
};

static bool
on_sd_event_fd(void *data, int fd, uint32_t active_flags)
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
            free(c);
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
    size_t len;
    int r;

    SOL_NULL_CHECK(bus, NULL);
    SOL_NULL_CHECK(service, NULL);

    /* D-Bus specifies that the maximum service name length is 255 bytes */
    len = strlen(service);
    SOL_INT_CHECK(len, >= 255, NULL);

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

static int
_match_properties_changed(sd_bus_message *m, void *userdata,
    sd_bus_error *ret_error)
{
    struct property_table *t = userdata;
    int r;

    /* Ignore PropertiesChanged signals until the GetAll() method returns */
    if (t->getall_slot)
        return 0;

    r = sd_bus_message_skip(m, "s");
    SOL_INT_CHECK(r, < 0, r);

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
    char matchstr[512];
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
        SOL_INT_CHECK(r, < 0, -ENOBUFS);

        r = sd_bus_add_match(client->bus, &client->properties_changed, matchstr,
            _match_properties_changed, t);
        SOL_INT_CHECK_GOTO(r, < 0, fail);
    }

    r = sol_ptr_vector_append(&client->property_tables, t);
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    /* When GetManagedObjects return we will have info about all the properties. */
    if (client->managed_objects)
        return 0;

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
