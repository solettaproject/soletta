obj-$(NETWORK) += networking.mod

obj-networking-$(NETWORK) := \
    sol-comms.o

obj-networking-$(PLATFORM_RIOTOS) += \
    sol-network-riot.o \
    sol-socket-riot.o

obj-networking-$(PLATFORM_LINUX) += \
    sol-network-linux.o \
    sol-socket-linux.o

obj-networking-$(COAP) += \
    coap.o \
    sol-coap.o

obj-networking-$(OIC) += \
    sol-oic-cbor.o \
    sol-oic-client.o \
    sol-oic-server.o \
    $(TINYCBOR_SRC_PATH)/cborencoder.o \
    $(TINYCBOR_SRC_PATH)/cborerrorstrings.o \
    $(TINYCBOR_SRC_PATH)/cborparser.o \
    $(TINYCBOR_SRC_PATH)/cborpretty.o

obj-networking-$(OIC)-extra-cflags := \
    -I$(TINYCBOR_SRC_PATH) \
    -Wno-cpp \
    -Wno-declaration-after-statement \
    -Wno-float-equal \
    -Wno-undef

obj-networking-$(HTTP) += \
    sol-http-common.o

obj-networking-$(HTTP_CLIENT) += \
    sol-http-client-impl-curl.o

obj-networking-$(HTTP_CLIENT)-extra-ldflags += \
    $(LIBCURL_LDFLAGS)

requires-private-$(HTTP_CLIENT) := \
    $(LIBCURL_REQUIRES_PRIVATE)

obj-networking-$(HTTP_SERVER) += \
    sol-http-server-impl-microhttpd.o

obj-networking-$(HTTP_SERVER)-extra-ldflags += \
    $(LIBMICROHTTPD_LDFLAGS)

requires-private-$(HTTP_SERVER) := \
    $(LIBMICROHTTPD_REQUIRES_PRIVATE)

obj-networking-$(MQTT) += \
	sol-mqtt-impl-mosquitto.o

obj-networking-$(MQTT)-extra-ldflags += \
	$(MOSQUITTO_LDFLAGS)

headers-networking-$(NETWORK) += \
    include/sol-network.h

headers-networking-$(COAP) += \
    include/sol-coap.h

headers-networking-$(OIC) += \
    include/sol-oic-common.h \
    include/sol-oic-client.h \
    include/sol-oic-server.h \
    sol-oic-cbor.h

headers-networking-$(HTTP) += \
    include/sol-http.h

headers-networking-$(HTTP_CLIENT) += \
    include/sol-http-client.h

headers-networking-$(HTTP_SERVER) += \
    include/sol-http-server.h

headers-networking-$(MQTT) += \
	include/sol-mqtt.h
