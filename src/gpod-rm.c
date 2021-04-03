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

int
main (int argc, char *argv[])
{
    GError *error = NULL;
    Itdb_iTunesDB*  itdb = NULL;
    int  ret = 0;

    if (argc < 3)
    {
        char *basename = g_path_get_basename (argv[0]);
        g_print ("usage: %s [ <dir ipod mount> | <file iTunesDB>]  <file0.mp3> [<file1.mp3> ...]\n\n%s%s%s%s", basename,
                 "    This utility removes specified file the iPod/iTunesDB\n",
                 "    Filenames are provided relative to the iPod mountpoint; ie\n",
                 "      /iPod_Control/Music/F08/NCQQ.mp3\n\n",
                 "    Filenames <-> tracks cab be determined using gpod-ls\n");
        g_free (basename);
        exit(-1);
    }

    _setlocale();

    char  mountpoint[PATH_MAX];
    strcpy(mountpoint, argv[1]);

    const char*  argtype = "unknown";
    if (g_file_test(argv[1], G_FILE_TEST_IS_DIR)) {
        itdb = itdb_parse (argv[1], &error);
        argtype = "directroy";
    }
    else {
        if (g_file_test(argv[1], G_FILE_TEST_EXISTS)) {
            itdb = itdb_parse_file(argv[1], &error);
            argtype = "file";
        }
        // location /mnt/iPod_Control/iTunes/iTunesDB we can figure this out
        char*  mp;
        if ( (mp = strstr(mountpoint, "iPod_Control/"))) {
            *mp = '\0';
        }
        else {
            return -1;
        }
    }

    if (error)
    {
        if (error->message) {
            g_printerr("failed to prase iTunesDB via (%s) %s - %s\n", argtype, argv[1], error->message);
        }
        g_error_free (error);
        error = NULL;
        return -1;
    }

    if (itdb == NULL) {
        g_print("failed to open iTunesDB via (%s) %s\n", argtype, argv[1]);
        return -1;
    }

    // unlink track and remove from all playlists

    Itdb_Playlist*  mpl = itdb_playlist_mpl(itdb);

    char  path[PATH_MAX];
    Itdb_Track*  track;
    Itdb_Track*  tmptrack;
    GList*  it;

    bool  first = true;
    uint64_t  removed = 0;
    uint64_t  requested = 0;
    struct tm  tm;
    char dt[20];

    char**  p = &argv[2];
    while (*p)
    {
        ++requested;
        const char*  ipod_path = *p++;

        sprintf(path, "%s/%s", mountpoint, ipod_path);

        if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
            g_printerr("Ignoring '%s' - No such file or directory\n", path);
            continue;
        }

        // find the corresponding track
        track = NULL;
        for (it = mpl->members; it != NULL; it = it->next)
        {
            tmptrack = (Itdb_Track *)it->data;
            if (first) {
                itdb_filename_ipod2fs(tmptrack->ipod_path);
            }

            if (strcmp(ipod_path, tmptrack->ipod_path) == 0) {
                track = tmptrack;
                break;
            }
        }
        first = false;

        if (track) {
            gmtime_r(&(track->time_added), &tm);
            strftime(dt, 20, "%Y-%m-%dT%H:%M:%S", &tm);

            g_print("%s -> { id=%d title='%s' artist='%s' album='%s' time_added=%d (%s)\n", ipod_path, track->id, track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "", track->time_added, dt);
        }
        else {
            g_printerr("%s -> { <on iPod filessystem, not in master> } - Ignoring\n", ipod_path);
            continue;
        }

        // remove from all playlists
        for (it = itdb->playlists; it != NULL; it = it->next) {
            Itdb_Playlist*  playlist = (Itdb_Playlist *)it->data;
            itdb_playlist_remove_track(playlist, track);
        }

        // remove (and free mem)
        itdb_track_remove(track);
        ++removed;
    }

    if (removed)
    {
        g_print("sync'ing iPod ... removing %d/%d\n", removed, requested);
        itdb_write(itdb, &error);

        if (error) {
            g_printerr("failed to write iPod database, %d files NOT removed - %s\n", requested, error->message ? error->message : "<unknown error>");
             g_error_free (error);
             ret = 1;
        }

    }
    else {
        g_printerr("failed to remove %d/%d\n", removed, requested);
    }
    itdb_free (itdb);

    return ret;
}
