obj-$(CORE) += core.mod

obj-core-$(CORE)             := sol-log.o sol-blob.o sol-mainloop.o
obj-core-$(CORE)             += sol-platform.o sol-types.o sol-platform-detect.o

obj-core-$(KDBUS)            += sol-bus.o
obj-core-$(KDBUS)-extra-cflags += $(SYSTEMD_CFLAGS)

obj-core-$(MAINLOOP_GLIB)    += sol-mainloop-impl-glib.o
obj-core-$(MAINLOOP_GLIB)-extra-cflags += $(GLIB_CFLAGS)

obj-core-$(MAINLOOP_POSIX)   += sol-mainloop-impl-posix.o
obj-core-$(MAINLOOP_RIOTOS)  += sol-mainloop-impl-riot.o
obj-core-$(PLATFORM_LINUX_MICRO) += sol-log-impl-linux.o sol-platform-impl-linux-micro.o
obj-core-$(SOCKET_LINUX)     += sol-socket-linux.o
obj-core-$(PLATFORM_RIOTOS)  += sol-platform-impl-riot.o
obj-core-$(PLATFORM_DUMMY)   += sol-platform-impl-dummy.o
obj-core-$(PLATFORM_LINUX_COMMON) += sol-platform-linux-common.o

obj-core-$(PLATFORM_SYSTEMD) += sol-platform-impl-systemd.o
obj-core-$(PLATFORM_SYSTEMD)-extra-cflags += $(SYSTEMD_CFLAGS) $(UDEV_CFLAGS)
