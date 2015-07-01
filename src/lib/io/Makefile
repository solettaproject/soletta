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
    sol-i2c-riot.o \
    sol-spi-riot.o \
    sol-uart-riot.o

ifeq (y, $(USE_PWM))
obj-io-$(IO) += \
    sol-pwm-riot.o
endif # USE_PWM
endif

ifeq (y,$(PLATFORM_CONTIKI))
obj-io-$(IO) += \
    sol-gpio-contiki.o
endif

ifeq (y,$(SOL_PLATFORM_LINUX))
obj-io-$(IO) += \
    sol-gpio-linux.o \
    sol-i2c-linux.o \
    sol-spi-linux.o \
    sol-uart-linux.o

ifeq (y, $(USE_PWM))
obj-io-$(IO) += \
    sol-pwm-linux.o
endif # USE_PWM
endif

headers-$(IO) := \
    include/sol-gpio.h \
    include/sol-i2c.h \
    include/sol-spi.h \
    include/sol-uart.h

ifeq (y, $(USE_PWM))
headers-$(IO) += \
    include/sol-pwm.h
endif
