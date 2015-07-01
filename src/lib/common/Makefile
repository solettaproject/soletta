obj-$(CORE) += core.mod

obj-core-$(CORE) := \
    sol-log.o \
    sol-blob.o \
    sol-mainloop.o \
    sol-platform.o \
    sol-types.o \
    sol-platform-detect.o

obj-core-$(KDBUS) += \
    sol-bus.o
obj-core-$(KDBUS)-extra-cflags += $(SYSTEMD_CFLAGS) $(GLIB_CFLAGS)
obj-core-$(KDBUS)-extra-ldflags += $(SYSTEMD_LDFLAGS) $(GLIB_LDFLAGS)

obj-core-$(MAINLOOP_GLIB) += \
    sol-mainloop-impl-glib.o
obj-core-$(MAINLOOP_GLIB)-extra-cflags += $(GLIB_CFLAGS)
obj-core-$(MAINLOOP_GLIB)-extra-ldflags += $(GLIB_LDFLAGS)

obj-core-$(MAINLOOP_POSIX) += \
    sol-mainloop-common.o \
    sol-mainloop-impl-posix.o

obj-core-$(MAINLOOP_RIOTOS) += \
    sol-mainloop-impl-riot.o
obj-core-$(PLATFORM_LINUX_MICRO) += \
    sol-platform-impl-linux-micro.o
obj-core-$(SOCKET_LINUX) += \
    sol-socket-linux.o
obj-core-$(PLATFORM_RIOTOS) += \
    sol-platform-impl-riot.o
obj-core-$(PLATFORM_DUMMY) += \
    sol-platform-impl-dummy.o

obj-core-$(SOL_PLATFORM_LINUX) += \
    sol-platform-linux-common.o \
    sol-log-impl-linux.o
obj-core-$(SOL_PLATFORM_LINUX)-extra-cflags += $(SYSTEMD_CFLAGS)
obj-core-$(SOL_PLATFORM_LINUX)-extra-ldflags += $(SYSTEMD_LDFLAGS)

obj-core-$(PLATFORM_SYSTEMD) += \
    sol-platform-impl-systemd.o
obj-core-$(PLATFORM_SYSTEMD)-extra-cflags += $(SYSTEMD_CFLAGS) $(UDEV_CFLAGS)
obj-core-$(PLATFORM_SYSTEMD)-extra-ldflags += $(SYSTEMD_LDFLAGS) $(UDEV_LDFLAGS)

ifeq (y,$(PTHREAD))
obj-core-$(CORE) += \
    sol-worker-thread.o
obj-core-$(MAINLOOP_GLIB) += \
    sol-worker-thread-impl-glib.o
obj-core-$(MAINLOOP_POSIX) += \
    sol-worker-thread-impl-posix.o
obj-core-y-extra-ldflags += $(PTHREAD_H_LDFLAGS)
endif

obj-core-$(HAVE_PIN_MUX) += \
    sol-pin-mux.o
