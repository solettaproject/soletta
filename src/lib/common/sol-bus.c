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

#include <glib.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#include "sol-bus.h"
#include "sol-mainloop.h"
#include "sol-platform-impl.h"
#include "sol-util.h"
#include "sol-vector.h"

struct event_source {
    GSource gsource;
    sd_event *event;
};

struct property_table {
    const struct sol_bus_properties *properties;
    const void *data;
    void (*changed)(void *data, uint64_t mask);
    sd_bus_slot *match_slot;
    sd_bus_slot *getall_slot;
};

struct ctx {
    struct event_source *event_source;
    sd_bus *bus;
    sd_event_source *ping;
    struct sol_ptr_vector property_tables;
    bool exiting;
};

static struct ctx _ctx;

static gboolean
event_prepare(GSource *gsource, gint *timeout)
{
    struct event_source *s = (struct event_source *)gsource;

    return sd_event_prepare(s->event) > 0;
}

static gboolean
event_check(GSource *gsource)
{
    struct event_source *s = (struct event_source *)gsource;

    return sd_event_wait(s->event, 0) > 0;
}

static gboolean
event_dispatch(GSource *gsource, GSourceFunc cb, gpointer user_data)
{
    struct event_source *s = (struct event_source *)gsource;

    return sd_event_dispatch(s->event) > 0;
}

static void
event_finalize(GSource *gsource)
{
    struct event_source *s = (struct event_source *)gsource;

    sd_event_unref(s->event);
}

static GSourceFuncs event_funcs = {
    .prepare = event_prepare,
    .check = event_check,
    .dispatch = event_dispatch,
    .finalize = event_finalize,
};

