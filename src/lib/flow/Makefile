obj-$(USE_FLOW) += flow.mod

obj-flow-$(USE_FLOW) := \
    sol-flow-node-options.o \
    sol-flow-packet.o \
    sol-flow-simple-c-type.o \
    sol-flow-single.o \
    sol-flow-static.o \
    sol-flow.o

obj-flow-$(NODE_DESCRIPTION) += \
    sol-flow-parser.o \
    sol-flow-composed.o \
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

obj-flow-$(USE_FLOW)-extra-cflags += $(def-resolver-flag)

headers-$(USE_FLOW) := \
    include/sol-flow.h \
    include/sol-flow-inspector.h \
    include/sol-flow-metatype.h \
    include/sol-flow-packet.h \
    include/sol-flow-simple-c-type.h \
    include/sol-flow-single.h \
    include/sol-flow-static.h \
    sol-flow-buildopts.h.in

headers-$(NODE_DESCRIPTION) += \
    include/sol-flow-parser.h \
    include/sol-flow-resolver.h \
    include/sol-flow-builder.h
