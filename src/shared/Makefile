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
    sol-util-impl-linux.o
obj-libshared-$(PLATFORM_LINUX)-extra-cflags += $(GLIB_CFLAGS)
obj-libshared-$(PLATFORM_LINUX)-extra-ldflags += $(GLIB_LDFLAGS)

obj-libshared-$(PLATFORM_RIOTOS) += \
    sol-util-impl-riot.o

obj-libshared-$(PLATFORM_CONTIKI) += \
    sol-util-impl-contiki.o

obj-libshared-y-extra-cflags += -fvisibility=default
