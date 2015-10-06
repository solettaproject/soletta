#include "sol-mainloop.h"
#include "sol-gpio.h"
#include "sol-log.h"

#include "led.h"

static struct sol_timeout *my_timeout;
static struct sol_gpio *gpio;
static bool write = true;

static bool
my_timeout_func(void *data)
{
    bool ret;

    ret = sol_gpio_write(gpio, write);
    write = !write;
    return ret;
}

static void
startup(void)
{
    SOL_WRN("startup\n");
    my_timeout = sol_timeout_add(2000, my_timeout_func, NULL);
    gpio = open_led();
}

static void
shutdown(void)
{
    SOL_WRN("shutdown\n");
    sol_gpio_close(gpio);
}
SOL_MAIN_DEFAULT(startup, shutdown);
