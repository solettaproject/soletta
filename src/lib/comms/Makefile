obj-$(NETWORK) += networking.mod \
    dtls.mod

obj-networking-$(NETWORK) := \
    sol-comms.o \
    sol-socket.o

obj-dtls-$(DTLS) += \
    $(TINYDTLS_SRC_PATH)/ccm.o \
    $(TINYDTLS_SRC_PATH)/crypto.o \
    $(TINYDTLS_SRC_PATH)/dtls.o \
    $(TINYDTLS_SRC_PATH)/dtls_time.o \
    $(TINYDTLS_SRC_PATH)/hmac.o \
    $(TINYDTLS_SRC_PATH)/netq.o \
    $(TINYDTLS_SRC_PATH)/peer.o \
    $(TINYDTLS_SRC_PATH)/session.o \
    $(TINYDTLS_SRC_PATH)/ecc/ecc.o \
    $(TINYDTLS_SRC_PATH)/aes/rijndael.o \
    $(TINYDTLS_SRC_PATH)/sha2/sha2.o \
    sol-socket-dtls-impl-tinydtls.o

obj-dtls-$(DTLS)-extra-cflags += \
    -I$(TINYDTLS_SRC_PATH) \
    -w

obj-networking-$(PLATFORM_RIOTOS) += \
    sol-network-impl-riot.o \
    sol-socket-impl-riot.o

obj-networking-$(PLATFORM_LINUX) += \
    sol-network-impl-linux.o \
    sol-socket-impl-linux.o

obj-networking-$(COAP) += \
    coap.o \
    sol-coap.o

obj-$(OIC) += tinycbor.mod

obj-tinycbor-y = \
    $(TINYCBOR_SRC_PATH)/cborencoder.o \
    $(TINYCBOR_SRC_PATH)/cborerrorstrings.o \
    $(TINYCBOR_SRC_PATH)/cborparser.o \
    $(TINYCBOR_SRC_PATH)/cborpretty.o

obj-tinycbor-private-headers += \
    $(TINYCBOR_SRC_PATH)/cbor.h

obj-tinycbor-y-extra-cflags += \
    -Wno-cpp \
    -Wno-declaration-after-statement \
    -Wno-float-equal \
    -Wno-undef

obj-networking-$(OIC) += \
    sol-oic-cbor.o \
    sol-oic-client.o \
    sol-oic-common.o \
    sol-oic-server.o \

obj-networking-$(OIC)-extra-cflags += \
    -Wno-cpp \
    -Wno-declaration-after-statement \
    -Wno-float-equal \
    -Wno-undef

obj-networking-$(HTTP) += \
    sol-http-common.o

obj-networking-$(HTTP_CLIENT) += \
    sol-http-client-impl-curl.o

obj-networking-$(HTTP_CLIENT)-extra-cflags += \
    $(LIBCURL_CFLAGS)

obj-networking-$(HTTP_CLIENT)-extra-ldflags += \
    $(LIBCURL_LDFLAGS)

requires-private-$(HTTP_CLIENT) += \
    $(LIBCURL_REQUIRES_PRIVATE)

obj-networking-$(HTTP_SERVER) += \
    sol-http-server-impl-microhttpd.o

obj-networking-$(HTTP_SERVER)-extra-cflags += \
    $(LIBMICROHTTPD_CFLAGS)

obj-networking-$(HTTP_SERVER)-extra-ldflags += \
    $(LIBMICROHTTPD_LDFLAGS) \
    $(LIBMAGIC_LDFLAGS)

requires-private-$(HTTP_SERVER) += \
    $(LIBMICROHTTPD_REQUIRES_PRIVATE)

obj-networking-$(MQTT) += \
	sol-mqtt-impl-mosquitto.o

obj-networking-$(MQTT)-extra-ldflags += \
	$(MOSQUITTO_LDFLAGS)

obj-networking-$(MAVLINK) += \
	sol-mavlink.o

obj-networking-$(MAVLINK)-extra-cflags += \
	-I$(MAVLINK_SRC_PATH)/common/ \
	-I$(MAVLINK_SRC_PATH)/ardupilotmega/ \
	-Wno-declaration-after-statement

obj-networking-$(LWM2M) += \
	sol-lwm2m.o

headers-$(NETWORK) += \
    include/sol-network.h

headers-$(COAP) += \
    include/sol-coap.h

headers-$(OIC) += \
    include/sol-oic-common.h \
    include/sol-oic-client.h \
    include/sol-oic-server.h

headers-$(HTTP) += \
    include/sol-http.h

headers-$(HTTP_CLIENT) += \
    include/sol-http-client.h

headers-$(HTTP_SERVER) += \
    include/sol-http-server.h

headers-$(MQTT) += \
	include/sol-mqtt.h

headers-$(MAVLINK) += \
	include/sol-mavlink.h

headers-$(LWM2M) += \
    include/sol-lwm2m.h
