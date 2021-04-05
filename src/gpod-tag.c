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
 * This product is not supported/written/published by Apple!
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>

#include <glib.h>
#include <gpod/itdb.h>


static const char*  _setlocale()
{
    const char*  attempts[] = {
        "en_US.UTF-8",
        "en_GB.UTF-8",
        "C.utf8",
        "C.UTF-8",  // debian specific version
        NULL
    };

    const char*  l;
    const char**  p = attempts;
    while (*p) {
        if ( (l = setlocale(LC_ALL, *p))) {
          break;
        }
        ++p;
    }
    return l;
}


struct gpod_opts {
    const char*  artist;
    const char*  album;
    const char*  title;
    const char*  genre;
    int  year;
    int  track;
};

struct gpod_arg {
    union {
        const char*  ipod_path;
        uint64_t  id;
    } u;
};

void  _usage(const char* argv_)
{
    char *basename = g_path_get_basename (argv_);
    g_print ("usage: %s [ -M <dir ipod mount> | -D <file iTunesDB>]  [-t <title>] [-a <artist>] [-A <album>] [-g <genre>] [-T <track>] [-y <year>]  <file id/ipod path> [ ...]\n\n"
             "    update meta tags for files as known in iPod/iTunesDB\n"
             "    use gpod-ls to determine ipod path/id\n",
             basename);
    g_free (basename);
    exit(-1);
}


int
main (int argc, char *argv[])
{
    GError *error = NULL;
    Itdb_iTunesDB*  itdb = NULL;
    int  ret = 0;

    struct gpod_opts  opts;
    memset(&opts, 0, sizeof(opts));
    opts.year = -1;
    opts.track = -1;

    const char*  mpt = NULL;
    const char*  db  = NULL;

    int c;
    while ( (c=getopt(argc, argv, "M:D:a:t:A:g:T:y:h")) != EOF) {
        switch (c) {
            case 'M':  mpt = optarg;  break;
            case 'D':  db = optarg;  break;

            case 'a':  opts.artist = optarg;  break;
            case 'A':  opts.album  = optarg;  break;
            case 't':  opts.title  = optarg;  break;
            case 'g':  opts.genre  = optarg;  break;
            case 'y':  opts.year   = atol(optarg);  break;
            case 'T':  opts.track  = atol(optarg);  break;

            case 'h':
            default:
                _usage(argv[0]);
        }
    }


    if (mpt == NULL && db == NULL && opts.title == NULL && opts.artist == NULL && opts.album == NULL && opts.genre == NULL && opts.year < 0 && opts.track < 0) {
        g_printerr("invalid opts\n");
        _usage(argv[0]);
    }

    if ( !(optind < argc) ) {
        g_printerr("no inputs\n");
        _usage(argv[0]);
    }


    _setlocale();

    char  mountpoint[PATH_MAX];

    const char*  argtype = "unknown";
    if (mpt) {
        strcpy(mountpoint, mpt);
        if (g_file_test(mpt, G_FILE_TEST_IS_DIR)) {
            itdb = itdb_parse (mpt, &error);
            argtype = "directroy";
        }
    }
    else if (db) {
        if (g_file_test(db, G_FILE_TEST_EXISTS)) {
            itdb = itdb_parse_file(db, &error);
            argtype = "file";
        }
        // location /mnt/iPod_Control/iTunes/iTunesDB we can figure this out
        char*  mp;
        if ( (mp = strstr(mountpoint, "iPod_Control/"))) {
            *mp = '\0';
        }
        else {
            itdb_free (itdb);
        }
    }

    if (error)
    {
        if (error->message) {
            g_printerr("failed to prase iTunesDB via %s - %s\n", argtype, error->message);
        }
        g_error_free (error);
        error = NULL;
        return -1;
    }

    if (itdb == NULL) {
        g_print("failed to open iTunesDB via (%s)\n", argtype);
        _usage(argv[0]);
    }


    g_print("updating iPod track meta { title='%s' artist='%s' album='%s' genre='%s' track=%d year=%d } ...\n", 
            opts.title ? opts.title : "<nul>",
            opts.artist ? opts.artist : "<nul>",
            opts.album ? opts.album : "<nul>",
            opts.genre ? opts.genre : "<nul>",
            opts.track,
            opts.year);


    Itdb_Playlist*  mpl = itdb_playlist_mpl(itdb);
    const uint32_t  current = g_list_length(mpl->members);

    Itdb_Track*  track;
    Itdb_Track*  tmptrack;
    GList*  it;

    uint64_t  updated = 0;
    uint64_t  requested = 0;
    unsigned  N = argc - optind;

    GTree*  idtree = itdb_track_id_tree_create(itdb);

    struct tm  tm;
    char  dt[21];
    char  path[PATH_MAX];

    const time_t  now = time(NULL);
    struct gpod_arg  arg;
    const char*  p = NULL;
    int  i = optind;
    while (i < argc)
    {
        ++requested;
        p = argv[i++];

        g_print("[%3u/%u]  %s ", requested, N, p);

        track = NULL;

        if (strncmp(p, "/iPod_Control/", 14) == 0)
        {
            // this is expensive!!!
 
            arg.u.ipod_path = p;

            sprintf(path, "%s/%s", mountpoint, p);

            if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
                g_printerr("{ } No such file or directory '%s'\n", path);
                continue;
            }

            for (it = mpl->members; it != NULL; it = it->next)
            {
                tmptrack = (Itdb_Track *)it->data;
                itdb_filename_ipod2fs(tmptrack->ipod_path);

                if (strcmp(p, tmptrack->ipod_path) == 0) {
                    track = tmptrack;
                    break;
                }
            }
         }
        else
        {
            arg.u.id = (uint32_t)atoll(p);
            
            if ((track = itdb_track_id_tree_by_id(idtree, arg.u.id)) ) {
                itdb_filename_ipod2fs(track->ipod_path);
            }
        }

        if (!track) {
            g_print("{ } - No such track\n");
            continue;
        }

        gmtime_r(&(track->time_modified), &tm);
        strftime(dt, 20, "%Y-%m-%dT%H:%M:%S", &tm);

        g_print("{ id=%u ipod_path='%s' { title='%s' artist='%s' album='%s' genre='%s' track=%u year=%u time_modified=%s } }\n",
               track->id,
               track->ipod_path,
               track->title ? track->title : "",
               track->artist ? track->artist : "",
               track->album ? track->album : "",
               track->genre ? track->genre : "",
               track->track_nr, track->year,
               dt);
 
#define TRACK_ASSIGN(x,y) { if (x) { free(x); } if (y) { x = strdup(y); } else { x = NULL; } }

        TRACK_ASSIGN(track->title, opts.title);
        TRACK_ASSIGN(track->artist, opts.artist);
        TRACK_ASSIGN(track->album, opts.artist);
        TRACK_ASSIGN(track->genre, opts.genre);

        if (opts.track >= 0) track->track_nr = opts.track; 
        if (opts.year >= 0) track->year = opts.year;

        ++updated;
    }
    itdb_track_id_tree_destroy(idtree);

    if (updated)
    {
        g_print("sync'ing iPod ... updated %d/%d\n", updated, requested);
        itdb_write(itdb, &error);

        if (error) {
            g_printerr("failed to write iPod database, %d files NOT updated - %s\n", requested, error->message ? error->message : "<unknown error>");
             g_error_free (error);
             ret = 1;
        }
        g_print("updated iPod, total tracks=%u\n", g_list_length(itdb_playlist_mpl(itdb)->members));
    }
    else {
        g_printerr("failed to update\n");
    }
    itdb_free (itdb);

    return ret;
}
