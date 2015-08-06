obj-y += libshared.mod

obj-libshared-y := \
    sol-monitors.o \
    sol-util.o

obj-libshared-$(FLOW) += \
    sol-fbp-graph.o \
    sol-fbp-internal-log.o \
    sol-fbp-internal-scanner.o \
    sol-fbp-parser.o

obj-libshared-$(PLATFORM_LINUX) += \
    sol-conffile.o \
    sol-file-reader.o \
    sol-util-linux.o \
    sol-iio.o

obj-libshared-y-extra-cflags += -fvisibility=default
