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

#include <stdbool.h>
#include <stdint.h>
#include <sol-common-buildopts.h>
#include <sol-macros.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for GPIO access under Soletta.
 */

/**
 * @defgroup IO I/O
 *
 * @brief These routines are used for general I/O access under Soletta
 * (namely GPIO, PWM, SPI, UART and I2C).
 *
 */

/**
 * @defgroup GPIO GPIO
 * @ingroup IO
 *
 * @brief GPIO (General Purpose Input/Output) API for Soletta.
 *
 * @{
 */

/**
 * @typedef sol_gpio
 * @brief A handle to a GPIO
 *
 * @see sol_gpio_open_by_label()
 * @see sol_gpio_open()
 * @see sol_gpio_open_raw()
 * @see sol_gpio_close()
 * @see sol_gpio_read()
 * @see sol_gpio_write()
 */
struct sol_gpio;
typedef struct sol_gpio sol_gpio;

/**
 * @brief Possible values for the direction of a GPIO.
 */
enum sol_gpio_direction {
    /**
     * The GPIO is an output.
     *
     * For things like LEDs.
     */
    SOL_GPIO_DIR_OUT = 0,
    /**
     * The GPIO is an input.
     *
     * For buttons or similar devices.
     */
    SOL_GPIO_DIR_IN = 1
};

/**
 * @brief Possible values for the edge mode of a GPIO.
 *
 * This indicate when an interrupt event should be generated.
 */
enum sol_gpio_edge {
    /**
     * Don't generate events.
     *
     * When using this mode, no interrupt handler will be registered and it's
     * up to the user to read the GPIO manually.
     */
    SOL_GPIO_EDGE_NONE = 0,
    /**
     * Events will be triggered on a rising edge.
     *
     * That is, when the state of the GPIO goes from low to high.
     */
    SOL_GPIO_EDGE_RISING,
    /**
     * Events will be triggered onf a falling edge.
     *
     * That is, when the state of the GPIO goes from high to low.
     */
    SOL_GPIO_EDGE_FALLING,
    /**
     * Events will be triggered for both edge levels.
     *
     * Both rising and falling edges will trigger events.
     */
    SOL_GPIO_EDGE_BOTH
};

/**
 * @brief Possible values for pull-up or pull-down resistor of a GPIO.
 *
 * It will avoid values to float when this pin isn't connected.
 * It'll define output value if nothing else is defined by software.
 */
enum sol_gpio_drive {
    /**
     * Do not set any state.
     */
    SOL_GPIO_DRIVE_NONE = 0,
    /**
     * When set as pull-up, resistor will be connected to VCC.
     * Logic value of output will be @c true while unset.
     */
    SOL_GPIO_DRIVE_PULL_UP,
    /**
     * When set as pull-down, resistor will be connected to ground.
     * Logic value of output will be @c false while unset.
     */
    SOL_GPIO_DRIVE_PULL_DOWN
};

/**
 * @brief Structure to hold the configuration of a GPIO device.
 *
 * When opening a GPIO with sol_gpio_open_by_label(), sol_gpio_open() or
 * sol_gpio_open_raw(), the parameters with which the GPIO is configured are
 * those defined in this structure.
 *
 * If there's a need to change any of these parameters, the GPIO must be closed
 * and opened again with a new configuration.
 */
typedef struct sol_gpio_config {
#ifndef SOL_NO_API_VERSION
#define SOL_GPIO_CONFIG_API_VERSION (1)
    uint16_t api_version; /**< The API version */
#endif
    /**
     * The direction in which to open the GPIO.
     */
    enum sol_gpio_direction dir;
    /**
     * Whether the GPIO is considered active when it's in a low state.
     *
     * If set, then the logical state of the GPIO will be reversed in relation
     * to the physical state. That is, for input GPIOs, when the current on
     * the wire goes to a low state, the value returned by sol_gpio_read() will
     * be @c true. Conversely, it will be @c false when the physical state is
     * high.
     *
     * The same logic applies for output GPIOs when a value is written through
     * sol_gpio_write().
     *
     * This is useful to keep the application logic simpler in the face of
     * different hardware configurations.
     */
    bool active_low;
    /**
     * Pull-up or pull-down resistor state for this GPIO.
     *
     * One of #sol_gpio_drive. Some platforms will configure GPIO taking
     * this in consideration, as Continki and RIOT.
     */
    enum sol_gpio_drive drive_mode;
    union {
        /**
         * Configuration parameters for input GPIOs.
         */
        struct {
            /**
             * When to trigger events for this GPIO.
             *
             * One of #sol_gpio_edge. If the value set is anything other
             * than #SOL_GPIO_EDGE_NONE, then the @c cb member must be set.
             */
            enum sol_gpio_edge trigger_mode;
            /**
             * The function to call when an event happens.
             *
             * Different systems handle interruptions differently, and so to
             * maintain consistency across them, there is no queue of values
             * triggered by interruptions. Instead, when an interruption
             * happens, the main loop will handle it and call the user function
             * provided here, with the value of the GPIO at that time.
             * This means that the if the application takes too long to return
             * to the main loop while interruptions are happening, some of those
             * values will be lost.
             *
             * @param data The user data pointer provided in @c user_data.
             * @param gpio The GPIO instance that triggered the event.
             * @param value The value of the GPIO at the moment the function
             *              is called.
             */
            void (*cb)(void *data, struct sol_gpio *gpio, bool value);
            /**
             * User data poinetr to pass to the @c cb function.
             */
            const void *user_data;
            /**
             * Time to poll for events, in milliseconds.
             *
             * In the case that interruptions are not supported by the selected
             * GPIO, the implementation will fall back to polling the pin for
             * changes in its value.
             *
             * The @c cb function provided will be called only when a change
             * in the value is detected, so if the timeout is too long, it may
             * lose events.
             */
            uint32_t poll_timeout;
        } in;
        /**
         * Configuration parameters for output GPIOs.
         */
        struct {
            /**
             * The initial value to write when the GPIO is opened.
             */
            bool value;
        } out;
    };
} sol_gpio_config;

