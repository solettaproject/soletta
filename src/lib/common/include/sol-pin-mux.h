/*
 * This file is part of the Soletta Project
 *
 * Copyright (c) 2015, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#pragma once

#include <stdint.h>

#include "sol-gpio.h"

enum sol_io_protocol {
    SOL_IO_AIO = 0x01,
    SOL_IO_GPIO = 0x02,
    SOL_IO_I2C = 0x04,
    SOL_IO_PWM = 0x08,
    SOL_IO_SPI = 0x10,
    SOL_IO_UART = 0x20,
};

/**
 * Select Pin Multiplexer instructions of a given board.
 *
 * Searches(and activate if found) for pin multiplexing instructions of the given board.
 * Built-in Pin Multiplexers have priority over modules.
 *
 * @param board The board name being used
 *
 * @return 'true' on success, 'false' otherwise.
 */
bool sol_pin_mux_select_mux(const char *board);

/**
 * Maps a pin label to the parameters necessary so it works on the desired protocol.
 *
 * Find if a given pin labeled 'label' is capable of operate on protocol 'prot' and return
 * the parameters needed to setup the protocol.
 *
 * @param label The label of the pin as see on the board
 * @param prot Protocol on which the pin should operate
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_pin_mux_map(const char *label, const enum sol_io_protocol prot, ...);

/**
 * Setup the given pin to operate as Analog I/O.
 *
 * If a pin multiplexer is loaded (from a successfully call to sol_pin_mux_select_mux),
 * execute the instructions needed to configure 'device'/'pin' pair to operate as Analog I/O.
 *
 * @param device the aio device number.
 * @param pin the aio pin number.
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_pin_mux_setup_aio(const int device, const int pin);

/**
 * Setup the given pin to operate as GPIO in the given direction (in or out).
 *
 * If a pin multiplexer is loaded (from a successfully call to sol_pin_mux_select_mux),
 * execute the instructions needed to configure 'pin' to operate as GPIO in direction
 * 'dir'.
 *
 * @param pin the gpio pin number.
 * @param dir direction (in or out) that the pin should operate.
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_pin_mux_setup_gpio(const int pin, const enum sol_gpio_direction dir);

/**
 * Setup the pins used of the given i2c bus number to operate in I2C mode.
 *
 * If a pin multiplexer is loaded (from a successfully call to sol_pin_mux_select_mux),
 * execute the instructions needed to configure the pins used by the given i2c bus
 * to operate in I2C mode.
 *
 * @param bus the i2c bus number.
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_pin_mux_setup_i2c(const uint8_t bus);

/**
 * Setup the given pin to operate as PWM.
 *
 * If a pin multiplexer is loaded (from a successfully call to sol_pin_mux_select_mux),
 * execute the instructions needed to configure 'device'/'channel' pair to operate as PWM.
 *
 * @param device the pwm device number.
 * @param channel the channel number on device.
 *
 * @return '0' on success, error code (always negative) otherwise.
 */
int sol_pin_mux_setup_pwm(const int device, const int channel);