static struct event_source *
event_create_source(sd_event *event)
{
    struct event_source *s;

    s = (struct event_source *)g_source_new(&event_funcs,
        sizeof(struct event_source));
    if (!s)
        return NULL;

    s->event = sd_event_ref(event);
    g_source_add_unix_fd(&s->gsource, sd_event_get_fd(event),
        G_IO_IN | G_IO_HUP | G_IO_ERR);

    return s;
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

    if (_ctx.event_source)
        return 0;

    r = sd_event_default(&e);
    if (r < 0)
        return r;

    _ctx.event_source = event_create_source(e);
    if (!_ctx.event_source)
        goto fail;

    g_source_attach(&_ctx.event_source->gsource, NULL);

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

    r = sd_bus_default_system(&bus);
    SOL_INT_CHECK(r, < 0, r);

    r = sd_bus_attach_event(bus, _ctx.event_source->event,
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

/* We attach libsystemd's mainloop to glib on the first time we need to
 * connect to the bus. Any fail on getting connected to the bus means the
 * mainloop terminates.
 */
sd_bus *
sol_bus_get(void (*bus_initialized)(sd_bus *bus))
{
    int r;

    if (_ctx.bus)
        return _ctx.bus;

    if (!_ctx.event_source) {
        r = event_attach_mainloop();
        SOL_INT_CHECK_GOTO(r, < 0, fail);
    }

    r = connect_bus();
    SOL_INT_CHECK_GOTO(r, < 0, fail);

    sol_ptr_vector_init(&_ctx.property_tables);

    if (bus_initialized)
        bus_initialized(_ctx.bus);

    return _ctx.bus;

fail:
    SOL_WRN("D-Bus requested but connection could not be made");
    sol_quit();

    return NULL;
}

void
sol_bus_close(void)
{
    _ctx.exiting = true;

    if (_ctx.bus) {
        struct property_table *t;
        uint16_t i;

        SOL_PTR_VECTOR_FOREACH_IDX (&_ctx.property_tables, t, i) {
            sd_bus_slot_unref(t->match_slot);
            sd_bus_slot_unref(t->getall_slot);
            free(t);
        }
        sol_ptr_vector_clear(&_ctx.property_tables);

        sd_bus_flush(_ctx.bus);
        sd_bus_close(_ctx.bus);
        sd_bus_unref(_ctx.bus);
        _ctx.bus = NULL;
    }

    if (_ctx.event_source) {
        sd_event_source_unref(_ctx.ping);
        sd_event_unref(_ctx.event_source->event);
        g_source_destroy(&_ctx.event_source->gsource);
        g_source_unref(&_ctx.event_source->gsource);
        _ctx.event_source = NULL;
    }
}

static int
_message_map_all_properties(sd_bus_message *m,
    struct property_table *t, sd_bus_error *ret_error)
{
    sd_bus *bus;
    uint64_t mask = 0;
    int r;

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    SOL_INT_CHECK(r, < 0, r);

    bus = sd_bus_message_get_bus(m);

    do {
        const struct sol_bus_properties *iter;
        const char *member;
        void *value;

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
            char contents[] = { iter->type, '\0'  };
            bool changed;

            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, contents);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            r = sd_bus_message_read_basic(m, iter->type, &value);
            SOL_INT_CHECK_GOTO(r, < 0, end);

            changed = iter->set((void *)t->data, value);
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
        t->changed((void *)t->data, mask);

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

int
sol_bus_map_cached_properties(sd_bus *bus,
    const char *dest, const char *path, const char *iface,
    const struct sol_bus_properties property_table[],
    void (*changed)(void *data, uint64_t mask),
    const void *data)
{
    sd_bus_message *m = NULL;
    const struct sol_bus_properties *iter_desc;
    struct property_table *t;
    char matchstr[4096];
    int r;

    /* Make sure uint64_t is sufficient to notify state changes - we only
     * support at most 64 properties */
    for (iter_desc = property_table; iter_desc->member != NULL;)
        iter_desc++;

    SOL_INT_CHECK(iter_desc - property_table, >= (int)sizeof(uint64_t), -ENOBUFS);

    r = snprintf(matchstr, sizeof(matchstr),
        "type='signal',"
        "sender='%s',"
        "path='%s',"
        "interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged',"
        "arg0='%s'",
        dest, path, iface);
    SOL_INT_CHECK(r, >= (int)sizeof(matchstr), -ENOBUFS);

    t = calloc(1, sizeof(*t));
    SOL_NULL_CHECK(t, -ENOMEM);
    t->properties = property_table;
    t->data = data;
    t->changed = changed;

    r = sol_ptr_vector_append(&_ctx.property_tables, t);
    SOL_INT_CHECK_GOTO(r, < 0, fail_append);

    r = sd_bus_add_match(bus, &t->match_slot, matchstr,
        _match_properties_changed, t);
    SOL_INT_CHECK_GOTO(r, < 0, fail_match);

    r = sd_bus_message_new_method_call(bus, &m, dest, path,
        "org.freedesktop.DBus.Properties",
        "GetAll");
    SOL_INT_CHECK_GOTO(r, < 0, fail_getall);

    r = sd_bus_message_append(m, "s", iface);
    SOL_INT_CHECK_GOTO(r, < 0, fail_getall);

    r = sd_bus_call_async(bus, &t->getall_slot, m,
        _getall_properties, t, 0);
    SOL_INT_CHECK_GOTO(r, < 0, fail_getall);

    sd_bus_message_unref(m);

    return 0;

fail_getall:
    sd_bus_message_unref(m);
    sd_bus_slot_unref(t->match_slot);
fail_match:
    sol_ptr_vector_del(&_ctx.property_tables,
        sol_ptr_vector_get_len(&_ctx.property_tables) - 1);
fail_append:
    free(t);

    return r;
}

int
sol_bus_unmap_cached_properties(const struct sol_bus_properties property_table[],
    const void *data)
{
    struct property_table *t, *found = NULL;
    uint16_t i;

    SOL_PTR_VECTOR_FOREACH_IDX (&_ctx.property_tables, t, i) {
        if (t->properties == property_table && t->data == data) {
            found = t;
            break;
        }
    }
    SOL_NULL_CHECK(found, -ENOENT);

    sd_bus_slot_unref(found->match_slot);
    sd_bus_slot_unref(found->getall_slot);
    sol_ptr_vector_del(&_ctx.property_tables, i);
    free(found);

    return 0;
}

int
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
