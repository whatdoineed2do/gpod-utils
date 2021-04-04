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
PKG_CHECK_MODULES(SQLITE3, [sqlite3])

PKG_CHECK_MODULES(TAGLIB, taglib, have_taglib=yes, have_taglib=no)
if test x"$have_taglib" = xyes; then
   AH_TEMPLATE([HAVE_TAGLIB], [Whether TagLib is installed, it's only used in a test program])
   AC_DEFINE_UNQUOTED(HAVE_TAGLIB, 1)
fi
AC_SUBST(TAGLIB_CFLAGS)
AC_SUBST(TAGLIB_LIBS)
AM_CONDITIONAL(HAVE_TAGLIB, test x"$have_taglib" = xyes)

PKG_CHECK_MODULES(FFMPEG, libavformat, have_ffmpeg=yes, have_ffmpeg=no)
if test x"$have_ffmpeg" = xyes; then
   AH_TEMPLATE([HAVE_FFMPEG], [Whether ffmpeg is installed, it's only used in a test program])
   AC_DEFINE_UNQUOTED(HAVE_FFMPEG, 1)
fi
AC_SUBST(FFMPEG_CFLAGS)
AC_SUBST(FFMPEG_LIBS)
AM_CONDITIONAL(HAVE_FFMPEG, test x"$have_ffmpeg" = xyes)


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
