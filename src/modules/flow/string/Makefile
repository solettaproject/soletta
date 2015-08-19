#  The string node implementation is split in three files, namely:
#  - string-{icu,ascii}.c
#  - string-replace-{icu,ascii}.c
#  - string-regexp.c
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

obj-$(FLOW_NODE_TYPE_STRING) += string.mod
obj-string-$(FLOW_NODE_TYPE_STRING) := string.json string-regexp.o

ifdef USE_ICU
obj-string-$(FLOW_NODE_TYPE_STRING) += string-icu.o string-replace-icu.o
else
obj-string-$(FLOW_NODE_TYPE_STRING) += string-ascii.o string-replace-ascii.o
endif

obj-string-$(FLOW_NODE_TYPE_STRING)-extra-cflags += $(LOCALE_CFLAGS) $(ICU_CFLAGS) $(LIBPCRE_CFLAGS)
obj-string-$(FLOW_NODE_TYPE_STRING)-extra-ldflags += $(LOCALE_LDFLAGS) $(ICU_LDFLAGS) $(LIBPCRE_LDFLAGS)
