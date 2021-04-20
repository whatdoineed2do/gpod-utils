ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

AM_CFLAGS = $(GPOD_CFLAGS) $(GLIB_CFLAGS) $(JSONC_CFLAGS) $(SQLITE3_CFLAGS) -Wno-unused-variable -Wshadow
AM_CXXFLAGS = $(AM_CFLAGS)

GPOD_OPT=
bin_PROGRAMS = gpod-ls gpod-rm gpod-tag $(GPOD_OPT)
check_PROGRAMS = test-ff-xcode

# using git version instead of the am values in config.h
gpod_ls_CPPFLAGS = -DWANT_GPOD_HASH
gpod_ls_LDFLAGS =  $(GPOD_LIBS) $(GLIB_LIBS) $(JSONC_LIBS) $(SQLITE3_LIBS) -lz
gpod_ls_SOURCES = \
	gpod-ls.c sha1.c gpod-utils.c

gpod_rm_CPPFLAGS = -DWANT_GPOD_HASH
gpod_rm_LDFLAGS =  $(GPOD_LIBS) $(GLIB_LIBS) $(JSONC_LIBS) $(SQLITE3_LIBS) -lz
gpod_rm_SOURCES = gpod-rm.c sha1.c gpod-utils.c

gpod_tag_LDFLAGS =  $(GPOD_LIBS) $(GLIB_LIBS)
gpod_tag_SOURCES = gpod-tag.c gpod-utils.c

if HAVE_FFMPEG
GPOD_OPT+=gpod-cp
gpod_cp_CPPFLAGS = -DWANT_GPOD_HASH
gpod_cp_CFLAGS = $(FFMPEG_CFLAGS) $(AM_CFLAGS)
gpod_cp_CXXFLAGS = $(gpod_cp_CPPFLAGS) $(FFMPEG_CFLAGS) $(AM_CXXFLAGS)
gpod_cp_SOURCES = gpod-cp.cc gpod-ffmpeg.c gpod-ffmpeg-transcode.c gpod-utils.c sha1.c
gpod_cp_LDADD = $(GPOD_LIBS) $(GLIB_LIBS) -lavformat -lavutil -lavcodec -lswresample

GPOD_OPT+=gpod-extract
gpod_extract_CFLAGS = $(FFMPEG_CFLAGS) $(AM_CFLAGS)
gpod_extract_SOURCES = gpod-extract.c gpod-utils.c
gpod_extract_LDADD = $(GPOD_LIBS) $(GLIB_LIBS) -lavformat -lavutil -lavcodec -lswresample

test_ff_xcode_CFLAGS = $(FFMPEG_CFLAGS)
test_ff_xcode_LDFLAGS = $(GPOD_LIBS) $(GLIB_LIBS) -lavformat -lavutil -lavcodec -lswresample
test_ff_xcode_SOURCES = test-ff-xcode.c gpod-ffmpeg-transcode.c gpod-ffmpeg.c
endif
