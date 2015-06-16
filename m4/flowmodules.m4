dnl SOL_FLOW_MODULE([NAME], [DEFAULT], [CHECKS_IF_ENABLED])
AC_DEFUN([SOL_FLOW_MODULE],
[dnl
m4_pushdef([DOWNNAME], m4_translit([$1], [-A-Z], [_a-z]))dnl
m4_pushdef([UPNAME], m4_translit([$1], [-a-z], [_A-Z]))dnl
        AC_ARG_WITH([flow-module-]$1,
                AS_HELP_STRING([--with-flow-module-]$1[=OPTION],
                        [choose how to build flow module for `$1', options: `yes', `no' or `builtin']),
                        [], [with_flow_module_]m4_defn([DOWNNAME])[=$2])

        strprefix=""
        AC_MSG_CHECKING([whenever `$1' flow module is to be built])
        AC_MSG_RESULT([${with_flow_module_[]m4_defn([DOWNNAME])}])
        case "${with_flow_module_[]m4_defn([DOWNNAME])}" in
             yes)
                strprefix="+"
                if test "${enable_dynamic_modules}" = "no"; then
                    AC_MSG_WARN([--with-flow-module-]$1[=yes but --disable-dynamic-modules was used. Either --enable-dynamic-modules or use --with-flow-module-]$1[=builtin. Assuming builtin.])
                    with_flow_module_[]m4_defn([DOWNNAME])="builtin"
                    flow_builtin_modules="$flow_builtin_modules m4_defn([DOWNNAME])"
                    strprefix="*"
                fi
                ;;
             no)
                strprefix="-"
                ;;
             builtin)
                flow_builtin_modules="$flow_builtin_modules m4_defn([DOWNNAME])"
                strprefix="*"
                ;;
             *)
                AC_MSG_ERROR([unsupported option: ${with_flow_module_[]m4_defn([DOWNNAME])}. Must be one of `yes', `no' or `builtin'])
                ;;
        esac
        if test "${with_flow_module_[]m4_defn([DOWNNAME])}" != "no"; then
           $3
           :
        fi
        AM_CONDITIONAL([BUILTIN_FLOW_]m4_defn([UPNAME]), [test "${with_flow_module_[]m4_defn([DOWNNAME])}" = "builtin"])
        AM_CONDITIONAL([MODULE_FLOW_]m4_defn([UPNAME]), [test "${with_flow_module_[]m4_defn([DOWNNAME])}" = "yes"])
        AM_CONDITIONAL([MODULE_OR_BUILTIN_FLOW_]m4_defn([UPNAME]), [test "${with_flow_module_[]m4_defn([DOWNNAME])}" != "no"])
        flow_modules="${flow_modules} ${strprefix}$1"
m4_popdef([UPNAME])dnl
m4_popdef([DOWNNAME])dnl
])

AC_DEFUN([SOL_FLOW_MODULE_RESOLVER],
[dnl
m4_pushdef([DOWNNAME], m4_translit([$1], [-A-Z], [_a-z]))dnl
m4_pushdef([UPNAME], m4_translit([$1], [-a-z], [_A-Z]))dnl
        AC_ARG_ENABLE([flow-module-resolver-]m4_defn([DOWNNAME]),
                AS_HELP_STRING([--disable-flow-module-resolver-]m4_defn([DOWNNAME]),
                        [disable build of I/O module resolver $4 @<:@default=$2@:>@]),
                        [enable_flow_module_resolver_[]m4_defn([DOWNNAME])=${enableval}],
                        [enable_flow_module_resolver_[]m4_defn([DOWNNAME])=$2])
        $3
        AM_CONDITIONAL([BUILD_SOL_FLOW_MODULE_RESOLVER_]m4_defn([UPNAME]), [test "${enable_flow_module_resolver_[]m4_defn([DOWNNAME])}" = "yes"])
        if test "${enable_flow_module_resolver_[]m4_defn([DOWNNAME])}" = "yes"; then
           flow_module_resolvers="m4_defn([DOWNNAME]) ${flow_module_resolvers}"
           SOL_BUILD_FLOW_MODULE_RESOLVER_[]m4_defn([UPNAME])=["#define SOL_BUILD_FLOW_MODULE_RESOLVER_]m4_defn([UPNAME])[ 1"]
        fi
        AC_SUBST([SOL_BUILD_FLOW_MODULE_RESOLVER_]m4_defn([UPNAME]))
m4_popdef([UPNAME])dnl
m4_popdef([DOWNNAME])dnl
])

AC_DEFUN([SOL_FLOW_MODULE_RESOLVER_DEFAULT],
[dnl
m4_pushdef([DOWNNAME], m4_translit([$1], [-A-Z], [_a-z]))dnl
AC_ARG_WITH([flow-module-default-resolver],
        AS_HELP_STRING([--with-flow-module-default-resolver=RESOLVER],
                [choose default module resolver to use. @<:@default=]m4_defn([DOWNNAME])[@:>@]),
                [with_flow_module_default_resolver=${withval}],
                [with_flow_module_default_resolver=[]m4_defn([DOWNNAME])])

sol_flow_module_resolver_default=""
if test "${with_flow_module_default_resolver}" = "none"; then
   sol_flow_module_resolver_default="NULL"
   AC_MSG_CHECKING([flow-module-default-resolver])
   AC_MSG_RESULT([${with_flow_module_default_resolver}])
elif echo "${flow_module_resolvers}" | grep -e "${with_flow_module_default_resolver}" >/dev/null 2>/dev/null; then
   sol_flow_module_resolver_default="sol_flow_resolver_${with_flow_module_default_resolver}"
   AC_MSG_CHECKING([flow-module-default-resolver])
   AC_MSG_RESULT([${with_flow_module_default_resolver}])
else
   AC_MSG_ERROR([unknown or disabled module flow-module-resolver "${with_flow_module_default_resolver}"])
fi
AC_DEFINE_UNQUOTED([sol_flow_default_resolver], [${sol_flow_module_resolver_default}], [default flow resolver to use])
m4_popdef([DOWNNAME])dnl
])
