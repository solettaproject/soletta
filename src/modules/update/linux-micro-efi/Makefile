obj-$(LINUX_MICRO_EFI_UPDATE) += linux-micro-efi-update.mod

obj-linux-micro-efi-update-$(LINUX_MICRO_EFI_UPDATE) += \
	../update-common/http.o \
	../update-common/file.o \
	linux-micro-efi-update.o

obj-linux-micro-efi-update-$(LINUX_MICRO_EFI_UPDATE)-extra-cflags += \
	-I$(top_srcdir)src/modules/update

obj-linux-micro-efi-update-$(LINUX_MICRO_EFI_UPDATE)-type := update
