ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

AM_CFLAGS = $(GPOD_UTILS_CFLAGS) $(GPOD_CFLAGS) $(GLIB_CFLAGS) $(JSONC_CFLAGS) $(SQLITE3_CFLAGS) $(FFMPEG_CFLAGS) -Wunused-function -Wunused-variable -Wshadow -fno-common -D_XOPEN_SOURCE=500
AM_CXXFLAGS = $(AM_CFLAGS)

noinst_LIBRARIES = libgpod-utils.a

libgpod_utils_a_CPPFLAGS = -DWANT_GPOD_HASH
libgpod_utils_a_CFLAGS = $(FFMPEG_CFLAGS) $(AM_CFLAGS)
libgpod_utils_a_SOURCES = gpod-utils.c sha1.c gpod-ffmpeg.c gpod-ffmpeg-transcode.c
