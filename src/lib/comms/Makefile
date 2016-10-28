obj-$(NETWORK) += networking.mod \
    dtls.mod

obj-$(BLUETOOTH) += \
    bluetooth.mod

obj-networking-$(NETWORK) := \
    sol-comms.o \
    sol-socket.o

obj-networking-$(NETCTL) += \
    sol-netctl-impl-connman.o
obj-networking-$(NETCTL)-extra-cflags += $(SYSTEMD_CFLAGS)
obj-networking-$(NETCTL)-extra-ldflags += $(SYSTEMD_LDFLAGS)

obj-dtls-$(DTLS) += \
    sol-socket-dtls-impl-tinydtls.o \
    $(TINYDTLS_SRC_PATH)/aes/rijndael.o \
    $(TINYDTLS_SRC_PATH)/ccm.o \
    $(TINYDTLS_SRC_PATH)/crypto.o \
    $(TINYDTLS_SRC_PATH)/dtls.o \
    $(TINYDTLS_SRC_PATH)/dtls_time.o \
    $(TINYDTLS_SRC_PATH)/ecc/ecc.o \
    $(TINYDTLS_SRC_PATH)/hmac.o \
    $(TINYDTLS_SRC_PATH)/netq.o \
    $(TINYDTLS_SRC_PATH)/peer.o \
    $(TINYDTLS_SRC_PATH)/session.o \
    $(TINYDTLS_SRC_PATH)/sha2/sha2.o

obj-dtls-$(DTLS)-extra-cflags += \
    -I$(TINYDTLS_SRC_PATH) \
    -w

obj-networking-$(PLATFORM_RIOTOS) += \
    sol-network-impl-riot.o \
    sol-socket-impl-riot.o

obj-networking-$(PLATFORM_LINUX) += \
    sol-network-impl-linux.o \
    sol-network-util-impl-linux.o \
    sol-socket-impl-linux.o

obj-networking-$(PLATFORM_ZEPHYR) += \
    sol-network-impl-zephyr.o \
    sol-network-util-impl-zephyr.o \
    sol-socket-impl-zephyr.o

obj-networking-$(PLATFORM_ZEPHYR)-extra-cflags += \
    -I$(ZEPHYR_BASE)/net/ip/contiki \
    -I$(ZEPHYR_BASE)/net/ip/contiki/os \
    -I$(ZEPHYR_BASE)/net/ip

obj-networking-$(PLATFORM_CONTIKI) += \
    sol-network-impl-contiki.o \
    sol-socket-impl-contiki.o

obj-networking-$(COAP) += \
    coap.o \
    sol-coap.o

obj-$(OIC) += tinycbor.mod

obj-tinycbor-y = \
    $(TINYCBOR_SRC_PATH)/cborencoder.o \
    $(TINYCBOR_SRC_PATH)/cborerrorstrings.o \
    $(TINYCBOR_SRC_PATH)/cborparser.o \
    $(TINYCBOR_SRC_PATH)/cborparser_dup_string.o \
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
    sol-oic-security.o

obj-networking-$(OIC)-extra-cflags += \
    -Wno-cpp \
    -Wno-declaration-after-statement \
    -Wno-float-equal \
    -Wno-undef \
    -DOIC_MANUFACTURER_NAME=$(OIC_SERVER_MANUFACTURER_NAME) \
    -DOIC_MANUFACTURER_URL=$(OIC_SERVER_MANUFACTURER_URL) \
    -DOIC_MODEL_NUMBER=$(OIC_SERVER_MODEL_NUMBER) \
    -DOIC_MANUFACTURE_DATE=$(OIC_SERVER_MANUFACTURE_DATE) \
    -DOIC_PLATFORM_VERSION=$(OIC_SERVER_PLATFORM_VERSION) \
    -DOIC_HARDWARE_VERSION=$(OIC_SERVER_HARDWARE_VERSION) \
    -DOIC_FIRMWARE_VERSION=$(OIC_SERVER_FIRMWARE_VERSION) \
    -DOIC_SUPPORT_URL=$(OIC_SERVER_SUPPORT_URL) \
    -DOIC_DEVICE_NAME=$(OIC_SERVER_DEVICE_NAME)

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
    sol-lwm2m-common.o \
    sol-lwm2m-client.o \
    sol-lwm2m-server.o \
    sol-lwm2m-bs-server.o \
    sol-lwm2m-security.o

obj-bluetooth-$(BLUETOOTH) += \
	sol-bluetooth.o

obj-bluetooth-$(BLUETOOTH_BLUEZ) += \
	sol-bluetooth-impl-bluez.o \
	sol-gatt-impl-bluez.o

obj-bluetooth-$(BLUETOOTH_NONE) += \
	sol-bluetooth-impl-none.o \
	sol-gatt-impl-none.o

headers-$(NETWORK) += \
    include/sol-network.h \
    include/sol-socket.h

headers-$(COAP) += \
    include/sol-coap.h

headers-$(BLUETOOTH) += \
    include/sol-bluetooth.h \
    include/sol-gatt.h

headers-$(NETCTL) += \
    include/sol-netctl.h

headers-$(OIC) += \
    include/sol-oic.h \
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
    include/sol-lwm2m.h \
    include/sol-lwm2m-client.h \
    include/sol-lwm2m-server.h \
    include/sol-lwm2m-bs-server.h
