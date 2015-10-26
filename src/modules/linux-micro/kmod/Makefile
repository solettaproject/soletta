obj-$(LINUX_MICRO_KMOD) += kmod.mod
obj-kmod-$(LINUX_MICRO_KMOD) += kmod.o ../uevent/sol-uevent.o
obj-kmod-$(LINUX_MICRO_KMOD)-type := linux-micro
obj-kmod-$(LINUX_MICRO_KMOD)-extra-ldflags := $(LIBKMOD_LDFLAGS)
obj-kmod-$(LINUX_MICRO_KMOD)-extra-cflags := -I$(top_srcdir)src/modules/linux-micro/uevent
