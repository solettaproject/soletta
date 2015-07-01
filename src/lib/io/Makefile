obj-$(IO) += io.mod

obj-io-$(IO) := \
    sol-gpio-common.o \
    sol-i2c-common.o

ifeq (y, $(USE_PWM))
obj-io-$(IO) += \
    sol-pwm-common.o
endif

ifeq (y,$(PLATFORM_RIOTOS))
obj-io-$(IO) += \
    sol-gpio-riot.o \
    sol-i2c-riot.o

ifeq (y, $(USE_PWM))
obj-io-$(IO) += \
    sol-pwm-riot.o
endif # USE_PWM

ifeq (y, $(USE_SPI))
obj-io-$(IO) += \
    sol-spi-riot.o
endif # USE_SPI

ifeq (y, $(USE_UART))
obj-io-$(IO) += \
    sol-uart-riot.o
endif # USE_UART
endif

ifeq (y,$(PLATFORM_CONTIKI))
obj-io-$(IO) += \
    sol-gpio-contiki.o
endif

ifeq (y,$(SOL_PLATFORM_LINUX))
obj-io-$(IO) += \
    sol-gpio-linux.o \
    sol-i2c-linux.o

ifeq (y, $(USE_PWM))
obj-io-$(IO) += \
    sol-pwm-linux.o
endif # USE_PWM

ifeq (y, $(USE_SPI))
obj-io-$(IO) += \
    sol-spi-linux.o
endif # USE_SPI

ifeq (y, $(USE_UART))
obj-io-$(IO) += \
    sol-uart-linux.o
endif # USE_UART
endif

headers-$(IO) := \
    include/sol-gpio.h \
    include/sol-i2c.h

ifeq (y, $(USE_PWM))
headers-$(IO) += \
    include/sol-pwm.h
endif

ifeq (y, $(USE_SPI))
headers-$(IO) += \
    include/sol-spi.h
endif

ifeq (y, $(USE_UART))
headers-$(IO) += \
    include/sol-uart.h
endif
