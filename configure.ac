AC_PREREQ([2.64])
AC_INIT([soletta], [1])

orig_CFLAGS="$CFLAGS"
CFLAGS=`echo "$CFLAGS" | sed -e "s/-W\(fatal-errors\|error\|error=@<:@^ \t@:>@\+\)//g"`
if test "$orig_CFLAGS" != "$CFLAGS"; then
   AC_MSG_WARN([Original \$CFLAGS="$orig_CFLAGS" contains -Werror or -Wfatal-errors which may break autoconf's checks. Removed these flags during autoconf tests, using CFLAGS="$CFLAGS"])
fi

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([sol_config.h])
AC_PROG_CC_C99
AC_PROG_LN_S
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

AM_SILENT_RULES([yes])
AM_INIT_AUTOMAKE([foreign subdir-objects silent-rules])
AM_MAINTAINER_MODE
LT_INIT([pic-only])
DOLT

AM_CONDITIONAL([ENABLE_STATIC], [test "${enable_static}" = "yes"])

AC_PROG_MKDIR_P
PKG_PROG_PKG_CONFIG

# MUSL does not have strndupa()
AC_CHECK_DECLS_ONCE([strndupa],,,[
#include <string.h>
])

# accept4() is Linux-only and GLIBC requires __GNU_SOURCE to enable it
AC_CHECK_FUNCS([accept4])

CC_CHECK_FLAGS_APPEND(with_cflags, [CFLAGS], [\
		       -pipe \
		       -Wall \
		       -W \
		       -Wextra \
		       -Wundef \
		       -Wformat=2 \
		       -Wlogical-op \
		       -Wsign-compare \
		       -Wformat-security \
		       -Wmissing-include-dirs \
		       -Wformat-nonliteral \
		       -Wold-style-definition \
		       -Wpointer-arith \
		       -Winit-self \
		       -Wdeclaration-after-statement \
		       -Wfloat-equal \
		       -Wmissing-prototypes \
		       -Wstrict-prototypes \
		       -Wredundant-decls \
		       -Wmissing-declarations \
		       -Wmissing-noreturn \
		       -Wno-unused-parameter \
		       -Wshadow \
		       -Wendif-labels \
		       -Wstrict-aliasing=3 \
		       -Wwrite-strings \
		       -Wno-long-long \
		       -Wno-missing-field-initializers \
		       -Wno-override-init \
		       -Wnested-externs \
		       -Wchar-subscripts \
		       -Wtype-limits \
		       -Wuninitialized \
		       -fno-common \
		       -fdiagnostics-show-option \
		       -fvisibility=hidden \
		       -ffunction-sections \
		       -fdata-sections])

AS_CASE([$CFLAGS], [*-O[[s12345\ ]]*],
        [CC_CHECK_FLAGS_APPEND([with_cflags], [CFLAGS], [-flto -ffat-lto-objects])],
        [AC_MSG_RESULT([skipping link-time-optimization (LTO) since no optimizations were used])])

CC_CHECK_FLAGS_APPEND([with_ldflags], [LDFLAGS], [ \
		       -Wl,--as-needed \
		       -Wl,--gc-sections])

AC_ARG_ENABLE([coverage],
	AS_HELP_STRING([--enable-coverage], [enable test coverage @<:@default=disabled@:>@]),
	[], [enable_coverage=no])
AS_IF([test "x$enable_coverage" = "xyes"], [
	AC_CHECK_PROG(have_coverage, [lcov], [yes], [no])
	AS_IF([test "x$have_coverage" = xno],[
		AC_MSG_ERROR([*** lcov support requested but the program was not found])
	], [
		lcov_version_major="`lcov --version | cut -d ' ' -f 4 | cut -d '.' -f 1`"
		lcov_version_minor="`lcov --version | cut -d ' ' -f 4 | cut -d '.' -f 2`"
		AS_IF([test "$lcov_version_major" -lt 1 -o "$lcov_version_minor" -lt 10], [
			AC_MSG_ERROR([*** lcov version is too old. 1.10 required])
		], [
			have_coverage=yes
                        CC_CHECK_FLAGS_APPEND([with_coverage_cflags], [CFLAGS], [\
                        -fprofile-arcs \
                        -ftest-coverage])
		])
        ])
])
AM_CONDITIONAL([ENABLE_COVERAGE], [test "x$enable_coverage" = "xyes"])

AC_ARG_ENABLE([dynamic-modules],
        AS_HELP_STRING([--disable-dynamic-modules], [Disable dlopen() and dynamic modules (.so), this is required to enable full-static builds]),
        [], [enable_dynamic_modules=yes])
AM_CONDITIONAL([ENABLE_DYNAMIC_MODULES], [test "${enable_dynamic_modules}" != "no"])
if test "${enable_dynamic_modules}" != "no"; then
    AC_DEFINE([ENABLE_DYNAMIC_MODULES], [1], [dynamic modules are enabled.])
    dl_libs="-ldl"
fi
AC_SUBST(dl_libs)

AC_SUBST([SOL_COMMON_CFLAGS], "$with_cflags $with_coverage_cflags")
AC_SUBST([SOL_COMMON_LDFLAGS], $with_ldflags)
AC_SUBST([SOL_CONFFILE_MODULES_DIR], ${libdir}/soletta/modules/)

AC_CHECK_FUNCS([isatty])

AC_ARG_WITH([pthread],
	AS_HELP_STRING([--without-pthread], [Disable pthread]))

AS_IF([test "x$with_pthread" = "xno"],
	[have_pthread=no], [AC_CHECK_HEADERS([pthread.h], [have_pthread=yes], [have_pthread=no])])

PTHREAD_LIBS=""
if test "${have_pthread}" = "yes"; then
   save_LIBS="$LIBS"
   AC_SEARCH_LIBS([pthread_create], [pthread])
   LIBS="${save_LIBS}"
   if test "${ac_cv_search_pthread_create}" = "no"; then
      AC_MSG_ERROR([Could not find pthread_create()])
   elif test "${ac_cv_search_pthread_create}" != "none required"; then
      PTHREAD_LIBS="${ac_cv_search_pthread_create}"
   fi
fi
AC_SUBST(PTHREAD_LIBS)

AM_CONDITIONAL([PTHREAD_LIBS], [test "${have_pthread}" = "yes"])

AC_ARG_ENABLE([sanitizer],
	AS_HELP_STRING([--disable-sanitizer],
		       [Enable sanitizer, one of: undefined,address]))
AC_MSG_CHECKING([sanitizer to be used])
AC_MSG_RESULT([${enable_sanitizer}])
case "${enable_sanitizer}" in
    yes|"")
	enable_sanitizer=undefined
	;;
    undefined|address)
	CC_CHECK_FLAGS_APPEND(with_cflags, [CFLAGS], [-fsanitize=${enable_sanitizer}])
	CC_CHECK_FLAGS_APPEND(with_ldflags, [LDFLAGS], [-fsanitize=${enable_sanitizer}])
	;;
    no)
	enable_sanitizer=none
	;;
    *)
	AC_MSG_ERROR([unknown sanitizer: ${enable_sanitizer}; valid: undefined,address])
	;;
