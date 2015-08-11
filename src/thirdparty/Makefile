obj-$(JAVASCRIPT) := javascript.mod

obj-javascript-$(JAVASCRIPT) := \
	duktape/src/duktape.o

obj-javascript-$(JAVASCRIPT)-extra-cflags := \
	-Wno-float-equal \
	-Wno-format-nonliteral \
	-Wno-suggest-attribute=noreturn \
	-fno-sanitize=all
