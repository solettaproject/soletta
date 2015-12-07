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
#include <fcntl.h>
#include <linux/tcp.h>
#include <mavlink.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define SOL_LOG_DOMAIN &_sol_mavlink_log_domain
#include <sol-log-internal.h>
#include <sol-mainloop.h>
#include <sol-str-slice.h>
#include <sol-str-table.h>
#include <sol-util.h>
#include <sol-vector.h>

#include "sol-mavlink.h"

SOL_LOG_INTERNAL_DECLARE(_sol_mavlink_log_domain, "mavlink");

#define STR_SLICE_VAL(_ptr)                     \
    *(const struct sol_str_slice *)_ptr         \

#define CHECK_HANDLER(_obj, _func)                      \
    (_obj->config && _obj->config->handlers &&          \
    _obj->config->handlers->_func)                     \

#define CALL_HANDLER(_obj, _func)                                       \
    do {                                                                \
        if (CHECK_HANDLER(_obj, _func) &&                               \
            _obj->status == SOL_MAVLINK_STATUS_READY) {                 \
            _obj->config->handlers->_func(_obj->data, _obj);            \
        }                                                               \
    } while (0)                                                          \

#define TYPE_MODE_MAPPING(_mode_map, _type)                             \
    {                                                                   \
        .mapping = (struct sol_mavlink_mode_mapping *)_mode_map,        \
        .len = ARRAY_SIZE(_mode_map),                                   \
        .type = _type,                                                  \
    }                                                                   \

typedef enum {
    SOL_MAVLINK_STATUS_INITIAL_SETUP = (1 << 1),
    SOL_MAVLINK_STATUS_GPS_SETUP     = (1 << 2),
    SOL_MAVLINK_STATUS_GPS_HOME_POS  = (1 << 3),
    SOL_MAVLINK_STATUS_FULL_SETUP    = (SOL_MAVLINK_STATUS_INITIAL_SETUP |
        SOL_MAVLINK_STATUS_GPS_SETUP |
        SOL_MAVLINK_STATUS_GPS_HOME_POS),
    SOL_MAVLINK_STATUS_CONN_NOTIFIED = (1 << 4),
    SOL_MAVLINK_STATUS_READY         = (SOL_MAVLINK_STATUS_FULL_SETUP |
        SOL_MAVLINK_STATUS_CONN_NOTIFIED),
} sol_mavlink_status;

struct sol_mavlink {
    const struct sol_mavlink_config *config;
    void *data;

    struct sol_str_slice *address;
    int port;
    int fd;
    struct sol_fd *watch;

    sol_mavlink_status status;
    uint8_t target_system;
    uint8_t target_comp;
    uint8_t type;

    bool custom_mode_enabled;
    sol_mavlink_mode mode;
    uint8_t base_mode;

    struct sol_mavlink_position curr_position;
    struct sol_mavlink_position home_position;
};

struct sol_mavlink_mode_mapping {
    sol_mavlink_mode sol_val;
    uint8_t mav_val;
};

struct sol_mavlink_type_mode {
    struct sol_mavlink_mode_mapping *mapping;
    unsigned int len;
    MAV_TYPE type;
};

struct sol_mavlink_armed_trans {
    uint8_t from;
    uint8_t to;
    bool armed;
};

static const struct sol_mavlink_armed_trans armed_transitions[] = {
    { MAV_MODE_MANUAL_DISARMED, MAV_MODE_MANUAL_ARMED, true },
    { MAV_MODE_MANUAL_ARMED, MAV_MODE_MANUAL_DISARMED, false },
    { MAV_MODE_TEST_DISARMED, MAV_MODE_TEST_ARMED, true },
    { MAV_MODE_TEST_ARMED, MAV_MODE_TEST_DISARMED, false },
    { MAV_MODE_STABILIZE_DISARMED, MAV_MODE_STABILIZE_ARMED, true },
    { MAV_MODE_STABILIZE_ARMED, MAV_MODE_STABILIZE_DISARMED, false },
    { MAV_MODE_GUIDED_DISARMED, MAV_MODE_GUIDED_ARMED, true },
    { MAV_MODE_GUIDED_ARMED, MAV_MODE_GUIDED_DISARMED, false },
    { MAV_MODE_AUTO_DISARMED, MAV_MODE_AUTO_ARMED, true },
    { MAV_MODE_AUTO_ARMED, MAV_MODE_AUTO_DISARMED, false },
};

