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
#include <sol-macros.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for GPIO access under Solleta.
 */

/**
 * @defgroup IO I/O
 *
 * These routines are used for general I/O access under Solleta
 * (namely GPIO, PWM, SPI, UART and I2C).
 *
 * @{
 */

struct sol_gpio;

enum sol_gpio_direction {
    SOL_GPIO_DIR_UNDEFINED = -1,
    SOL_GPIO_DIR_OUT = 0,
    SOL_GPIO_DIR_IN = 1
};

enum sol_gpio_edge {
    SOL_GPIO_EDGE_NONE = 0,
    SOL_GPIO_EDGE_RISING,
    SOL_GPIO_EDGE_FALLING,
    SOL_GPIO_EDGE_BOTH
};

enum sol_gpio_drive {
    SOL_GPIO_DRIVE_NONE = 0,
    SOL_GPIO_DRIVE_PULL_UP,
    SOL_GPIO_DRIVE_PULL_DOWN
};

struct sol_gpio_config {
#define SOL_GPIO_CONFIG_API_VERSION (1)
    uint16_t api_version;
    enum sol_gpio_direction dir;
    bool active_low;
    enum sol_gpio_drive drive_mode;
    union {
        struct {
            enum sol_gpio_edge trigger_mode;
            void (*cb)(void *data, struct sol_gpio *gpio);
            const void *user_data;
            int poll_timeout; /* Will be used if interruptions are not possible */
        } in;
        struct {
            bool value;
        } out;
    };
};

struct sol_gpio *sol_gpio_open(int pin, const struct sol_gpio_config *config) SOL_ATTR_WARN_UNUSED_RESULT;
struct sol_gpio *sol_gpio_open_raw(int pin, const struct sol_gpio_config *config) SOL_ATTR_WARN_UNUSED_RESULT;
void sol_gpio_close(struct sol_gpio *gpio);

bool sol_gpio_write(struct sol_gpio *gpio, bool value);
int sol_gpio_read(struct sol_gpio *gpio);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
