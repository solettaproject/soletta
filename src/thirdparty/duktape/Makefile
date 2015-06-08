obj-$(JAVASCRIPT) += javascript.mod

obj-javascript-$(JAVASCRIPT) := \
    duktape.o

obj-javascript-$(JAVASCRIPT)-extra-cflags := \
	-Wno-float-equal \
	-Wno-format-nonliteral \
	-Wno-suggest-attribute=noreturn