static const struct sol_mavlink_mode_mapping mode_mapping_apm[] = {
    { SOL_MAVLINK_MODE_MANUAL, 0 },
    { SOL_MAVLINK_MODE_CIRCLE, 1 },
    { SOL_MAVLINK_MODE_STABILIZE, 2 },
    { SOL_MAVLINK_MODE_TRAINING, 3 },
    { SOL_MAVLINK_MODE_ACRO, 4 },
    { SOL_MAVLINK_MODE_FBWA, 5 },
    { SOL_MAVLINK_MODE_FBWB, 6 },
    { SOL_MAVLINK_MODE_CRUISE, 7 },
    { SOL_MAVLINK_MODE_AUTOTUNE, 8 },
    { SOL_MAVLINK_MODE_AUTO, 10 },
    { SOL_MAVLINK_MODE_RTL, 11 },
    { SOL_MAVLINK_MODE_LOITER, 12 },
    { SOL_MAVLINK_MODE_LAND, 14 },
    { SOL_MAVLINK_MODE_GUIDED, 15 },
    { SOL_MAVLINK_MODE_INITIALISING, 16 },
};

static const struct sol_mavlink_mode_mapping mode_mapping_acm[] = {
    { SOL_MAVLINK_MODE_STABILIZE, 0 },
    { SOL_MAVLINK_MODE_ACRO, 1 },
    { SOL_MAVLINK_MODE_ALT_HOLD, 2 },
    { SOL_MAVLINK_MODE_AUTO, 3 },
    { SOL_MAVLINK_MODE_GUIDED, 4 },
    { SOL_MAVLINK_MODE_LOITER, 5 },
    { SOL_MAVLINK_MODE_RTL, 6 },
    { SOL_MAVLINK_MODE_CIRCLE, 7 },
    { SOL_MAVLINK_MODE_POSITION, 8 },
    { SOL_MAVLINK_MODE_LAND, 9 },
    { SOL_MAVLINK_MODE_OF_LOITER, 10 },
    { SOL_MAVLINK_MODE_DRIFT, 11 },
    { SOL_MAVLINK_MODE_SPORT, 13 },
    { SOL_MAVLINK_MODE_FLIP, 14 },
    { SOL_MAVLINK_MODE_AUTOTUNE, 15 },
    { SOL_MAVLINK_MODE_POSHOLD, 16 },
};

static const struct sol_mavlink_mode_mapping mode_mapping_rover[] = {
    { SOL_MAVLINK_MODE_MANUAL, 0 },
    { SOL_MAVLINK_MODE_LEARNING, 1 },
    { SOL_MAVLINK_MODE_STEERING, 2 },
    { SOL_MAVLINK_MODE_HOLD, 3 },
    { SOL_MAVLINK_MODE_AUTO, 10 },
    { SOL_MAVLINK_MODE_RTL, 11 },
    { SOL_MAVLINK_MODE_GUIDED, 15 },
    { SOL_MAVLINK_MODE_INITIALISING, 16 },
};

static const struct sol_mavlink_mode_mapping mode_mapping_tracker[] = {
    { SOL_MAVLINK_MODE_MANUAL, 0 },
    { SOL_MAVLINK_MODE_STOP, 1 },
    { SOL_MAVLINK_MODE_SCAN, 2 },
    { SOL_MAVLINK_MODE_AUTO, 10 },
    { SOL_MAVLINK_MODE_INITIALISING, 16 },
};

