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
#include <stdarg.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gpod/itdb.h>

#include "gpod-utils.h"

int gpod_signal = 0;
bool gpod_stop = false;

static void  _sighandler(const int sig_)
{
    gpod_signal = sig_;
    gpod_stop = true;
}

int  gpod_rm_init()
{
    struct sigaction  act, oact;

    memset(&act,  0, sizeof(struct sigaction));
    memset(&oact, 0, sizeof(struct sigaction));

    act.sa_handler = _sighandler;
    sigemptyset(&act.sa_mask);

    const int   sigs[] = { SIGINT, SIGTERM, SIGHUP, -1 };
    const int*  p = sigs;
    while (*p != -1) {
        sigaddset(&act.sa_mask, *p++);
    }
}


static bool  _remove_confirm(bool interactv_, const char* fmt_, ...)
{
    va_list  args;
    va_start(args, fmt_);
    g_vprintf(fmt_, args);
    va_end(args);

    if (interactv_)
    {
        g_print("  [y/N]: ");

	/* remember getchar() on the term will also get the '\n' on the next 
	 * getchar() call - cheaply flush the buffer afterwards
	 * */
        char  c = getchar();
	bool  more = false;
	while (c != '\n' && getchar() != '\n') {
	    more = true;
	}
	if (more) {
	    c = 'n';
	}

	switch (c)
	{
	    case 'Y':
	    case 'y':
		return true;
		break;

	    case 'N':
	    case 'n':
	    default:
		return false;
		break;
	}
    }
    else {
        g_print("\n");
    }
    return true;

}

static void  _remove_track(bool interactv_, Itdb_iTunesDB* itdb_, Itdb_Track* track_, uint64_t* removed_, size_t* bytes_, const unsigned current_, const unsigned N_)
{
    struct tm  tm;
    char dt[20];

    gmtime_r(&(track_->time_added), &tm);
    strftime(dt, 20, "%Y-%m-%dT%H:%M:%S", &tm);

    if (!_remove_confirm(interactv_, 
            "[%3u/%u]  %s -> { id=%d title='%s' artist='%s' album='%s' time_added=%d (%s)", 
	    current_, N_,
	    track_->ipod_path, track_->id, track_->title ? track_->title : "", track_->artist ? track_->artist : "", track_->album ? track_->album : "", track_->time_added, dt)) {
	return;
    }


    // remove from all playlists
    GList*  i;
    for (i = itdb_->playlists; i!=NULL; i=i->next) {
        Itdb_Playlist*  playlist = (Itdb_Playlist *)i->data;
        itdb_playlist_remove_track(playlist, track_);
    }

    char  path[PATH_MAX];
    sprintf(path, "%s/%s", itdb_get_mountpoint(track_->itdb), track_->ipod_path);
    g_unlink(path);
    *bytes_ += track_->size;
  
    // remove (and free mem)
    itdb_track_remove(track_);
    ++(*removed_);
}

static void  _remove_playlist(bool interactv_, Itdb_iTunesDB* itdb_, Itdb_Playlist* playlist_, uint64_t* removed_, const unsigned current_, const unsigned N_)
{
    struct tm  tm;
    char dt[20];

    gmtime_r(&(playlist_->timestamp), &tm);
    strftime(dt, 20, "%Y-%m-%dT%H:%M:%S", &tm);

    if (!_remove_confirm(interactv_, 
            "[%3u/%u]  %s -> { count=%d time_added=%d (%s)", 
	    current_, N_,
	    playlist_->name, g_list_length(playlist_->members), playlist_->timestamp, dt)) {
	return;
    }

    // remove (and free mem)
    itdb_playlist_remove(playlist_);
    ++(*removed_);
}

static void  autoclean(bool interactv_, Itdb_iTunesDB* itdb_, uint64_t* removed_, size_t* bytes_)
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
	    const Itdb_Track*  h = (Itdb_Track*)l->data;
	    const guint  h_cksum = gpod_saved_cksum(h);

            for (GSList* j=l->next; j!=NULL; j=j->next) {
		Itdb_Track*  track = (Itdb_Track*)j->data;
		const guint  t_cksum = gpod_saved_cksum(track);

		if (h->size == track->size &&
		     ( (h_cksum == 0 || t_cksum == 0) ||
			(t_cksum == h_cksum) )
		   )
		{
		    _remove_track(interactv_, itdb_, track, removed_, bytes_, *removed_+1, 0);
		}
		else {
                    g_print("[---]  ignoring, cksum/size mismatch %s -> { id=%d cksum=%ld size=%ld title='%s' artist='%s' album='%s' } vs %s -> { id=%d cksum=%ld size=%ld title='%s' artist='%s' album='%s' }\n",
			    h->ipod_path, h->id, h_cksum, h->size, h->title ? h->title : "", h->artist ? h->artist : "", h->album ? h->album : "",
			    track->ipod_path, track->id, t_cksum, track->size, track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "");

		}
            }
        }
    }

    gpod_track_fs_hash_destroy(&tfsh);
}

