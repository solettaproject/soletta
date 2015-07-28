obj-$(NETWORK) += networking.mod

obj-networking-$(NETWORK) := \
    sol-comms.o

obj-networking-$(PLATFORM_RIOTOS) += \
    sol-network-riot.o

obj-networking-$(PLATFORM_LINUX) += \
    sol-network-linux.o \
    sol-socket-linux.o

obj-networking-$(COAP) += \
    coap.o \
    sol-coap.o

obj-networking-$(OIC) += \
    sol-oic-client.o \
    sol-oic-server.o

obj-networking-$(HTTP) += \
    sol-http-common.o

obj-networking-$(HTTP_CLIENT) += \
    sol-http-client-impl-curl.o

obj-networking-$(HTTP_CLIENT)-extra-ldflags += \
    $(LIBCURL_LDFLAGS)

obj-networking-$(HTTP_SERVER) += \
    sol-http-server-impl-microhttpd.o

obj-networking-$(HTTP_SERVER)-extra-ldflags += \
    $(LIBMICROHTTPD_LDFLAGS)

headers-networking-$(NETWORK) += \
    include/sol-network.h

headers-networking-$(COAP) += \
    include/sol-coap.h

headers-networking-$(OIC) += \
    include/sol-oic-client.h \
    include/sol-oic-server.h

headers-networking-$(HTTP) += \
    include/sol-http.h

headers-networking-$(HTTP_CLIENT) += \
    include/sol-http-client.h

headers-networking-$(HTTP_SERVER) += \
    include/sol-http-server.h