/**
 * @brief Converts a string GPIO direction to sol_gpio_direction.
 *
 * This function converts a string GPIO direction to enumeration sol_gpio_direction.
 *
 * @see sol_gpio_direction_to_str().
 *
 * @param direction Valid values are "in", "out".
 *
 * @return enumeration sol_gpio_direction.
 */
enum sol_gpio_direction sol_gpio_direction_from_str(const char *direction)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts sol_gpio_direction to a string name.
 *
 * This function converts sol_gpio_direction enumeration to a string GPIO direction name.
 *
 * @see sol_gpio_direction_from_str().
 *
 * @param direction sol_gpio_direction.
 *
 * @return String representation of the sol_gpio_direction.
 */
const char *sol_gpio_direction_to_str(enum sol_gpio_direction direction)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts a string GPIO edge to sol_gpio_edge
 *
 * This function converts a string GPIO edge to enumeration sol_gpio_edge
 *
 * @see sol_gpio_edge_to_str().
 *
 * @param edge Valid values are "none", "rising", "falling", "any".
 *
 * @return enumeration sol_gpio_edge
 */
enum sol_gpio_edge sol_gpio_edge_from_str(const char *edge)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts sol_gpio_edge to a string name.
 *
 * This function converts sol_gpio_edge enumeration to a string GPIO edge name
 *
 * @see sol_gpio_edge_from_str().
 *
 * @param edge sol_gpio_edge
 *
 * @return String representation of the sol_gpio_edge
 */
const char *sol_gpio_edge_to_str(enum sol_gpio_edge edge)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts a string GPIO drive to sol_gpio_drive.
 *
 * This function converts a string GPIO drive to enumeration sol_gpio_drive.
 *
 * @see sol_gpio_drive_to_str().
 *
 * @param drive Valid values are "none", "up", "down".
 *
 * @return enumeration sol_gpio_drive.
 */
enum sol_gpio_drive sol_gpio_drive_from_str(const char *drive)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Converts sol_gpio_drive to a string name.
 *
 * This function converts sol_gpio_drive enumeration to a string GPIO drive name.
 *
 * @see sol_gpio_drive_from_str().
 *
 * @param drive sol_gpio_drive.
 *
 * @return String representation of the sol_gpio_drive.
 */
const char *sol_gpio_drive_to_str(enum sol_gpio_drive drive)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Opens a given pin by its board label as general purpose input or output.
 *
 * This function only works when the board was successfully detected
 * by Soletta and a corresponding
 * pin multiplexer module was found.
 *
 * A pin should be opened just once, calling this function more than once
 * for the same pin results in undefined behavior - per platform basis.
 *
 * @see sol_gpio_open_raw(), sol_gpio_close().
 *
 * @param label The pin to be opened.
 * @param config Contains the pin configuration.
 *
 * @return A new @c sol_gpio instance on success, @c NULL otherwise.
 */
struct sol_gpio *sol_gpio_open_by_label(const char *label, const struct sol_gpio_config *config)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Opens a given pin as general purpose input or output.
 *
 * A pin should be opened just once, calling this function more than once
 * for the same pin results in undefined behaviour - per platform basis.
 *
 * @see sol_gpio_open_raw(), sol_gpio_close().
 *
 * @param pin The pin to be opened.
 * @param config Contains the pin configuration.
 *
 * @return A new @c sol_gpio instance on success, @c NULL otherwise.
 */
struct sol_gpio *sol_gpio_open(uint32_t pin, const struct sol_gpio_config *config)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;


/**
 * @brief Opens a given pin as general purpose input or output.
 *
 * A pin should be opened just once, calling this function more than once
 * for the same pin results in undefined behaviour - per platform basis.
 *
 * @see sol_gpio_open(), sol_gpio_close().
 *
 * @param pin The pin to be opened.
 * @param config Contains the pin configuration.
 *
 * @return A new @c sol_gpio instance on success, @c NULL otherwise.
 */
struct sol_gpio *sol_gpio_open_raw(uint32_t pin, const struct sol_gpio_config *config)
#ifndef DOXYGEN_RUN
    SOL_ATTR_WARN_UNUSED_RESULT
#endif
    ;

/**
 * @brief Closes a given GPIO pin.
 *
 * @see sol_gpio_open(), sol_gpio_open_raw().
 *
 * @param gpio The open @c sol_gpio representing the pin to be closed.
 */
void sol_gpio_close(struct sol_gpio *gpio);

/**
 * @brief Set an arbitrary @c value to @c pin.
 *
 * @param gpio The opened @c sol_gpio that contains the @c pin that will be used.
 * @param value The value that will be set to the pin.
 *
 * @return @c true on success, otherwise @c false.
 */
bool sol_gpio_write(struct sol_gpio *gpio, bool value);

/**
 * @brief Get the current @c value set to @c pin.
 *
 * @param gpio The opened @c sol_gpio that contains the @c pin that will be read.
 *
 * @return The value of the pin on success, otherwise a negative errno value.
 */
int sol_gpio_read(struct sol_gpio *gpio);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
