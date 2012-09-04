AC_PREREQ([2.60])
AC_INIT([ofono-efl], [1])

AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

PKG_PROG_PKG_CONFIG

AC_LANG_C

AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AM_PROG_CC_C_O
AC_C___ATTRIBUTE__

AC_DISABLE_STATIC
AC_PROG_LIBTOOL

EFL_COMPILER_FLAG
EFL_COMPILER_FLAG([-Wall])
EFL_COMPILER_FLAG([-Wextra])
EFL_COMPILER_FLAG([-Wshadow])
EFL_COMPILER_FLAG([-fvisibility=hidden])

PKG_CHECK_MODULES([EFL],
	[
		elementary
		edbus
		dbus-1
	 ])

EFL_WITH_BIN([edje], [edje-cc], [edje_cc])

want_tizen="auto"
have_tizen="no"
AC_ARG_ENABLE([tizen],
   AC_HELP_STRING([--enable-tizen], [enable Tizen integration. @<:@default=auto@:>@]),
   [want_tizen="${enableval}"], [:])

if test "$want_tizen" != "no"; then
   PKG_CHECK_MODULES([TIZEN],
                [
                        ecore-x
                        appcore-efl
                        ui-gadget
                        capi-social-contacts
                        vconf
                        capi-system-power
                        utilX
                        aul
                        appsvc
                ],
                [have_tizen="yes"],
                [have_tizen="no"])
fi

if test "$want_tizen" = "yes" -a "$have_tizen" = "no"; then
   AC_MSG_ERROR([Want tizen, but not found])
elif test "$have_tizen" = "yes"; then
     AC_DEFINE([HAVE_TIZEN], 1, [Tizen support])
fi

AM_CONDITIONAL([HAVE_TIZEN], [test "$have_tizen" = "yes"])

with_max_log_level="EINA_LOG_LEVEL_DBG"
AC_ARG_WITH(maximum-log-level,
   [AC_HELP_STRING([--with-maximum-log-level=NUMBER],
                   [limit log level, any call to EINA_LOG() with values greater than this will be compiled out, ignoring runtime settings, but saving function calls.])],
   [with_max_log_level="${withval}"], [:])
AC_DEFINE_UNQUOTED(EINA_LOG_LEVEL_MAXIMUM, ${with_max_log_level}, [if set, logging is limited to this amount.])

with_dbusservicesdir=`pkg-config --variable=session_bus_services_dir dbus-1`
AC_ARG_WITH(dbusservicesdir,
   [AC_HELP_STRING([--with-dbusservicesdir=PATH],
      [Where to place dbus services files])],
   [with_dbusservicesdir="${withval}"], [:])
AC_SUBST(with_dbusservicesdir)


with_desktopdir="/opt/share/applications"
AC_ARG_WITH(desktopdir,
   [AC_HELP_STRING([--with-desktopdir=PATH],
      [Where to place desktop files. Default=/opt/share/applications/])],
   [with_desktopdir="${withval}"], [:])
AC_SUBST(with_desktopdir)

AC_CONFIG_FILES([
Makefile
])

AC_OUTPUT
