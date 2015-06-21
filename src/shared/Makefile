obj-$(CONFIG_INTERNAL_SHARED) += libshared.mod
obj-libshared-static := yes

obj-libshared-$(CONFIG_INTERNAL_SHARED)   := sol-arena.o sol-fbp-graph.o sol-fbp-internal-log.o
obj-libshared-$(CONFIG_INTERNAL_SHARED)   += sol-fbp-internal-scanner.o sol-util.o
obj-libshared-$(CONFIG_INTERNAL_SHARED)   += sol-monitors.o sol-str-slice.o sol-str-table.o
obj-libshared-$(CONFIG_INTERNAL_SHARED)   += sol-vector.o sol-json.o sol-buffer.o

ifeq (y,$(CONFIG_RESOLVER_CONFFILE))
obj-libshared-m += sol-fbp-parser.o
endif

ifeq (y,$(CONFIG_MODULES))
obj-libshared-m += sol-conffile.o
endif

ifeq (y,$(CONFIG_PTHREAD))
obj-libshared-m += sol-worker-thread-pthread.o
endif

ifeq (y,$(CONFIG_PLATFORM_GALILEO))
obj-libshared-m  += sol-galileo.o
endif

ifeq (y,$(CONFIG_PLATFORM_RIOTOS))
obj-libshared-m += sol-gpio-riot.o sol-i2c-riot.o sol-pwm-riot.o
obj-libshared-m += sol-spi-riot.o sol-uart-riot.o sol-network-riot.o
endif

ifeq (y,$(CONFIG_PLATFORM_LINUX_MICRO))
obj-libshared-m += sol-file-reader.o sol-gpio-linux.o sol-network-linux.o
obj-libshared-m += sol-pwm-linux.o sol-i2c-linux.o sol-spi-linux.o sol-uart-linux.o
endif
