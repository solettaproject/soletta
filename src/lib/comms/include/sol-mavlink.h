/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Routines to handle Mavlink protocol.
 *
 * Wrapper library for Mavlink communication.
 */

/**
 * @defgroup MAVLINK MAVLINK
 * @ingroup Comms
 *
 * MAVLink or Micro Air Vehicle Link is a protocol for communicating with
 * small unmanned vehicles. It is designed as a header-only message marshaling
 * library.
 *
 * @warning Experimental API. Changes are expected in future releases.
 *
 * @{
 */

/**
 * @typedef sol_mavlink
 *
 * @brief Mavlink Object
 *
 * @see sol_mavlink_connect
 *
 * This object is the abstraction of a Mavlink connection. This is the base
 * structure for all Mavlink operations and is obtained through the
 * sol_mavlink_connect() API.
 */
struct sol_mavlink;
typedef struct sol_mavlink sol_mavlink;

/**
 * @enum sol_mavlink_mode
 *
 * @brief Mavlink flight modes
 */
enum sol_mavlink_mode {
    /**
     * Acro mode (Rate mode) uses the RC sticks to control the angular velocity
     * of the copter.
     */
    SOL_MAVLINK_MODE_ACRO = 1,

    /**
     * In altitude hold mode, Copter maintains a consistent altitude while
     * allowing roll, pitch, and yaw to be controlled normally.
     */
    SOL_MAVLINK_MODE_ALT_HOLD = 2,
    SOL_MAVLINK_MODE_ALTITUDE = 3,

    /**
     * In Auto mode the copter will follow a pre-programmed mission script
     * stored in the autopilot which is made up of navigation commands
     * (i.e. waypoints) and “do” commands (i.e. commands that do not affect the
     * location of the copter including triggering a camera shutter).
     */
    SOL_MAVLINK_MODE_AUTO = 4,

    SOL_MAVLINK_MODE_AUTO_TUNE = 5,

    /**
     * In circle mode the copter will orbit a point of interest with the nose
     * of the vehicle pointed towards the center.
     */
    SOL_MAVLINK_MODE_CIRCLE = 6,

    SOL_MAVLINK_MODE_CRUISE = 7,

    /**
     * Drift Mode allows the user to fly a multi-copter as if it were a plane
     * with built in automatic coordinated turns.
     */
    SOL_MAVLINK_MODE_DRIFT = 8,

    SOL_MAVLINK_MODE_EASY = 9,
    SOL_MAVLINK_MODE_FBWA = 10,
    SOL_MAVLINK_MODE_FBWB = 11,
    SOL_MAVLINK_MODE_FLIP = 12,

    /**
     * Guided mode is a capability of Copter to dynamically guide the copter to
     * a target location wirelessly using a telemetry radio module, ground
     * station application or a companion board application.
     */
    SOL_MAVLINK_MODE_GUIDED = 13,
    SOL_MAVLINK_MODE_HOLD = 14,
    SOL_MAVLINK_MODE_INITIALISING = 15,

    /**
     * Land mode attempts to bring the copter straight down.
     */
    SOL_MAVLINK_MODE_LAND = 16,
    SOL_MAVLINK_MODE_LEARNING = 17,

    /**
     * Loiter mode automatically attempts to maintain the current location,
     * heading and altitude.
     */
    SOL_MAVLINK_MODE_LOITER = 18,
    SOL_MAVLINK_MODE_MANUAL = 19,
    SOL_MAVLINK_MODE_OF_LOITER = 20,

    /**
     * It is similar to Loiter in that the vehicle maintains a constant location,
     * heading, and altitude but is generally more popular because the pilot
     * stick inputs directly control the vehicle’s lean angle providing a more
     * “natural” feel.
     */
    SOL_MAVLINK_MODE_POS_HOLD = 21,

    /**
     * Position mode is the same as loiter mode, but with manual throttle
     * control.
     */
    SOL_MAVLINK_MODE_POSITION = 22,

    /**
     * RTL mode (Return To Launch mode) navigates Copter from its current
     * position to hover above the home position.
     */
    SOL_MAVLINK_MODE_RTL = 23,
    SOL_MAVLINK_MODE_SCAN = 24,

    /**
     * Sport Mode is also known as “rate controlled stabilize” plus Altitude
     * Hold.
     */
    SOL_MAVLINK_MODE_SPORT = 25,

