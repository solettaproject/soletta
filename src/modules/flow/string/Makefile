#  The string node implementation is split in the following files:
#  - string-{icu,ascii}.c
#  - string-replace-{icu,ascii}.c (this has CPython-licensed code)
#  - string-uuid.c
#
#  For many of the string nodes, there may be two implementations
#  (depending on the presence of the ICU library or not), thus ICU or
#  ASCII. We could have these remaining nodes put on a single file per
#  implementation, but since the string replacing nodes have a
#  different license than the others, we kept them split like that.

obj-$(FLOW_NODE_TYPE_STRING) += string.mod
obj-string-$(FLOW_NODE_TYPE_STRING) := string.json string-uuid.o string-common.o

ifeq (y,$(USE_ICU))
obj-string-$(FLOW_NODE_TYPE_STRING) += string-icu.o string-replace-icu.o
else
obj-string-$(FLOW_NODE_TYPE_STRING) += string-ascii.o string-replace-ascii.o
warning-msg += "You're building the string nodes module without i18n support. That way, some nodes will only act properly on pure ASCII input, not the intended UTF-8 for Soletta. Please re-configure after you have ICU development packages installed to get the intended string nodes behavior."
endif

obj-string-$(FLOW_NODE_TYPE_STRING)-extra-cflags += $(LOCALE_CFLAGS)
obj-string-$(USE_ICU)-extra-cflags += $(ICU_CFLAGS)

obj-string-$(FLOW_NODE_TYPE_STRING)-extra-ldflags += $(LOCALE_LDFLAGS)
obj-string-$(USE_ICU)-extra-ldflags += $(ICU_LDFLAGS)

obj-string-$(FLOW_NODE_TYPE_STRING)-type := flow

requires-private-$(FLOW_NODE_TYPE_STRING) := \
    $(ICU_REQUIRES_PRIVATE)