static const struct sol_mavlink_mode_mapping mode_mapping_px4[] = {
    { SOL_MAVLINK_MODE_MANUAL, 0 },
    { SOL_MAVLINK_MODE_ATTITUDE, 1 },
    { SOL_MAVLINK_MODE_EASY, 2 },
    { SOL_MAVLINK_MODE_AUTO, 3 },
};

static const struct sol_mavlink_type_mode type_mode_mapping[] = {
    TYPE_MODE_MAPPING(mode_mapping_acm, MAV_TYPE_QUADROTOR),
    TYPE_MODE_MAPPING(mode_mapping_acm, MAV_TYPE_HELICOPTER),
    TYPE_MODE_MAPPING(mode_mapping_acm, MAV_TYPE_HEXAROTOR),
    TYPE_MODE_MAPPING(mode_mapping_acm, MAV_TYPE_OCTOROTOR),
    TYPE_MODE_MAPPING(mode_mapping_acm, MAV_TYPE_TRICOPTER),
    TYPE_MODE_MAPPING(mode_mapping_apm, MAV_TYPE_FIXED_WING),
    TYPE_MODE_MAPPING(mode_mapping_rover, MAV_TYPE_GROUND_ROVER),
    TYPE_MODE_MAPPING(mode_mapping_tracker, MAV_TYPE_ANTENNA_TRACKER),
    { }
};

static inline sol_mavlink_mode
mavlink_mode_to_sol_mode_lookup(uint8_t type, uint8_t mode)
{
    const struct sol_mavlink_type_mode *itr;

    for (itr = type_mode_mapping; itr->mapping; itr++) {
        struct sol_mavlink_mode_mapping *mapping = itr->mapping;
        uint8_t i;

        if (itr->type != type)
            continue;

        for (i = 0; i < itr->len; i++) {
            if (mapping[i].mav_val == mode)
                return mapping[i].sol_val;
        }
    }

    return SOL_MAVLINK_MODE_UNKNOWN;
}

static inline uint8_t
sol_mode_to_mavlink_mode_lookup(uint8_t type, sol_mavlink_mode mode)
{
    const struct sol_mavlink_type_mode *itr;

    for (itr = type_mode_mapping; itr->mapping; itr++) {
        struct sol_mavlink_mode_mapping *mapping = itr->mapping;
        uint8_t i;

        if (itr->type != type)
            continue;

        for (i = 0; i < itr->len; i++) {
            if (mapping[i].sol_val == mode)
                return mapping[i].mav_val;
        }
    }

    return MAV_MODE_ENUM_END;
}

static inline sol_mavlink_mode
sol_mavlink_get_mode(uint8_t type, mavlink_message_t *msg, uint8_t *base_mode)
{
    uint8_t mode;

    mode = mavlink_msg_heartbeat_get_base_mode(msg);
    *base_mode = mode;

    if (mode & MAV_MODE_FLAG_CUSTOM_MODE_ENABLED) {
        mode = mavlink_msg_heartbeat_get_custom_mode(msg);
    }

    return mavlink_mode_to_sol_mode_lookup(type, mode);
}

static void
sol_mavlink_armed_transition(struct sol_mavlink *mavlink, uint8_t base_mode)
{
    uint8_t i, mask;

    if (mavlink->custom_mode_enabled)
        mask = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED;

    for (i = 0; i < ARRAY_SIZE(armed_transitions); i++) {
        uint8_t from, to;
        const struct sol_mavlink_armed_trans *curr = &armed_transitions[i];

        from = curr->from | mask;
        to = curr->to | mask;

        if (from != mavlink->base_mode && to != base_mode)
            continue;

        if (curr->armed) {
            CALL_HANDLER(mavlink, armed);
        } else {
            CALL_HANDLER(mavlink, disarmed);
        }

        break;
    }

    mavlink->base_mode = base_mode;
}

static inline bool
sol_mavlink_check_known_vehicle(uint8_t type)
{
    const struct sol_mavlink_type_mode *itr;

    for (itr = type_mode_mapping; itr->mapping; itr++) {
        if (itr->type == type)
            return true;
    }

    return false;
}

