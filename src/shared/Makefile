obj-y += libshared.mod util.mod

obj-libshared-y := \
    sol-monitors.o \
    sol-util.o

obj-$(UTIL) += \
    util.mod

obj-util-$(PLATFORM_LINUX) += \
    sol-util-impl-linux.o
obj-util-$(PLATFORM_RIOTOS) += \
    sol-util-impl-riot.o
obj-util-$(PLATFORM_CONTIKI) += \
    sol-util-impl-contiki.o

obj-libshared-$(FLOW) += \
    sol-fbp-graph.o \
    sol-fbp-internal-log.o \
    sol-fbp-internal-scanner.o \
    sol-fbp-parser.o

obj-libshared-$(PLATFORM_LINUX) += \
    sol-conffile.o \
    sol-file-reader.o \
    sol-util-linux.o
obj-libshared-$(PLATFORM_LINUX)-extra-cflags += $(GLIB_CFLAGS)
obj-libshared-$(PLATFORM_LINUX)-extra-ldflags += $(GLIB_LDFLAGS)

obj-libshared-y-extra-cflags += -fvisibility=default
