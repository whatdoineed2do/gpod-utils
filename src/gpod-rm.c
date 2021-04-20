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

#include <unistd.h>
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


static void  _remove_track(Itdb_iTunesDB* itdb_, Itdb_Track* track_, uint64_t* removed_, const unsigned current_, const unsigned N_)
{
    struct tm  tm;
    char dt[20];

    gmtime_r(&(track_->time_added), &tm);
    strftime(dt, 20, "%Y-%m-%dT%H:%M:%S", &tm);

    g_print("[%3u/%u]  %s -> { id=%d title='%s' artist='%s' album='%s' time_added=%d (%s)\n", 
	    current_, N_,
	    track_->ipod_path, track_->id, track_->title ? track_->title : "", track_->artist ? track_->artist : "", track_->album ? track_->album : "", track_->time_added, dt);


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
                _remove_track(itdb_, (Itdb_Track*)j->data, removed_, *removed_+1, 0);
            }
        }
    }

    gpod_track_fs_hash_destroy(&tfsh);
}

void  _usage(const char* argv0_)
{
    char *basename = g_path_get_basename (argv0_);
    g_print ("usage: %s  -M <dir ipod mount>  [ -a ] [ <file | ipod id> ... ]\n"
	     "\n"
	     "    Removes specified file(s) the iPod/iTunesDB\n"
	     "    -M <iPod dir>   location of iPod data as directoy mount point\n"
	     "    -a              automatically purge duplicate files based on cksum leaving the track added first.  Must be first arg\n"
	     "\n"
	     "    Filenames are provided relative to the iPod mountpoint; ie\n"
	     "      /iPod_Control/Music/F08/NCQQ.mp3\n\n"
	     "\n"
	     "    Filenames <-> tracks cab be determined using gpod-ls\n",
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

    struct {
        const char*  itdb_path;
	bool  autoclean;
    } opts = { NULL, false };


    int c;
    while ( (c=getopt(argc, argv, "M:ah")) != EOF) {
        switch (c) {
            case 'M':  opts.itdb_path = optarg;  break;
            case 'a':  opts.autoclean = true;  break;

            case 'h':
            default:
                _usage(argv[0]);
        }
    }


    if (opts.itdb_path == NULL) {
        _usage(argv[0]);
    }

    if ( !(optind < argc) && !opts.autoclean) {
        g_printerr("no inputs\n");
        _usage(argv[0]);
    }


    gpod_setlocale();

    char  mountpoint[PATH_MAX];
    strcpy(mountpoint, argv[1]);


    Itdb_Device*  itdev = NULL;

    if (g_file_test(opts.itdb_path, G_FILE_TEST_IS_DIR)) {
        itdb = itdb_parse (opts.itdb_path, &error);
	itdev = itdb_device_new();
	itdb_device_set_mountpoint(itdev, opts.itdb_path);
    }

    if (error)
    {
        if (error->message) {
            g_printerr("failed to prase iTunesDB via %s - %s\n", opts.itdb_path, error->message);
        }
        g_error_free (error);
        error = NULL;
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


    const Itdb_IpodInfo*  ipodinfo = itdb_device_get_ipod_info(itdev);
    g_print("removing tracks from iPod %s %s, currently %u tracks%s\n",
                itdb_info_get_ipod_generation_string(ipodinfo->ipod_generation),
                ipodinfo->model_number,
                current);

    if (opts.autoclean) {
        autoclean(itdb, &removed);
    }
    char**  p = &argv[optind];
    const unsigned  N = argv+argc - p;

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
		_remove_track(itdb, track, &removed, requested, N);
	    }
	    else
	    {
		if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
		    g_printerr("[%3u/%u]  %s -> { Not on iPod/iTunesDB }\n", requested, N, ipod_path);
		}
		else {
		    g_print("[%3u/%u]  %s -> { Not on iTunesDB }\n", requested, N, ipod_path);
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
		_remove_track(itdb, track, &removed, requested, N);
	    }
	    else {
		g_print("[%3u/%u]  %s -> { Not on iPod/iTunesDB }\n", requested, N, arg);
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

    itdb_device_free(itdev);
    itdb_free (itdb);

    return ret;
}
