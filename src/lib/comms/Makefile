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

headers-comms-$(NETWORK) += \
    include/sol-network.h

headers-comms-$(COAP) += \
    include/sol-coap.h

headers-comms-$(OIC) += \
    include/sol-oic-client.h \
    include/sol-oic-server.h

ifeq (y,$(HTTP_CLIENT))
obj-comms-$(COMMS) += \
    sol-http-client.o

obj-comms-$(COMMS)-extra-ldflags += \
    $(LIBCURL_LDFLAGS)
endif

headers-comms-$(HTTP_CLIENT) += \
    include/sol-http-client.h
