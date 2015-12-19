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
 * small unmanned vehicle. It is designed as a header-only message marshaling
 * library.
 *
 * @{
 */

/**
 * @struct sol_mavlink
 *
 * @brief Mavlink Object
 *
 * @see sol_mavlink_connect
 *
 * This object is the abstraction of a Mavlink connection. This is the base
 * structure for all Mavlink operations and is obtained through the
 * sol_mavlink_connect API.
 */
struct sol_mavlink;

typedef enum {
    SOL_MAVLINK_MODE_ACRO = 1,
    SOL_MAVLINK_MODE_ALT_HOLD = 2,
    SOL_MAVLINK_MODE_ATTITUDE = 3,
    SOL_MAVLINK_MODE_AUTO = 4,
    SOL_MAVLINK_MODE_AUTOTUNE = 5,
    SOL_MAVLINK_MODE_CIRCLE = 6,
    SOL_MAVLINK_MODE_CRUISE = 7,
    SOL_MAVLINK_MODE_DRIFT = 8,
    SOL_MAVLINK_MODE_EASY = 9,
    SOL_MAVLINK_MODE_FBWA = 10,
    SOL_MAVLINK_MODE_FBWB = 11,
    SOL_MAVLINK_MODE_FLIP = 12,
    SOL_MAVLINK_MODE_GUIDED = 13,
    SOL_MAVLINK_MODE_HOLD = 14,
    SOL_MAVLINK_MODE_INITIALISING = 15,
    SOL_MAVLINK_MODE_LAND = 16,
    SOL_MAVLINK_MODE_LEARNING = 17,
    SOL_MAVLINK_MODE_LOITER = 18,
    SOL_MAVLINK_MODE_MANUAL = 19,
    SOL_MAVLINK_MODE_OF_LOITER = 20,
    SOL_MAVLINK_MODE_POSHOLD = 21,
    SOL_MAVLINK_MODE_POSITION = 22,
    SOL_MAVLINK_MODE_RTL = 23,
    SOL_MAVLINK_MODE_SCAN = 24,
    SOL_MAVLINK_MODE_SPORT = 25,
    SOL_MAVLINK_MODE_STABILIZE = 26,
    SOL_MAVLINK_MODE_STEERING = 27,
    SOL_MAVLINK_MODE_STOP = 28,
    SOL_MAVLINK_MODE_TRAINING = 29,
    SOL_MAVLINK_MODE_UNKNOWN = 30,
} sol_mavlink_mode;

/**
 * @struct sol_mavlink_position
 *
 * @brief Mavlink position structure
 */
struct sol_mavlink_position {
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
};

/**
 * @struct sol_mavlink_handlers
 *
 * @brief Mavlink callback handlers
 */
struct sol_mavlink_handlers {
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
     * @see sol_mavlink_check_armed
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
     * @see sol_mavlink_check_armed
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
     * @see sol_mavlink_takeoff
     * @see sol_mavlink_get_curr_position
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
     * @see sol_mavlink_takeoff
     * @see sol_mavlink_goto
     *
     * Callback called when the vehicle has reached the current mission's
     * destination.
     */
    void (*mission_reached) (void *data, struct sol_mavlink *mavlink);
};

/**
 * @struct sol_mavlink_config
 *
 * @brief Server Configuration
 */
struct sol_mavlink_config {
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
};

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
struct sol_mavlink *sol_mavlink_connect(const char *addr, const struct sol_mavlink_config *config, void *data);

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
 * @see sol_mavlink_check_armed
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
 * @see sol_mavlink_check_armed
 * @see sol_mavlink_get_mode
 * @see SOL_MAVLINK_MODE_GUIDED
 *
 * @return 0 on success, -EINVAL on invalid argument, -errno on error
 *
 * This call will attempt to take the vehicle off, for this the vehicle must
 * in SOL_MAVLINK_MODE_GUIDED and armed. If the vehicle has already taken off
 * calling this function will have no effect.
 */
int sol_mavlink_takeoff(struct sol_mavlink *mavlink, struct sol_mavlink_position *pos);

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
int sol_mavlink_set_mode(struct sol_mavlink *mavlink, sol_mavlink_mode mode);

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
sol_mavlink_mode sol_mavlink_get_mode(struct sol_mavlink *mavlink);

/**
 * @brief Check if the vehicle is currently armed
 *
 * @param mavlink Mavlink Object;
 *
 * @see sol_mavlink_set_armed
 *
 * @return true the vehicle is currently armed, false otherwise
 */
bool sol_mavlink_check_armed(struct sol_mavlink *mavlink);

/**
 * @brief Get the vehicle's current position
 *
 * @param mavlink Mavlink Object;
 * @param pos sol_mavlink_position pointer to set the positions values to;
 *
 * @return 0 on success, -EINVAL on invalid argument
 */
int sol_mavlink_get_curr_position(struct sol_mavlink *mavlink, struct sol_mavlink_position *pos);

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
int sol_mavlink_goto(struct sol_mavlink *mavlink, struct sol_mavlink_position *pos);

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
