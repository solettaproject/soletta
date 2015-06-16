
dnl Usage: SOL_WITH_BIN(binary, default)
dnl Call AC_SUBST(_binary) (_binary is the lowercase of binary, - being transformed into _ by default, or the value set by the user)

AC_DEFUN([SOL_WITH_BIN],
[

m4_pushdef([DOWN], m4_translit([[$1]], [-A-Z], [_a-z]))dnl
m4_pushdef([UP], m4_translit([[$1]], [-a-z], [_A-Z]))dnl
dnl configure option

DOWN=$2

AC_ARG_WITH([$1],
   [AC_HELP_STRING([--with-$1=PATH], [specify a specific path to ]DOWN[ @<:@default=]DOWN[@:>@])],
   [
    if [test "x${withval}" = "xno"]; then
        DOWN=
    elif [test "x${withval}" != "xyes"]; then
        DOWN=${withval}
        with_[]DOWN="yes"
    fi
   ],
   [with_[]DOWN="yes"])

if test "x${with_[]DOWN}" = "xno"; then
    AC_MSG_NOTICE(DOWN[ not set])
    with_binary_[]m4_defn([DOWN])="no"
else
    AC_MSG_NOTICE(DOWN[ set to ${DOWN}])
    with_binary_[]m4_defn([DOWN])=${DOWN}
fi

AM_CONDITIONAL(HAVE_[]UP, [test "x${with_[]DOWN}" = "xyes"])
AC_SUBST(DOWN)

])
