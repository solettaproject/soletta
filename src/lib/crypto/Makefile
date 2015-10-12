obj-y += crypto.mod

obj-crypto-y := \
    sol-crypto.o

obj-crypto-$(CRYPTO_MESSAGE_DIGEST_LINUX_KCAPI) += \
   sol-message-digest-impl-linux-kcapi.o

obj-crypto-$(CRYPTO_MESSAGE_DIGEST_NONE) += \
   sol-message-digest-impl-none.o

headers-y := \
    include/sol-message-digest.h
