AC_PREREQ(2.53)
AC_INIT(meego-panel-networks, 1.3.2)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)

AC_CANONICAL_HOST

IT_PROG_INTLTOOL([0.35.0])

GETTEXT_PACKAGE=meego-panel-networks
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [The gettext package name])
AS_ALL_LINGUAS
AM_GLIB_GNU_GETTEXT

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

localedir='${prefix}/${DATADIRNAME}/locale'
AC_SUBST(localedir)

AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_ISC_POSIX
AC_HEADER_STDC
AM_PROG_CC_C_O

AS_AC_EXPAND(LIBEXECDIR, $libexecdir)

THEME_DIR="${datadir}/${PACKAGE}/theme"
AS_AC_EXPAND(THEME_DIR, $THEME_DIR)
AC_DEFINE_UNQUOTED([THEME_DIR], "$THEME_DIR", [Carrick theme directory])

PKG_CHECK_MODULES(GTK, gtk+-2.0 >= 2.12)
PKG_CHECK_MODULES(DBUS, gthread-2.0 dbus-glib-1 >= 0.73)
PKG_CHECK_MODULES(MX, mx-gtk-1.0)
PKG_CHECK_MODULES(LIBNOTIFY, libnotify)
PKG_CHECK_MODULES(REST, rest-0.7)
PKG_CHECK_MODULES(GCONF, gconf-2.0)

AC_MSG_CHECKING([whether to use the MeeGo environment])
AC_ARG_WITH([meego],[AS_HELP_STRING([--without-meego], [disable support for MeeGo environment])],
            [], [with_meego=yes])
AS_IF(
[test "$with_meego" = yes],
[
        AC_MSG_RESULT([yes])
        AC_DEFINE([WITH_MEEGO], 1, [Use MeeGo])

        PKG_CHECK_MODULES(MPL, meego-panel >= 0.49.0)

        PANEL_DIR=$($PKG_CONFIG meego-panel --variable=meego_panel_panels_dir)
        AC_SUBST(PANEL_DIR)
],
AC_MSG_RESULT([no])
)
AM_CONDITIONAL([WITH_MEEGO], [test "$with_meego" = "yes"])


AC_MSG_CHECKING([for mobile-broadband-provider-info])
PKG_CHECK_EXISTS(mobile-broadband-provider-info,
        _PKG_CONFIG(MOBILE_DATA, [variable=database], [mobile-broadband-provider-info])
        AC_DEFINE_UNQUOTED(MOBILE_DATA, "$pkg_cv_MOBILE_DATA", [Mobile provider database])
        AC_MSG_RESULT([yes]),
        AC_MSG_ERROR([Cannot find mobile-broadband-provider-info]))

AC_MSG_CHECKING([for iso-codes])
PKG_CHECK_EXISTS(iso-codes,
        _PKG_CONFIG(ISOCODES, [variable=prefix], [iso-codes])
        AC_DEFINE_UNQUOTED(ISOCODES_PREFIX, "$pkg_cv_ISOCODES", [iso-codes prefix])
        AC_MSG_RESULT([yes]),
        AC_MSG_ERROR([Cannot find iso-codes]))

AC_OUTPUT([
        Makefile
        carrick/Makefile
        src/Makefile
        tests/Makefile
        data/Makefile
        data/carrick.desktop.in
        data/icons/Makefile
        data/theme/Makefile
        callouts/Makefile
        callouts/3g/Makefile
        po/Makefile.in
])
