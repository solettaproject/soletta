obj-y += io.mod

ifeq (y, $(USE_AIO))
obj-io-$(IO) += \
    sol-aio-common.o
endif

ifeq (y, $(USE_GPIO))
obj-io-y += \
    sol-gpio-common.o
endif

ifeq (y, $(USE_PWM))
obj-io-y += \
    sol-pwm-common.o
endif

ifeq (y, $(USE_I2C))
obj-io-y += \
    sol-i2c-common.o
endif

ifeq (y,$(PLATFORM_RIOTOS))
ifeq (y, $(USE_AIO))
obj-io-$(IO) += \
    sol-aio-riot.o
endif # USE_AIO

ifeq (y, $(USE_GPIO))
obj-io-y += \
    sol-gpio-riot.o
endif # USE_GPIO

ifeq (y, $(USE_PWM))
obj-io-y += \
    sol-pwm-riot.o
endif # USE_PWM

ifeq (y, $(USE_SPI))
obj-io-y += \
    sol-spi-riot.o
endif # USE_SPI

ifeq (y, $(USE_UART))
obj-io-y += \
    sol-uart-riot.o
endif # USE_UART

ifeq (y, $(USE_I2C))
obj-io-y += \
    sol-i2c-riot.o
endif # USE_I2C
endif # PLATFORM_RIOTOS

ifeq (y,$(PLATFORM_CONTIKI))
ifeq (y, $(USE_GPIO))
obj-io-y += \
    sol-gpio-contiki.o
endif # USE_GPIO
endif # PLATFORM_CONTIKI

ifeq (y,$(PLATFORM_LINUX))
ifeq (y, $(USE_AIO))
obj-io-$(IO) += \
    sol-aio-linux.o
endif # USE_AIO

ifeq (y, $(USE_GPIO))
obj-io-y += \
    sol-gpio-linux.o
endif # USE_GPIO

ifeq (y, $(USE_PWM))
obj-io-y += \
    sol-pwm-linux.o
endif # USE_PWM

ifeq (y, $(USE_SPI))
obj-io-y += \
    sol-spi-linux.o
endif # USE_SPI

ifeq (y, $(USE_UART))
obj-io-y += \
    sol-uart-linux.o
endif # USE_UART

ifeq (y, $(USE_I2C))
obj-io-y += \
    sol-i2c-linux.o
endif # USE_I2C
endif # PLATFORM_LINUX

ifeq (y, $(USE_AIO))
headers-$(IO) += \
    include/sol-aio.h
endif

ifeq (y, $(USE_GPIO))
headers-y += \
    include/sol-gpio.h
endif

ifeq (y, $(USE_PWM))
headers-y += \
    include/sol-pwm.h
endif

ifeq (y, $(USE_SPI))
headers-y += \
    include/sol-spi.h
endif

ifeq (y, $(USE_UART))
headers-y += \
    include/sol-uart.h
endif

ifeq (y, $(USE_I2C))
headers-y += \
    include/sol-i2c.h
endif
