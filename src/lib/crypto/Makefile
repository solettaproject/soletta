obj-y += crypto.mod

obj-crypto-y := \
    sol-crypto.o

obj-crypto-$(CRYPTO_MESSAGE_DIGEST_COMMON) += \
   sol-message-digest-common.o

obj-crypto-$(CRYPTO_MESSAGE_DIGEST_LINUX_KCAPI) += \
   sol-message-digest-impl-linux-kcapi.o

obj-crypto-$(CRYPTO_MESSAGE_DIGEST_OPENSSL) += \
   sol-message-digest-impl-openssl.o
obj-crypto-$(CRYPTO_MESSAGE_DIGEST_OPENSSL)-extra-ldflags += \
    $(OPENSSL_LDFLAGS)
obj-crypto-$(CRYPTO_MESSAGE_DIGEST_OPENSSL)-extra-cflags += \
    $(OPENSSL_CFLAGS)

obj-crypto-$(CRYPTO_MESSAGE_DIGEST_RIOT) += \
    sol-message-digest-impl-riot.o

obj-crypto-$(CRYPTO_MESSAGE_DIGEST_NONE) += \
   sol-message-digest-impl-none.o

headers-y := \
    include/sol-message-digest.h
