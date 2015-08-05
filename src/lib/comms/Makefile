obj-$(NETWORK) += networking.mod

obj-networking-$(NETWORK) := \
    sol-comms.o

ifeq (y,$(PLATFORM_RIOTOS))
obj-networking-$(NETWORK) += \
    sol-network-riot.o
endif

ifeq (y,$(PLATFORM_LINUX))
obj-networking-$(NETWORK) += \
    sol-network-linux.o \
    sol-socket-linux.o
endif

ifeq (y,$(COAP))
obj-networking-$(NETWORK) += \
    coap.o \
    sol-coap.o
endif

ifeq (y,$(OIC))
obj-networking-$(NETWORK) += \
    sol-oic-client.o \
    sol-oic-server.o
endif

headers-networking-$(NETWORK) += \
    include/sol-network.h

headers-networking-$(COAP) += \
    include/sol-coap.h

headers-networking-$(OIC) += \
    include/sol-oic-client.h \
    include/sol-oic-server.h

ifeq (y,$(HTTP_CLIENT))
obj-networking-$(NETWORK) += \
    sol-http-client-impl-curl.o

obj-networking-$(NETWORK)-extra-ldflags += \
    $(LIBCURL_LDFLAGS)

endif

headers-networking-$(HTTP_CLIENT) += \
    include/sol-http-client.h
