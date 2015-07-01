obj-$(INTERNAL_SHARED) += libshared.mod
obj-libshared-static := yes

obj-libshared-$(INTERNAL_SHARED) := \
    sol-fbp-graph.o \
    sol-fbp-internal-log.o \
    sol-fbp-internal-scanner.o \
    sol-gpio-common.o \
    sol-i2c-common.o \
    sol-monitors.o \
    sol-pwm-common.o \
    sol-util.o

ifeq (y,$(RESOLVER_CONFFILE))
obj-libshared-m += sol-fbp-parser.o
endif

ifeq (y,$(MODULES))
obj-libshared-m += sol-conffile.o
obj-libshared-m-extra-cflags += $(GLIB_CFLAGS)
obj-libshared-m-extra-ldflags += $(GLIB_LDFLAGS)
endif

ifeq (y,$(PLATFORM_RIOTOS))
obj-libshared-m += \
    sol-gpio-riot.o \
    sol-i2c-riot.o \
    sol-network-riot.o \
    sol-pwm-riot.o \
    sol-spi-riot.o \
    sol-uart-riot.o
endif

ifeq (y,$(SOL_PLATFORM_LINUX))
obj-libshared-m += \
    sol-file-reader.o \
    sol-gpio-linux.o \
    sol-i2c-linux.o \
    sol-network-linux.o \
    sol-pwm-linux.o \
    sol-spi-linux.o \
    sol-uart-linux.o
endif