    /**
     * Stabilize mode allows you to fly your vehicle manually, but self-levels
     * the roll and pitch axis.
     */
    SOL_MAVLINK_MODE_STABILIZE = 26,
    SOL_MAVLINK_MODE_STEERING = 27,
    SOL_MAVLINK_MODE_STOP = 28,
    SOL_MAVLINK_MODE_TRAINING = 29,
    SOL_MAVLINK_MODE_UNKNOWN = 30,
};

/**
 * @brief Mavlink position structure
 */
typedef struct sol_mavlink_position {
    /** Latitude in degrees */
    float latitude;

    /** Longitude in degrees */
    float longitude;

    /** Altitude in meters */
    float altitude;

    /** Local X position of this position in the local coordinate frame */
    float x;

    /** Local Y position of this position in the local coordinate frame */
    float y;

    /** Local Z position of this position in the local coordinate frame */
    float z;
} sol_mavlink_position;

/**
 * @brief Mavlink callback handlers
 */
typedef struct sol_mavlink_handlers {
#ifndef SOL_NO_API_VERSION
#define SOL_MAVLINK_HANDLERS_API_VERSION (1)
    /**
     * Should always be set to SOL_MAVLINK_HANDLERS_API_VERSION
     */
    uint16_t api_version;
#endif

    /**
     * @brief On connect callback
     *
     * @param data User provided data
     * @param mavlink Mavlink Object
     *
     * @see sol_mavlink_connect
     *
     * Callback called when a connect request has been processed
     */
    void (*connect) (void *data, struct sol_mavlink *mavlink);

    /**
     * @brief On mode changed callback
     *
     * @param data User provided data
     * @param mavlink Mavlink Object
     *
     * @see sol_mavlink_set_mode
     *
     * Callback called when a mode change has been processed
     */
    void (*mode_changed) (void *data, struct sol_mavlink *mavlink);

    /**
     * @brief On armed callback
     *
     * @param data User provided data
     * @param mavlink Mavlink Object
     *
     * @see sol_mavlink_set_armed
     * @see sol_mavlink_is_armed
     *
     * Callback called when the vehicle has been armed, no matter if
     * it was armed by your application or not
     */
    void (*armed) (void *data, struct sol_mavlink *mavlink);

    /**
     * @brief On armed callback
     *
     * @param data User provided data
     * @param mavlink Mavlink Object
     *
     * @see sol_mavlink_set_armed
     * @see sol_mavlink_is_armed
     *
     * Callback called when the vehicle has been disarmed, no matter if
     * it was disarmed by your application or not
     */
    void (*disarmed) (void *data, struct sol_mavlink *mavlink);

    /**
     * @brief On position changed callback
     *
     * @param data User provided data
     * @param mavlink Mavlink Object
     *
     * @see sol_mavlink_take_off
     * @see sol_mavlink_get_current_position
     *
     * Callback called when the vehicle has changed its position, no matter
     * if it was moved by your application or not
     */
    void (*position_changed) (void *data, struct sol_mavlink *mavlink);

    /**
     * @brief On destination reached callback
     *
     * @param data User provided data
     * @param mavlink Mavlink Object
     *
     * @see sol_mavlink_take_off
     * @see sol_mavlink_go_to
     *
     * Callback called when the vehicle has reached the current mission's
     * destination.
     */
    void (*mission_reached) (void *data, struct sol_mavlink *mavlink);
} sol_mavlink_handlers;

/**
 * @brief Server Configuration
 */
typedef struct sol_mavlink_config {
#ifndef SOL_NO_API_VERSION
#define SOL_MAVLINK_CONFIG_API_VERSION (1)
    /**
     * Should always be set to SOL_MAVLINK_CONFIG_API_VERSION
     */
    uint16_t api_version;
#endif

    /**
     * Handlers to be used with this connection
     */
    const struct sol_mavlink_handlers *handlers;

    /**
     * In case of serial protocol set the baud_rate, default set to 115200.
     */
    int baud_rate;
} sol_mavlink_config;

/**
 * @brief Connect to a mavlink server
 *
 * @param addr The target mavlink server address
 * @param config Configuration and callbacks
 * @param data User data provided to the callbacks
 *
 * @return New mavlink object on success, NULL otherwise
 *
 * The @b addr argument is composed of protocol:address:port where port is
 * optional depending on protocol.
 *
 * Currently supported protocols are tcp and serial, valid @b addr would be:
 *   tcp:localhost:5726
 *   serial:/dev/ttyUSB0
 */
struct sol_mavlink *sol_mavlink_connect(const char *addr, const struct sol_mavlink_config *config, const void *data);