static bool
sol_mavlink_request_home_position(struct sol_mavlink *mavlink)
{
    mavlink_message_t msg;
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    uint16_t len;

    mavlink_msg_command_long_pack
        (mavlink->target_system, mavlink->target_comp, &msg, 0, 0,
        MAV_CMD_GET_HOME_POSITION, 0, 0, 0, 0, 0, 0, 0, 0);

    len = mavlink_msg_to_send_buffer(buf, &msg);
    return write(mavlink->fd, buf, len) == len;
}

static void
sol_mavlink_initial_status(struct sol_mavlink *mavlink, mavlink_message_t *msg)
{
    uint8_t base_mode, type;
    sol_mavlink_mode mode;

    type = mavlink_msg_heartbeat_get_type(msg);
    if (!sol_mavlink_check_known_vehicle(type)) {
        SOL_INF("Unknown vehicle type, we'll retry on next heartbeat");
        return;
    }

    mode = sol_mavlink_get_mode(type, msg, &base_mode);
    if (mode == SOL_MAVLINK_MODE_UNKNOWN) {
        SOL_INF("Could not determine mode, we'll retry on next heartbeat");
        return;
    }

    mavlink->mode = mode;
    mavlink->target_system = msg->sysid;
    mavlink->target_comp = msg->compid;
    mavlink->type = type;

    mavlink->base_mode = base_mode;
    mavlink->custom_mode_enabled = base_mode & MAV_MODE_FLAG_CUSTOM_MODE_ENABLED;

    mavlink->status |= SOL_MAVLINK_STATUS_INITIAL_SETUP;
    sol_mavlink_request_home_position(mavlink);
}

static void
sol_mavlink_heartbeat_handler(struct sol_mavlink *mavlink, mavlink_message_t *msg)
{
    uint8_t base_mode;
    sol_mavlink_mode mode;

    if (!(mavlink->status & SOL_MAVLINK_STATUS_INITIAL_SETUP)) {
        sol_mavlink_initial_status(mavlink, msg);
        return;
    }

    if (mavlink->target_system != msg->sysid ||
        mavlink->target_comp != msg->sysid)
        return;

    mode = sol_mavlink_get_mode(mavlink->type, msg, &base_mode);
    if (mavlink->mode != mode) {
        mavlink->mode = mode;
        CALL_HANDLER(mavlink, mode_changed);
    }

    if (mavlink->base_mode != base_mode) {
        sol_mavlink_armed_transition(mavlink, base_mode);
        mavlink->base_mode = base_mode;
    }
}

static void
sol_mavlink_position_handler(struct sol_mavlink *mavlink, mavlink_message_t *msg)
{
    int32_t lat, longi, alt;
    struct sol_mavlink_position *pos = &mavlink->curr_position;

    lat = mavlink_msg_gps_raw_int_get_lat(msg);
    longi = mavlink_msg_gps_raw_int_get_lon(msg);
    alt = mavlink_msg_gps_raw_int_get_alt(msg);

    if (lat != pos->latitude || longi != pos->longitude || alt != pos->altitude) {
        pos->latitude = lat;
        pos->longitude = longi;
        pos->altitude = alt;
        mavlink->status |= SOL_MAVLINK_STATUS_GPS_SETUP;
        CALL_HANDLER(mavlink, position_changed);
    }
}

static void
sol_mavlink_statustext_handler(mavlink_message_t *msg)
{
    char text[50];

    mavlink_msg_statustext_get_text(msg, text);
    SOL_DBG("%s", text);
}

static void
sol_mavlink_home_position_handler(struct sol_mavlink *mavlink, mavlink_message_t *msg)
{
    struct sol_mavlink_position *pos = &mavlink->home_position;

    pos->latitude = mavlink_msg_home_position_get_latitude(msg);
    pos->longitude = mavlink_msg_home_position_get_longitude(msg);
    pos->altitude = mavlink_msg_home_position_get_altitude(msg) * 1000;
    pos->x = mavlink_msg_home_position_get_x(msg);
    pos->y = mavlink_msg_home_position_get_y(msg);
    pos->z = mavlink_msg_home_position_get_z(msg);

    mavlink->status |= SOL_MAVLINK_STATUS_GPS_HOME_POS;
}

