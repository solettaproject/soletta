obj-y += core.mod

obj-core-y := \
    sol-blob.o \
    sol-mainloop.o \
    sol-platform.o \
    sol-types.o

obj-$(POWER_SUPPLY) += \
    power-supply.mod

obj-power-supply-$(PLATFORM_LINUX) += \
    sol-power-supply-impl-linux.o

obj-$(LOG) += \
    log.mod

obj-log-$(LOG) := \
    sol-log.o
obj-log-$(PLATFORM_LINUX) += \
    sol-log-impl-linux.o
obj-log-$(PLATFORM_RIOTOS) += \
    sol-log-impl-riot.o
obj-log-$(PLATFORM_CONTIKI) += \
    sol-log-impl-contiki.o

obj-core-$(SOL_BUS) += \
    sol-bus.o
obj-core-$(SOL_BUS)-extra-cflags += $(SYSTEMD_CFLAGS)
obj-core-$(SOL_BUS)-extra-ldflags += $(SYSTEMD_LDFLAGS)

obj-core-$(MAINLOOP_GLIB) += \
    sol-mainloop-impl-glib.o
obj-core-$(MAINLOOP_GLIB)-extra-cflags += $(GLIB_CFLAGS)
obj-core-$(MAINLOOP_GLIB)-extra-ldflags += $(GLIB_LDFLAGS)

obj-core-$(MAINLOOP_POSIX) += \
    sol-mainloop-common.o \
    sol-mainloop-impl-posix.o

obj-core-$(MAINLOOP_RIOTOS) += \
    sol-interrupt_scheduler_riot.o  \
    sol-mainloop-common.o \
    sol-mainloop-impl-riot.o

obj-core-$(MAINLOOP_CONTIKI) += \
    sol-mainloop-common.o \
    sol-mainloop-impl-contiki.o

obj-core-$(PLATFORM_LINUX_MICRO) += \
    sol-platform-impl-linux-micro.o
obj-core-$(PLATFORM_RIOTOS) += \
    sol-platform-impl-riot.o
obj-core-$(PLATFORM_CONTIKI) += \
    sol-platform-impl-contiki.o

obj-core-$(PLATFORM_LINUX) += \
    sol-platform-linux-common.o

obj-core-$(DETECT_BOARD_NAME) += \
    sol-board-detect.o

obj-core-$(PLATFORM_SYSTEMD) += \
    sol-platform-impl-systemd.o
obj-core-$(PLATFORM_SYSTEMD)-extra-cflags += $(SYSTEMD_CFLAGS) $(UDEV_CFLAGS)
obj-core-$(PLATFORM_SYSTEMD)-extra-ldflags += $(SYSTEMD_LDFLAGS) $(UDEV_LDFLAGS)

obj-$(WORKER_THREAD) += \
    thread.mod
obj-thread-$(WORKER_THREAD) := \
    sol-worker-thread.o
obj-thread-$(MAINLOOP_GLIB) += \
    sol-worker-thread-impl-glib.o
obj-thread-$(MAINLOOP_GLIB)-extra-cflags += $(GLIB_CFLAGS)
obj-thread-$(MAINLOOP_GLIB)-extra-ldflags += $(GLIB_LDFLAGS)
obj-thread-$(MAINLOOP_POSIX) += \
    sol-worker-thread-impl-posix.o
obj-thread-$(MAINLOOP_RIOTOS) += \
    sol-worker-thread-impl-riot.o
obj-core-$(PTHREAD)-extra-ldflags += $(PTHREAD_H_LDFLAGS)

obj-core-$(USE_PIN_MUX) += \
    sol-pin-mux.o

obj-core-$(USE_AUTO_UPDATE) += \
    sol-update.o

requires-private-$(MAINLOOP_GLIB) += \
    $(GLIB_REQUIRES_PRIVATE)

requires-private-$(SOL_BUS) += \
    $(SYSTEMD_REQUIRES_PRIVATE)

requires-private-$(PLATFORM_SYSTEMD) += \
    $(SYSTEMD_REQUIRES_PRIVATE) \
    $(UDEV_REQUIRES_PRIVATE)


headers-y := \
    include/sol-log.h \
    include/sol-macros.h \
    include/sol-mainloop.h \
    include/sol-platform.h \
    include/sol-power-supply.h \
    include/sol-types.h \
    sol-common-buildopts.h.in

headers-$(PLATFORM_LINUX) += \
    include/sol-glib-integration.h

headers-$(USE_PIN_MUX) += \
    include/sol-pin-mux.h \
    include/sol-pin-mux-modules.h

headers-$(WORKER_THREAD) += \
    include/sol-worker-thread.h

headers-$(MAINLOOP_CONTIKI) += \
    include/sol-mainloop-contiki.h

headers-$(USE_AUTO_UPDATE) += \
    include/sol-update.h \
    include/sol-update-modules.h
