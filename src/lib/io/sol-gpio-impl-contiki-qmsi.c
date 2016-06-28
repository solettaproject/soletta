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

#include <errno.h>
#include <stdlib.h>

#include <qm_gpio.h>
#include <qm_interrupt.h>
#include <qm_scss.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-event-handler-contiki.h"
#include "sol-gpio.h"
#include "sol-log-internal.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-vector.h"

#define GPIO_PORT_MASK 0xffff0000
#define GPIO_PIN_MASK  0x0000ffff
#define GPIO_GET_PORT(pin) (((pin) & GPIO_PORT_MASK) >> 16)
#define GPIO_GET_PIN(pin)  ((pin) & GPIO_PIN_MASK)

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "gpio");

struct sol_gpio {
    uint32_t pin;
    void (*cb)(void *data, struct sol_gpio *gpio, bool value);
    const void *cb_data;
    bool active_low;
};

struct gpio_port_config {
    void (*port_callback)(uint32_t int_status);
    int (*setup_isr)(qm_gpio_port_config_t *port_cfg);
    qm_gpio_t port_num;
    uint8_t num_pins;
};

struct gpio_port {
    const struct gpio_port_config * const config;
    void (*previous_callback)(uint32_t int_status);
    struct sol_ptr_vector registered_irqs;
    uint32_t opened_pins;
    uint32_t int_status;
};

static process_event_t gpio_irq_event;
extern struct process soletta_app_process;

static void gpio_0_cb(uint32_t int_status);
static int gpio_0_setup_isr(qm_gpio_port_config_t *port_cfg);
#if HAS_AON_GPIO
static void gpio_aon_0_cb(uint32_t int_status);
static int gpio_aon_0_setup_isr(qm_gpio_port_config_t *port_cfg);
#endif

static struct gpio_port ports[] = {
    {
        .config = &((const struct gpio_port_config) {
            .port_callback = gpio_0_cb,
            .setup_isr = gpio_0_setup_isr,
            .port_num = QM_GPIO_0,
            .num_pins = QM_NUM_GPIO_PINS
            }),
        .registered_irqs = SOL_PTR_VECTOR_INIT,
    },
#if HAS_AON_GPIO
    {
        .config = &((const struct gpio_port_config) {
            .port_callback = gpio_aon_0_cb,
            .setup_isr = gpio_aon_0_setup_isr,
            .port_num = QM_AON_GPIO_0,
            .num_pins = QM_NUM_AON_GPIO_PINS
            }),
        .registered_irqs = SOL_PTR_VECTOR_INIT,
    }
#endif
};
#define NUM_GPIO_PORTS sol_util_array_size(ports)

static void
gpio_cb_dispatch(void *user_data, process_event_t ev, process_data_t ev_data)
{
    struct gpio_port *port;
    struct sol_gpio *gpio;
    uint32_t int_status, vals;
    uint16_t idx;

    port = (struct gpio_port *)ev_data;
    int_status = port->int_status;
    port->int_status = 0;
    vals = qm_gpio_read_port(port->config->port_num);

    SOL_PTR_VECTOR_FOREACH_IDX(&port->registered_irqs, gpio, idx) {
        if (gpio->cb && (int_status & BIT(GPIO_GET_PIN(gpio->pin)))) {
            bool value = !!(vals & BIT(GPIO_GET_PIN(gpio->pin))) ^ gpio->active_low;

            gpio->cb((void *)gpio->cb_data, gpio, value);
        }
    }
}

static void
gpio_irq_dispatch(qm_gpio_t port_num, uint32_t int_status)
{
    struct gpio_port *port = &ports[port_num];

    if (port->previous_callback)
        port->previous_callback(int_status);

    port->int_status |= int_status;
    process_post(&soletta_app_process, gpio_irq_event, port);
}

static int
gpio_setup_isr(qm_gpio_t port_num, qm_gpio_port_config_t *port_cfg)
{
    struct gpio_port *port = &ports[port_num];

    if (port->config->port_callback == port_cfg->callback)
        return 0;

    if (!gpio_irq_event) {
        bool ret;

        gpio_irq_event = process_alloc_event();
        ret = sol_mainloop_contiki_event_handler_add(&gpio_irq_event, NULL,
            gpio_cb_dispatch, NULL);
        SOL_EXP_CHECK(!ret, -ENOMEM);
    }

    port->previous_callback = port_cfg->callback;
    port_cfg->callback = port->config->port_callback;

    return 0;
}

static void
gpio_0_cb(uint32_t int_status)
{
    gpio_irq_dispatch(QM_GPIO_0, int_status);
}

static int
gpio_0_setup_isr(qm_gpio_port_config_t *port_cfg)
{
    qm_irq_request(QM_IRQ_GPIO_0, qm_gpio_isr_0);
    clk_periph_enable(CLK_PERIPH_CLK | CLK_PERIPH_GPIO_REGISTER
        | CLK_PERIPH_GPIO_INTERRUPT | CLK_PERIPH_GPIO_DB);
    return gpio_setup_isr(QM_GPIO_0, port_cfg);
}

#if HAS_AON_GPIO
static void
gpio_aon_0_cb(uint32_t int_status)
{
    gpio_irq_dispatch(QM_AON_GPIO_0, int_status);
}

