obj-$(FLOW_NODE_TYPE_GTK) += gtk.mod
obj-gtk-$(FLOW_NODE_TYPE_GTK) := gtk.json gtk.o byte-editor.o
obj-gtk-$(FLOW_NODE_TYPE_GTK) += label.o led.o pushbutton.o pwm-editor.o
obj-gtk-$(FLOW_NODE_TYPE_GTK) += pwm-viewer.o rgb-editor.o slider.o
obj-gtk-$(FLOW_NODE_TYPE_GTK) += spinbutton.o toggle.o window.o
obj-gtk-$(FLOW_NODE_TYPE_GTK)-extra-cflags += $(GTK_CFLAGS) $(GLIB_CFLAGS)
obj-gtk-$(FLOW_NODE_TYPE_GTK)-extra-ldflags += $(GTK_LDFLAGS) $(GLIB_LDFLAGS)
obj-gtk-$(FLOW_NODE_TYPE_GTK)-type := flow

requires-private-$(FLOW_NODE_TYPE_GTK) := \
    $(GTK_REQUIRES_PRIVATE)
