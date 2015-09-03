obj-y += libshared.mod

obj-libshared-y := \
    sol-monitors.o \
    sol-util.o \
    sol-random.o

obj-libshared-$(FLOW_SUPPORT) += \
    sol-fbp-graph.o \
    sol-fbp-internal-scanner.o \
    sol-fbp-parser.o

ifeq (y,$(LOG))
obj-libshared-$(FLOW_SUPPORT) += \
    sol-fbp-internal-log.o
endif

obj-libshared-$(PLATFORM_LINUX) += \
    sol-conffile.o \
    sol-file-reader.o \
    sol-lib-loader.o \
    sol-util-linux.o \
    sol-util-file.o \
    sol-iio.o

obj-libshared-$(PLATFORM_RIOTOS) += \
    sol-util-riot.o

obj-libshared-$(PLATFORM_CONTIKI) += \
    sol-util-contiki.o

obj-libshared-y-extra-cflags += -fvisibility=default
