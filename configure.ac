AC_PREREQ(2.53)
AC_INIT(carrick, 1.0.0)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AC_CONFIG_MACRO_DIR([build/autotools])
AM_CONFIG_HEADER(config.h)

AC_CANONICAL_HOST

IT_PROG_INTLTOOL([0.35.0])

GETTEXT_PACKAGE=carrick
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [The gettext package name])
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
PKG_CHECK_MODULES(DBUS, gthread-2.0 dbus-glib-1 >= 0.73)
PKG_CHECK_MODULES(NBTK, nbtk-gtk-1.0 >= 0.7.0)
PKG_CHECK_MODULES(LIBNOTIFY, libnotify)
PKG_CHECK_MODULES(MPL, moblin-panel)

SHAVE_INIT([build/autotools], [enable])

AC_OUTPUT([
        Makefile
        build/Makefile
        build/autotools/Makefile
        build/autotools/shave
        build/autotools/shave-libtool
        carrick/Makefile
        src/Makefile
        tests/Makefile
        data/Makefile
        data/icons/Makefile
        data/theme/Makefile
        po/Makefile.in
])
