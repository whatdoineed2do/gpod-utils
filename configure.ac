# autoreconf --install
# autoconf
#

AC_PREREQ([2.69])
AC_INIT([gpod-utils],[m4_esyscmd([git describe --tags --dirty --always | tr -d '\n'])],[https://github.com/whatdoineed2do/gpod-utils/issues],[https://github.com/whatdoineed2do/gpod-utils],[whatdoineed2do@nospam-github.com])
AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([dist-bzip2 no-dist-gzip])
AC_CONFIG_MACRO_DIRS([m4])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_CONFIG_SRCDIR([src/])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AC_PROG_RANLIB
AC_PROG_INSTALL
PKG_PROG_PKG_CONFIG

dnl On macOS two glib stacks commonly coexist (Homebrew + MacPorts); a binary
dnl that loads both glibs aborts at runtime with duplicate GObject classes.
dnl Anchor all pkg-config lookups to whichever prefix provides libgpod, and
dnl drop leaked -I/-L flags (e.g. Homebrew's CPPFLAGS/LDFLAGS in the shell
dnl environment) that would mix in a second glib or mismatched ffmpeg headers.
gpod_pc_prefix=
case "$host_os" in
darwin*)
    gpod_pc_prefix=`$PKG_CONFIG --variable=prefix libgpod-1.0 2>/dev/null`
    if test -n "$gpod_pc_prefix"; then
        gpod_pc_path="$gpod_pc_prefix/lib/pkgconfig:$gpod_pc_prefix/share/pkgconfig"
        # MacPorts keeps versioned ffmpeg under $prefix/libexec/ffmpeg<N>/
        for d in "$gpod_pc_prefix"/libexec/ffmpeg*/lib/pkgconfig; do
            test -d "$d" && gpod_pc_path="$gpod_pc_path:$d"
        done
        PKG_CONFIG_PATH="$gpod_pc_path${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
        export PKG_CONFIG_PATH
        AC_MSG_NOTICE([darwin: anchoring pkg-config lookups to $gpod_pc_prefix])

        gpod_kept_flags=
        for f in $CPPFLAGS; do
            gpod_drop=no
            case "$f" in
            -I"$gpod_pc_prefix"/*) ;;
            -I*)
                d=`expr "x$f" : 'x-I\(.*\)'`
                if test -d "$d/glib-2.0" || test -f "$d/libavcodec/avcodec.h"; then
                    gpod_drop=yes
                fi ;;
            esac
            if test "$gpod_drop" = yes; then
                AC_MSG_NOTICE([darwin: dropping conflicting $f from CPPFLAGS])
            else
                gpod_kept_flags="$gpod_kept_flags $f"
            fi
        done
        CPPFLAGS=$gpod_kept_flags

        gpod_kept_flags=
        for f in $LDFLAGS; do
            gpod_drop=no
            case "$f" in
            -L"$gpod_pc_prefix"/*) ;;
            -L*)
                d=`expr "x$f" : 'x-L\(.*\)'`
                if ls "$d"/libglib-2.0.* >/dev/null 2>&1 || ls "$d"/libavcodec.* >/dev/null 2>&1; then
                    gpod_drop=yes
                fi ;;
            esac
            if test "$gpod_drop" = yes; then
                AC_MSG_NOTICE([darwin: dropping conflicting $f from LDFLAGS])
            else
                gpod_kept_flags="$gpod_kept_flags $f"
            fi
        done
        LDFLAGS=$gpod_kept_flags
    fi
    ;;
esac

PKG_CHECK_MODULES(GLIB, [glib-2.0])
PKG_CHECK_MODULES(GPOD, [libgpod-1.0 >= 0.8.3])

if test -n "$gpod_pc_prefix"; then
    glib_pc_prefix=`$PKG_CONFIG --variable=prefix glib-2.0 2>/dev/null`
    case "$glib_pc_prefix" in
    "$gpod_pc_prefix"|"$gpod_pc_prefix"/*) ;;
    *)
        AC_MSG_ERROR([glib-2.0 resolves to '$glib_pc_prefix' but libgpod to '$gpod_pc_prefix'; linking two glib copies into one binary crashes at runtime.  Re-run configure with PKG_CONFIG_PATH pointing only at the stack that provides libgpod.]) ;;
    esac
fi
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

PKG_CHECK_MODULES(FFMPEG, libavformat >= 58.20.100 libavcodec >= 58.35.100 libavutil >= 56.22.100 libswresample >= 3.3.100 libswscale)
PKG_CHECK_EXISTS([libavcodec >= 59.24.100],
		 [AC_DEFINE([HAVE_FF5_CH_LAYOUT], 1,
			    [Defined if ffmpeg/libavcodec defines new 5.1.x ch_layout])])
AC_SUBST(FFMPEG_CFLAGS)
AC_SUBST(FFMPEG_LIBS)

AC_ARG_ENABLE(debug,
        AS_HELP_STRING([--enable-debug],[enable debug code (default is no)]),
        [ debug="${enableval}" ], [ debug=no ] )
if test "x$debug" = xno; then
        AC_DEFINE(NDEBUG,1,[debug code])
fi

AC_CONFIG_FILES([
Makefile
src/lib/Makefile
src/Makefile
])
AC_OUTPUT
