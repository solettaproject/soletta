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

enum adapter_state {
    ADAPTER_STATE_UNKNOWN,
    ADAPTER_STATE_OFF,
    ADAPTER_STATE_ON,
};

struct context {
    sd_bus *system_bus;
    struct sol_bus_client *bluez;
    char *adapter_path;
    struct sol_ptr_vector devices;
    struct sol_ptr_vector sessions;
    struct sol_ptr_vector scans;
    struct sol_ptr_vector conns;
    enum adapter_state original_state;
    enum adapter_state current_state;
};

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
    struct sol_ptr_vector attrs;
    struct sol_bt_device_info info;
    struct sol_ptr_vector pending_discoveries;
    bool resolved;
};

struct pending_discovery {
    struct sol_bt_conn *conn;
    const struct sol_bt_uuid *uuid;
    const struct sol_gatt_attr *parent;
    enum sol_gatt_attr_type type;
    bool (*func)(void *user_data, struct sol_bt_conn *conn,
        const struct sol_gatt_attr *attr);
    const void *user_data;
};

enum pending_type {
    PENDING_READ,
    PENDING_WRITE,
    PENDING_NOTIFY,
    PENDING_INDICATE,
    PENDING_REMOTE_READ,
    PENDING_REMOTE_WRITE,
};

struct sol_gatt_pending {
    const struct sol_gatt_attr *attr;
    sd_bus_message *m;
    sd_bus_slot *slot;
    enum pending_type type;
    struct sol_buffer *buf;
    union {
        void (*read)(void *user_data, bool success,
            const struct sol_gatt_attr *attr,
            const struct sol_buffer *buf);
        void (*write)(void *user_data, bool success,
            const struct sol_gatt_attr *attr);
    };
    const void *user_data;
};

void clear_applications(void);

struct context *bluetooth_get_context(void);

uint16_t dbus_string_array_to_flags(enum sol_gatt_attr_type type, sd_bus_message *m);

void trigger_gatt_discover(struct pending_discovery *disc);

void destroy_pending_discovery(struct pending_discovery *disc);