esac

SOL_WITH_BIN([sol-fbp-generator], [src/bin/sol-fbp-generator/sol-fbp-generator])
SOL_WITH_BIN([sol-fbp-runner], [src/bin/sol-fbp-runner/sol-fbp-runner])
SOL_WITH_BIN([sol-fbp-to-dot], [src/bin/sol-fbp-to-dot/sol-fbp-to-dot])
SOL_WITH_BIN([sol-flow-node-types], [src/bin/sol-flow-node-types/sol-flow-node-types])

AC_ARG_WITH([sol-fbp-generator],
	AS_HELP_STRING([--without-sol-fbp-generator], [Do not build sol-fbp-generator]))

AC_ARG_WITH([sol-fbp-runner],
    AS_HELP_STRING([--without-sol-fbp-runner], [Do not build sol-fbp-runner]))

AC_ARG_WITH([sol-fbp-to-dot],
    AS_HELP_STRING([--without-sol-fbp-to-dot], [Do not build sol-fbp-to-dot]))

AC_ARG_WITH([glib],
    AS_HELP_STRING([--without-glib], [Disable usage of Glib]),
    [], [with_glib=auto])
have_glib=no
if test "${with_glib}" != "no"; then
    PKG_CHECK_MODULES([GLIB], [glib-2.0], [have_glib=yes], [have_glib=no])
