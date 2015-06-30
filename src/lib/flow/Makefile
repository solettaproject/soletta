obj-$(FLOW) += flow.mod

obj-flow-$(FLOW) := \
    sol-flow-node-options.o \
    sol-flow-packet.o \
    sol-flow-static.o \
    sol-flow.o

obj-flow-$(RESOLVER_CONFFILE) += \
    sol-flow-resolver-conffile.o \
    sol-flow-resolver.o
obj-flow-$(RESOLVER_CONFFILE)-extra-cflags := $(GLIB_CFLAGS)
obj-flow-$(RESOLVER_CONFFILE)-extra-ldflags += $(GLIB_LDFLAGS)

obj-flow-$(NODE_DESCRIPTION) += \
    sol-flow-builder.o \
    sol-flow-parser.o
