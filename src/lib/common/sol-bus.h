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

#pragma once

#include <stdbool.h>
#include <systemd/sd-bus.h>

/**
 * @file
 * @brief Helpers for the systemd D-Bus bindings.
 *
 * D-Bus is the "de-facto" mechanism for inter-process communication
 * in the Linux Desktop environment. systemd provides an API for the D-Bus
 * protocol.
 */

/**
 * @defgroup D-Bus D-Bus
 * @ingroup Comms
 *
 * @brief API to handle the D-Bus interprocess communication protocol.
 *
 * D-Bus[1] is a message bus system, providing both a way of
 * applications talking to one another and a way of managing the
 * life-cycle of applications.
 *
 * Usually, D-Bus provides two daemons, one per system, that manages
 * system level events ("new hardware detected", "battery level low"),
 * and one per user login daemon, for communication between user
 * applications. Also, if two applications desire to communicate
 * directly, they can create private peer-to-peer buses.
 *
 * @a sol-bus deals mostly helping avoid repetition when handling the
 * @a org.freedesktop.DBus.ObjectManager and the
 * @a org.freedesktop.DBus.Properties part of the hierarchy.
 *
 * [1] http://www.freedesktop.org/wiki/Software/dbus/
 *
 * @{
 */

/**
 * Representation of a monitored D-Bus property.
 *
 * To be used with sol_bus_map_cached_properties().
 */
struct sol_bus_properties {
    /**
     * Name of the property being monitored.
     */
    const char *member;
    /**
     * Called when the value of the property changes.
     *
     * @param data User data provided to sol_bus_map_cached_properties().
     * @param path The object path of the property.
     * @param m The D-Bus message containing the value of the property.
     *
     * @return True if the changed() callback of sol_bus_map_cached_properties() is to be called
     *         False if the changed() callback is not to be called.
     */
    bool (*set)(void *data, const char *path, sd_bus_message *m);
};

/**
 * Representation of a monitored D-Bus interface.
 *
 * To be used with sol_bus_watch_interfaces().
 */
struct sol_bus_interfaces {
    /**
     * Name of the interface being monitored.
     */
    const char *name;
    /**
     * Called each time a interface matching #name appears on the client.
     *
     * @param data User data provided to sol_bus_watch_interfaces()
     * @param path The object path of the new interface.
     */
    void (*appeared)(void *data, const char *path);
    /**
     * Called each time a interface matching #name disappears on the client.
     *
     * @param data User data provided to sol_bus_watch_interfaces()
     * @param path The object path of the removed interface.
     */
    void (*removed)(void *data, const char *path);
};

/**
 * Represents an remote service on a bus.
 */
struct sol_bus_client;

/**
 * Opens and returns an connection to the system bus.
 *
 * @param bus_initialized Called when the connection is ready.
 *
 * @return \a sd_bus pointer when the connection could be made
 *         NULL in case of failure.
 **/
sd_bus *sol_bus_get(void (*bus_initialized)(sd_bus *bus));

/**
 * Closes the connection to the system bus.
 */
void sol_bus_close(void);

/**
 * Creates a new #sol_bus_client instance.
 *
 * Most of the other operations depend on the existance of a remote client.
 *
 * @param bus Connection to a D-Bus bus (system, session or private)
 * @param service Name of the service to be monitored, per the D-Bus specification,
 *        at most 255 bytes long
 *
 * @return \a new #sol_bus_client object, NULL if an error occurs.
 */
struct sol_bus_client *sol_bus_client_new(sd_bus *bus, const char *service);

/**
 * Frees the resources associated with a #sol_bus_client
 *
 * @param client The #sol_bus_client to release.
 */
void sol_bus_client_free(struct sol_bus_client *client);

/**
 * Returns the service name associated with a client.
 *
 * This will be most used when using the systemd sd_bus bindings to create messages.
 *
 * @param client The #sol_bus_client used to retrieve the service name.
 *
 * @return The service name.
 */
const char *sol_bus_client_get_service(struct sol_bus_client *client);

