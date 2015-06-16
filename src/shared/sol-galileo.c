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

/*
 * This file is Devil reincarnated (whitelist), so first we need to ask for forgiveness:
 *
 * Our Father in heaven, hallowed be your name, your kingdom come,
 * your will be done on earth as it is in heaven.
 * Give us today our daily bread.
 * Forgive us our debts, as we also have forgiven our debtors.
 * And lead us not into temptation, but deliver us from the evil one.
 * Amen.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <limits.h>

#include "sol-galileo.h"
#include "sol-util.h"

#define LOW                 0
#define HIGH                    1
#define NONE                    2

// PIN MODES
#define FN_GPIO_INPUT_PULLUP            0x01
#define FN_GPIO_INPUT_PULLDOWN          0x02
#define FN_GPIO_INPUT_HIZ           0x04
#define FN_GPIO_OUTPUT              0x08
#define FN_PWM                  0x10
#define FN_I2C                  0x20
#define FN_ANALOG               0x40
#define FN_UART                 0x80
#define FN_SPI                  0x100
#define FN_SWITCH               0x200   // Output-only, for toggling board level mux switches, pull-ups, buffers, leds, etc.
#define FN_RESERVED             0x400   // Reserved - not to be touched by sketch layer

/* Combinations of the above for convenience */
#define FN_GPIO_INPUT (FN_GPIO_INPUT_PULLUP | FN_GPIO_INPUT_PULLDOWN | FN_GPIO_INPUT_HIZ)
#define FN_GPIO (FN_GPIO_INPUT | FN_GPIO_OUTPUT)

#define GPIO_DRIVE_PULLUP           0
#define GPIO_DRIVE_PULLDOWN         1
#define GPIO_DRIVE_STRONG           2
#define GPIO_DRIVE_HIZ              3

#define BASE "/sys/class/gpio"

#define PIN_EINVAL 0xFFFFFFFF

#define MUX_SIZE(x) sizeof(x) / sizeof(mux_sel_t)

// =============================================================================
// Galileo Mux Settings
// =============================================================================
typedef struct mux_select {
    int pin; // GPIOLib ID that controls the mux
    uint32_t val; // HIGH, LOW or NONE to disable output (HiZ INPUT)
    uint32_t func; // Function in the HIGH or LOW state
}mux_sel_t;

