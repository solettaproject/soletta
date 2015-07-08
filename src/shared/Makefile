obj-y += libshared.mod

obj-libshared-y := \
    sol-fbp-graph.o \
    sol-fbp-internal-log.o \
    sol-fbp-internal-scanner.o \
    sol-monitors.o \
    sol-util.o

ifeq (y,$(FLOW))
obj-libshared-y += \
    sol-fbp-parser.o
endif

ifeq (y,$(SOL_PLATFORM_LINUX))
obj-libshared-y += \
    sol-conffile.o \
    sol-file-reader.o \
    sol-util-linux.o
obj-libshared-y-extra-cflags += $(GLIB_CFLAGS)
obj-libshared-y-extra-ldflags += $(GLIB_LDFLAGS)
endif

obj-libshared-y-extra-cflags += -fvisibility=default
