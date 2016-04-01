bin-$(FBP_RUNNER) += sol-fbp-runner
bin-sol-fbp-runner-$(FBP_RUNNER) := main.c runner.c

ifeq (y,$(INSPECTOR))
bin-sol-fbp-runner-$(FBP_RUNNER) += inspector.c

ifeq (y,$(HTTP_SERVER))
bin-sol-fbp-runner-$(FBP_RUNNER) += web-inspector.c

# TODO: share/soletta/web-inspector static contents
endif

endif
