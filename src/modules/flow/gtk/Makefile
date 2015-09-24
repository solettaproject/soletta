obj-$(FLOW_NODE_TYPE_GTK) += gtk.mod
obj-gtk-$(FLOW_NODE_TYPE_GTK) := \
	byte-editor.o \
	gtk.json \
	gtk.o \
	label.o \
	led.o \
	led-7seg.o \
	pushbutton.o \
	pwm-editor.o \
	pwm-viewer.o \
	rgb-editor.o \
	slider.o \
	spinbutton.o \
	toggle.o \
	window.o

obj-gtk-$(FLOW_NODE_TYPE_GTK)-extra-cflags += $(GTK_CFLAGS) $(GLIB_CFLAGS)
obj-gtk-$(FLOW_NODE_TYPE_GTK)-extra-ldflags += $(GTK_LDFLAGS) $(GLIB_LDFLAGS)
obj-gtk-$(FLOW_NODE_TYPE_GTK)-type := flow

requires-private-$(FLOW_NODE_TYPE_GTK) := \
    $(GTK_REQUIRES_PRIVATE)
