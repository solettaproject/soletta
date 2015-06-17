obj-$(CONFIG_CORE) += core.mod

obj-core-$(CONFIG_CORE)             := sol-log.o sol-blob.o sol-mainloop.o
obj-core-$(CONFIG_CORE)             += sol-platform.o sol-types.o sol-platform-detect.o
obj-core-$(CONFIG_KDBUS)            += sol-bus.o
obj-core-$(CONFIG_MAINLOOP_GLIB)    += sol-mainloop-impl-glib.o
obj-core-$(CONFIG_MAINLOOP_POSIX)   += sol-mainloop-impl-posix.o
obj-core-$(CONFIG_MAINLOOP_RIOTOS)  += sol-mainloop-impl-riot.o
obj-core-$(CONFIG_PLATFORM_LINUX_MICRO) += sol-log-impl-linux.o sol-platform-impl-linux-micro.o
obj-core-$(CONFIG_SOCKET_LINUX)     += sol-socket-linux.o
obj-core-$(CONFIG_PLATFORM_RIOTOS)  += sol-platform-impl-riot.o
obj-core-$(CONFIG_PLATFORM_DUMMY)   += sol-platform-impl-dummy.o
obj-core-$(CONFIG_PLATFORM_SYSTEMD) += sol-platform-impl-systemd.o
