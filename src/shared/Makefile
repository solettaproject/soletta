obj-y += libshared.mod

obj-libshared-y := \
    sol-modules.o \
    sol-monitors.o \
    sol-random.o

obj-libshared-$(USE_FLOW) += \
    sol-fbp-graph.o \
    sol-fbp-internal-scanner.o \
    sol-fbp-parser.o

ifeq (y,$(LOG))
obj-libshared-$(USE_FLOW) += \
    sol-fbp-internal-log.o
endif

obj-libshared-$(PLATFORM_LINUX) += \
    sol-conffile.o \
    sol-lib-loader.o \

obj-libshared-y-extra-cflags += -fvisibility=default

obj-libshared-y-extra-ldflags += -lm