fi
if test "${with_glib}" = "yes" -a "${have_glib}" = "no"; then
   AC_MSG_ERROR([Glib is to be used but was not found.])
fi
AM_CONDITIONAL([HAVE_GLIB], [test "${have_glib}" = "yes"])

AC_ARG_WITH([platform],
        AS_HELP_STRING([--with-platform=IMPLEMENTATION],
                       [enable integration with one of: systemd, riot, linux-micro or linux-dummy]),
        [],
        [with_platform="linux-dummy"])
AC_MSG_CHECKING([platform to be used])
AC_MSG_RESULT([${with_platform}])
platform_linux="no"
case "${with_platform}" in
    systemd)
        platform_linux="yes"
        have_systemd=no
        CC_PKG_CHECK_MODULES_WITH_FUNC(SYSTEMD, [ libsystemd ], [systemd], [sd_event_prepare],
                       [have_systemd=yes], [have_systemd=no])
        AS_IF([test "$have_systemd" = "yes"],
            [AC_DEFINE(HAVE_SYSTEMD, 1, [Define if libsystemd is available])],
            [AC_MSG_ERROR([*** systemd requested but not found with required symbols.])])
        ;;
    linux-micro)
        platform_linux="yes"
        ;;
    linux-dummy)
        platform_linux="yes"
        ;;
    riot)
        default_mainloop="riot"
        possible_mainloops="riot"
        ;;
    *)
        AC_MSG_ERROR([unknown platform=${with_platform}])
        ;;
esac

