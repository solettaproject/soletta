obj-$(FLOW_NODE_TYPE_REGEXP) += regexp.mod
obj-regexp-$(FLOW_NODE_TYPE_REGEXP) := regexp.json regexp.o
obj-regexp-$(FLOW_NODE_TYPE_REGEXP)-extra-cflags += $(LOCALE_CFLAGS)
obj-regexp-$(FLOW_NODE_TYPE_REGEXP)-extra-cflags += $(LIBPCRE_CFLAGS)
obj-regexp-$(FLOW_NODE_TYPE_REGEXP)-extra-ldflags += $(LOCALE_LDFLAGS)
obj-regexp-$(FLOW_NODE_TYPE_REGEXP)-extra-ldflags += $(LIBPCRE_LDFLAGS)
obj-regexp-$(FLOW_NODE_TYPE_REGEXP)-type := flow
requires-private-$(FLOW_NODE_TYPE_REGEXP) := $(LIBPCRE_REQUIRES_PRIVATE)
