dnl as-ac-expand.m4 0.2.0                                   -*- autoconf -*-
dnl autostars m4 macro for expanding directories using configure's prefix

dnl (C) 2003, 2004, 2005 Thomas Vander Stichele <thomas at apestaart dot org>

dnl Copying and distribution of this file, with or without modification,
dnl are permitted in any medium without royalty provided the copyright
dnl notice and this notice are preserved.

dnl AS_AC_EXPAND(VAR, CONFIGURE_VAR)

dnl example:
dnl AS_AC_EXPAND(SYSCONFDIR, $sysconfdir)
dnl will set SYSCONFDIR to /usr/local/etc if prefix=/usr/local

AC_DEFUN([AS_AC_EXPAND],
[
  EXP_VAR=[$1]
  FROM_VAR=[$2]

  dnl first expand prefix and exec_prefix if necessary
  prefix_save=$prefix
  exec_prefix_save=$exec_prefix

  dnl if no prefix given, then use /usr/local, the default prefix
  if test "x$prefix" = "xNONE"; then
    prefix="$ac_default_prefix"
  fi
  dnl if no exec_prefix given, then use prefix
  if test "x$exec_prefix" = "xNONE"; then
    exec_prefix=$prefix
  fi

  full_var="$FROM_VAR"
  dnl loop until it doesn't change anymore
  while true; do
    new_full_var="`eval echo $full_var`"
    if test "x$new_full_var" = "x$full_var"; then break; fi
    full_var=$new_full_var
  done

  dnl clean up
  full_var=$new_full_var
  AC_SUBST([$1], "$full_var")

  dnl restore prefix and exec_prefix
  prefix=$prefix_save
  exec_prefix=$exec_prefix_save
])


# Set ALL_LINGUAS based on the .po files present. Optional argument is the name
# of the po directory.
AC_DEFUN([AS_ALL_LINGUAS],
[
 AC_MSG_CHECKING([for linguas])
 podir="m4_default([$1],[$srcdir/po])"
 ALL_LINGUAS=`cd $podir && ls *.po 2>/dev/null | awk 'BEGIN { FS="."; ORS=" " } { print $[]1 }'`
 AC_SUBST([ALL_LINGUAS])
 AC_MSG_RESULT($ALL_LINGUAS)
])