if test "${platform_linux}" = "yes"; then
    if test "${have_glib}" = "yes"; then
        default_mainloop="glib"
        possible_mainloops="glib posix"
    else
        default_mainloop="posix"
        possible_mainloops="posix"
    fi
    AC_CHECK_HEADERS([linux/rtnetlink.h linux/netlink.h linux/watchdog.h], ,[linux_headers=no; break;])
    AS_IF([test "$linux_headers" = "no"],
	  [AC_MSG_ERROR([Unable to find linux headers. Set your CFLAGS properly before run configure, e.g. CFLAGS=\"-I/path/to/linux/headers\"])])
fi

AM_CONDITIONAL(HAVE_PLATFORM_SYSTEMD, [test "$with_platform" = "systemd"])
AM_CONDITIONAL(HAVE_PLATFORM_LINUX_MICRO, [test "$with_platform" = "linux-micro"])
AM_CONDITIONAL(HAVE_PLATFORM_LINUX_DUMMY, [test "$with_platform" = "linux-dummy"])
AM_CONDITIONAL(HAVE_PLATFORM_RIOT, [test "$with_platform" = "riot"])

AM_CONDITIONAL(HAVE_PLATFORM_LINUX, [test "$platform_linux" = "yes"])

if test "${platform_linux}" = "yes"; then
    SOL_PLATFORM_LINUX="#define SOL_PLATFORM_LINUX (1)"
else
    SOL_PLATFORM_LINUX="/* #undef SOL_PLATFORM_LINUX */"
fi
AC_SUBST(SOL_PLATFORM_LINUX)
if test "${platform_riot}" = "yes"; then
    SOL_PLATFORM_RIOT="#define SOL_PLATFORM_RIOT (1)"
else
    SOL_PLATFORM_RIOT="/* #undef SOL_PLATFORM_RIOT */"
fi
AC_SUBST(SOL_PLATFORM_RIOT)

linux_rc_micro_rc_default="${enable_dynamic_modules}"
AC_DEFUN([CHECK_LINUX_MICRO_RC_D], [dnl
   if test "${with_linux_micro_rc_d}" = "builtin"; then
      AC_MSG_ERROR([--with-linux-micro-rc-d=builtin is unusable. Either use --enable-dynamic-modules and --with-linux-micro-rc-d=yes, or --with-linux-micro-rc-d.])
   fi
])

SOL_LINUX_MICRO_MODULE([hostname], [builtin], [yes])
SOL_LINUX_MICRO_MODULE([locale], [yes], [yes])
SOL_LINUX_MICRO_MODULE([dbus], [yes], [yes])
SOL_LINUX_MICRO_MODULE([bluetooth], [yes], [yes])
SOL_LINUX_MICRO_MODULE([rc-d], [${linux_rc_micro_rc_default}], [no], [CHECK_LINUX_MICRO_RC_D])
SOL_LINUX_MICRO_MODULE([sysctl], [builtin], [yes])
SOL_LINUX_MICRO_MODULE([fstab], [builtin], [yes])
SOL_LINUX_MICRO_MODULE([network-up], [builtin], [yes])
SOL_LINUX_MICRO_MODULE([console], [yes], [yes])
SOL_LINUX_MICRO_MODULE([watchdog], [yes], [yes])

AC_SUBST(linux_micro_builtin_modules)
AC_SUBST(linux_micro_initial_services)

AC_ARG_WITH([mainloop],
        AS_HELP_STRING([--with-mainloop=IMPLEMENTATION],
                       [enable mainloop integration with one of platform-dependent implementation: Linux=(glib, posix), RIOT=riot.]),
        [],
        [with_mainloop="${default_mainloop}"])
AC_MSG_CHECKING([mainloop to be used])
if ! echo "${possible_mainloops}" | grep "${with_mainloop}" >/dev/null 2>/dev/null; then
   AC_MSG_ERROR([unsupported mainloop=${with_mainloop} for platform ${with_platform}, supported: ${possible_mainloops}])
fi
AC_MSG_RESULT([${with_mainloop}])

case "${with_mainloop}" in
    glib)
        ;;
    posix)
        AC_CHECK_FUNCS([ppoll])
        ;;
    riot)
        ;;
    *)
        AC_MSG_ERROR([unknown platform=${with_platform}])
        ;;
esac

AM_CONDITIONAL([HAVE_MAINLOOP_GLIB], [test "${with_mainloop}" = "glib"])
AM_CONDITIONAL([HAVE_MAINLOOP_POSIX], [test "${with_mainloop}" = "posix"])
AM_CONDITIONAL([HAVE_MAINLOOP_RIOT], [test "${with_mainloop}" = "riot"])

flow_module_resolvers=""

AC_DEFUN([SOL_FLOW_MODULE_RESOLVER_CONFFILE_CHECKS],
[
AS_IF([test "$have_glib" = "no" -a "$enable_flow_module_resolver_conffile" = "yes"],
	[AC_MSG_ERROR([--enable-flow-module-resolver-conffile depends on --with-glib, not to be used])])
AS_IF([test "$enable_flow_node_type_description" = "no" -a "$enable_flow_module_resolver_conffile" = "yes"],
	[AC_MSG_ERROR([--enable-flow-module-resolver-conffile depends on --enable-flow-node-type-description, not to be used])])
AS_IF([test "$enable_dynamic_modules" = "no" -a "$enable_flow_module_resolver_conffile" = "yes"],
	[AC_MSG_ERROR([--enable-flow-module-resolver-conffile depends on --enable-dynamic-modules, not to be used])])
])

SOL_FLOW_MODULE_RESOLVER([conffile], [yes],
                        [SOL_FLOW_MODULE_RESOLVER_CONFFILE_CHECKS],
                        [that uses configuration files (ini-like)])

