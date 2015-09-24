obj-$(USE_AIO) += \
    io-aio.mod

obj-$(USE_GPIO) += \
    io-gpio.mod

obj-$(USE_PWM) += \
    io-pwm.mod

obj-$(USE_UART) += \
    io-uart.mod

obj-$(USE_I2C) += \
    io-i2c.mod

obj-$(USE_SPI) += \
    io-spi.mod

obj-$(USE_IIO) += \
    io-iio.mod

obj-io-aio-$(USE_AIO) := \
    sol-aio-common.o
obj-io-aio-$(PLATFORM_RIOTOS) += \
    sol-aio-riot.o
obj-io-aio-$(PLATFORM_LINUX) += \
    sol-aio-linux.o

obj-io-gpio-$(USE_GPIO) := \
    sol-gpio-common.o
obj-io-gpio-$(PLATFORM_RIOTOS) += \
    sol-gpio-riot.o
obj-io-gpio-$(PLATFORM_CONTIKI) += \
    sol-gpio-contiki.o
obj-io-gpio-$(PLATFORM_LINUX) += \
    sol-gpio-linux.o

obj-io-pwm-$(USE_PWM) := \
    sol-pwm-common.o
obj-io-pwm-$(PLATFORM_RIOTOS) += \
    sol-pwm-riot.o
obj-io-pwm-$(PLATFORM_LINUX) += \
    sol-pwm-linux.o

obj-io-i2c-$(USE_I2C) := \
    sol-i2c-common.o
obj-io-i2c-$(PLATFORM_RIOTOS) += \
    sol-i2c-riot.o
obj-io-i2c-$(PLATFORM_LINUX) += \
    sol-i2c-linux.o

obj-io-spi-$(PLATFORM_RIOTOS) := \
    sol-spi-riot.o
obj-io-spi-$(PLATFORM_LINUX) := \
    sol-spi-linux.o

obj-io-uart-$(PLATFORM_RIOTOS) := \
    sol-uart-riot.o
obj-io-uart-$(PLATFORM_LINUX) := \
    sol-uart-linux.o

obj-io-iio-$(USE_IIO) := \
    sol-iio.o

headers-$(USE_AIO) += \
    include/sol-aio.h
headers-$(USE_GPIO) += \
    include/sol-gpio.h
headers-$(USE_PWM) += \
    include/sol-pwm.h
headers-$(USE_I2C) += \
    include/sol-i2c.h
headers-$(USE_SPI) += \
    include/sol-spi.h
headers-$(USE_UART) += \
    include/sol-uart.h
headers-$(USE_IIO) += \
    include/sol-iio.h
