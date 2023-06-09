ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

AM_CFLAGS = $(GPOD_UTILS_CFLAGS) $(GPOD_CFLAGS) $(GLIB_CFLAGS) $(JSONC_CFLAGS) $(SQLITE3_CFLAGS) $(FFMPEG_CFLAGS) -Ilib -Wunused-function -Wunused-variable -Wshadow -fno-common -D_XOPEN_SOURCE=500
AM_CXXFLAGS = $(AM_CFLAGS)
AM_LDFLAGS = $(GPOD_UTILS_LDFLAGS) $(GLIB_LIBS) $(GPOD_LIBS) $(FFMPEG_LDFLAGS) -lavformat -lavutil -lavcodec

GPOD_OPT=
bin_PROGRAMS = $(GPOD_OPT) gpod-ls gpod-rm gpod-tag gpod-recent-pl gpod-hashsum
check_PROGRAMS = test-ff-xcode test-init-ipod test-gpool

# using git version instead of the am values in config.h
gpod_ls_SOURCES = gpod-ls.c
gpod_ls_LDADD = -Llib -lgpod-utils $(AM_LDFLAGS) $(GPOD_LIBS) $(GLIB_LIBS) $(JSONC_LIBS) $(SQLITE3_LIBS)

gpod_rm_SOURCES = gpod-rm.c
gpod_rm_LDADD = -Llib -lgpod-utils $(AM_LDFLAGS) $(GPOD_LIBS) $(GLIB_LIBS) $(JSONC_LIBS) $(SQLITE3_LIBS)

gpod_tag_SOURCES = gpod-tag.c
gpod_tag_LDADD = -Llib -lgpod-utils $(AM_LDFLAGS) $(GPOD_LIBS) $(GLIB_LIBS)

gpod_recent_pl_SOURCES = gpod-recent-pl.c
gpod_recent_pl_LDADD = -Llib -lgpod-utils $(AM_LDFLAGS) $(GPOD_LIBS) $(GLIB_LIBS)


gpod_hashsum_CFLAGS = ${AM_CFLAGS}
gpod_hashsum_SOURCES = gpod-hashsum.c
gpod_hashsum_LDADD =  -Llib -lgpod-utils $(AM_LDFLAGS) $(GLIB_LIBS) $(GPOD_LIBS) $(FFMPEG_LDFLAGS) -lavformat -lavutil -lavcodec

GPOD_OPT+=gpod-cp
gpod_cp_CFLAGS = $(FFMPEG_CFLAGS) $(AM_CFLAGS)
gpod_cp_CXXFLAGS = $(gpod_cp_CPPFLAGS) $(FFMPEG_CFLAGS) $(AM_CXXFLAGS)
gpod_cp_SOURCES = gpod-cp.cc
gpod_cp_LDADD = -Llib -lgpod-utils $(AM_LDFLAGS) $(GPOD_LIBS) $(GLIB_LIBS) $(FFMPEG_LIBS) -lavformat -lavutil -lavcodec -lswresample


GPOD_OPT+=gpod-extract
gpod_extract_CFLAGS = $(FFMPEG_CFLAGS) $(AM_CFLAGS)
gpod_extract_SOURCES = gpod-extract.c
gpod_extract_LDADD = -Llib -lgpod-utils $(AM_LDFLAGS) $(GPOD_LIBS) $(GLIB_LIBS) $(FFMPEG_LIBS) -lavformat -lavutil -lavcodec -lswresample

GPOD_OPT+=gpod-verify
gpod_verify_CFLAGS = $(FFMPEG_CFLAGS) $(AM_CFLAGS)
gpod_verify_SOURCES = gpod-verify.c
gpod_verify_LDADD =  -Llib -lgpod-utils $(AM_LDFLAGS) $(GPOD_LIBS) $(GLIB_LIBS) $(FFMPEG_LIBS) -lavformat -lavutil -lavcodec -lswresample


test_ff_xcode_CFLAGS = $(FFMPEG_CFLAGS) $(AM_CFLAGS) -DGPOD_FF_STANDALONE
test_ff_xcode_SOURCES = test-ff-xcode.c
test_ff_xcode_LDADD = -Llib -lgpod-utils $(AM_LDFLAGS) $(GLIB_LIBS) $(FFMPEG_LIBS) -lavformat -lavutil -lavcodec -lswresample

test_init_ipod_SOURCES = test-init-ipod.c 
test_init_ipod_LDADD = $(AM_LDFLAGS) $(GPOD_LIBS) $(GLIB_LIBS)

test_gpool_SOURCES = test-gpool.c 
test_gpool_LDADD = $(GLIB_LIBS)
