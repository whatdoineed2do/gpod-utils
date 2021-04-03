ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

AM_CFLAGS = $(GPOD_CFLAGS) $(GLIB_CFLAGS) $(JSONC_CFLAGS) $(SQLITE3_CFLAGS)

bin_PROGRAMS = gpod-ls gpod-rm

# using git version instead of the am values in config.h
gpod_ls_CPPFLAGS = 
gpod_ls_LDFLAGS =  $(GPOD_LIBS) $(GLIB_LIBS) $(JSONC_LIBS) $(SQLITE3_LIBS) -lz
gpod_ls_SOURCES = 
	gpod-ls.c

gpod_rm_LDFLAGS =  $(GPOD_LIBS) $(GLIB_LIBS) $(JSONC_LIBS) $(SQLITE3_LIBS) -lz
gpod_rm_SOURCES = gpod-rm.c
