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
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gpod/itdb.h>

#include "gpod-utils.h"


static void  _remove_track(Itdb_iTunesDB* itdb_, Itdb_Track* track_, uint64_t* removed_)
{
    struct tm  tm;
    char dt[20];

    gmtime_r(&(track_->time_added), &tm);
    strftime(dt, 20, "%Y-%m-%dT%H:%M:%S", &tm);

    g_print("%s -> { id=%d title='%s' artist='%s' album='%s' time_added=%d (%s)\n", track_->ipod_path, track_->id, track_->title ? track_->title : "", track_->artist ? track_->artist : "", track_->album ? track_->album : "", track_->time_added, dt);


    // remove from all playlists
    GList*  i;
    for (i = itdb_->playlists; i!=NULL; i=i->next) {
        Itdb_Playlist*  playlist = (Itdb_Playlist *)i->data;
        itdb_playlist_remove_track(playlist, track_);
    }

    char  path[PATH_MAX];
    sprintf(path, "%s/%s", itdb_get_mountpoint(track_->itdb), track_->ipod_path);
    g_unlink(path);
  
    // remove (and free mem)
    itdb_track_remove(track_);
    ++(*removed_);
}

static void  autoclean(Itdb_iTunesDB* itdb_, uint64_t* removed_)
{
    // cksum all files and remove the dupl, keeping the oldest

    struct gpod_track_fs_hash  tfsh;
    gpod_track_fs_hash_init(&tfsh, itdb_);


    GHashTable*  htbl = tfsh.tbl;

    gpointer  key;
    gpointer  value;

    GHashTableIter  i;
    g_hash_table_iter_init (&i, htbl);
    while (g_hash_table_iter_next(&i, &key, &value))
    {
        GSList*  l = (GSList*)value;
        if (g_slist_length(l) > 1)
        {
            for (GSList* j=l->next; j!=NULL; j=j->next) {
                _remove_track(itdb_, (Itdb_Track*)j->data, removed_);
            }
        }
    }

    gpod_track_fs_hash_destroy(&tfsh);
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
        g_print ("usage: %s [ <dir ipod mount> | <file iTunesDB>]  [--autoclean | <file0.mp3> [<file1.mp3> ...]\n"
                 "\n"
                 "    This utility removes specified file the iPod/iTunesDB\n"
                 "    Filenames are provided relative to the iPod mountpoint; ie\n"
                 "      /iPod_Control/Music/F08/NCQQ.mp3\n\n"
                 "    Filenames <-> tracks cab be determined using gpod-ls\n",
                 basename);
        g_free (basename);
        exit(-1);
    }

    gpod_setlocale();

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
    const uint32_t  current = g_list_length(mpl->members);

    char  path[PATH_MAX];
    Itdb_Track*  track;
    Itdb_Track*  tmptrack;
    GList*  it;

    bool  first = true;
    uint64_t  removed = 0;
    uint64_t  requested = 0;

    char**  p = &argv[2];
    if (strcmp(*p, "--autoclean") == 0) {
        autoclean(itdb, &removed);
        ++p;
    }

    GTree*  tree = NULL;
    while (*p)
    {
        ++requested;
        const char*  arg = *p++;

	// is it a path or an id
	const char*  d = arg;
	while (*d && isdigit(*d)) {
	    ++d;
	}

	if (*d)
	{
	    const char*  ipod_path = arg;

	    sprintf(path, "%s%s/%s", (*arg == '/' ? "" : "/"), mountpoint, ipod_path);


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
		_remove_track(itdb, track, &removed);
	    }
	    else
	    {
		if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
		    g_printerr("%s -> { Not on iPod/iTunesDB }\n", ipod_path);
		}
		else {
		    g_print("%s -> { Not on iTunesDB }\n", ipod_path);
		    g_unlink(path);
		    ++removed;
		}
	    }
        }
	else
	{
	    if (tree == NULL) {
		tree = itdb_track_id_tree_create(itdb);
	    }

	    track = itdb_track_id_tree_by_id(tree, atol(arg));
	    if (track) {
		_remove_track(itdb, track, &removed);
	    }
	    else {
		g_print("%s -> { Not on iPod/iTunesDB }\n", arg);
	    }
	}
    }
    if (tree) {
	itdb_track_id_tree_destroy(tree);
    }
    tree = NULL;

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
    g_print("iPod total tracks=%u (originally=%u)\n", g_list_length(itdb_playlist_mpl(itdb)->members), current);

    itdb_free (itdb);

    return ret;
}
