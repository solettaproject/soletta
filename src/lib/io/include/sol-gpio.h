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
 * These routines are used for general I/O access under Soletta
 * (namely GPIO, PWM, SPI, UART and I2C).
 *
 */

/**
 * @defgroup GPIO GPIO
 * @ingroup IO
 *
 * GPIO (General Purpose Input/Output) API for Soletta.
 *
 * @{
 */

struct sol_gpio;

/**
 * Possible values for the direction of a GPIO.
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
 * Possible values for the edge mode of a GPIO.
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

enum sol_gpio_drive {
    SOL_GPIO_DRIVE_NONE = 0,
    SOL_GPIO_DRIVE_PULL_UP,
    SOL_GPIO_DRIVE_PULL_DOWN
};

/**
 * Structure to hold the configuration of a GPIO device.
 *
 * When opening a GPIO with sol_gpio_open_by_label(), sol_gpio_open() or
 * sol_gpio_open_raw(), the parameters with which the GPIO is configured are
 * those defined in this structure.
 *
 * If there's a need to change any of these parameters, the GPIO must be closed
 * and opened again with a new configuration.
 */
struct sol_gpio_config {
#ifndef SOL_NO_API_VERSION
#define SOL_GPIO_CONFIG_API_VERSION (1)
    uint16_t api_version;
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
    enum sol_gpio_drive drive_mode;
    union {
        /**
         * Configuration parameters for input GPIOs.
         */
        struct {
            /**
             * When to trigger events for this GPIO.
             *
             * One of #sol_gpio_drive. If the value set is anything other
             * than #SOL_GPIO_DRIVE_NONE, then the @c cb member must be set.
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
};

/**
 * Opens a given pin by its board label as general purpose input or output.
 *
 * This function only works when the board was successfully detect by Soletta and a corresponding
 * pin multiplexer module was found.
 *
 * A pin should be opened just once, calling this function more than once
 * for the same pin results in undefined behavior - per platform basis.
 *
 * @see sol_gpio_open_raw(), sol_gpio_close().
 *
 * @param pin The pin to be opened.
 * @param config Contains the pin configuration.
 *
 * @return A new @c sol_gpio instance on success, @c NULL otherwise.
 */
struct sol_gpio *sol_gpio_open_by_label(const char *label, const struct sol_gpio_config *config) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * Opens a given pin as general purpose input or output.
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
struct sol_gpio *sol_gpio_open(uint32_t pin, const struct sol_gpio_config *config) SOL_ATTR_WARN_UNUSED_RESULT;


/**
 * Opens a given pin as general purpose input or output.
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
struct sol_gpio *sol_gpio_open_raw(uint32_t pin, const struct sol_gpio_config *config) SOL_ATTR_WARN_UNUSED_RESULT;

/**
 * Closes a given GPIO pin.
 *
 * @see sol_gpio_open(), sol_gpio_open_raw().
 *
 * @param gpio The open @c sol_gpio representing the pin to be closed.
 */
void sol_gpio_close(struct sol_gpio *gpio);

/**
 * Set an arbitrary @c value to @c pin.
 *
 * @param gpio The opened @c sol_gpio that contains the @c pin that will be used.
 * @param value The value that will be set to the pin.
 *
 * @return @c true on success, otherwise @c false.
 */
bool sol_gpio_write(struct sol_gpio *gpio, bool value);

/**
 * Get the current @c value set to @c pin.
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