static bool
sol_mavlink_fd_handler(void *data, int fd, uint32_t cond)
{
    struct sol_mavlink *mavlink = data;
    mavlink_message_t msg;
    mavlink_status_t status;
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    int i, res;
    sol_mavlink_status sol_status = mavlink->status;

    bzero(buf, MAVLINK_MAX_PACKET_LEN);
    res = recv(mavlink->fd, buf, MAVLINK_MAX_PACKET_LEN, 0);

    for (i = 0; i < res; ++i) {
        if (!mavlink_parse_char(MAVLINK_COMM_0, buf[i], &msg, &status))
            continue;

        switch (msg.msgid) {
        case MAVLINK_MSG_ID_GPS_RAW_INT:
            sol_mavlink_position_handler(mavlink, &msg);
            break;
        case MAVLINK_MSG_ID_HEARTBEAT:
            sol_mavlink_heartbeat_handler(mavlink, &msg);
            break;
        case MAVLINK_MSG_ID_STATUSTEXT:
            sol_mavlink_statustext_handler(&msg);
            break;
        case MAVLINK_MSG_ID_HOME_POSITION:
            sol_mavlink_home_position_handler(mavlink, &msg);
            break;
        default:
            SOL_DBG("Unhandled event, msgid: %d", msg.msgid);
        }
    }

    if (mavlink->status == SOL_MAVLINK_STATUS_FULL_SETUP &&
        mavlink->status != sol_status) {
        mavlink->status |= SOL_MAVLINK_STATUS_CONN_NOTIFIED;
        CALL_HANDLER(mavlink, connect);
    }

    return true;
}

static void
setup_data_stream(struct sol_mavlink *mavlink)
{
    mavlink_message_t msg;
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    uint16_t len;

    SOL_NULL_CHECK(mavlink, false);

    mavlink_msg_request_data_stream_pack
        (0, 0, &msg, mavlink->target_system, mavlink->target_comp,
        MAV_DATA_STREAM_EXTENDED_STATUS, 1, 1);

    len = mavlink_msg_to_send_buffer(buf, &msg);
    write(mavlink->fd, buf, len);
}

