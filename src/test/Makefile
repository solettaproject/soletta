test-$(TEST_ARENA) += test-arena
test-test-arena-$(TEST_ARENA) := test.c test-arena.c

test-internal-$(TEST_COAP) += test-coap
test-internal-test-coap-$(TEST_COAP) := test.c test-coap.c
test-internal-test-coap-$(TEST_COAP)-deps := lib/comms/coap.o lib/comms/sol-coap.o

test-$(TEST_FBP) += test-fbp
test-test-fbp-$(TEST_FBP) := test.c test-fbp.c

test-$(TEST_FBP_SCANNER) += test-fbp-scanner
test-test-fbp-scanner-$(TEST_FBP_SCANNER) := test.c test-fbp-scanner.c

test-$(TEST_FLOW) += test-flow
test-test-flow-$(TEST_FLOW) := test.c test-flow.c
test-test-flow-$(TEST_FLOW)-deps := flow/timer.mod flow/pwm.mod flow/console.mod

test-$(TEST_FLOW_BUILDER) += test-flow-builder
test-test-flow-builder-$(TEST_FLOW_BUILDER) := test.c test-flow-builder.c

test-$(TEST_FLOW_PARSER) += test-flow-parser
test-test-flow-parser-$(TEST_FLOW_PARSER) := test.c test-flow-parser.c
test-test-flow-parser-$(TEST_FLOW_PARSER)-deps := flow/boolean.mod

test-$(TEST_JAVASCRIPT) += test-javascript
test-test-javascript-$(TEST_JAVASCRIPT) := test.c test-javascript.c

test-$(TEST_MAINLOOP) += test-mainloop
test-test-mainloop-$(TEST_MAINLOOP) := test-mainloop.c

test-$(TEST_MAINLOOP_LINUX) += test-mainloop-linux
test-test-mainloop-linux-$(TEST_MAINLOOP_LINUX) := test-mainloop-linux.c

test-$(TEST_MAINLOOP_THREADS) += test-mainloop-threads
test-test-mainloop-threads-$(TEST_MAINLOOP_THREADS) := test-mainloop-threads.c
test-test-mainloop-threads-$(TEST_MAINLOOP_THREADS)-extra-ldflags += $(PTHREAD_H_LDFLAGS)

test-$(TEST_MAINLOOP_THREADS_SOL_RUN) += test-mainloop-threads-sol-run
test-test-mainloop-threads-sol-run-$(TEST_MAINLOOP_THREADS_SOL_RUN) := test-mainloop-threads-sol-run.c
test-test-mainloop-threads-sol-run-$(TEST_MAINLOOP_THREADS_SOL_RUN)-extra-ldflags += $(PTHREAD_H_LDFLAGS)

test-$(TEST_MAINLOOP_GLIB_INTEGRATION) += test-mainloop-glib-integration
test-test-mainloop-glib-integration-$(TEST_MAINLOOP_GLIB_INTEGRATION) := test-mainloop-glib-integration.c
test-test-mainloop-glib-integration-$(TEST_MAINLOOP_GLIB_INTEGRATION)-extra-cflags += $(GLIB_CFLAGS)
test-test-mainloop-glib-integration-$(TEST_MAINLOOP_GLIB_INTEGRATION)-extra-ldflags += $(GLIB_LDFLAGS)

test-$(TEST_MONITORS) += test-monitors
test-test-monitors-$(TEST_MONITORS) := test.c test-monitors.c

test-$(TEST_STR_SLICE) += test-str-slice
test-test-str-slice-$(TEST_STR_SLICE) := test.c test-str-slice.c

test-$(TEST_STR_SPLIT) += test-str-split
test-test-str-split-$(TEST_STR_SPLIT) := test.c test-str-split.c

test-$(TEST_STR_TABLE) += test-str-table
test-test-str-table-$(TEST_STR_TABLE) := test.c test-str-table.c

test-$(TEST_VECTOR) += test-vector
test-test-vector-$(TEST_VECTOR) := test.c test-vector.c

test-$(TEST_BUFFER) += test-buffer
test-test-buffer-$(TEST_BUFFER) := test.c test-buffer.c

test-$(TEST_JSON) += test-json
test-test-json-$(TEST_JSON) := test.c test-json.c

test-$(TEST_UTIL) += test-util
test-test-util-$(TEST_UTIL) := test.c test-util.c

test-$(TEST_COMPOSED_TYPE) += test-composed-type
test-test-composed-type-$(TEST_COMPOSED_TYPE) := test.c test-composed-type.c