/**
 * Returns the bus connection associated with a client.
 *
 * This will be most used when using the systemd sd_bus bindings to create messages.
 *
 * @param client The #sol_bus_client used to retrieve the service name.
 *
 * @return The #sd_bus connection.
 */
sd_bus *sol_bus_client_get_bus(struct sol_bus_client *client);

/**
 * Allows users to be notified when a service name enters the bus.
 *
 * @param client The #sol_bus_client representing the monitored service
 * @param connect Function to be called when the service enters the bus
 * @param data The user data pointer to pass to the @a connect function.
 *
 * @return 0 on success, -errno otherwise.
 */
int sol_bus_client_set_connect_handler(struct sol_bus_client *client,
    void (*connect)(void *data, const char *unique),
    void *data);

/**
 * Allows users to be notified when a service name exits the bus.
 *
 * @param client The #sol_bus_client representing the monitored service
 * @param disconnect Function to be called when the service exits the bus
 * @param data The user data pointer to pass to the @a disconnect function.
 *
 * @return 0 on success, -errno otherwise.
 */
int sol_bus_client_set_disconnect_handler(struct sol_bus_client *client,
    void (*disconnect)(void *data),
    void *data);

/**
 * Allows controlled notification of changes in properties.
 *
 * Controlled notification means that the changed() callback will only be called when the
 * @a sol_bus_properties set() callback returns true, allowing the changed() callback to be
 * only called when interesting changes happen.
 *
 * @param client The #sol_bus_client representing the monitored service
 * @param path Object path of the object with the properties to be monitored
 * @param iface Interface name in which the properties are present
 * @param property_table array of #sol_bus_properties, if the set()
 *        function of the @a nth property in the array returns True, the @a
 *        nth bit of @a changed @a mask will be set
 * @param changed Function that will be called when the change is
 *        signalled as interesting enough by the #sol_bus_properties set()
 *        function returning True
 * @param data The user data pointer to pass to the @a changed function.
 *
 * @return 0 on success, -errno otherwise.
 */
int sol_bus_map_cached_properties(struct sol_bus_client *client,
    const char *path, const char *iface,
    const struct sol_bus_properties property_table[],
    void (*changed)(void *data, const char *path, uint64_t mask),
    const void *data);

/**
 * Removes the notifications handlers for a set of properties.
 *
 * @param client The #sol_bus_client representing the monitored service
 * @param property_table array of #sol_bus_properties, the same passed to
 *        the sol_bus_map_cached_properties()
 * @param data User data pointer, the same passed to the
 *        sol_bus_map_cached_properties().
 *
 * @return 0 on success, -errno otherwise.
 */
int sol_bus_unmap_cached_properties(struct sol_bus_client *client,
    const struct sol_bus_properties property_table[],
    const void *data);

/**
 * Allows for interfaces of a service to be monitored.
 *
 * @param client The #sol_bus_client representing the monitored service
 * @param interfaces Array of #sol_bus_interfaces
 * @param data The user data pointer to pass to each appeared() and disappeared() callback.
 *
 * @return 0 on success, -errno otherwise.
 */
int sol_bus_watch_interfaces(struct sol_bus_client *client,
    const struct sol_bus_interfaces interfaces[],
    const void *data);

/**
 * Removes the notification handlers for a set of interfaces.
 *
 * @param interfaces Array of #sol_bus_interfaces, same as passed to sol_bus_watch_interfaces()
 * @param data User data pointer, same as passed to sol_bus_watch_interfaces()
 *
 * @return 0 on success, -errno otherwise.
 */
int sol_bus_remove_interfaces_watch(struct sol_bus_client *client,
    const struct sol_bus_interfaces interfaces[],
    const void *data);

/**
 * Convenience method for logging the result of a method call.
 *
 * It has the signature expected of a #sd_bus_message_handler_t, so it be used in its place,
 * and it will log the result of the call.
 *
 * @param reply The result of a remote method call
 * @param data User data pointer
 * @param ret_error Pointer into which store the result of the operation.
 *
 * @return 0 on success, -errno otherwise.
 */
int sol_bus_log_callback(sd_bus_message *reply, void *userdata,
    sd_bus_error *ret_error);
