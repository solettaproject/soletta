#  The string node implementation is split in the following files:
#  - string-{icu,ascii}.c
#  - string-replace-{icu,ascii}.c
#  - string-regexp.c
#  - string-uuid.c
#
#  string-regexp.c contains the code for nodes dealing with regular
#  expressions and only one RE backend is supported: libpcre. If that
#  dependency is not present, the RE nodes will be built but made
#  useless -- they will always generate error packets after any input
#  ones.
#
#  For each of the remaining string nodes, there may be two
#  implementations (depending on the presence of the ICU library or
#  not), thus ICU or ASCII. We could have these remaining nodes put on
#  a single file per implementation, but since the string replacing
#  nodes have a different license than the others, we kept them split
#  like that.

ifeq (,$(USE_LIBPCRE))
warning-msg := "You're building the string nodes module without Perl regular expressions support. That way, the string module will still be built, but the regular expression nodes will be made useless -- any input packet on them only issue an error output packet. Please re-configure after you have LIBPCRE development packages installed to get the intended string nodes behavior.\n\n"
endif

obj-$(FLOW_NODE_TYPE_STRING) += string.mod
obj-string-$(FLOW_NODE_TYPE_STRING) := string.json string-regexp.o string-uuid.o string-common.o

ifeq (y,$(USE_ICU))
obj-string-$(FLOW_NODE_TYPE_STRING) += string-icu.o string-replace-icu.o
else
obj-string-$(FLOW_NODE_TYPE_STRING) += string-ascii.o string-replace-ascii.o
warning-msg += "You're building the string nodes module without i18n support. That way, some nodes will only act properly on pure ASCII input, not the intended UTF-8 for Soletta. Please re-configure after you have ICU development packages installed to get the intended string nodes behavior."
endif

obj-string-$(FLOW_NODE_TYPE_STRING)-extra-cflags += $(LOCALE_CFLAGS) $(ICU_CFLAGS) $(LIBPCRE_CFLAGS)
obj-string-$(FLOW_NODE_TYPE_STRING)-extra-ldflags += $(LOCALE_LDFLAGS) $(ICU_LDFLAGS) $(LIBPCRE_LDFLAGS)
obj-string-$(FLOW_NODE_TYPE_STRING)-type := flow

requires-private-$(FLOW_NODE_TYPE_STRING) := \
    $(ICU_REQUIRES_PRIVATE) \
    $(LIBPCRE_REQUIRES_PRIVATE)
