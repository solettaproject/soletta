obj-$(INTERNAL_SHARED) += libshared.mod
obj-libshared-static := yes

obj-libshared-$(INTERNAL_SHARED)   := sol-arena.o sol-fbp-graph.o sol-fbp-internal-log.o
obj-libshared-$(INTERNAL_SHARED)   += sol-fbp-internal-scanner.o sol-util.o
obj-libshared-$(INTERNAL_SHARED)   += sol-monitors.o sol-str-slice.o sol-str-table.o
obj-libshared-$(INTERNAL_SHARED)   += sol-vector.o sol-json.o sol-buffer.o
obj-libshared-$(INTERNAL_SHARED)   += sol-gpio-common.o
obj-libshared-$(INTERNAL_SHARED)   += sol-i2c-common.o

ifeq (y,$(USE_PWM))
obj-libshared-$(INTERNAL_SHARED)   += sol-pwm-common.o
endif

ifeq (y,$(RESOLVER_CONFFILE))
obj-libshared-m += sol-fbp-parser.o
endif

ifeq (y,$(MODULES))
obj-libshared-m += sol-conffile.o
obj-libshared-m-extra-cflags += $(GLIB_CFLAGS)
obj-libshared-m-extra-ldflags += $(GLIB_LDFLAGS)
endif

ifeq (y,$(PLATFORM_RIOTOS))
ifeq (y,$(USE_PWM))
obj-libshared-m += sol-pwm-riot.o
endif

ifeq (y,$(USE_SPI))
obj-libshared-m += sol-spi-riot.o
endif

ifeq (y,$(USE_UART))
obj-libshared-m += sol-uart-riot.o
endif

obj-libshared-m += sol-gpio-riot.o sol-i2c-riot.o
obj-libshared-m += sol-network-riot.o
endif

ifeq (y,$(SOL_PLATFORM_LINUX))
ifeq (y,$(USE_PWM))
obj-libshared-m += sol-pwm-linux.o
endif

ifeq (y,$(USE_SPI))
obj-libshared-m += sol-spi-linux.o
endif

ifeq (y,$(USE_UART))
obj-libshared-m += sol-uart-linux.o
endif

obj-libshared-m += sol-file-reader.o sol-gpio-linux.o sol-network-linux.o
obj-libshared-m += sol-i2c-linux.o
endif
