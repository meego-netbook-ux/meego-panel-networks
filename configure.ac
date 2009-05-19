AC_PREREQ(2.53)
AC_INIT(carrick, 0.0.7)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)

AC_CANONICAL_HOST

IT_PROG_INTLTOOL([0.35.0])

GETTEXT_PACKAGE=carrick
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [The gettext package name])

ALL_LINGUAS=""
AM_GLIB_GNU_GETTEXT

locaeldir='${prefix}/${DATADIRNAME}/locale'
AC_SUBST(localedir)

AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_ISC_POSIX
AC_HEADER_STDC
AM_PROG_CC_C_O

PKG_CHECK_MODULES(GTK, gtk+-2.0 >= 2.12)
PKG_CHECK_MODULES(DBUS, dbus-glib-1 >= 0.73)
PKG_CHECK_MODULES(GCONNMAN, gconnman >= 0.1.12)
PKG_CHECK_MODULES(NBTK, nbtk-gtk-1.0 >= 0.7.0)
PKG_CHECK_MODULES(LIBNOTIFY, libnotify)

AC_OUTPUT([
        Makefile
        carrick/Makefile
        src/Makefile
        tests/Makefile
        data/Makefile
        data/icons/Makefile
        po/Makefile.in
])
