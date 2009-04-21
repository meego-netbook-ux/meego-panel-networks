AC_PREREQ(2.53)
AC_INIT(carrick, 0.0.2)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)

AC_CANONICAL_HOST

AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_ISC_POSIX
AC_HEADER_STDC
AM_PROG_CC_C_O

PKG_CHECK_MODULES(GTK, gtk+-2.0)
PKG_CHECK_MODULES(DBUS, dbus-glib-1 >= 0.73)
PKG_CHECK_MODULES(GCONNMAN, gconnman)

AC_OUTPUT([
        Makefile
        mux/Makefile
	carrick/Makefile
        src/Makefile
	tests/Makefile
        data/Makefile
        data/icons/Makefile
])