static int
gpio_aon_0_setup_isr(qm_gpio_port_config_t *port_cfg)
{
    qm_irq_request(QM_IRQ_AONGPIO_0, qm_aon_gpio_isr_0);
    return gpio_setup_isr(QM_AON_GPIO_0, port_cfg);
}
#endif

struct sol_gpio *
sol_gpio_open_raw(uint32_t pin, const struct sol_gpio_config *config)
{
    struct sol_gpio *gpio;
    struct gpio_port *port;
    uint32_t mask;
    qm_gpio_port_config_t port_cfg;
    qm_gpio_t port_num;
    uint8_t pin_num;
    qm_rc_t ret;

    SOL_LOG_INTERNAL_INIT_ONCE;

#ifndef SOL_NO_API_VERSION
    if (unlikely(config->api_version != SOL_GPIO_CONFIG_API_VERSION)) {
        SOL_WRN("Couldn't open gpio that has unsupported version '%u', "
            "expected version is '%u'",
            config->api_version, SOL_GPIO_CONFIG_API_VERSION);
        return NULL;
    }
#endif

    port_num = GPIO_GET_PORT(pin);
    SOL_INT_CHECK(port_num, >= NUM_GPIO_PORTS, NULL);

    port = &ports[port_num];

    pin_num = GPIO_GET_PIN(pin);
    SOL_INT_CHECK(pin_num, >= port->config->num_pins, NULL);

    if (port->opened_pins & BIT(pin_num)) {
        SOL_WRN("GPIO pin %d of port %u is already open", pin_num, port_num);
        return NULL;
    }

    ret = qm_gpio_get_config(port_num, &port_cfg);
    SOL_EXP_CHECK(ret != QM_RC_OK, NULL);

    gpio = calloc(1, sizeof(*gpio));
    SOL_NULL_CHECK(gpio, NULL);

    gpio->pin = pin;
    gpio->active_low = config->active_low;

    mask = BIT(pin_num);

    if (config->dir == SOL_GPIO_DIR_IN) {
        gpio->cb = config->in.cb;
        gpio->cb_data = config->in.user_data;

        port_cfg.direction &= ~mask;

        if (config->in.trigger_mode == SOL_GPIO_EDGE_NONE)
            port_cfg.int_en &= ~mask;
        else {
            int iret;

            port_cfg.int_en |= mask;
            iret = port->config->setup_isr(&port_cfg);
            SOL_INT_CHECK_GOTO(iret, < 0, setup_isr_error);
            iret = sol_ptr_vector_append(&port->registered_irqs, gpio);
            SOL_INT_CHECK_GOTO(iret, < 0, append_error);

            if (config->in.trigger_mode == SOL_GPIO_EDGE_BOTH)
                port_cfg.int_bothedge |= mask;
            else {
                uint8_t polarity = 0;

                port_cfg.int_bothedge &= ~mask;
                port_cfg.int_type |= mask;

                if (config->in.trigger_mode == SOL_GPIO_EDGE_RISING)
                    polarity = 1;
                polarity ^= gpio->active_low;

                if (polarity)
                    port_cfg.int_polarity |= mask;
                else
                    port_cfg.int_polarity &= ~mask;
            }
        }
    } else {
        port_cfg.direction |= mask;
        port_cfg.int_en &= ~mask;
    }

    ret = qm_gpio_set_config(port_num, &port_cfg);
    SOL_EXP_CHECK_GOTO(ret != QM_RC_OK, set_config_error);

    port->opened_pins |= mask;

    return gpio;

set_config_error:
    sol_ptr_vector_remove(&port->registered_irqs, gpio);
setup_isr_error:
append_error:
    free(gpio);
    return NULL;
}

void
sol_gpio_close(struct sol_gpio *gpio)
{
    struct gpio_port *port;
    qm_gpio_port_config_t port_cfg;
    uint32_t mask;
    qm_gpio_t port_num;
    uint8_t pin_num;
    qm_rc_t ret;

    SOL_NULL_CHECK(gpio);

    port_num = GPIO_GET_PORT(gpio->pin);
    pin_num = GPIO_GET_PIN(gpio->pin);
    mask = BIT(pin_num);

    port = &ports[port_num];
    port->opened_pins &= ~mask;

    sol_ptr_vector_remove(&port->registered_irqs, gpio);

    ret = qm_gpio_get_config(port_num, &port_cfg);
    SOL_EXP_CHECK_GOTO(ret != QM_RC_OK, end);

    port_cfg.int_en &= ~mask;

    qm_gpio_set_config(port_num, &port_cfg);

end:
    free(gpio);
}

bool
sol_gpio_write(struct sol_gpio *gpio, bool value)
{
    qm_gpio_t port;
    uint8_t pin;

    SOL_NULL_CHECK(gpio, false);

    port = GPIO_GET_PORT(gpio->pin);
    pin = GPIO_GET_PIN(gpio->pin);

    if (value ^ gpio->active_low)
        qm_gpio_set_pin(port, pin);
    else
        qm_gpio_clear_pin(port, pin);

    return true;
}

int
sol_gpio_read(struct sol_gpio *gpio)
{
    qm_gpio_t port;
    uint8_t pin;
    bool ret;

    SOL_NULL_CHECK(gpio, -EINVAL);

    port = GPIO_GET_PORT(gpio->pin);
    pin = GPIO_GET_PIN(gpio->pin);

    ret = qm_gpio_read_pin(port, pin);
    return ret ^ gpio->active_low;
}
