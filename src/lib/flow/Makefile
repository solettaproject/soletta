obj-$(FLOW) += flow.mod

obj-flow-$(FLOW)              := sol-flow.o sol-flow-node-options.o
obj-flow-$(FLOW)              += sol-flow-packet.o sol-flow-parser.o sol-flow-static.o

obj-flow-$(FLOW_RESOLVER)     += sol-flow-resolver-conffile.o
obj-flow-$(RESOLVER_CONFFILE) += sol-flow-builder.o sol-flow-resolver.o
obj-flow-$(RESOLVER_CONFFILE)-extra-cflags += $(GLIB_CFLAGS)
obj-flow-$(RESOLVER_CONFFILE)-extra-ldflags += $(GLIB_LDFLAGS)
