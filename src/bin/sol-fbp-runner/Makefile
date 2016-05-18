bin-$(FBP_RUNNER) += sol-fbp-runner
bin-sol-fbp-runner-$(FBP_RUNNER) := main.c runner.c

ifeq (y,$(INSPECTOR))
bin-sol-fbp-runner-$(FBP_RUNNER) += inspector.c

ifeq (y,$(HTTP_SERVER))
bin-sol-fbp-runner-$(FBP_RUNNER) += web-inspector.c

data-$(FBP_RUNNER) += web-inspector
data-web-inspector-dir := web-inspector
data-web-inspector-files := web-inspector.html web-inspector.js web-inspector.css

endif

endif
