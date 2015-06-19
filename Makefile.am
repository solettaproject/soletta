DISTCLEAN_LOCAL_HOOKS =
EXTRA_DIST =
CLEANFILES = $(BUILT_FILES) $(BUILT_SOURCES)
DISTCLEANFILES =
BUILT_FILES =
BUILT_SOURCES =
ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory

GCC_COLORS ?= 'yes'
export GCC_COLORS

pkgsysconfdir=$(sysconfdir)/soletta

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA =

flowtypedescdir=$(pkgdatadir)/flow/descriptions
flowtypedesc_DATA =

flowtypeschemasdir=$(pkgdatadir)/flow/schemas
flowtypeschemas_DATA=

dist_pkgdata_DATA = \
$(top_builddir)/data/jsons/platform_detect.json

if MAINTAINER_MODE
flowmodulesdir=$(abs_top_builddir)/src/modules/flow/.libs
else
flowmodulesdir = $(SOL_CONFFILE_MODULES_DIR)/flow
endif
flowmodules_LTLIBRARIES =

AM_CPPFLAGS = \
	-include $(top_builddir)/sol_config.h \
	-I$(top_builddir)/src/lib/common \
	-I$(top_builddir)/src/lib/flow \
	-I$(top_builddir)/src/modules/flow/aio \
	-I$(top_builddir)/src/modules/flow/app \
	-I$(top_builddir)/src/modules/flow/boolean \
	-I$(top_builddir)/src/modules/flow/byte \
	-I$(top_builddir)/src/modules/flow/calamari \
	-I$(top_builddir)/src/modules/flow/color \
	-I$(top_builddir)/src/modules/flow/console \
	-I$(top_builddir)/src/modules/flow/constant \
	-I$(top_builddir)/src/modules/flow/converter \
	-I$(top_builddir)/src/modules/flow/dummy \
	-I$(top_builddir)/src/modules/flow/evdev \
	-I$(top_builddir)/src/modules/flow/file \
	-I$(top_builddir)/src/modules/flow/filter-repeated \
	-I$(top_builddir)/src/modules/flow/float \
	-I$(top_builddir)/src/modules/flow/fs \
	-I$(top_builddir)/src/modules/flow/gpio \
	-I$(top_builddir)/src/modules/flow/gtk \
	-I$(top_builddir)/src/modules/flow/hub \
	-I$(top_builddir)/src/modules/flow/int \
	-I$(top_builddir)/src/modules/flow/keyboard \
	-I$(top_builddir)/src/modules/flow/lcd-grove \
	-I$(top_builddir)/src/modules/flow/network \
	-I$(top_builddir)/src/modules/flow/oic \
	-I$(top_builddir)/src/modules/flow/piezo-speaker \
	-I$(top_builddir)/src/modules/flow/platform \
	-I$(top_builddir)/src/modules/flow/pwm \
	-I$(top_builddir)/src/modules/flow/random \
	-I$(top_builddir)/src/modules/flow/servo-motor \
	-I$(top_builddir)/src/modules/flow/string \
	-I$(top_builddir)/src/modules/flow/test \
	-I$(top_builddir)/src/modules/flow/timer \
	-I$(top_builddir)/src/modules/flow/trigonometry \
	-I$(top_builddir)/src/modules/flow/udev \
	-I$(top_builddir)/src/modules/flow/unix-socket \
	-I$(top_builddir)/src/modules/flow/wallclock \
	-I$(top_srcdir)/src/lib/flow \
	-I$(top_srcdir)/src/lib/common \
	-I$(top_srcdir)/src/shared \
	-I$(top_builddir) \
	-DDATADIR=\"$(pkgdatadir)\" \
	-DPKGSYSCONFDIR=\"$(pkgsysconfdir)\" \
	-DFLOWMODULESDIR=\"$(flowmodulesdir)\" \
	-DLINUXMICROMODULESDIR=\"$(linuxmicromodulesdir)\" \
	-DPINMUXDIR=\"$(pinmuxdir)\"
AM_CFLAGS = $(SOL_COMMON_CFLAGS) -std=gnu99
AM_LDFLAGS = $(SOL_COMMON_LDFLAGS)

if BUILD_COAP
AM_CPPFLAGS += -I$(top_srcdir)/src/lib/coap
endif