SOL_FLOW_MODULE_RESOLVER_DEFAULT([conffile])

AC_ARG_WITH([sol-flow-node-types],
    AS_HELP_STRING([--without-sol-flow-node-types], [Do not build sol-flow-node-types]))


AM_CONDITIONAL([BUILD_COAP], [test "$with_coap" != "no"])

AC_DEFUN([CHECK_FLOW_OIC], [dnl
    AS_IF([test "${with_coap}" = "no"],
          [AC_MSG_ERROR([--with-flow-module-oic depends on --with-coap, not to be used.])])
])

AC_DEFUN([CHECK_FLOW_UDEV], [dnl
    PKG_CHECK_MODULES([UDEV], [libudev])
])

AC_DEFUN([CHECK_FLOW_GTK], [dnl
   PKG_CHECK_MODULES([GTK], [gtk+-3.0])
])

SOL_FLOW_MODULE([accelerometer], [${platform_linux}])
SOL_FLOW_MODULE([calamari], [yes])
SOL_FLOW_MODULE([dummy], [no])
SOL_FLOW_MODULE([evdev], [${platform_linux}])
SOL_FLOW_MODULE([file], [${have_pthread}])
SOL_FLOW_MODULE([fs], [yes])
SOL_FLOW_MODULE([grove], [yes])
SOL_FLOW_MODULE([gtk], [${platform_linux}], [CHECK_FLOW_GTK])
SOL_FLOW_MODULE([gyroscope], [${platform_linux}])
SOL_FLOW_MODULE([keyboard], [${platform_linux}])
SOL_FLOW_MODULE([network], [${platform_linux}])
SOL_FLOW_MODULE([oic], [yes], [CHECK_FLOW_OIC])
SOL_FLOW_MODULE([piezo-speaker], [yes])
SOL_FLOW_MODULE([servo-motor], [yes])
SOL_FLOW_MODULE([test], [yes])
SOL_FLOW_MODULE([udev], [${platform_linux}], [CHECK_FLOW_UDEV])
SOL_FLOW_MODULE([unix-socket], [${platform_linux}])
SOL_FLOW_MODULE([process], [${platform_linux}])

SOL_FLOW_MODULE([constant], [builtin])
SOL_FLOW_MODULE([console], [builtin])
SOL_FLOW_MODULE([timer], [builtin])
SOL_FLOW_MODULE([aio], [builtin])
SOL_FLOW_MODULE([app], [builtin])
SOL_FLOW_MODULE([boolean], [builtin])
SOL_FLOW_MODULE([byte], [builtin])
SOL_FLOW_MODULE([color], [builtin])
SOL_FLOW_MODULE([converter], [builtin])
SOL_FLOW_MODULE([float], [builtin])
SOL_FLOW_MODULE([filter-repeated], [builtin])
SOL_FLOW_MODULE([gpio], [builtin])
SOL_FLOW_MODULE([hub], [builtin])
SOL_FLOW_MODULE([platform], [builtin])
SOL_FLOW_MODULE([pwm], [builtin])
SOL_FLOW_MODULE([int], [builtin])
SOL_FLOW_MODULE([random], [builtin])
SOL_FLOW_MODULE([string], [builtin])
SOL_FLOW_MODULE([trigonometry], [builtin])
SOL_FLOW_MODULE([wallclock], [builtin])

AC_SUBST(flow_builtin_modules)


AC_ARG_WITH([samples],
	AS_HELP_STRING([--without-samples], [Do not build samples]))

AS_IF([test "x$with_samples" != "xno"],
	[build_samples=yes],
	[build_samples=no])

AM_CONDITIONAL([BUILD_SAMPLES], [test "x$build_samples" = "xyes"])

AC_ARG_WITH([coap],
	AS_HELP_STRING([--without-coap], [Do not build COAP support]))

AS_IF([test "x$with_coap" != "xno"],
	[build_coap=yes],
	[build_coap=no])

