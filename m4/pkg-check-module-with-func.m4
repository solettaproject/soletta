dnl Macro to check the presence of package with a specific function
dnl It combines AC_PKG_CHECK_MODULES() and AC_CHECK_LIB(), correctly
dnl overriding CFLAGS and LDFLAGS
dnl CC_PKG_CHECK_MODULES_WITH_FUNC(prefix, list-of-modules, library, function, action-if-found
dnl                                action-if-not-found)
AC_DEFUN([CC_PKG_CHECK_MODULES_WITH_FUNC], [
  PKG_CHECK_MODULES([$1], [$2], [
    cc_save_CFLAGS="$CFLAGS"
    cc_save_LDFLAGS="$LDFLAGS"
    CFLAGS="$CFLAGS $$1_CFLAGS"
    LDFLAGS="$LDFLAGS $$1_LIBS"

    AC_CHECK_LIB([$3], [$4], [$5], [
      $1_CFLAGS=""
      $1_LDFLAGS=""
      $6]
    )

    CFLAGS="$cc_save_CFLAGS"
    LDFLAGS="$cc_save_LDFLAGS"
  ], [$6])
])