# conf files
EXTRA_DIST += \
	$(wildcard src/tests-fbp/*.conf)

# fbp files
EXTRA_DIST += \
	$(wildcard src/tests-fbp/*.fbp) \
	$(wildcard src/samples/flow/*/*.fbp) \
	$(wildcard src/samples/flow/*/README)

bin_PROGRAMS =
dist_bin_SCRIPTS =
lib_LTLIBRARIES =
noinst_PROGRAMS =
noinst_LTLIBRARIES =
pkginclude_HEADERS =
nodist_pkginclude_HEADERS =

dist_bin_SCRIPTS += \
data/scripts/sol-flow-node-type-gen.py \
data/scripts/sol-flow-node-type-find.py \
data/scripts/sol-flow-node-type-validate.py

EXTRA_DIST += \
data/scripts/sol-builtins-gen.py \
data/scripts/sol-flow-node-type-find.py.in \
data/scripts/sol-flow-node-type-gen-cheat-sheet.py \
data/scripts/sol-flow-node-type-gen-html.py \
data/scripts/sol-flow-node-type-stub-gen.py \
data/scripts/sol-flow-node-types-h-gen.py \
data/scripts/json-format.py \
data/scripts/json-format-check.sh \
data/scripts/flow-merge-builtins.py \
data/gdb/libsoletta.so-gdb.py \
data/vim/fbp.vim

gdbautoloaddir = $(datadir)/gdb/auto-load/$(libdir)
gdbautoload_DATA = data/gdb/libsoletta.so-gdb.py

install-data-hook:
	cd $(DESTDIR)$(gdbautoloaddir) && \
		rm -f libsoletta.so.0.0.0-gdb.py && \
		$(LN_S) libsoletta.so-gdb.py libsoletta.so.0.0.0-gdb.py

data/scripts/sol-flow-node-type-find.py: data/scripts/sol-flow-node-type-find.py.in Makefile
	@mkdir -p data/scripts
	$(AM_V_GEN)sed 's:@flowtypedescdir@:$(flowtypedescdir):g' $< > $@ && chmod +x $@
BUILT_FILES += data/scripts/sol-flow-node-type-find.py

data/scripts/sol-flow-node-type-gen.py: data/scripts/sol-flow-node-type-gen.py.in Makefile
	@mkdir -p data/scripts
	$(AM_V_GEN)sed 's:@pkgdatadir@:$(pkgdatadir):g' $< > $@ && chmod +x $@
BUILT_FILES += data/scripts/sol-flow-node-type-gen.py

lib_LTLIBRARIES += src/lib/libsoletta.la
src_lib_libsoletta_la_SOURCES = \
src/lib/common/sol-log-impl.h \
src/lib/common/sol-log-internal.h \
src/lib/common/sol-mainloop.c \
src/lib/common/sol-mainloop-impl.h \
src/lib/common/sol-platform.c \
src/lib/common/sol-platform-impl.h \
src/lib/common/sol-blob.c \
src/lib/common/sol-socket.h \
src/lib/common/sol-types.c \
src/lib/flow/sol-flow-internal.h \
src/lib/flow/sol-flow-packet.c \
src/lib/flow/sol-flow-resolver.c \
src/lib/flow/sol-flow-static.c \
src/lib/flow/sol-flow-node-options.c \
src/lib/flow/sol-flow.c

if SOL_LOG_ENABLED
src_lib_libsoletta_la_SOURCES += src/lib/common/sol-log.c

if HAVE_PLATFORM_LINUX
src_lib_libsoletta_la_SOURCES += \
src/lib/common/sol-log-impl-linux.c \
src/lib/common/sol-platform-detect.c \
src/lib/common/sol-platform-detect.h
endif
if HAVE_PLATFORM_RIOT
src_lib_libsoletta_la_SOURCES += src/lib/common/sol-log-impl-riot.c
endif

endif

SOLETTA_FLOW_TYPE_JSONS =

SOLETTA_FLOW_NODE_TYPE_SCHEMA = $(top_srcdir)/data/schemas/node-type-genspec.schema
flowtypeschemas_DATA += $(SOLETTA_FLOW_NODE_TYPE_SCHEMA)

SOLETTA_FLOW_BUILT_SRCS = $(patsubst %.json,%-gen.c,$(SOLETTA_FLOW_TYPE_JSONS))
SOLETTA_FLOW_BUILT_HDRS = $(patsubst %.json,%-gen.h,$(SOLETTA_FLOW_TYPE_JSONS))
nodist_pkginclude_HEADERS += $(SOLETTA_FLOW_BUILT_HDRS)
EXTRA_DIST += $(SOLETTA_FLOW_TYPE_JSONS) $(SOLETTA_FLOW_NODE_TYPE_SCHEMA)

BUILT_SOURCES += $(SOLETTA_FLOW_BUILT_HDRS) $(SOLETTA_FLOW_BUILT_SRCS)

# BUILT_SOURCES are generated before any other target, but they are also part
# of parallel builds, so with enough processing we may end up trying to build
# the library before the files are generated. By adding this explicit
# depdendency we can ensure that all the required files will be there by the
# time they are needed.
$(src_lib_libsoletta_la_OBJECTS): $(SOLETTA_FLOW_BUILT_HDRS) $(SOLETTA_FLOW_BUILT_SRCS) src/lib/flow/sol-flow-node-types.h

src/lib/flow/sol-flow-node-types.h: $(SOLETTA_FLOW_BUILT_HDRS) $(top_srcdir)/data/scripts/sol-flow-node-types-h-gen.py
	$(AM_V_GEN)$(PYTHON) $(top_srcdir)/data/scripts/sol-flow-node-types-h-gen.py  src/lib/flow/sol-flow-node-types.h $(SOLETTA_FLOW_BUILT_HDRS)
BUILT_SOURCES += src/lib/flow/sol-flow-node-types.h

if SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
if HAVE_SOL_FLOW_NODE_TYPES
bin_PROGRAMS += src/bin/sol-flow-node-types/sol-flow-node-types
src_bin_sol_flow_node_types_sol_flow_node_types_CFLAGS = \
	$(src_lib_libsoletta_la_CFLAGS)
src_bin_sol_flow_node_types_sol_flow_node_types_LDADD = \
	src/lib/libsoletta.la \
	src/shared/libshared.la \
	@dl_libs@
src_bin_sol_flow_node_types_sol_flow_node_types_SOURCES = \
	src/bin/sol-flow-node-types/main.c
endif

src_lib_libsoletta_la_SOURCES += \
	src/lib/flow/sol-flow-builder.c \
	src/lib/flow/sol-flow-parser.c

pkginclude_HEADERS += \
	src/lib/flow/sol-flow-builder.h \
	src/lib/flow/sol-flow-parser.h

if HAVE_SOL_FLOW_NODE_TYPES
flowtypedesc_DATA += src/lib/flow/builtins.json
BUILT_FILES += ${flowtypedesc_DATA}
src/lib/flow/builtins.json: src/lib/libsoletta.la
	$(AM_V_GEN)$(PYTHON) $(top_srcdir)/data/scripts/flow-merge-builtins.py --modules-dir=$(top_builddir)/src/modules/flow $@ $(flow_builtin_modules)
endif # HAVE_SOL_FLOW_NODE_TYPES
endif

if BUILD_SOL_FLOW_MODULE_RESOLVER_CONFFILE
src_lib_libsoletta_la_SOURCES += \
src/lib/flow/sol-flow-resolver-conffile.c
endif

src_lib_libsoletta_la_CFLAGS = \
$(AM_CFLAGS)
src_lib_libsoletta_la_LIBADD = \
src/shared/libshared.la \
-lm \
@dl_libs@

if HAVE_PLATFORM_LINUX
src_lib_libsoletta_la_SOURCES += \
src/lib/common/sol-socket-linux.c
endif

if HAVE_MAINLOOP_GLIB
src_lib_libsoletta_la_SOURCES += \
src/lib/common/sol-mainloop-impl-glib.c
src_lib_libsoletta_la_CFLAGS += @GLIB_CFLAGS@
src_lib_libsoletta_la_LIBADD += @GLIB_LIBS@
endif

if HAVE_MAINLOOP_POSIX
src_lib_libsoletta_la_SOURCES += \
src/lib/common/sol-mainloop-impl-posix.c
endif

if HAVE_MAINLOOP_RIOT
src_lib_libsoletta_la_SOURCES += \
src/lib/common/sol-mainloop-impl-riot.c \
src/lib/common/sol-interrupt_scheduler_riot.c \
src/lib/common/sol-interrupt_scheduler_riot.h
endif

if HAVE_PLATFORM_SYSTEMD
src_lib_libsoletta_la_SOURCES += \
src/lib/common/sol-bus.c \
src/lib/common/sol-bus.h \
src/lib/common/sol-platform-impl-systemd.c
src_lib_libsoletta_la_CFLAGS += @SYSTEMD_CFLAGS@
src_lib_libsoletta_la_LIBADD += @SYSTEMD_LIBS@
endif

if HAVE_PLATFORM_LINUX_MICRO
src_lib_libsoletta_la_SOURCES += \
src/lib/common/sol-platform-impl-linux-micro.c
pkginclude_HEADERS += \
src/lib/common/sol-platform-linux-micro.h

if MAINTAINER_MODE
linuxmicromodulesdir = $(abs_top_builddir)/src/modules/linux-micro/.libs
else
linuxmicromodulesdir = $(SOL_CONFFILE_MODULES_DIR)/linux-micro
endif
linuxmicromodules_LTLIBRARIES =

linuxmicromodules_DATA = data/linux-micro/initial-services

data/linux-micro/initial-services: Makefile.am
	@rm -f $@
	@mkdir -p data/linux-micro
	$(AM_V_GEN)echo "$(linux_micro_initial_services)" | tr ' ' '\n' > $@
BUILT_FILES += data/linux-micro/initial-services

src/lib/common/sol-platform-linux-micro-builtins-gen.h: Makefile data/scripts/sol-builtins-gen.py
	@rm -f $@
	@mkdir -p src/lib/common
	$(AM_V_GEN)$(PYTHON) $(top_srcdir)/data/scripts/sol-builtins-gen.py \
		--output=$@ \
		--array="static const struct sol_platform_linux_micro_module *SOL_PLATFORM_LINUX_MICRO_MODULE_ALL[]" \
		--count="SOL_PLATFORM_LINUX_MICRO_MODULE_COUNT" \
		--decl="struct sol_platform_linux_micro_module SOL_PLATFORM_LINUX_MICRO_MODULE_@NAME@" \
		--item="&SOL_PLATFORM_LINUX_MICRO_MODULE_@NAME@" \
		$(linux_micro_builtin_modules)

BUILT_SOURCES += src/lib/common/sol-platform-linux-micro-builtins-gen.h

include sol_linux_micro_modules.mk
endif # HAVE_PLATFORM_LINUX_MICRO

if HAVE_PLATFORM_LINUX_DUMMY
src_lib_libsoletta_la_SOURCES += \
src/lib/common/sol-platform-impl-dummy.c
endif

if HAVE_PLATFORM_RIOT
src_lib_libsoletta_la_SOURCES += \
src/lib/common/sol-platform-impl-riot.c
endif

pkginclude_HEADERS += \
src/lib/common/sol-macros.h \
src/lib/common/sol-log.h \
src/lib/common/sol-mainloop.h \
src/lib/common/sol-platform.h \
src/lib/common/sol-blob.h \
src/lib/common/sol-types.h \
src/lib/common/sol-missing.h \
src/lib/flow/sol-flow.h \
src/lib/flow/sol-flow-resolver.h \
src/lib/flow/sol-flow-packet.h

nodist_pkginclude_HEADERS += \
src/lib/common/sol-common-buildopts.h \
src/lib/flow/sol-flow-buildopts.h \
src/lib/flow/sol-flow-node-types.h \
src/lib/flow/sol-flow-builtins-gen.h

if SOL_FLOW_INSPECTOR_ENABLED
pkginclude_HEADERS += src/lib/flow/sol-flow-inspector.h
endif

pkgconfig_DATA += \
pc/soletta.pc

src_shared_libshared_la_CFLAGS =
src_shared_libshared_la_LIBADD =

noinst_LTLIBRARIES += src/shared/libshared.la
src_shared_libshared_la_SOURCES = \
src/shared/sol-arena.c \
src/shared/sol-arena.h \
src/shared/sol-buffer.c \
src/shared/sol-buffer.h \
src/shared/sol-fbp-graph.c \
src/shared/sol-fbp-internal-log.c \
src/shared/sol-fbp-internal-log.h \
src/shared/sol-fbp-internal-scanner.c \
src/shared/sol-fbp-internal-scanner.h \
src/shared/sol-fbp-parser.c \
src/shared/sol-fbp.h \
src/shared/sol-util.c \
src/shared/sol-util.h \
src/shared/sol-file-reader.h \
src/shared/sol-gpio.h \
src/shared/sol-i2c.h \
src/shared/sol-list.h \
src/shared/sol-monitors.c \
src/shared/sol-monitors.h \
src/shared/sol-network.h \
src/shared/sol-pwm.h \
src/shared/sol-spi.h \
src/shared/sol-str-slice.c \
src/shared/sol-str-slice.h \
src/shared/sol-str-table.c \
src/shared/sol-str-table.h \
src/shared/sol-vector.c \
src/shared/sol-uart.h \
src/shared/sol-vector.h \
src/shared/sol-worker-thread.h \
src/shared/sol-json.h \
src/shared/sol-json.c

if HAVE_GLIB
if SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
src_shared_libshared_la_SOURCES += \
src/shared/sol-conffile.c \
src/shared/sol-conffile.h

src_shared_libshared_la_CFLAGS += @GLIB_CFLAGS@
src_shared_libshared_la_LIBADD += @GLIB_LIBS@
endif # SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
endif # HAVE_GLIB

if HAVE_PLATFORM_LINUX
src_shared_libshared_la_SOURCES += \
src/shared/sol-file-reader.c \
src/shared/sol-gpio-linux.c \
src/shared/sol-network-linux.c \
src/shared/sol-pwm-linux.c \
src/shared/sol-i2c-linux.c \
src/shared/sol-spi-linux.c \
src/shared/sol-uart-linux.c

if PTHREAD_LIBS
src_shared_libshared_la_SOURCES += \
src/shared/sol-worker-thread-pthread.c
endif

endif

if HAVE_PLATFORM_RIOT
src_shared_libshared_la_SOURCES += \
src/shared/sol-gpio-riot.c \
src/shared/sol-i2c-riot.c \
src/shared/sol-pwm-riot.c \
src/shared/sol-spi-riot.c \
src/shared/sol-uart-riot.c \
src/shared/sol-network-riot.c
endif

src_shared_libshared_la_CFLAGS += $(AM_CFLAGS)

if PTHREAD_LIBS
src_shared_libshared_la_LIBADD += \
	@PTHREAD_LIBS@
endif

if BUILD_COAP
src_lib_libsoletta_la_SOURCES += \
src/lib/coap/coap.c \
src/lib/coap/coap.h \
src/lib/coap/sol-coap.c \
src/lib/coap/sol-coap.h \
src/lib/coap/sol-oic-server.c \
src/lib/coap/sol-oic-server.h \
src/lib/coap/sol-oic-client.c \
src/lib/coap/sol-oic-client.h
endif

if BUILD_SAMPLES

noinst_PROGRAMS += src/samples/common/platform-simple
src_samples_common_platform_simple_CFLAGS = $(src_lib_libsoletta_la_CFLAGS)
src_samples_common_platform_simple_LDADD = src/lib/libsoletta.la

if HAVE_PLATFORM_LINUX_MICRO
noinst_PROGRAMS += src/samples/common/linux-micro-init
src_samples_common_linux_micro_init_CFLAGS = $(src_lib_libsoletta_la_CFLAGS)
src_samples_common_linux_micro_init_LDADD = src/lib/libsoletta.la src/shared/libshared.la
if ENABLE_STATIC
if ENABLE_DYNAMIC_MODULES
src_samples_common_linux_micro_init_LDFLAGS = -static -rdynamic
else
src_samples_common_linux_micro_init_LDFLAGS = -all-static
endif
endif

noinst_PROGRAMS += src/samples/common/uart
src_samples_common_uart_CFLAGS = $(src_lib_libsoletta_la_CFLAGS)
src_samples_common_uart_LDADD = src/lib/libsoletta.la src/shared/libshared.la

endif

noinst_LTLIBRARIES += src/samples/flow/c-api/libcustom-node-types.la
src_samples_flow_c_api_libcustom_node_types_la_CFLAGS = $(src_lib_libsoletta_la_CFLAGS) -I$(top_builddir)/src/samples/flow/c-api
src_samples_flow_c_api_libcustom_node_types_la_LIBADD = src/lib/libsoletta.la
src_samples_flow_c_api_libcustom_node_types_la_SOURCES = src/samples/flow/c-api/custom-node-types.c

src/samples/flow/c-api/custom-node-types-gen.h src/samples/flow/c-api/custom-node-types-gen.c src/samples/flow/c-api/custom-node-types.json: src/samples/flow/c-api/custom-node-types-spec.json $(top_builddir)/data/scripts/sol-flow-node-type-gen.py
	$(AM_V_GEN)$(PYTHON) $(top_builddir)/data/scripts/sol-flow-node-type-gen.py --genspec-schema=$(SOLETTA_FLOW_NODE_TYPE_SCHEMA) $<  src/samples/flow/c-api/custom-node-types-gen.h src/samples/flow/c-api/custom-node-types-gen.c src/samples/flow/c-api/custom-node-types.json


BUILT_SOURCES += src/samples/flow/c-api/custom-node-types-gen.h src/samples/flow/c-api/custom-node-types-gen.c

noinst_PROGRAMS += src/samples/flow/c-api/lowlevel
src_samples_flow_c_api_lowlevel_CFLAGS = $(src_lib_libsoletta_la_CFLAGS) -I$(top_builddir)/src/samples/flow/c-api
src_samples_flow_c_api_lowlevel_LDADD = src/lib/libsoletta.la src/samples/flow/c-api/libcustom-node-types.la

if SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
noinst_PROGRAMS += src/samples/flow/c-api/highlevel
src_samples_flow_c_api_highlevel_CFLAGS = $(src_lib_libsoletta_la_CFLAGS) -I$(top_builddir)/src/samples/flow/c-api
src_samples_flow_c_api_highlevel_LDADD = src/lib/libsoletta.la src/samples/flow/c-api/libcustom-node-types.la
endif

EXTRA_DIST += src/samples/flow/c-api/custom-node-types-spec.json

if BUILD_COAP

noinst_PROGRAMS += \
src/samples/coap/simple-server
src_samples_coap_simple_server_CFLAGS = \
$(src_lib_libsoletta_la_CFLAGS)
src_samples_coap_simple_server_LDADD = \
src/lib/libsoletta.la \
src/shared/libshared.la

noinst_PROGRAMS += \
src/samples/coap/simple-client
src_samples_coap_simple_client_CFLAGS = \
$(src_lib_libsoletta_la_CFLAGS)
src_samples_coap_simple_client_LDADD = \
src/lib/libsoletta.la \
src/shared/libshared.la

noinst_PROGRAMS += \
src/samples/coap/oic-server
src_samples_coap_oic_server_CFLAGS = \
$(src_lib_coap_libcoap_la_CFLAGS) \
-I$(top_srcdir)/src/lib/coap
src_samples_coap_oic_server_LDADD = \
src/lib/libsoletta.la \
src/shared/libshared.la

noinst_PROGRAMS += \
src/samples/coap/oic-client
src_samples_coap_oic_client_CFLAGS = \
$(src_lib_coap_libcoap_la_CFLAGS) \
-I$(top_srcdir)/src/lib/coap
src_samples_coap_oic_client_LDADD = \
src/lib/libsoletta.la \
src/shared/libshared.la
endif

endif # BUILD_SAMPLES

if BUILD_PIN_MUX
src_lib_libsoletta_la_SOURCES += \
	src/lib/common/sol-pin-mux.c \
	src/lib/common/sol-pin-mux.h \
	src/lib/common/sol-pin-mux-impl.h

pkginclude_HEADERS += \
src/lib/common/sol-pin-mux-impl.h

if MAINTAINER_MODE
pinmuxdir = $(abs_top_builddir)/src/modules/pin-mux/.libs
else
pinmuxdir = $(SOL_CONFFILE_MODULES_DIR)/pin-mux
endif
pinmux_LTLIBRARIES =

src/lib/common/sol-pin-mux-builtins-gen.h: Makefile data/scripts/sol-builtins-gen.py
	@rm -f $@
	@mkdir -p src/lib/common
	$(AM_V_GEN)$(PYTHON) $(top_srcdir)/data/scripts/sol-builtins-gen.py \
	    --output=$@ \
	    --array="static const struct sol_pin_mux *SOL_PIN_MUX_BUILTINS_ALL[]" \
	    --count="SOL_PIN_MUX_BUILTIN_COUNT" \
	    --decl="const struct sol_pin_mux SOL_PIN_MUX_@NAME@" \
	    --item="&SOL_PIN_MUX_@NAME@" \
	    $(pin_mux_builtins)

BUILT_SOURCES += src/lib/common/sol-pin-mux-builtins-gen.h

include sol_pin_mux_modules.mk
endif #BUILD_PIN_MUX

if MODULE_OR_BUILTIN_FLOW_DUMMY

BUILT_SOURCES += src/modules/flow/dummy/dummy.c

src/modules/flow/dummy/dummy.c: src/modules/flow/dummy/dummy.json $(top_builddir)/data/scripts/sol-flow-node-type-stub-gen.py src/modules/flow/dummy/dummy-gen.c  src/modules/flow/dummy/dummy-gen.h
	$(AM_V_GEN)$(PYTHON) $(top_builddir)/data/scripts/sol-flow-node-type-stub-gen.py --prefix=sol-flow-node-type --force=yes src/modules/flow/dummy/dummy.c src/modules/flow/dummy/dummy.json

endif #MODULE_OR_BUILTIN_FLOW_DUMMY

src/modules/flow/%-gen.h src/modules/flow/%-gen.c: src/modules/flow/%.json $(top_builddir)/data/scripts/sol-flow-node-type-gen.py
	$(AM_V_GEN)$(PYTHON) $(top_builddir)/data/scripts/sol-flow-node-type-gen.py --prefix=sol-flow-node-type --genspec-schema=$(SOLETTA_FLOW_NODE_TYPE_SCHEMA) $<  src/modules/flow/$*-gen.h src/modules/flow/$*-gen.c $(@D).json

flowtypedescs =

src/lib/flow/sol-flow-builtins-gen.h: Makefile data/scripts/sol-builtins-gen.py
	@rm -f $@
	@mkdir -p src/lib/flow
	$(AM_V_GEN)$(PYTHON) $(top_srcdir)/data/scripts/sol-builtins-gen.py \
		--output=$@ \
		--array="static const void *SOL_FLOW_BUILTIN_NODE_TYPE_ALL[]" \
		--count="SOL_FLOW_BUILTIN_NODE_TYPE_COUNT" \
		--decl="const struct sol_flow_node_type *sol_flow_foreach_builtin_node_type_@name@(bool (*)(void *, const struct sol_flow_node_type *), const void *)" \
		--item="sol_flow_foreach_builtin_node_type_@name@" \
		$(flow_builtin_modules)
BUILT_SOURCES += src/lib/flow/sol-flow-builtins-gen.h

include sol_flow_modules.mk


if HAVE_SOL_FLOW_NODE_TYPES
if SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED

flowtypedesc_DATA += $(flowtypedescs)
endif
endif # HAVE_SOL_FLOW_NODE_TYPES

if BUILD_SAMPLES
if BUILD_SOL_FLOW_MODULE_RESOLVER_CONFFILE

if !HAVE_MAINLOOP_GLIB
src_lib_libsoletta_la_CFLAGS += @GLIB_CFLAGS@
src_lib_libsoletta_la_LIBADD += @GLIB_LIBS@
endif

endif # BUILD_SOL_FLOW_MODULE_RESOLVER_CONFFILE
endif # BUILD_SAMPLES

if HAVE_SOL_FBP_GENERATOR
bin_PROGRAMS += src/bin/sol-fbp-generator/sol-fbp-generator
src_bin_sol_fbp_generator_sol_fbp_generator_CFLAGS = $(src_lib_libsoletta_la_CFLAGS)
src_bin_sol_fbp_generator_sol_fbp_generator_LDADD = src/lib/libsoletta.la src/shared/libshared.la
src_bin_sol_fbp_generator_sol_fbp_generator_SOURCES = \
	src/bin/sol-fbp-generator/type-store.c \
	src/bin/sol-fbp-generator/type-store.h \
	src/bin/sol-fbp-generator/main.c

%-fbp.c: %.fbp src/lib/flow/builtins.json src/bin/sol-fbp-generator/sol-fbp-generator
	$(AM_V_GEN)$(sol_fbp_generator) -j src/lib/flow/builtins.json $< > $@


if BUILD_SAMPLES
noinst_PROGRAMS += src/samples/flow/basics/simple-fbp
src_samples_flow_basics_simple_fbp_CFLAGS = \
	$(src_lib_libsoletta_la_CFLAGS)
src_samples_flow_basics_simple_fbp_LDADD = \
	src/lib/libsoletta.la
nodist_src_samples_flow_basics_simple_fbp_SOURCES = \
	src/samples/flow/basics/simple-fbp.c
dist_noinst_DATA = \
	src/samples/flow/basics/simple.fbp

BUILT_SOURCES += src/samples/flow/basics/simple-fbp.c
endif # BUILD_SAMPLES
endif # HAVE_SOL_FBP_GENERATOR

if HAVE_SOL_FBP_RUNNER
bin_PROGRAMS += src/bin/sol-fbp-runner/sol-fbp-runner
src_bin_sol_fbp_runner_sol_fbp_runner_CFLAGS = \
	$(src_lib_libsoletta_la_CFLAGS)
src_bin_sol_fbp_runner_sol_fbp_runner_LDADD = \
	src/lib/libsoletta.la \
	src/shared/libshared.la
src_bin_sol_fbp_runner_sol_fbp_runner_SOURCES = \
	src/bin/sol-fbp-runner/main.c \
	src/bin/sol-fbp-runner/runner.c \
	src/bin/sol-fbp-runner/runner.h
if SOL_FLOW_INSPECTOR_ENABLED
src_bin_sol_fbp_runner_sol_fbp_runner_SOURCES += \
	src/bin/sol-fbp-runner/inspector.c
endif
endif # HAVE_SOL_FBP_RUNNER

check_LTLIBRARIES = \
	src/test/libtest.la

src_test_libtest_la_SOURCES = \
	src/test/test.c \
	src/test/test.h
src_test_libtest_la_LIBADD = \
	src/lib/libsoletta.la \
	src/shared/libshared.la

if HAVE_SOL_FBP_TO_DOT
bin_PROGRAMS += src/bin/sol-fbp-to-dot/sol-fbp-to-dot
src_bin_sol_fbp_to_dot_sol_fbp_to_dot_CFLAGS = \
	$(src_lib_libsoletta_la_CFLAGS) -O0
src_bin_sol_fbp_to_dot_sol_fbp_to_dot_LDADD = \
	src/lib/libsoletta.la \
	src/shared/libshared.la
src_bin_sol_fbp_to_dot_sol_fbp_to_dot_SOURCES = \
	src/bin/sol-fbp-to-dot/main.c
endif # HAVE_SOL_FBP_TO_DOT

check_PROGRAMS = \
	src/test/test-arena \
	src/test/test-buffer \
	src/test/test-coap \
	src/test/test-fbp \
	src/test/test-fbp-scanner \
	src/test/test-flow \
	src/test/test-mainloop \
	src/test/test-monitors \
	src/test/test-str-slice \
	src/test/test-str-table \
	src/test/test-util \
	src/test/test-vector

if BUILD_COAP
check_PROGRAMS += src/test/test-coap
endif

if HAVE_PLATFORM_LINUX
check_PROGRAMS += src/test/test-mainloop-linux
src_test_test_mainloop_linux_CFLAGS = $(TEST_CFLAGS)
src_test_test_mainloop_linux_LDADD = $(TEST_LDADD)

if HAVE_MAINLOOP_POSIX
if PTHREAD_LIBS
check_PROGRAMS += \
	src/test/test-mainloop-threads \
	src/test/test-mainloop-threads-sol-run
src_test_test_mainloop_threads_CFLAGS = $(TEST_CFLAGS)
src_test_test_mainloop_threads_LDADD = $(TEST_LDADD) -lpthread
src_test_test_mainloop_threads_sol_run_CFLAGS = $(TEST_CFLAGS)
src_test_test_mainloop_threads_sol_run_LDADD = $(TEST_LDADD) -lpthread
endif # PTHREAD_LIBS
endif # HAVE_MAINLOOP_POSIX
endif # HAVE_PLATFORM_LINUX

if SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
check_PROGRAMS += \
	src/test/test-flow-builder \
	src/test/test-flow-parser
endif

TESTS = $(check_PROGRAMS)

TEST_CFLAGS = $(AM_CFLAGS)
TEST_LDADD = \
	src/lib/libsoletta.la \
	src/shared/libshared.la \
	src/test/libtest.la

EXTRA_DIST += src/test/test.h

src_test_test_arena_CFLAGS = $(TEST_CFLAGS)
src_test_test_arena_LDADD = $(TEST_LDADD)

src_test_test_buffer_CFLAGS = $(TEST_CFLAGS)
src_test_test_buffer_LDADD = $(TEST_LDADD)

src_test_test_fbp_CFLAGS = $(TEST_CFLAGS)
src_test_test_fbp_LDADD = $(TEST_LDADD)

src_test_test_fbp_scanner_CFLAGS = $(TEST_CFLAGS)
src_test_test_fbp_scanner_LDADD = $(TEST_LDADD)

src_test_test_flow_CFLAGS = $(TEST_CFLAGS)
src_test_test_flow_LDADD = $(TEST_LDADD)

if SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
src_test_test_flow_builder_CFLAGS = $(TEST_CFLAGS)
src_test_test_flow_builder_LDADD = $(TEST_LDADD)
endif

if BUILD_COAP
src_test_test_coap_SOURCES = \
	src/lib/coap/coap.c \
	src/lib/coap/coap.h \
	src/test/test-coap.c

src_test_test_coap_CFLAGS = $(TEST_CFLAGS)
src_test_test_coap_LDADD = \
	$(TEST_LDADD)
endif

src_test_test_mainloop_CFLAGS = $(TEST_CFLAGS)
src_test_test_mainloop_LDADD = $(TEST_LDADD)

src_test_test_monitors_CFLAGS = $(TEST_CFLAGS)
src_test_test_monitors_LDADD = $(TEST_LDADD)

src_test_test_flow_parser_CFLAGS = $(TEST_CFLAGS)
src_test_test_flow_parser_LDADD = $(TEST_LDADD)

src_test_test_str_slice_CFLAGS = $(TEST_CFLAGS)
src_test_test_str_slice_LDADD = $(TEST_LDADD)

src_test_test_str_table_CFLAGS = $(TEST_CFLAGS)
src_test_test_str_table_LDADD = $(TEST_LDADD)

src_test_test_util_CFLAGS = $(TEST_CFLAGS)
src_test_test_util_LDADD = $(TEST_LDADD)

src_test_test_vector_CFLAGS = $(TEST_CFLAGS)
src_test_test_vector_LDADD = $(TEST_LDADD)

if HAVE_SOL_FBP_RUNNER
check-fbp: src/bin/sol-fbp-runner/sol-fbp-runner $(flowmodules_LTLIBRARIES)
	@$(top_srcdir)/src/test-fbp/run

check-fbp-valgrind: src/bin/sol-fbp-runner/sol-fbp-runner $(flowmodules_LTLIBRARIES)
	@VALGRIND=yes $(top_srcdir)/src/test-fbp/run

EXTRA_DIST += \
	src/test-fbp/run \
	$(wildcard src/test-fbp/*.fbp)

EXTRA_DIST += \
	src/samples/flow/systemd/unix-socket-writer.service.in \
	src/samples/flow/systemd/unix-socket-reader-fail.service.in \
	src/samples/flow/systemd/unix-socket-reader.service.in
endif # HAVE_SOL_FBP_RUNNER

CHEAT_SHEET_RESOURCES = \
	$(wildcard doc/node-types-html/images/*.jpg) \
	$(wildcard doc/node-types-html/js/*.js) \
	doc/node-types-html/index.html.in \
	doc/node-types-html/styles.css

cheat-sheet: $(top_srcdir)/data/scripts/sol-flow-node-type-gen-cheat-sheet.py $(CHEAT_SHEET_RESOURCES) $(flowtypedesc_DATA)
	$(top_srcdir)/data/scripts/sol-flow-node-type-gen-html.py $(top_srcdir)/doc/node-types-html/index.html.in doc/node-types-html/index.html $(flowtypedesc_DATA)

DISTCLEANFILES += $(DOLT_CLEANFILES)

@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES = $(top_srcdir)/src/test/test.supp
EXTRA_DIST += src/test/test.supp

# ------------------------------------------------------------------------------
# coverage
# ------------------------------------------------------------------------------

# .PHONY so it always rebuilds it
.PHONY: coverage lcov-run lcov-report coverage-sync check-fbp check-fbp-valgrind

# run lcov from scratch, always
coverage: all
	$(MAKE) lcov-run
	$(MAKE) lcov-report

coverage_dir = coverage
coverage_opts = --base-directory $(srcdir) --directory $(builddir) \
		--rc 'geninfo_adjust_src_path=$(abspath $(srcdir))=>$(abspath $(builddir))'

if ENABLE_COVERAGE
# reset run coverage tests
lcov-run:
	@rm -rf $(coverage_dir)
	lcov $(coverage_opts) --zerocounters
	-$(MAKE) check
	-$(MAKE) check-fbp

# generate report based on current coverage data
lcov-report:
	$(MKDIR_P) $(coverage_dir)
	lcov $(coverage_opts) --capture --no-external --ignore-errors graph \
		| sed 's|$(abspath $(builddir))|$(abspath $(srcdir))|' > $(coverage_dir)/.lcov.info
	lcov --remove $(coverage_dir)/.lcov.info --output-file $(coverage_dir)/.lcov-clean.info 'test-*'
	genhtml -t "soletta test coverage" -o $(coverage_dir) $(coverage_dir)/.lcov-clean.info
	@echo "Coverage report generated in $(abs_builddir)/$(coverage_dir)/index.html"

else

lcov-run lcov-report:
	echo "Need to reconfigure with --enable-coverage"

endif
