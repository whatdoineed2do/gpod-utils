/*
 *  Copyright (C) 2021 Ray <whatdoineed2do @ gmail com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * iTunes and iPod are trademarks of Apple
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>

#include <glib.h>
#include <gmodule.h>
#include <gpod/itdb.h>

#include "gpod-db.h"
#include "gpod-utils.h"


void  _usage(char* argv0_)
{
    char *basename = g_path_get_basename (argv0_);
    g_print ("usage: %s -M <dir ipod mount> | <file iTunesDB> [-n album_limit]\n"
             "\n"
             "    creates set of playlists of recently added albums\n"
             "\n"
             "    Playlists of: 0wk (most recent update), 1wk, 1months, 3months, 6months, 12months\n"
             "\n"
             "    -M <dir | file>   location of iPod data, as directory mount point or\n"
             "                      as a iTunesDB file  \n"
             "\n"
            , basename);
    g_free (basename);
    exit(-1);
}


int
main (int argc, char *argv[])
{
    GError *error = NULL;
    Itdb_iTunesDB*  itdb = NULL;
    Itdb_Device*  itdev = NULL;
    struct {
        const char*  itdb_path;
        const char*  db_path;
	unsigned  album_limit;
    } opts = { NULL, NULL, 50 };

    int  ret = 0;

    int  c;
    while ( (c=getopt(argc, argv, "M:Q:n:h")) != EOF)
    {
        switch (c) {
            case 'M':  opts.itdb_path = optarg;  break;
            case 'Q':  opts.db_path = optarg;  break;
            case 'n':  opts.album_limit = atol(optarg);  break;

            case 'h':
            default:
                _usage(argv[0]);
        }
    }

    char  mountpoint[PATH_MAX] = { 0 };
    if (opts.itdb_path == NULL) {
        opts.itdb_path = gpod_default_mountpoint(mountpoint, sizeof(mountpoint));
    }
    else {
	strcpy(mountpoint, opts.itdb_path);
    }

    gpod_setlocale();

    const char*  argtype = "unknown";
    if (g_file_test(opts.itdb_path, G_FILE_TEST_IS_DIR)) {
        itdb = itdb_parse (opts.itdb_path, &error);
        argtype = "directroy";
        itdev = itdb_device_new();
        itdb_device_set_mountpoint(itdev, opts.itdb_path);
    }
    else {
        if (g_file_test(opts.itdb_path, G_FILE_TEST_EXISTS)) {
            itdb = itdb_parse_file(opts.itdb_path, &error);
            argtype = "file";

            // the Device info is /mnt/iPod_Control/Device - if we've been given a db 
            // location /mnt/iPod_Control/iTunes/iTunesDB we can figure this out

            char*  dmp;
            if ( (dmp = strstr(mountpoint, "iPod_Control/"))) {
                itdev = itdb_device_new();
                *dmp = '\0';
                itdb_device_set_mountpoint(itdev, mountpoint);
            }
        }
    }

    if (error)
    {
        if (error->message) {
            g_printerr("failed to prase iTunesDB via (%s) %s - %s\n", argtype, opts.itdb_path, error->message);
        }
        g_error_free (error);
        error = NULL;
        return -1;
    }

    if (itdb == NULL) {
        g_print("failed to open iTunesDB via (%s) %s\n", argtype, opts.itdb_path);
        return -1;
    }

    unsigned  recent_pl, recent_tracks;

    gpod_playlist_recent(&recent_pl, &recent_tracks, itdb, opts.album_limit, 0);

    if (recent_tracks > 0)
    {
        g_print("sync'ing iPod ...\n");
        itdb_write(itdb, &error);

        if (error) {
            g_printerr("failed to write playlists iPod database - %s\n", error->message ? error->message : "<unknown error>");
             g_error_free (error);
             ret = 1;
        }
    }

    g_print("iPod playlists=%u (limited to %d) with tracks=%u\n", recent_pl, opts.album_limit, recent_tracks);


cleanup:
    itdb_device_free(itdev);
    itdb_free (itdb);

    return ret;
}
