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

AC_PROG_RANLIB
AC_PROG_CXX
AC_HEADER_STDC
AC_PROG_INSTALL
PKG_PROG_PKG_CONFIG

AX_CXX_COMPILE_STDCXX([14], [noext], [mandatory])

PKG_CHECK_MODULES(GLIB, [glib-2.0])
PKG_CHECK_MODULES(GPOD, [libgpod-1.0 >= 0.8.3])
PKG_CHECK_MODULES(JSONC, [json-c >= 0.13])

AC_ARG_ENABLE(sqlite3,
	AS_HELP_STRING([--disable-sqlite3],[do not use generate db from iTunesDB]),
	[ use_sqlite3="${enableval}" ], [ use_sqlite3=yes ] )

if test "x$use_sqlite3" != xno ; then
	PKG_CHECK_MODULES(SQLITE3, sqlite3, [ use_sqlite3=yes ], [ use_sqlite3=no ])
        AH_TEMPLATE([HAVE_SQLITE3], [generate db from iTunesDB])
	AS_IF([test "x$use_sqlite3" = "xyes"], [
	        AC_DEFINE([HAVE_SQLITE3])
		AC_SUBST(SQLITE3_CFLAGS)
		AC_SUBST(SQLITE3_LIBS)
	   ])
fi
AM_CONDITIONAL([HAVE_SQLITE3], [test "x$use_sqlite3" = xyes])

PKG_CHECK_MODULES(FFMPEG, libavformat >= 58.20.100 libavcodec >= 58.35.100 libavutil >= 56.22.100 libswresample >= 3.3.100)
PKG_CHECK_EXISTS([libavcodec >= 59.24.100],
		 [AC_DEFINE([HAVE_FF5_CH_LAYOUT], 1,
			    [Defined if ffmpeg/libavcodec defines new 5.1.x ch_layout])])
AC_SUBST(FFMPEG_CFLAGS)
AC_SUBST(FFMPEG_LIBS)


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
