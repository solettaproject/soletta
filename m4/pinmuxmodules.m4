dnl SOL_PIN_MUX_MODULE([NAME], [DEFAULT], [DESCRIPTION])
AC_DEFUN([SOL_PIN_MUX_MODULE],
[dnl
m4_pushdef([DOWNNAME], m4_translit([$1], [-A-Z], [_a-z]))dnl
m4_pushdef([UPNAME], m4_translit([$1], [-a-z], [_A-Z]))dnl
        AC_ARG_WITH($1[-mux],
                AS_HELP_STRING([--with-]$1[-mux], $3[. Options: 'yes', 'no' or 'builtin']),
                [],
                [with_[]m4_defn([DOWNNAME])_mux=$2])

        AS_IF([test "x$with_[]m4_defn([DOWNNAME])_mux" != "xno" -a "x$with_pin_mux" = "xno"], [
                AC_MSG_ERROR([--with-]$1[-mux=${with_[]m4_defn([DOWNNAME])_mux} can't be used --with-pin-mux=no.])
        ])

        case "$with_[]m4_defn([DOWNNAME])_mux" in
            yes)
                if test "x$enable_dynamic_modules" = "xno"; then
                    AC_MSG_WARN([--with-]$1[-mux=yes but --disable-dynamic-modules was used. Either --enable-dynamic-modules or use --with-]$1[-mux=builtin. Assuming builtin.])
                    build_[]m4_defn([DOWNNAME])_mux="builtin"
                    pin_mux_builtins="$pin_mux_builtins []m4_defn([DOWNNAME])"
                else
                    build_[]m4_defn([DOWNNAME])_mux="yes"
                    pin_mux_modules="$pin_mux_modules []m4_defn([DOWNNAME])"
                fi
                ;;
            builtin)
                build_[]m4_defn([DOWNNAME])_mux="builtin"
                pin_mux_builtins="$pin_mux_builtins []m4_defn([DOWNNAME])"
                ;;
            no)
                build_[]m4_defn([DOWNNAME])_mux="no"
                ;;
            *)
                AC_MSG_ERROR([--with-]$1[-mux. Unsupported option: ${with_[]m4_defn([DOWNNAME])_mux}. Must be one of 'yes', 'no' or 'builtin'])
                ;;
        esac

        AM_CONDITIONAL([BUILTIN_PIN_MUX_]m4_defn([UPNAME]),
                [test "x$build_[]m4_defn([DOWNNAME])_mux" = "xbuiltin"])
        AM_CONDITIONAL([MODULE_PIN_MUX_]m4_defn([UPNAME]),
                [test "x$build_[]m4_defn([DOWNNAME])_mux" = "xyes"])

m4_popdef([UPNAME])dnl
m4_popdef([DOWNNAME])dnl
])