AM_CONDITIONAL([BUILD_COAP], [test "x$build_coap" = "xyes"])

AC_ARG_ENABLE([log],
        AS_HELP_STRING([--disable-log], [Disable sol-log functionality.]),
        [], [enable_log=yes])
if test "${enable_log}" != "no"; then
   SOL_LOG_ENABLED="#define SOL_LOG_ENABLED (1)"
else
   SOL_LOG_ENABLED="/* #undef SOL_LOG_ENABLED */"
fi
AC_SUBST(SOL_LOG_ENABLED)
AM_CONDITIONAL([SOL_LOG_ENABLED], [test "${enable_log}" != "no"])

AC_ARG_WITH([platform-name],
	AS_HELP_STRING([--with-platform-name=NAME],
                       [Use NAME as platform name. Disable auto detect mechanisms]),
        [],
        [with_platform_name="no"])

if test "x$with_platform_name" != "xno" -a "x$with_platform_name" != "xyes"; then
    AC_DEFINE([PLATFORM_NAME], [${with_platform_name}], [String to be used as platform name])
else
    with_platform_name="auto"
fi

AC_ARG_WITH([pin-mux],
	AS_HELP_STRING([--with-pin-mux], [Build pin multiplexer support]),
        [],
        [with_pin_mux="no"])

AM_CONDITIONAL([HAVE_PIN_MUX], [test "x$with_pin_mux" != "xno"])
if test "x$with_pin_mux" != "xno"; then
    AC_DEFINE([HAVE_PIN_MUX], [1], [Pin multiplexer is enabled.])
fi

SOL_PIN_MUX_MODULE([intel-edison-rev-c], [no], [Add Intel Edison pin mux support])
SOL_PIN_MUX_MODULE([intel-galileo-rev-d], [no], [Add Intel Galileo 1 pin mux support])
SOL_PIN_MUX_MODULE([intel-galileo-rev-g], [no], [Add Intel Galileo 2 pin mux support])

AC_SUBST(pin_mux_builtins)

AC_ARG_WITH([maximum-internal-log-level],
        AS_HELP_STRING([--with-maximum-internal-log-level=NUMBER],
                       [Specifies the maximum log level to be used in soletta library. This will remove all log messages greater than the given number from the final binary if it is compiled with optimizations. By default all log levels are allowed]))
if test "x${with_maximum_internal_log_level}" != "x" -a "${enable_log}" != "no"; then
   AC_DEFINE_UNQUOTED([SOL_LOG_LEVEL_MAXIMUM], [${with_maximum_internal_log_level}], [Maximum log level to compile in Soletta library])
fi

