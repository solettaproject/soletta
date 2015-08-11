obj-$(FLOW_NODE_TYPE_STRING) += string.mod
obj-string-$(FLOW_NODE_TYPE_STRING) := string.json

ifeq (y,$(HAVE_ICU))
obj-string-$(FLOW_NODE_TYPE_STRING) += string-icu.o string-replace-icu.o
else
obj-string-$(FLOW_NODE_TYPE_STRING) += string-ascii.o string-replace-ascii.o
warning-msg := "You're building the string nodes module without i18n support -- some nodes will only act properly on pure ASCII input, not the intended utf-8 for Soletta. Please re-configure after you have ICU development packages installed to get the intended string nodes behavior."
endif

obj-string-$(FLOW_NODE_TYPE_STRING)-extra-cflags += $(LOCALE_CFLAGS) $(ICU_CFLAGS)
obj-string-$(FLOW_NODE_TYPE_STRING)-extra-ldflags += $(LOCALE_LDFLAGS) $(ICU_LDFLAGS)
