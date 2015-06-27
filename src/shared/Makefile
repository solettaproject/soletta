obj-$(INTERNAL_SHARED) += libshared.mod
obj-libshared-static := yes

obj-libshared-$(INTERNAL_SHARED)   := sol-arena.o sol-fbp-graph.o sol-fbp-internal-log.o
obj-libshared-$(INTERNAL_SHARED)   += sol-fbp-internal-scanner.o sol-util.o
obj-libshared-$(INTERNAL_SHARED)   += sol-monitors.o sol-str-slice.o sol-str-table.o
obj-libshared-$(INTERNAL_SHARED)   += sol-vector.o sol-json.o sol-buffer.o
obj-libshared-$(INTERNAL_SHARED)   += sol-pwm-common.o sol-gpio-common.o
obj-libshared-$(INTERNAL_SHARED)   += sol-i2c-common.o

ifeq (y,$(RESOLVER_CONFFILE))
obj-libshared-m += sol-fbp-parser.o
endif

ifeq (y,$(MODULES))
obj-libshared-m += sol-conffile.o
obj-libshared-m-extra-cflags += $(GLIB_CFLAGS)
endif

ifeq (y,$(PTHREAD))
obj-libshared-m += sol-worker-thread-pthread.o
endif

ifeq (y,$(PLATFORM_GALILEO))
obj-libshared-m  += sol-galileo.o
endif

ifeq (y,$(PLATFORM_RIOTOS))
obj-libshared-m += sol-gpio-riot.o sol-i2c-riot.o sol-pwm-riot.o
obj-libshared-m += sol-spi-riot.o sol-uart-riot.o sol-network-riot.o
endif

ifeq (y,$(SOL_PLATFORM_LINUX))
obj-libshared-m += sol-file-reader.o sol-gpio-linux.o sol-network-linux.o
obj-libshared-m += sol-pwm-linux.o sol-i2c-linux.o sol-spi-linux.o sol-uart-linux.o
endif