AC_ARG_ENABLE([flow-node-type-description],
        AS_HELP_STRING([--disable-flow-node-type-description],
                       [Disable flow's node type description, this saves memory but disables introspection and automatically parse of options -- only to be used in very constrained systems.]),
        [], [enable_flow_node_type_description=yes])
if test "x${enable_flow_node_type_description}" = "xno"; then
   SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED="/* #undef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED */"
else
   SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED="#define SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED (1)"
fi
AC_SUBST(SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED)
AM_CONDITIONAL([SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED], [test "x${enable_flow_node_type_description}" != "xno"])

AC_ARG_ENABLE([flow-inspector],
        AS_HELP_STRING([--disable-flow-inspector],
                       [Disable flow's inspector support. Inspector has minimal impact when not used -- only to be used in very constrained systems.]),
        [], [enable_flow_inspector=yes])
if test "x${enable_flow_inspector}" = "xno"; then
   SOL_FLOW_INSPECTOR_ENABLED="/* #undef SOL_FLOW_INSPECTOR_ENABLED */"
else
   SOL_FLOW_INSPECTOR_ENABLED="#define SOL_FLOW_INSPECTOR_ENABLED (1)"
fi
AC_SUBST(SOL_FLOW_INSPECTOR_ENABLED)
AM_CONDITIONAL([SOL_FLOW_INSPECTOR_ENABLED], [test "x${enable_flow_inspector}" != "xno"])

AC_CHECK_SIZEOF([time_t])
if test "x$ac_cv_sizeof_time_t" != "x4" -a "x$ac_cv_sizeof_time_t" != "x8"; then
   AC_MSG_ERROR([time_t size ${ac_cv_sizeof_time_t} is neither 32 or!64-bit])
fi

# TODO: Python is used by data/scripts/sol-flow-node-type-gen.py to
# generate source/headers from JSON at compile time.  Eventually it
# will be rewritten in C and not require 3rd party dependencies.
AM_PATH_PYTHON([3.2], [], [
    AC_MSG_ERROR([Python3.2 is required by data/scripts/sol-flow-node-type-gen.py])])
if [ ! $PYTHON -c 'import jsonschema' > /dev/null 2>&1 ]; then
    AC_MSG_ERROR([Python module 'jsonschema' not found.])
fi

CFLAGS="$orig_CFLAGS"

AX_VALGRIND_CHECK

AC_CONFIG_LINKS([
doc/node-types-html/styles.css:doc/node-types-html/styles.css
])

AC_CONFIG_COMMANDS([cheat-sheet],
[( $MKDIR_P doc/node-types-html && cd doc/node-types-html/ &&
   [[ ! -e images ]] &&  $LN_S $ac_abs_srcdir/doc/node-types-html/images
   [[ ! -e js ]] &&  $LN_S $ac_abs_srcdir/doc/node-types-html/js
)]
)

AC_CONFIG_FILES([
Makefile
pc/soletta.pc
src/lib/common/sol-common-buildopts.h
src/lib/flow/sol-flow-buildopts.h
src/samples/flow/systemd/unix-socket-reader.service
src/samples/flow/systemd/unix-socket-reader-fail.service
src/samples/flow/systemd/unix-socket-writer.service
])

AC_OUTPUT
AC_MSG_RESULT([

$PACKAGE $VERSION

general:
        prefix:                         ${prefix}
        sysconfdir:                     ${sysconfdir}
        libdir:                         ${libdir}
        includedir:                     ${includedir}
        bindir:                         ${bindir}

        compiler:                       ${CC}
        cflags:                         ${with_cflags} ${CFLAGS}
        ldflags:                        ${with_ldflags} ${LDFLAGS}

        maximum internal log level:     ${with_maximum_internal_log_level}
        log enabled:                    ${enable_log}

        sanitizer:                      ${enable_sanitizer}
        coverage:                       ${enable_coverage}

        build samples:                  ${build_samples}
        build COAP:                     ${build_coap}

        threads:                        ${have_pthread}
        dynamic modules:                ${enable_dynamic_modules}

sol-common:
        platform:                       ${with_platform}
        platform name:                  ${with_platform_name}
        mainloop:                       ${with_mainloop}
        pin mux:                        ${with_pin_mux}dnl
])

if test -n "${pin_mux_modules}"; then
   AC_MSG_RESULT([dnl
        pin mux modules:               ${pin_mux_modules}dnl
])
fi

if test -n "${pin_mux_builtins}"; then
   AC_MSG_RESULT([dnl
        pin mux builtins:              ${pin_mux_builtins}dnl
])
fi

if test "${with_platform}" = "linux-micro"; then
   AC_MSG_RESULT([dnl
        linux-micro modules:           ${linux_micro_modules}
        linux-micro initial services:  ${linux_micro_initial_services}dnl
])
fi

AC_MSG_RESULT([

sol-flow:
        sol-fbp-generator:               ${with_sol_fbp_generator}
        sol-fbp-runner:                  ${with_sol_fbp_runner}
        sol-flow-node-types:             ${with_sol_flow_node_types}
        node type descriptions:         ${enable_flow_node_type_description}
        inspector:                      ${enable_flow_inspector}
        module resolvers:               ${flow_module_resolvers}
        default module resolver:        ${with_flow_module_default_resolver}
        modules:                       ${flow_modules}
])