static mux_sel_t MuxDesc0[] = {
    //gpio, value, type
    { 32, LOW, FN_GPIO_OUTPUT }, // Output enable
    { 32, HIGH, FN_UART | FN_GPIO_INPUT }, // Output disable
    { 33, NONE, FN_UART | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 33, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
};

static mux_sel_t MuxDesc1[] = {
    //gpio, value, type
    { 45, LOW, FN_GPIO },
    { 45, HIGH, FN_UART },
    { 28, LOW, FN_UART | FN_GPIO_OUTPUT }, // Output enable
    { 28, HIGH, FN_GPIO_INPUT }, // Output disable
    { 29, NONE, FN_UART | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 29, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 29, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc2[] = {
    //gpio, value, type
    { 77, LOW, FN_GPIO },
    { 77, HIGH, FN_UART },
    { 34, LOW, FN_GPIO_OUTPUT }, // Output disable
    { 34, HIGH, FN_UART | FN_GPIO_INPUT }, // Output enable
    { 35, NONE, FN_UART | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 35, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 35, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
    { 13, NONE, FN_GPIO }, // Disable GPIO #13 output, connected on same pin
    { 61, NONE, FN_UART }, // Disable GPIO #61 output, connected on same pin
};

static mux_sel_t MuxDesc3[] = {
    //gpio, value, type
    { 64, LOW, FN_GPIO },
    { 64, HIGH, FN_PWM },
    { 76, LOW, FN_GPIO | FN_PWM },
    { 76, HIGH, FN_UART },
    { 16, LOW, FN_UART | FN_PWM | FN_GPIO_OUTPUT }, // Output enable
    { 16, HIGH, FN_GPIO_INPUT }, // Output disable
    { 17, NONE, FN_UART | FN_PWM | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 17, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 17, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
    { 14, NONE, FN_GPIO }, // Disable GPIO #14 output, connected on same pin
    { 62, NONE, FN_PWM | FN_UART }, // Disable GPIO #62 output, connected on same pin
};

static mux_sel_t MuxDesc4[] = {
    //gpio, value, type
    { 36, LOW, FN_GPIO_OUTPUT }, // Output enable
    { 36, HIGH, FN_GPIO_INPUT }, // Output disable
    { 37, NONE, FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 37, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 37, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc5[] = {
    //gpio, value, type
    { 66, LOW, FN_GPIO },
    { 66, HIGH, FN_PWM },
    { 18, LOW, FN_PWM | FN_GPIO_OUTPUT }, // Output-enable
    { 18, HIGH, FN_GPIO_INPUT }, // Output-disable
    { 19, NONE, FN_PWM | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 19, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 19, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc6[] = {
    //gpio, value, type
    { 68, LOW, FN_GPIO },
    { 68, HIGH, FN_PWM },
    { 20, LOW, FN_PWM | FN_GPIO_OUTPUT }, // Output-enable
    { 20, HIGH, FN_GPIO_INPUT }, // Output-disable
    { 21, NONE, FN_PWM | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 21, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 21, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc7[] = {
    //gpio, value, type
    { 39, NONE, FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 39, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 39, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc8[] = {
    //gpio, value, type
    { 41, NONE, FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 41, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 41, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc9[] = {
    //gpio, value, type
    { 70, LOW, FN_GPIO },
    { 70, HIGH, FN_PWM },
    { 22, LOW, FN_PWM | FN_GPIO_OUTPUT }, // Output-enable
    { 22, HIGH, FN_GPIO_INPUT }, // Output-disable
    { 23, NONE, FN_PWM | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 23, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 23, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc10[] = {
    //gpio, value, type
    { 74, LOW, FN_GPIO },
    { 74, HIGH, FN_PWM },
    { 26, LOW, FN_PWM | FN_GPIO_OUTPUT }, // Output enable
    { 26, HIGH, FN_GPIO_INPUT }, // Output disable
    { 27, NONE, FN_PWM | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 27, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 27, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc11[] = {
    //gpio, value, type
    { 44, LOW, FN_GPIO },
    { 44, HIGH, FN_SPI },
    { 72, LOW, FN_GPIO },
    { 72, LOW, FN_SPI },
    { 72, HIGH, FN_PWM },
    { 24, LOW, FN_PWM | FN_SPI | FN_GPIO_OUTPUT }, // Output-enable
    { 24, HIGH, FN_GPIO_INPUT }, // Output-enable
    { 25, NONE, FN_PWM | FN_SPI | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 25, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 25, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc12[] = {
    //gpio, value, type
    { 42, LOW, FN_GPIO_OUTPUT }, // Output enable
    { 42, HIGH, FN_SPI | FN_GPIO_INPUT }, // Output disable
    { 43, NONE, FN_SPI | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 43, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 43, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc13[] = {
    //gpio, value, type
    { 46, LOW, FN_GPIO },
    { 46, HIGH, FN_SPI },
    { 30, LOW, FN_SPI | FN_GPIO_OUTPUT }, // Output enable
    { 30, HIGH, FN_GPIO_INPUT }, // Output disable
    { 31, NONE, FN_SPI | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 31, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 31, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc14[] = {
    //gpio, value, type
    { 48, NONE, FN_ANALOG }, // Prevent leakage current for ADC
    { 49, NONE, FN_ANALOG | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 49, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 49, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc15[] = {
    //gpio, value, type
    { 50, NONE, FN_ANALOG }, // Prevent leakage current for ADC
    { 51, NONE, FN_ANALOG | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 51, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 51, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc16[] = {
    //gpio, value, type
    { 52, NONE, FN_ANALOG }, // Prevent leakage current for ADC
    { 53, NONE, FN_ANALOG | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 53, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 53, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc17[] = {
    //gpio, value, type
    { 54, NONE, FN_ANALOG }, // Prevent leakage current for ADC
    { 55, NONE, FN_ANALOG | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 55, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 55, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc18[] = {
    //gpio, value, type
    { 78, LOW, FN_ANALOG },
    { 78, HIGH, FN_GPIO },
    { 60, LOW, FN_I2C },
    { 60, HIGH, FN_ANALOG },
    { 60, HIGH, FN_GPIO },
    { 56, NONE, FN_ANALOG | FN_I2C }, // Prevent leakage current for ADC
    { 57, NONE, FN_ANALOG | FN_I2C | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 57, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 57, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

static mux_sel_t MuxDesc19[] = {
    //gpio, value, type
    { 79, LOW, FN_ANALOG },
    { 79, HIGH, FN_GPIO },
    { 60, LOW, FN_I2C },
    { 60, HIGH, FN_ANALOG },
    { 60, HIGH, FN_GPIO },
    { 58, NONE, FN_ANALOG | FN_I2C }, // Prevent leakage current for ADC
    { 59, NONE, FN_ANALOG | FN_I2C | FN_GPIO_INPUT_HIZ | FN_GPIO_OUTPUT }, // Pullup disable
    { 59, HIGH, FN_GPIO_INPUT_PULLUP }, // Pullup enable
    { 59, LOW, FN_GPIO_INPUT_PULLDOWN }, // Pulldown enable
};

typedef struct ardu_map_s {
    uint8_t len;
    mux_sel_t *mux;
} ardu_map_t;

static ardu_map_t ardu_mux_map[] = {
    { MUX_SIZE(MuxDesc0), (mux_sel_t *)&MuxDesc0 },
    { MUX_SIZE(MuxDesc1), (mux_sel_t *)&MuxDesc1 },
    { MUX_SIZE(MuxDesc2), (mux_sel_t *)MuxDesc2  },
    { MUX_SIZE(MuxDesc3), (mux_sel_t *)MuxDesc3  },
    { MUX_SIZE(MuxDesc4), (mux_sel_t *)MuxDesc4  },
    { MUX_SIZE(MuxDesc5), (mux_sel_t *)MuxDesc5  },
    { MUX_SIZE(MuxDesc6), (mux_sel_t *)MuxDesc6  },
    { MUX_SIZE(MuxDesc7), (mux_sel_t *)MuxDesc7  },
    { MUX_SIZE(MuxDesc8), (mux_sel_t *)MuxDesc8  },
    { MUX_SIZE(MuxDesc9), (mux_sel_t *)MuxDesc9  },
    { MUX_SIZE(MuxDesc10), (mux_sel_t *)MuxDesc10 },
    { MUX_SIZE(MuxDesc11), (mux_sel_t *)MuxDesc11 },
    { MUX_SIZE(MuxDesc12), (mux_sel_t *)MuxDesc12 },
    { MUX_SIZE(MuxDesc13), (mux_sel_t *)MuxDesc13 },
    { MUX_SIZE(MuxDesc14), (mux_sel_t *)MuxDesc14 },
    { MUX_SIZE(MuxDesc15), (mux_sel_t *)MuxDesc15 },
    { MUX_SIZE(MuxDesc16), (mux_sel_t *)MuxDesc16 },
    { MUX_SIZE(MuxDesc17), (mux_sel_t *)MuxDesc17 },
    { MUX_SIZE(MuxDesc18), (mux_sel_t *)MuxDesc18 },
    { MUX_SIZE(MuxDesc19), (mux_sel_t *)MuxDesc19 },
};

static void
gpio_set(int pin, enum sol_gpio_direction dir, int drive, bool val)
{
    int len;
    struct stat st;
    char path[PATH_MAX];
    struct sol_gpio *gpio;
    const char *drive_str;
    struct sol_gpio_config gpio_config = { 0 };

    gpio_config.dir = dir;
    gpio_config.out.value = val;

    gpio = sol_gpio_open(pin, &gpio_config);
    if (!gpio)
        return;

    //drive
    /* This is not a standard interface in upstream Linux, so the
     * linux implementation of sol-gpio doesn't handle it, thus the need
     * to set it here manually
     */
    len = snprintf(path, sizeof(path), BASE "/gpio%d/drive", pin);
    if (!(len < 0 || len > PATH_MAX)) {
        if (stat(path, &st) != -1) {
            switch (drive) {
            case GPIO_DRIVE_PULLUP:
                drive_str = "pullup";
                break;
            case GPIO_DRIVE_PULLDOWN:
                drive_str = "pulldown";
                break;
            case GPIO_DRIVE_HIZ:
                drive_str = "hiz";
                break;
            default:
                drive_str = "strong";
            }

            sol_util_write_file(path, "%s", drive_str);
        }
    }

    sol_gpio_close(gpio);
}

static void
mux_select(uint8_t arduino_pin, uint32_t _func)
{
    int i = 0;
    ardu_map_t *p = NULL;

    if (arduino_pin > 19)
        return;

    p = &ardu_mux_map[arduino_pin];

    for (i = 0; i < p->len; i++) {
        if (p->mux[i].func & _func) {
            if (p->mux[i].val == NONE) {
                /* No output, so switch to HiZ input */
                gpio_set(p->mux[i].pin, SOL_GPIO_DIR_IN, GPIO_DRIVE_HIZ, false);
            } else {
                /* Output defined as LOW or HIGH */
                gpio_set(p->mux[i].pin, SOL_GPIO_DIR_OUT, GPIO_DRIVE_STRONG, p->mux[i].val);
            }
        }
    }
}


// =============================================================================
// ANALOG SETUP
// =============================================================================

//AIO -> ArduPin, *** zero == empty ***
static int aio_to_arduino[] = {
    14, 15, 16, 17, 18, 19,
};

void
aio_setup(int pin)
{
    if (pin > (int)sizeof(aio_to_arduino))
        return;

    mux_select(aio_to_arduino[pin], FN_ANALOG);
}

// =============================================================================
// GPIO SETUP
// =============================================================================

//GPIO -> ArduPin, *** -1 == empty (zero is valid output) ***
static int gpio_to_arduino[63] = {
    [0] = 5,
    [1] = 6,
    [2 ... 3] = -1,
    [4] = 9,
    [5] = 11,
    [6] = 4,
    [7] = 13,
    [8 ... 9] = -1,
    [10] = 10,
    [11] = 0,
    [12] = 1,
    [13 ... 14] = -1,
    [15] = 12,
    [16 ... 37] = -1,
    [38] = 7,
    [39] = -1,
    [40] = 8,
    [41 ... 47] = -1,
    [48] = 14,
    [49] = -1,
    [50] = 15,
    [51] = -1,
    [52] = 16,
    [53] = -1,
    [54] = 17,
    [55] = -1,
    [56] = 18,
    [57] = -1,
    [58] = 19,
    [59 ... 60] = -1,
    [61] = 2,
    [62] = 3,
};

void
gpio_setup(int pin, enum sol_gpio_direction dir)
{
    if (pin > (int)sizeof(gpio_to_arduino))
        return;

    if (gpio_to_arduino[pin] != -1)
        mux_select(gpio_to_arduino[pin],
            dir == SOL_GPIO_DIR_OUT ? FN_GPIO_OUTPUT : FN_GPIO_INPUT_PULLUP);
}

// =============================================================================
// I2C SETUP
// =============================================================================

void
i2c_setup(void)
{
    mux_select(18, FN_I2C);
    mux_select(19, FN_I2C);
}

// =============================================================================
// PWM SETUP
// =============================================================================

//PWM -> ArduPin, *** zero == empty ***
static int pwm_to_arduino[12] = {
    [1] = 3,
    [3] = 5,
    [5] = 6,
    [7] = 9,
    [9] = 11,
    [11] = 10,
};

void
pwm_setup(int pin)
{
    if (pin > (int)sizeof(pwm_to_arduino))
        return;

    if (pwm_to_arduino[pin])
        mux_select(pwm_to_arduino[pin], FN_PWM);
}
