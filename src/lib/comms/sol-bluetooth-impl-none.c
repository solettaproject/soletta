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

#include <sol-bluetooth.h>

SOL_API struct sol_bt_conn *
sol_bt_conn_ref(struct sol_bt_conn *conn)
{
    return NULL;
}

SOL_API void
sol_bt_conn_unref(struct sol_bt_conn *conn)
{

}

SOL_API struct sol_bt_conn *
sol_bt_connect(const struct sol_network_link_addr *addr,
    bool (*on_connect)(void *user_data, struct sol_bt_conn *conn),
    void (*on_disconnect)(void *user_data, struct sol_bt_conn *conn),
    void (*on_error)(void *user_data, int error),
    const void *user_data)
{
    return NULL;
}

SOL_API int
sol_bt_disconnect(struct sol_bt_conn *conn)
{
    return -ENOSYS;
}

SOL_API struct sol_bt_session *
sol_bt_enable(void (*enabled)(void *data, bool powered), const void *user_data)
{
    return NULL;
}

SOL_API int
sol_bt_disable(struct sol_bt_session *session)
{
    return -ENOSYS;
}

SOL_API struct sol_bt_scan_pending *
sol_bt_start_scan(enum sol_bt_transport transport,
    void (*cb)(void *user_data, const struct sol_bt_device_info *device),
    const void *user_data)
{
    return NULL;
}

SOL_API int
sol_bt_stop_scan(struct sol_bt_scan_pending *handle)
{
    return -ENOSYS;
}

SOL_API const struct sol_network_link_addr *
sol_bt_conn_get_addr(const struct sol_bt_conn *conn)
{
    return NULL;
}

