obj-$(FLOW_SUPPORT) += flow.mod

obj-flow-$(FLOW_SUPPORT) := \
    sol-flow-node-options.o \
    sol-flow-packet.o \
    sol-flow-simplectype.o \
    sol-flow-static.o \
    sol-flow.o

obj-flow-$(NODE_DESCRIPTION) += \
    sol-flow-parser.o \
    sol-flow-resolver.o \
    sol-flow-builder.o

obj-flow-$(ENABLE_DYNAMIC_MODULES) += \
    sol-flow-modules.o \
    sol-flow-parser-dynamic.o

ifeq (y,$(RESOLVER_CONFFILE))
def-resolver-flag = -Dsol_flow_default_resolver=sol_flow_resolver_conffile
else
def-resolver-flag = -Dsol_flow_default_resolver=sol_flow_resolver_builtins
endif

obj-flow-$(RESOLVER_CONFFILE) += \
    sol-flow-resolver-conffile.o

obj-flow-$(FLOW_SUPPORT)-extra-cflags += $(def-resolver-flag)

headers-$(FLOW_SUPPORT) := \
    include/sol-flow-builder.h \
    include/sol-flow.h \
    include/sol-flow-inspector.h \
    include/sol-flow-metatype.h \
    include/sol-flow-packet.h \
    include/sol-flow-parser.h \
    include/sol-flow-resolver.h \
    include/sol-flow-simplectype.h \
    include/sol-flow-static.h \
    sol-flow-buildopts.h.in