/**
 * @brief Disconnect from mavlink server
 *
 * @param mavlink Mavlink Object;
 *
 * @see sol_mavlink_connect
 *
 * Terminate the connection with the mavlink server and free the resources
 * associated to the mavlink object.
 */
void sol_mavlink_disconnect(struct sol_mavlink *mavlink);

/**
 * @brief Set the vehicle to @b armed or not
 *
 * @param mavlink Mavlink Object;
 * @param armed true to set as armed, false otherwise;
 *
 * @see sol_mavlink_is_armed
 *
 * @return 0 on success, -EINVAL on invalid argument, -errno on error
 */
int sol_mavlink_set_armed(struct sol_mavlink *mavlink, bool armed);

/**
 * @brief Takeoff the vehicle
 *
 * @param mavlink Mavlink Object;
 * @param pos The target position;
 *
 * @see sol_mavlink_set_armed
 * @see sol_mavlink_is_armed
 * @see sol_mavlink_get_mode
 * @see SOL_MAVLINK_MODE_GUIDED
 *
 * @return 0 on success, -EINVAL on invalid argument, -errno on error
 *
 * This call will attempt to take the vehicle off, for this the vehicle must
 * in SOL_MAVLINK_MODE_GUIDED and armed. If the vehicle has already taken off
 * calling this function will have no effect.
 */
int sol_mavlink_take_off(struct sol_mavlink *mavlink, struct sol_mavlink_position *pos);

/**
 * @brief Set the vehicle @b mode
 *
 * @param mavlink Mavlink Object;
 * @param mode The mode to be set;
 *
 * @see sol_mavlink_get_mode
 * @see sol_mavlink_mode
 *
 * @return 0 on success, -EINVAL on invalid argument, -errno on error
 */
int sol_mavlink_set_mode(struct sol_mavlink *mavlink, enum sol_mavlink_mode mode);

/**
 * @brief Get the current vehicle's mode
 *
 * @param mavlink Mavlink Object;
 *
 * @see sol_mavlink_set_mode
 * @see sol_mavlink_mode
 *
 * @return The current vehicle's mode
 */
enum sol_mavlink_mode sol_mavlink_get_mode(struct sol_mavlink *mavlink);

/**
 * @brief Check if the vehicle is currently armed
 *
 * @param mavlink Mavlink Object;
 *
 * @see sol_mavlink_set_armed
 *
 * @return true the vehicle is currently armed, false otherwise
 */
bool sol_mavlink_is_armed(struct sol_mavlink *mavlink);

/**
 * @brief Get the vehicle's current position
 *
 * @param mavlink Mavlink Object;
 * @param pos sol_mavlink_position pointer to set the positions values to;
 *
 * @return 0 on success, -EINVAL on invalid argument
 */
int sol_mavlink_get_current_position(struct sol_mavlink *mavlink, struct sol_mavlink_position *pos);

/**
 * @brief Get the vehicle's home position
 *
 * @param mavlink Mavlink Object;
 * @param pos sol_mavlink_position pointer to set the positions values to;
 *
 * @return 0 on success, -EINVAL on invalid argument
 *
 * Home position represents the location and altitude where the vehicle
 * took off from.
 */
int sol_mavlink_get_home_position(struct sol_mavlink *mavlink, struct sol_mavlink_position *pos);

/**
 * @brief Land the vehicle's
 *
 * @param mavlink Mavlink Object;
 * @param pos sol_mavlink_position The position where it should land to;
 *
 * @return 0 on success, -EINVAL on invalid argument, -errno on error
 */
int sol_mavlink_land(struct sol_mavlink *mavlink, struct sol_mavlink_position *pos);

/**
 * @brief Navigate to a given location
 *
 * @param mavlink Mavlink Object;
 * @param pos sol_mavlink_position The position where the vehicle should go to;
 *
 * @return 0 on success, -EINVAL on invalid argument, -errno on error
 */
int sol_mavlink_go_to(struct sol_mavlink *mavlink, struct sol_mavlink_position *pos);

/**
 * @brief Change the vehicle speed
 *
 * @param mavlink Mavlink Object;
 * @param speed The desired speed in m/s;
 * @param airspeed True if @b speed in airspeed, groundspeed is used otherwise;
 *
 * @return 0 on success, -EINVAL on invalid argument, -errno on error
 */
int sol_mavlink_change_speed(struct sol_mavlink *mavlink, float speed, bool airspeed);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
