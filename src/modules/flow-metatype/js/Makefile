obj-$(FLOW_METATYPE_JAVASCRIPT) += js.mod

obj-js-$(FLOW_METATYPE_JAVASCRIPT) += \
	js.o \
	$(DUKTAPE_SRC_PATH)/duktape.o

obj-js-$(FLOW_METATYPE_JAVASCRIPT)-type := flow-metatype

# TODO: Allow CFLAGS_object.o as a way to set CFLAGS only for
# compiling a certain object, so that we can apply the extra CFLAGS
# below only for the duktape.o file.

obj-js-$(FLOW_METATYPE_JAVASCRIPT)-extra-cflags := \
	-Wno-float-equal \
	-Wno-format-nonliteral \
	-Wno-suggest-attribute=noreturn

ifneq (,$(NO_SANITIZE_CFLAGS))
	obj-js-$(FLOW_METATYPE_JAVASCRIPT)-extra-cflags += $(NO_SANITIZE_CFLAGS)
endif

