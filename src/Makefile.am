ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

AM_CFLAGS = $(GPOD_CFLAGS) $(GLIB_CFLAGS) $(JSONC_CFLAGS) $(SQLITE3_CFLAGS)
AM_CXXFLAGS = $(TAGLIB_CFLAGS) $(AM_CFLAGS)

GPOD_OPT=
bin_PROGRAMS = gpod-ls gpod-rm $(GPOD_OPT)

# using git version instead of the am values in config.h
gpod_ls_CPPFLAGS = 
gpod_ls_LDFLAGS =  $(GPOD_LIBS) $(GLIB_LIBS) $(JSONC_LIBS) $(SQLITE3_LIBS) -lz
gpod_ls_SOURCES = \
	gpod-ls.c

gpod_rm_LDFLAGS =  $(GPOD_LIBS) $(GLIB_LIBS) $(JSONC_LIBS) $(SQLITE3_LIBS) -lz
gpod_rm_SOURCES = gpod-rm.c

if HAVE_TAGLIB
GPOD_OPT+=gpod-cp
gpod_cp_CFLAGS = $(FFMPEG_CFLAGS) $(AM_CFLAGS)
gpod_cp_CXXFLAGS = $(FFMPEG_CFLAGS) $(AM_CXXFLAGS)
gpod_cp_SOURCES = gpod-cp.cc gpod-ffmpeg.c
gpod_cp_LDADD = $(TAGLIB_LIBS) $(GPOD_LIBS) $(GLIB_LIBS) -lavformat -lavutil -lavcodec -lswresample
endif

if HAVE_FFMPEG
GPOD_OPT+=gpod-transcode
gpod_transcode_SOURCES = gpod-transcode.cc
gpod_transcode_CFLAGS = $(FFMPEG_CFLAGS)
gpod_transcode_LDADD =  $(FFMPEG_LIBS)
endif
