obj-$(COMMS) += comms.mod

obj-comms-$(COMMS) :=

ifeq (y,$(NETWORK))
    ifeq (y,$(PLATFORM_RIOTOS))
    obj-comms-$(COMMS) += \
        sol-network-riot.o
    endif

    ifeq (y,$(SOL_PLATFORM_LINUX))
    obj-comms-$(COMMS) += \
        sol-network-linux.o
    endif
endif

ifeq (y,$(COAP))
obj-comms-$(COMMS) += \
    coap.o \
    sol-coap.o
endif

ifeq (y,$(OIC))
obj-comms-$(COMMS) += \
    sol-oic-client.o \
    sol-oic-server.o
endif

headers-$(COMMS) :=

ifeq (y,$(NETWORK))
headers-$(COMMS) += \
    include/sol-network.h
endif

ifeq (y,$(COAP))
headers-$(COMMS) += \
    include/sol-coap.h
endif

ifeq (y,$(OIC))
headers-$(COMMS) += \
    include/sol-oic-client.h \
    include/sol-oic-server.h
endif
