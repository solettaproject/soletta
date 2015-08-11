obj-$(FLOW_NODE_TYPE_STRING) += string.mod
obj-string-$(FLOW_NODE_TYPE_STRING) := string.json

ifdef LINUX
ifdef HAVE_ICU
obj-string-$(FLOW_NODE_TYPE_STRING) += string-icu.o string-replace-icu.o
else
obj-string-$(FLOW_NODE_TYPE_STRING) += string-ascii.o string-replace-ascii.o
endif
else
obj-string-$(FLOW_NODE_TYPE_STRING) += string-ascii.o
endif

obj-string-$(FLOW_NODE_TYPE_STRING)-extra-cflags += $(LOCALE_CFLAGS) $(ICU_CFLAGS)
obj-string-$(FLOW_NODE_TYPE_STRING)-extra-ldflags += $(LOCALE_LDFLAGS) $(ICU_LDFLAGS)
