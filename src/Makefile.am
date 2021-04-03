ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

AM_CFLAGS = $(GPOD_CFLAGS) $(GLIB_CFLAGS) $(JSONC_CFLAGS) $(SQLITE3_CFLAGS)
AM_CXXFLAGS = $(TAGLIB_CFLAGS) $(AM_CFLAGS)


bin_PROGRAMS = gpod-ls gpod-rm $(GPOD_CP)

# using git version instead of the am values in config.h
gpod_ls_CPPFLAGS = 
gpod_ls_LDFLAGS =  $(GPOD_LIBS) $(GLIB_LIBS) $(JSONC_LIBS) $(SQLITE3_LIBS) -lz
gpod_ls_SOURCES = \
	gpod-ls.c

gpod_rm_LDFLAGS =  $(GPOD_LIBS) $(GLIB_LIBS) $(JSONC_LIBS) $(SQLITE3_LIBS) -lz
gpod_rm_SOURCES = gpod-rm.c

if HAVE_TAGLIB
GPOD_CP=gpod-cp
gpod_cp_SOURCES = gpod-cp.cc
gpod_cp_LDADD = $(TAGLIB_LIBS) $(GPOD_LIBS) $(GLIB_LIBS)
else
GPOD_CP=
endif
