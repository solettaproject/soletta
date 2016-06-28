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

#include <sol-gatt.h>

SOL_API int
sol_gatt_pending_reply(struct sol_gatt_pending *pending,
    int error,
    struct sol_buffer *buf)
{
    return -ENOSYS;
}

SOL_API int
sol_gatt_register_attributes(struct sol_gatt_attr *attrs)
{
    return -ENOSYS;
}

SOL_API int
sol_gatt_unregister_attributes(struct sol_gatt_attr *attrs)
{
    return -ENOSYS;
}

SOL_API int
sol_gatt_read_attr(struct sol_bt_conn *conn, struct sol_gatt_attr *attr,
    void (*cb)(void *user_data, bool success,
    const struct sol_gatt_attr *attr,
    const struct sol_buffer *buf),
    const void *user_data)
{
    return -ENOSYS;
}

SOL_API int
sol_gatt_write_attr(struct sol_bt_conn *conn, struct sol_gatt_attr *attr,
    struct sol_buffer *buf,
    void (*cb)(void *user_data, bool success,
    const struct sol_gatt_attr *attr),
    const void *user_data)
{
    return -ENOSYS;
}

SOL_API int
sol_gatt_discover(struct sol_bt_conn *conn, enum sol_gatt_attr_type type,
    const struct sol_gatt_attr *parent,
    const struct sol_bt_uuid *uuid,
    bool (*func)(void *user_data, struct sol_bt_conn *conn,
    const struct sol_gatt_attr *attr),
    const void *user_data)
{
    return -ENOSYS;
}

SOL_API int
sol_gatt_subscribe(struct sol_bt_conn *conn, struct sol_gatt_attr *attr,
    bool (*cb)(void *user_data, struct sol_gatt_attr *attr, const struct sol_buffer *buffer),
    const void *user_data)
{
    return -ENOSYS;
}

SOL_API int
sol_gatt_unsubscribe(struct sol_bt_conn *conn, struct sol_gatt_attr *attr,
    bool (*cb)(void *user_data, struct sol_gatt_attr *attr,
    const struct sol_buffer *buffer))
{
    return -ENOSYS;
}

SOL_API int
sol_gatt_indicate(struct sol_bt_conn *conn, const struct sol_gatt_attr *attr)
{
    return -ENOSYS;
}

SOL_API int
sol_gatt_notify(struct sol_bt_conn *conn, const struct sol_gatt_attr *attr)
{
    return -ENOSYS;
}