void  _usage(const char* argv0_)
{
    char *basename = g_path_get_basename (argv0_);
    g_print ("usage: %s  -M <dir ipod mount>  [ -a ] [ -i ] [-P] [ <file | ipod id> ... ]\n"
	     "\n"
	     "    Removes specified file(s) the iPod/iTunesDB\n"
	     "    -M <iPod dir>   location of iPod data as directoy mount point\n"
	     "    -a              automatically purge duplicate files based on cksum leaving the track added first.  Must be first arg\n"
	     "    -i              interactive/confirmation for delete\n"
	     "    -P              removing playlists rather than files (accepts names only)\n"
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
	bool  interactv;
        bool  playlists;
    } opts = { NULL, false, false, false };


    int c;
    while ( (c=getopt(argc, argv, "M:aiPh")) != EOF) {
        switch (c) {
            case 'M':  opts.itdb_path = optarg;  break;
            case 'a':  opts.autoclean = true;  break;
            case 'i':  opts.interactv = true;  break;

            case 'P':  opts.playlists = true;  break;

            case 'h':
            default:
                _usage(argv[0]);
        }
    }


    if (opts.itdb_path == NULL) {
        _usage(argv[0]);
    }

    if ( !(optind < argc) && !opts.autoclean && !opts.playlists) {
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

    struct {
	uint32_t  music;
	uint32_t  video;
	uint32_t  other;
	size_t    bytes;
    } stats = { 0, 0, 0, 0 };


    // unlink track and remove from all playlists

    Itdb_Playlist*  mpl = itdb_playlist_mpl(itdb);
    const uint32_t  current = g_list_length(mpl->members);

    char  path[PATH_MAX];
    Itdb_Track*  track;
    GList*  it;

    bool  first = true;
    uint64_t  removed = 0;
    uint64_t  requested = 0;


    const Itdb_IpodInfo*  ipodinfo = itdb_device_get_ipod_info(itdev);
    const bool  supported = gpod_write_supported(ipodinfo);
    
    g_print("removing tracks from iPod %s %s, currently %u tracks%s\n",
                itdb_info_get_ipod_generation_string(ipodinfo->ipod_generation),
                ipodinfo->model_number,
                current, supported ? "" : " - device NOT supportd");

    if (!supported) {
        ret = -1;
        goto cleanup;
    }

    if (opts.autoclean) {
        autoclean(opts.interactv, itdb, &removed, &stats.bytes);
    }
    char**  p = &argv[optind];
    const unsigned  N = argv+argc - p;

    GTree*  tree = NULL;
    GHashTable*  hash = NULL;
    while (!gpod_stop && *p)
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
	    if (opts.playlists)
            {
                Itdb_Playlist*  playlist = itdb_playlist_by_name(itdb, (gchar*)arg);
                if (playlist && !itdb_playlist_is_mpl(playlist)) {
                    _remove_playlist(opts.interactv, itdb, playlist, &removed, requested, N);
                }
                else {
                    g_print("[%3u/%u]  %s -> { Not on iPod/iTunesDB }\n", requested, N, arg);
                }
                continue;
            }

            const char*  ipod_path = arg;

	    sprintf(path, "%s%s/%s", (*arg == '/' ? "" : "/"), mountpoint, ipod_path);


	    // find the corresponding track
	    track = NULL;
	    if (hash == NULL)
	    {
		hash = g_hash_table_new(g_str_hash, g_str_equal);
		for (it = mpl->members; it != NULL; it = it->next)
		{
		    track = (Itdb_Track *)it->data;
		    itdb_filename_ipod2fs(track->ipod_path);
		    g_hash_table_insert(hash, track->ipod_path, track);
		}
	    }
	    track = g_hash_table_lookup(hash, ipod_path);

	    if (track) {
		_remove_track(opts.interactv, itdb, track, &removed, &stats.bytes, requested, N);
	    }
	    else
	    {
		if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
		    g_printerr("[%3u/%u]  %s -> { Not on iPod/iTunesDB }\n", requested, N, ipod_path);
		}
		else {
		    if (_remove_confirm(opts.interactv,
				        "[%3u/%u]  %s -> { Not on iTunesDB }", requested, N, ipod_path)) {
			struct stat  st;
			stat(path, &st);
			g_unlink(path);
			++removed;
			stats.bytes += st.st_size;
		    }
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
		_remove_track(opts.interactv, itdb, track, &removed, &stats.bytes, requested, N);
	    }
	    else {
		g_print("[%3u/%u]  %s -> { Not on iPod/iTunesDB }\n", requested, N, arg);
	    }
	}
    }
    if (tree) {
	itdb_track_id_tree_destroy(tree);
	tree = NULL;
    }
    if (hash) {
	g_hash_table_destroy(hash);
	hash = NULL;
    }

    if (removed)
    {
        g_print("sync'ing iPod ...\n");
        itdb_write(itdb, &error);

        if (error) {
            g_printerr("failed to write iPod database, %d files physically removed but NOT from database - %s\n", requested, error->message ? error->message : "<unknown error>");
             g_error_free (error);
             ret = 1;
        }
    }

    char  stats_size[128] = { 0 };
    if (stats.bytes) {
        gpod_bytes_to_human(stats_size, sizeof(stats_size), stats.bytes, true);
    }


    g_print("iPod total tracks=%u  removed %u/%u items %s\n", g_list_length(itdb_playlist_mpl(itdb)->members), ret < 0 ? 0 : removed, requested, stats_size);


cleanup:
    itdb_device_free(itdev);
    itdb_free (itdb);

    return ret;
}
