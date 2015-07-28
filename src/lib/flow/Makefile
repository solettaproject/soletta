obj-$(FLOW) += flow.mod

obj-flow-$(FLOW) := \
    sol-flow-node-options.o \
    sol-flow-packet.o \
    sol-flow-simplectype.o \
    sol-flow-static.o \
    sol-flow.o

obj-flow-$(NODE_DESCRIPTION) += \
    sol-flow-parser.o \
    sol-flow-resolver.o \
    sol-flow-builder.o

obj-flow-$(JAVASCRIPT) += \
	sol-flow-js.o

ifeq (y,$(RESOLVER_CONFFILE))
def-resolver-flag = -Dsol_flow_default_resolver=sol_flow_resolver_conffile
else
def-resolver-flag = -Dsol_flow_default_resolver=sol_flow_resolver_builtins
endif

obj-flow-$(RESOLVER_CONFFILE) += \
    sol-flow-resolver-conffile.o

obj-flow-$(RESOLVER_CONFFILE)-extra-cflags := $(GLIB_CFLAGS)
obj-flow-$(FLOW)-extra-cflags += $(def-resolver-flag)
obj-flow-$(RESOLVER_CONFFILE)-extra-ldflags += $(GLIB_LDFLAGS)

headers-$(FLOW) := \
    include/sol-flow-builder.h \
    include/sol-flow.h \
    include/sol-flow-inspector.h \
    include/sol-flow-js.h \
    include/sol-flow-packet.h \
    include/sol-flow-parser.h \
    include/sol-flow-resolver.h \
    include/sol-flow-simplectype.h \
    include/sol-flow-static.h
