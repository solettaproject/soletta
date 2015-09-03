obj-$(FLOW_NODE_TYPE_UDEV) += udev.mod
obj-udev-$(FLOW_NODE_TYPE_UDEV) := udev.json udev.o
obj-udev-$(FLOW_NODE_TYPE_UDEV)-extra-cflags += $(UDEV_CFLAGS)
obj-udev-$(FLOW_NODE_TYPE_UDEV)-extra-ldflags += $(UDEV_LDFLAGS)
obj-udev-$(FLOW_NODE_TYPE_UDEV)-type := flow

requires-private-$(FLOW_NODE_TYPE_UDEV) := \
    $(UDEV_REQUIRES_PRIVATE)
