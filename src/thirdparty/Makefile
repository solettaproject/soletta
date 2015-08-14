obj-$(JAVASCRIPT) := javascript.mod

obj-javascript-$(JAVASCRIPT) := \
	$(DUKTAPE_SRC_PATH)/duktape.o

obj-javascript-$(JAVASCRIPT)-extra-cflags := \
	-Wno-float-equal \
	-Wno-format-nonliteral \
	-Wno-suggest-attribute=noreturn

ifneq (,$(NO_SANITIZE_CFLAGS))
	obj-javascript-$(JAVASCRIPT)-extra-cflags += $(NO_SANITIZE_CFLAGS)
endif
