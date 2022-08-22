ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

AM_CFLAGS = $(GPOD_CFLAGS) $(GLIB_CFLAGS) $(JSONC_CFLAGS) $(SQLITE3_CFLAGS) -Wunused-function -Wno-unused-variable -Wshadow -fno-common -D_XOPEN_SOURCE=500
AM_CXXFLAGS = $(AM_CFLAGS)

GPOD_OPT=
bin_PROGRAMS = gpod-ls gpod-rm gpod-tag gpod-recent-pl $(GPOD_OPT)
check_PROGRAMS = test-ff-xcode test-init-ipod test-gpool

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

gpod_recent_pl_LDFLAGS = $(GPOD_LIBS) $(GLIB_LIBS)
gpod_recent_pl_SOURCES = gpod-recent-pl.c gpod-utils.c


if HAVE_FFMPEG
GPOD_OPT+=gpod-cp
gpod_cp_CPPFLAGS = -DWANT_GPOD_HASH
gpod_cp_CFLAGS = $(FFMPEG_CFLAGS) $(AM_CFLAGS)
gpod_cp_CXXFLAGS = $(gpod_cp_CPPFLAGS) $(FFMPEG_CFLAGS) $(AM_CXXFLAGS)
gpod_cp_SOURCES = gpod-cp.cc gpod-ffmpeg.c gpod-ffmpeg-transcode.c gpod-utils.c sha1.c
gpod_cp_LDADD = $(GPOD_LIBS) $(GLIB_LIBS) $(FFMPEG_LIBS) -lavformat -lavutil -lavcodec -lswresample

GPOD_OPT+=gpod-extract
gpod_extract_CFLAGS = $(FFMPEG_CFLAGS) $(AM_CFLAGS)
gpod_extract_SOURCES = gpod-extract.c gpod-utils.c gpod-ffmpeg.c
gpod_extract_LDADD = $(GPOD_LIBS) $(GLIB_LIBS) $(FFMPEG_LIBS) -lavformat -lavutil -lavcodec -lswresample

GPOD_OPT+=gpod-verify
gpod_verify_CPPFLAGS = -DWANT_GPOD_HASH
gpod_verify_CFLAGS = $(FFMPEG_CFLAGS) $(AM_CFLAGS)
gpod_verify_SOURCES = gpod-verify.c gpod-utils.c gpod-ffmpeg.c sha1.c
gpod_verify_LDFLAGS =  $(GPOD_LIBS) $(GLIB_LIBS) $(FFMPEG_LIBS) -lavformat -lavutil -lavcodec -lswresample

test_ff_xcode_CFLAGS = $(FFMPEG_CFLAGS) $(AM_CFLAGS) -DGPOD_FF_STANDALONE
test_ff_xcode_LDFLAGS =  $(GLIB_LIBS) $(FFMPEG_LIBS) -lavformat -lavutil -lavcodec -lswresample -zundefs
test_ff_xcode_SOURCES = test-ff-xcode.c gpod-ffmpeg-transcode.c gpod-ffmpeg.c
endif

test_init_ipod_LDFLAGS = $(GPOD_LIBS) $(GLIB_LIBS)
test_init_ipod_SOURCES = test-init-ipod.c 

test_gpool_LDFLAGS = $(GLIB_LIBS)
test_gpool_SOURCES = test-gpool.c 

