# autoreconf --install
# autoconf
#

AC_PREREQ(2.64)
AC_INIT([gpod-utils], [m4_esyscmd([git describe --tags --dirty --always | tr -d '\n'])], [https://github.com/whatdoineed2do/gpod-utils/issues], [https://github.com/whatdoineed2do/gpod-utils], [whatdoineed2do@nospam-github.com])
AM_INIT_AUTOMAKE([dist-bzip2 no-dist-gzip])
AC_CONFIG_MACRO_DIRS([m4])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_CONFIG_SRCDIR([src/])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CXX
AC_HEADER_STDC
AC_PROG_INSTALL
PKG_PROG_PKG_CONFIG

AX_CXX_COMPILE_STDCXX([14], [noext], [mandatory])

PKG_CHECK_MODULES(GLIB, [glib-2.0])
PKG_CHECK_MODULES(GPOD, [libgpod-1.0 >= 0.8.3])
PKG_CHECK_MODULES(JSONC, [json-c >= 0.13])

AC_ARG_ENABLE([debug-code],
	[AS_HELP_STRING([--enable-debug],
	[enable debug (default is no)])],
	[no_debug=false],
	[no_debug=true])
if test "$no_debug" = true; then
        building_debug=no
        AC_DEFINE(NDEBUG,1,[debug code])
else
        building_debug=yes
        AC_DEFINE(DEBUG,1,[debug code])
fi

AC_OUTPUT([
Makefile
src/Makefile
])
