obj-$(COMMS) += comms.mod

obj-comms-$(COMMS) := \
    coap.o \
    sol-coap.o \
    sol-oic-client.o \
    sol-oic-server.o

ifeq (y,$(PLATFORM_RIOTOS))
obj-comms-$(COMMS) += \
    sol-network-riot.o
endif

ifeq (y,$(SOL_PLATFORM_LINUX))
obj-comms-$(COMMS) += \
    sol-network-linux.o
endif
