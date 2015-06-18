#include <errno.h>
#include <stdlib.h>

#include <contiki.h>
#include <dev/button-sensor.h>
#include <dev/leds.h>
#include <lib/sensors.h>

#define SOL_LOG_DOMAIN &_log_domain
#include "sol-log-internal.h"
#include "sol-gpio.h"
#include "sol-mainloop.h"
#include "sol-util.h"
#include "sol-event-handler-contiki.h"

static SOL_LOG_INTERNAL_DECLARE(_log_domain, "gpio");

struct sol_gpio
{
    int pin;
    struct sensors_sensor *button_sensor;
    bool active_low;
    struct {
        void (*cb)(void *data, struct sol_gpio *gpio);
        void *data;
    } irq;
};

static void
event_handler_cb(void *user_data, process_event_t ev, process_data_t ev_data)
{
    struct sol_gpio *gpio = user_data;
    gpio->irq.cb(gpio->irq.data, gpio);
}

struct sol_gpio *
sol_gpio_open_raw(int pin, const struct sol_gpio_config *config)
{
    struct sol_gpio *gpio;
    struct sensors_sensor *found = NULL;

    SOL_LOG_INTERNAL_INIT_ONCE;

    process_start(&sensors_process, NULL);

    if (config->drive_mode != SOL_GPIO_DRIVE_NONE) {
        SOL_ERR("Unable to set pull resistor on pin=%d", pin);
        return NULL;
    }

    if (config->dir == SOL_GPIO_DIR_IN) {
        const struct sensors_sensor *sensor = sensors_first();
        int i = 0;

        while (sensor) {
            if (strcmp(sensor->type, BUTTON_SENSOR)) {
                sensor = sensors_next(sensor);
                continue;
            }
            if (i == pin) {
                found = (struct sensors_sensor *) sensor;
                break;
            }
            sensor = sensors_next(sensor);
            i++;
        }

        if (!found) {
            SOL_ERR("GPIO pin=%d not found.", pin);
            return NULL;
        }
    } else {
        if (pin < 0 || pin > 7) {
            SOL_ERR("GPIO pin=%d not found.", pin);
            return NULL;
        }
    }

    gpio = malloc(sizeof(struct sol_gpio));
    SOL_NULL_CHECK(gpio, NULL);

    gpio->pin = pin;
    gpio->button_sensor = found;
    gpio->active_low = config->active_low;

    if (config->dir == SOL_GPIO_DIR_IN) {
        gpio->irq.cb = config->in.cb;
        gpio->irq.data = (void *) config->in.user_data;
        if (config->in.cb)
            sol_mainloop_contiki_event_handler_add(&sensors_event, found,
                                                   event_handler_cb, gpio);
    } else
        sol_gpio_write(gpio, config->out.value);

    return gpio;
}

void
sol_gpio_close(struct sol_gpio *gpio)
{
    SOL_NULL_CHECK(gpio);
    if (gpio->irq.cb)
        sol_mainloop_contiki_event_handler_del(&sensors_event,
                                               gpio->button_sensor,
                                               event_handler_cb, gpio);
    free(gpio);
}

bool
sol_gpio_write(struct sol_gpio *gpio, bool value)
{
    SOL_NULL_CHECK(gpio, false);

    if (gpio->button_sensor)
        return false;

    value = gpio->active_low ^ value;

    if (value)
        leds_set(leds_get() | (1 << gpio->pin));
    else
        leds_set(leds_get() & (~(1 << gpio->pin)));
    return true;
}

int
sol_gpio_read(struct sol_gpio *gpio)
{
    SOL_NULL_CHECK(gpio, -EINVAL);

    if (gpio->button_sensor)
        return gpio->active_low ^ !!gpio->button_sensor->value(0);

    return gpio->active_low ^ !!(leds_get() & (1 << gpio->pin));
}
