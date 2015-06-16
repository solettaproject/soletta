flowmodules_gtk_extra_cflags += @GTK_CFLAGS@
flowmodules_gtk_extra_libs += @GTK_LIBS@

flowmodules_gtk_extra_src += \
	src/modules/flow/gtk/byte-editor.c \
	src/modules/flow/gtk/byte-editor.h \
	src/modules/flow/gtk/common.h \
	src/modules/flow/gtk/label.c \
	src/modules/flow/gtk/label.h \
	src/modules/flow/gtk/led.c \
	src/modules/flow/gtk/led.h \
	src/modules/flow/gtk/pushbutton.c \
	src/modules/flow/gtk/pushbutton.h \
	src/modules/flow/gtk/pwm-editor.c \
	src/modules/flow/gtk/pwm-editor.h \
	src/modules/flow/gtk/pwm-viewer.c \
	src/modules/flow/gtk/pwm-viewer.h \
	src/modules/flow/gtk/rgb-editor.c \
	src/modules/flow/gtk/rgb-editor.h \
	src/modules/flow/gtk/slider.c \
	src/modules/flow/gtk/slider.h \
	src/modules/flow/gtk/spinbutton.c \
	src/modules/flow/gtk/spinbutton.h \
	src/modules/flow/gtk/toggle.c \
	src/modules/flow/gtk/toggle.h \
	src/modules/flow/gtk/window.c \
	src/modules/flow/gtk/window.h

EXTRA_DIST += \
	src/modules/flow/gtk/label.c \
	src/modules/flow/gtk/led.c \
	src/modules/flow/gtk/pwm-editor.c \
	src/modules/flow/gtk/pwm-viewer.c \
	src/modules/flow/gtk/slider.c \
	src/modules/flow/gtk/spinbutton.c \
	src/modules/flow/gtk/toggle.c \
	src/modules/flow/gtk/rgb-editor.c \
	src/modules/flow/gtk/byte-editor.c \
	src/modules/flow/gtk/pushbutton.c \
	src/modules/flow/gtk/gtk.json
