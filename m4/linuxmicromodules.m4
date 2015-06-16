dnl SOL_LINUX_MICRO_MODULE([NAME], [DEFAULT], [INITIAL_SERVICE], [CHECKS_IF_ENABLED])
AC_DEFUN([SOL_LINUX_MICRO_MODULE],
[dnl
m4_pushdef([DOWNNAME], m4_translit([$1], [-A-Z], [_a-z]))dnl
m4_pushdef([UPNAME], m4_translit([$1], [-a-z], [_A-Z]))dnl
        AC_ARG_WITH([linux-micro-]$1,
                AS_HELP_STRING([--with-linux-micro-]$1[=OPTION],
                        [choose how to build linux-micro module for `$1', options: `yes', `no' or `builtin']),
                        [], [with_linux_micro_[]m4_defn([DOWNNAME])=$2])

        strprefix=""
        AC_MSG_CHECKING([whenever `$1' linux-micro module is to be built])
        AC_MSG_RESULT([${with_linux_micro_[]m4_defn([DOWNNAME])}])
        initial_service_suffix=""
        case "${with_linux_micro_[]m4_defn([DOWNNAME])}" in
             yes)
                strprefix="+"
                initial_service_suffix="?"
                if test "${enable_dynamic_modules}" = "no"; then
                    AC_MSG_WARN([--with-linux-micro-]$1[=yes but --disable-dynamic-modules was used. Either --enable-dynamic-modules or use --with-linux-micro-]$1[=builtin. Assuming builtin.])
                    with_linux_micro_[]m4_defn([DOWNNAME])="builtin"
                    initial_service_suffix=""
                    linux_micro_builtin_modules="$linux_micro_builtin_modules m4_defn([DOWNNAME])"
                    strprefix="*"
                fi
                ;;
             no)
                strprefix="-"
                ;;
             builtin)
                linux_micro_builtin_modules="$linux_micro_builtin_modules m4_defn([DOWNNAME])"
                strprefix="*"
                ;;
             *)
                AC_MSG_ERROR([unsupported option: ${with_linux_micro_[]m4_defn([DOWNNAME])}. Must be one of `yes', `no' or `builtin'])
                ;;
        esac
        if test "${with_linux_micro_[]m4_defn([DOWNNAME])}" != "no"; then
           $4
           :
           if test "$3" = "yes"; then
              linux_micro_initial_services="${linux_micro_initial_services} $1${initial_service_suffix}"
           fi
        fi
        AM_CONDITIONAL([BUILTIN_PLATFORM_LINUX_MICRO_]m4_defn([UPNAME]), [test "${with_linux_micro_[]m4_defn([DOWNNAME])}" = "builtin"])
        AM_CONDITIONAL([MODULE_PLATFORM_LINUX_MICRO_]m4_defn([UPNAME]), [test "${with_linux_micro_[]m4_defn([DOWNNAME])}" = "yes"])
        linux_micro_modules="${linux_micro_modules} ${strprefix}$1"
m4_popdef([UPNAME])dnl
m4_popdef([DOWNNAME])dnl
])
