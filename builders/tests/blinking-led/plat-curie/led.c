#include "stdint.h"
#include "sol-gpio.h"

#include "led.h"

struct sol_gpio *
open_led(void)
{
    struct sol_gpio_config cfg = {
        .api_version = SOL_GPIO_CONFIG_API_VERSION,
        .dir = SOL_GPIO_DIR_OUT,
    };

    return sol_gpio_open(15, &cfg);
}