static int
sol_mavlink_init_tcp(struct sol_mavlink *mavlink)
{
    struct hostent *server;
    struct sockaddr_in serveraddr;
    char hostname[PATH_MAX];
    unsigned int err;
    int tcp_flag = 1;

    mavlink->fd = socket(PF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (mavlink->fd == -1) {
        SOL_ERR("Could not create a socket to specified address - %s",
            sol_util_strerrora(errno));
        return -errno;
    }

    err = snprintf(hostname, mavlink->address->len + 1, "%.*s",
        SOL_STR_SLICE_PRINT(*mavlink->address));
    if (err == 0 || err < mavlink->address->len) {
        SOL_ERR("Could not format hostname string");
        goto err;
    }

    server = gethostbyname(hostname);
    if (!server) {
        SOL_ERR("No such host: %s - (%s)", hostname,
            sol_util_strerrora(h_errno));
        errno = h_errno;
        goto err;
    }

    bzero(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy(server->h_addr, &serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(mavlink->port);

    if (setsockopt(mavlink->fd, IPPROTO_TCP, TCP_NODELAY, &tcp_flag, sizeof(int)) < 0) {
        SOL_ERR("Could not set NODELAY option to tcp socket - (%s)",
            sol_util_strerrora(errno));
        goto err;
    }

    if (connect(mavlink->fd, &serveraddr, sizeof(serveraddr)) < 0) {
        SOL_ERR("Could not stablish connection to: %s:%d - (%s)", hostname,
            mavlink->port, sol_util_strerrora(errno));
        goto err;
    }

    mavlink->watch = sol_fd_add(mavlink->fd, SOL_FD_FLAGS_IN,
        sol_mavlink_fd_handler, mavlink);
    SOL_NULL_CHECK_GOTO(mavlink->watch, err);

    setup_data_stream(mavlink);

    return 0;

err:
    close(mavlink->fd);
    return -errno;
}

static int
sol_mavlink_init_serial(struct sol_mavlink *mavlink)
{
    SOL_ERR("Not implemented.");
    return 0;
}

static const struct sol_str_table_ptr protocol_table[] = {
    SOL_STR_TABLE_PTR_ITEM("tcp", sol_mavlink_init_tcp),
    SOL_STR_TABLE_PTR_ITEM("serial", sol_mavlink_init_serial),
    { }
};

static inline const void *
sol_mavlink_parse_addr_protocol(const char *str, struct sol_str_slice *addr, int *port)
{
    struct sol_vector tokens;
    struct sol_str_slice slice = sol_str_slice_from_str(str);
    const void *init;

    tokens = sol_str_slice_split(slice, ":", 0);
    if (tokens.len <= 1) {
        SOL_ERR("Invalid addr string, it must specify at least <prot>:<addr>");
        return NULL;
    }

    init = sol_str_table_ptr_lookup_fallback(
        protocol_table, STR_SLICE_VAL(sol_vector_get(&tokens, 0)),
        sol_mavlink_init_tcp);
    if (!init) {
        SOL_ERR("Invalid protocol");
        return NULL;
    }

    *addr = STR_SLICE_VAL(sol_vector_get(&tokens, 1));

    if (tokens.len >= 3)
        sol_str_slice_to_int(STR_SLICE_VAL(sol_vector_get(&tokens, 2)), port);

    sol_vector_clear(&tokens);

    return init;
}

SOL_API struct sol_mavlink *
sol_mavlink_connect(const char *addr, const struct sol_mavlink_config *config, void *data)
{
    struct sol_mavlink *mavlink;
    struct sol_str_slice address;
    int port;

    int (*init) (struct sol_mavlink *mavlink);

    SOL_NULL_CHECK(addr, NULL);

    init = sol_mavlink_parse_addr_protocol(addr, &address, &port);
    SOL_NULL_CHECK(init, NULL);

    mavlink = calloc(1, sizeof(*mavlink));
    SOL_NULL_CHECK(mavlink, NULL);

    mavlink->address = &address;
    SOL_NULL_CHECK_GOTO(mavlink->address, err);

    mavlink->port = port;
    SOL_NULL_CHECK_GOTO(mavlink->port, err);

    mavlink->config = config;
    mavlink->data = data;

    bzero(&mavlink->curr_position, sizeof(mavlink->curr_position));
    bzero(&mavlink->home_position, sizeof(mavlink->home_position));

    if (init(mavlink) < 0) {
        SOL_ERR("Could not initialize mavlink connection.");
        goto err;
    }

    return mavlink;

err:
    free(mavlink);
    return NULL;
}

SOL_API void
sol_mavlink_disconnect(struct sol_mavlink *mavlink)
{
    SOL_NULL_CHECK(mavlink);
    close(mavlink->fd);
    free(mavlink);
}

SOL_API bool
sol_mavlink_set_armed(struct sol_mavlink *mavlink, bool armed)
{
    mavlink_message_t msg;
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    uint16_t len;
    bool curr;

    SOL_NULL_CHECK(mavlink, false);

    curr = sol_mavlink_check_armed(mavlink);
    SOL_EXP_CHECK(curr == !!armed, true);

    mavlink_msg_command_long_pack(mavlink->target_system, mavlink->target_comp,
        &msg, 0, 0, MAV_CMD_COMPONENT_ARM_DISARM, 0,
        !!armed, 0, 0, 0, 0, 0, 0);
    len = mavlink_msg_to_send_buffer(buf, &msg);
    return write(mavlink->fd, buf, len) == len;
}

SOL_API bool
sol_mavlink_takeoff(struct sol_mavlink *mavlink, struct sol_mavlink_position *pos)
{
    mavlink_message_t msg;
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    uint16_t len;

    SOL_NULL_CHECK(mavlink, false);
    SOL_NULL_CHECK(pos, false);

    mavlink_msg_command_long_pack
        (mavlink->target_system, mavlink->target_comp, &msg, 0, 0,
        MAV_CMD_NAV_TAKEOFF, 0, pos->x, 0, 0, pos->x, pos->latitude,
        pos->longitude, pos->altitude);

    len = mavlink_msg_to_send_buffer(buf, &msg);
    return write(mavlink->fd, buf, len) == len;
}

SOL_API bool
sol_mavlink_land(struct sol_mavlink *mavlink, struct sol_mavlink_position *pos)
{
    mavlink_message_t msg;
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    uint16_t len;

    SOL_NULL_CHECK(mavlink, false);
    SOL_NULL_CHECK(pos, false);

    mavlink_msg_command_long_pack
        (mavlink->target_system, mavlink->target_comp, &msg, 0, 0,
        MAV_CMD_NAV_LAND, 0, 0, 0, 0, pos->x, pos->latitude,
        pos->longitude, pos->altitude);

    len = mavlink_msg_to_send_buffer(buf, &msg);
    return write(mavlink->fd, buf, len) == len;
}

SOL_API bool
sol_mavlink_set_mode(struct sol_mavlink *mavlink, sol_mavlink_mode mode)
{
    mavlink_message_t msg;
    uint8_t buf[MAVLINK_MAX_PACKET_LEN], custom_mode;
    uint16_t len;

    SOL_NULL_CHECK(mavlink, false);

    custom_mode = sol_mode_to_mavlink_mode_lookup(mavlink->type, mode);
    SOL_INT_CHECK(custom_mode, == MAV_MODE_ENUM_END, false);

    mavlink_msg_set_mode_pack
        (0, 0, &msg, mavlink->target_system, mavlink->base_mode, custom_mode);

    len = mavlink_msg_to_send_buffer(buf, &msg);
    return write(mavlink->fd, buf, len) == len;
}

SOL_API bool
sol_mavlink_check_mode(struct sol_mavlink *mavlink, sol_mavlink_mode mode)
{
    SOL_NULL_CHECK(mavlink, false);
    return mavlink->mode == mode;
}

SOL_API bool
sol_mavlink_check_armed(struct sol_mavlink *mavlink)
{
    uint8_t mask, base_mode;

    SOL_NULL_CHECK(mavlink, false);

    if (mavlink->custom_mode_enabled)
        mask = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED;

    base_mode = mavlink->base_mode ^ mask;

    switch (base_mode) {
    case MAV_MODE_MANUAL_ARMED:
        return true;
    case MAV_MODE_TEST_ARMED:
        return true;
    case MAV_MODE_STABILIZE_ARMED:
        return true;
    case MAV_MODE_GUIDED_ARMED:
        return true;
    case MAV_MODE_AUTO_ARMED:
        return true;
    default:
        return false;
    }
}

SOL_API bool
sol_mavlink_get_curr_position(struct sol_mavlink *mavlink, struct sol_mavlink_position *pos)
{
    SOL_NULL_CHECK(mavlink, false);
    SOL_NULL_CHECK(pos, false);

    pos->latitude = mavlink->curr_position.latitude;
    pos->longitude = mavlink->curr_position.longitude;
    pos->altitude = mavlink->curr_position.altitude;

    return true;
}

SOL_API bool
sol_mavlink_get_home_position(struct sol_mavlink *mavlink, struct sol_mavlink_position *pos)
{
    SOL_NULL_CHECK(mavlink, false);
    SOL_NULL_CHECK(pos, false);

    pos->latitude = mavlink->home_position.latitude;
    pos->longitude = mavlink->home_position.longitude;
    pos->altitude = mavlink->home_position.altitude;
    pos->x = mavlink->home_position.x;
    pos->y = mavlink->home_position.y;
    pos->z = mavlink->home_position.z;

    return true;
}
