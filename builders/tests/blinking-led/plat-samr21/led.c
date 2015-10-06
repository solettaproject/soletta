#include "stdint.h"
#include "sol-gpio.h"

#include "led.h"

/* Using platform-specific headers is fine for platform-specific code. */
#include <periph_cpu.h>

struct sol_gpio *
open_led(void)
{
    struct sol_gpio_config cfg = {
        .api_version = SOL_GPIO_CONFIG_API_VERSION,
        .dir = SOL_GPIO_DIR_OUT,
    };

    return sol_gpio_open(GPIO(PA, 19), &cfg);
}
