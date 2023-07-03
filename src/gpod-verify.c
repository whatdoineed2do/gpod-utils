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
 */

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <getopt.h>

#include <glib.h>
#include <gmodule.h>
#include <glib/gstdio.h>

#include <gpod/itdb.h>

#include "gpod-utils.h"
#include "gpod-ffmpeg.h"


#define GPOD_MODE_LS  1<<0
#define GPOD_MODE_DB  1<<1
#define GPOD_MODE_FS  1<<2
#define GPOD_MODE_CKSUM  1<<3
#define GPOD_MODE_CKSUM_REGEN  1<<4


static gint  _track_path_cmp(gconstpointer x_, gconstpointer y_)
{
    return strcmp((const char*)x_, (const char*)y_);
}

static Itdb_Track*  _track(const char* file_, char** err_, Itdb_IpodGeneration idevice_, bool sanitize_)
{
    struct gpod_ff_media_info  mi;
    gpod_ff_media_info_init(&mi);

    if (gpod_ff_scan(&mi, file_, idevice_, err_) < 0) {
	if (!mi.has_audio) {
            if (*err_) {
                const char*  err = "no audio - ";
                char*  tmp = (char*)malloc(strlen(*err_) + strlen(err) +1);
                sprintf(tmp, "%s%s", err, *err_);
                free(*err_);
                *err_ = tmp;
            }
            else {
                *err_ = strdup("no audio");
            }
	}
        gpod_ff_media_info_free(&mi);
        return NULL;
    }

    Itdb_Track*  track = gpod_ff_meta_to_track(&mi, 0, sanitize_);

    gpod_ff_media_info_free(&mi);

    gpod_store_cksum(track, file_);
    return track;
}


struct Stats {
    unsigned  ttl;
    size_t  rm_bytes;
    size_t  add_bytes;
    size_t  orphan_bytes;
    guint   cksum_time;
} stats = { 0, 0, 0, 0, 0 };


struct _cksum_args {
    const char*  resolved_path;
    Itdb_Track*  track;
};

struct _cksum_pool_args {
    const char* mountpoint;
    Itdb_iTunesDB*  itdb;

    unsigned short  sync_limit;

    struct Stats*  stats;
    uint32_t*  checksumed;
    guint  ttl;
    GMutex  lck;
};


static void  _cksum_thread(gpointer args_, gpointer pool_args_)
{
    Itdb_Track*  track = args_;
    struct _cksum_pool_args*  pool_args = pool_args_;

    char resolved_path[PATH_MAX] = { 0 };
    sprintf(resolved_path, "%s%s", pool_args->mountpoint, track->ipod_path);

    const guint  existing = gpod_saved_cksum(track);

    const guint  then = g_get_monotonic_time();
    gpod_store_cksum(track, resolved_path);
    const guint  now = g_get_monotonic_time();
    if (existing > 0 && existing != gpod_saved_cksum(track)) {
	g_print("checksumed id=%5ld path=%s -> %lld (updating from %lld)\n", track->id, resolved_path, gpod_saved_cksum(track), existing);
    }
    g_debug("checksumed %s -> %ld  %lld\n", resolved_path, track->id, gpod_saved_cksum(track));

    g_mutex_lock(&pool_args->lck);
    {
	pool_args->stats->cksum_time += now - then;

	if ((++*(pool_args->checksumed))%pool_args->sync_limit == 0) {
	    GError *error = NULL;
	    g_print("checksumed %d / %d (possible)\n", *(pool_args->checksumed), pool_args->ttl);
	    itdb_write(pool_args->itdb, &error);

	    if (error) {
		g_printerr("failed to write iPod database - %s\n", error->message ? error->message : "<unknown error>");
		g_error_free (error);
	    }
	}
    }
    g_mutex_unlock(&pool_args->lck);
}

static void  _cksum_q(Itdb_Track* track_, GThreadPool* cksum_tp_, const unsigned mode_)
{
    if ( (mode_ & GPOD_MODE_CKSUM && gpod_saved_cksum(track_) == 0) || mode_ & GPOD_MODE_CKSUM_REGEN) {
	g_thread_pool_push(cksum_tp_, (void*)track_, NULL);
    }
}


void  _usage(char* argv0_)
{
    char *basename = g_path_get_basename (argv0_);
    g_print ("%s\n", PACKAGE_STRING);
    g_print ("usage: %s -M <dir ipod mount> OPTIONS\n"
             "\n"
             "    validates the integrity of the iTunesDB (entries in iTunesDB compared to filessystem)\n"
             "    will [CLEAN] db of entries that don't have filesystem entries and optionally add/remove\n"
             "    files on filesystem but not in db\n"
             "\n"
             "    -M  --mount-point  <dir>   location of iPod data, as directory mount point or\n"
             "                               as a iTunesDB file  \n"
             "    -a  --add                  [ADD]   sync iTunesDB as files on device\n"
             "                               all files on device will have entry to db\n"
             "    -d  --delete               [REMVE] sync files iTunesDB as files on device\n"
             "                               all db entries must have corresponding file on device\n"
             "                               db entries with no files are removed\n"
	     "    -c  --checksum-missing     generate missing cksums for all files on device\n"
	     "    -C  --checksum-regen       regenerate cksums for all files on device\n"
	     "    -T  --checksum-threads     max threads used for generating cksums\n"
	     "    -n  --checksum-snyc  <n>   sync after N cksums\n"
	     "    -S  --sanitize             disable text sanitization; chars like ’ to '\n"
             , basename);
    g_free (basename);
    exit(-1);
}

int main (int argc, char *argv[])
{
    GError *error = NULL;
    Itdb_iTunesDB*  itdb = NULL;
    Itdb_Device*  itdev = NULL;
    int  ret = 0;

    struct {
        const char*  itdb_path;
        unsigned  mode;
	bool  sanitize;
	unsigned short  threads;
	unsigned short  sync_limit;
    } opts = { NULL, 0, true, 4, 100 };


    const struct option  long_opts[] = {
	{ "mount-point", 	1, 0, 'M' },

	{ "add", 		0, 0, 'a' },
	{ "delete",		0, 0, 'd' },
	{ "checksum-missing",	0, 0, 'c' },
	{ "checksum-regen",	0, 0, 'C' },
	{ "checksum-threads",	1, 0, 'T' },
	{ "checksum-sync",	1, 0, 'n' },
	{ "santize", 		2, 0, 'S' },
	{ "help", 		0, 0, 'h' },
	{ 0, 0, 0, 0 }
    };
    char  opt_args[1+ sizeof(long_opts)*2] = { 0 };
    {
	char*  og = opt_args;
	const struct option* op = long_opts;
	while (op->name) {
	    *og++ = op->val;
	    if (op->has_arg != no_argument) {
		*og++ = ':';
	    }
	    ++op;
	}
    }


    int  c;
    while ( (c=getopt_long(argc, argv, opt_args, long_opts, NULL)) != -1)
    {
        switch (c) {
            case 'M':  opts.itdb_path = optarg;  break;
            case 'a':  opts.mode |= GPOD_MODE_FS;  break;
            case 'd':  opts.mode |= GPOD_MODE_DB;  break;
	    case 'c':  opts.mode |= GPOD_MODE_CKSUM; break;
	    case 'C':  opts.mode |= GPOD_MODE_CKSUM_REGEN; break;
	    case 'n':  opts.sync_limit = atol(optarg); break;
	    case 'T': 
	    {
		const unsigned short  t = opts.threads;
		opts.threads = (unsigned short)atoi(optarg);
		if (opts.threads > t) {
		    opts.threads = t;
		}
	    } break;

            case 'S':
            {
                opts.sanitize = false;
                if (optarg) {
                    if      (toupper(*optarg) == 'Y')  opts.sanitize = true;
                    else if (toupper(*optarg) == 'N')  opts.sanitize = false;
                }
            } break;

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

    if (itdb == NULL) {
        g_print("failed to open iTunesDB via %s\n", opts.itdb_path);
        return -1;
    }

    if (mountpoint[strlen(mountpoint)-1] != '/') {
        strcat(mountpoint, "/");
    }

    Itdb_Playlist*  mpl = itdb_playlist_mpl(itdb);
    const uint32_t  dbcount = g_list_length(mpl->members);

    gchar*  musicdir = itdb_get_music_dir(mountpoint);
    gchar*  p = musicdir + strlen(musicdir);
    while (p > musicdir && *p == '/') {
        *p-- = '\0';
    }

    GSList*  files = NULL;
    gpod_walk_dir(musicdir, &files);
    const uint32_t  fscount = g_slist_length(files);

    g_free(musicdir);
    musicdir = NULL;

    const Itdb_IpodInfo*  ipodinfo = itdb_device_get_ipod_info(itdev);
    const bool  supported = gpod_write_supported(ipodinfo);

    g_print("validating tracks from iPod %s %s, currently %u/%u db/filesystem tracks%s\n",
             itdb_info_get_ipod_generation_string(ipodinfo->ipod_generation),
             ipodinfo->model_number,
             dbcount, fscount, supported ? "" : " - DB updates NOT supported");

    uint32_t  removed = 0;
    uint32_t  added = 0;
    uint32_t  orphaned = 0;
    uint32_t  checksumed = 0;
    char  path[PATH_MAX] = { 0 };
    strcpy(path, mountpoint);
    char*  pbase = path+strlen(mountpoint)-1;
    Itdb_Track*  track;

    /* initial cleanup for stuff that in db and not on fs
     */
    GHashTable*  hash = g_hash_table_new(g_str_hash, g_str_equal);
    GSList*  clean = NULL;
    for (GList* it=mpl->members; it!=NULL; it=it->next)
    {
	track = (Itdb_Track *)it->data;
	itdb_filename_ipod2fs(track->ipod_path);
	if (!g_hash_table_insert(hash, track->ipod_path, track))
	{
	    /* theres already something with the same file name -- this is wrong
	     * drop it
	     */
	}
	else
	{
	    strcpy(pbase, track->ipod_path);

	    if (g_slist_find_custom(files, path, _track_path_cmp) != NULL) {
		// all good - in db and on fs
		continue;
	    }
	    // in db, not on fs.. can't recover this, drop from db
	}
	clean = g_slist_append(clean, track);
    }

    for (GSList* i=clean; i!=NULL; i=i->next)
    {
	track = (Itdb_Track*)i->data;

        g_print("CLEAN  %s -> { id=%d title='%s' artist='%s' album='%s' time_added=%u }\n", 
                track->ipod_path, track->id, track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "", track->time_added);

        if (!supported) {
            continue;
        }
        ++removed;

	for (GList* j=itdb->playlists; j!=NULL; j=j->next) {
	    itdb_playlist_remove_track((Itdb_Playlist*)j->data, track);
	}
        itdb_track_remove(track);
        stats.rm_bytes += track->size;
    }
    if (clean) {
	g_slist_free(clean);
	clean = NULL;
    }

    for (GSList* i=files ; i!=NULL; i=i->next)
    {
        const char*  resolved_path = i->data;

        if ( (track = g_hash_table_lookup(hash, resolved_path+strlen(mountpoint)-1)) ) {
	    // name on fs is in (hash of paths) db
            continue;
        }

        track = NULL;

        /* not in db, on fs .. what to do
         */
        char*  err = NULL;
        track = _track(resolved_path, &err, ipodinfo->ipod_generation, opts.sanitize);
        if (!track) {
            free(err);
            ret = -1;
            continue;
        }

        if (opts.mode & GPOD_MODE_FS)
        {
            // trust filesystem, add back to db
            // no xcode, if its a supported file, add it back to db
            track->ipod_path = g_strdup(resolved_path+strlen(mountpoint)-1);

            g_print("ADD   %s -> { title='%s' artist='%s' album='%s' }\n", 
                    track->ipod_path, track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "");
            stats.add_bytes += track->size;

            if (!supported) {
                continue;
            }
            ++added;
            itdb_filename_fs2ipod(track->ipod_path);
            itdb_track_add(itdb, track, -1);
            itdb_playlist_add_track(mpl, track, -1);
        }
        else
        {
            if (opts.mode & GPOD_MODE_DB)
            {
                // trust the db, remove from fs
                g_print("REMVE  %s -> { title='%s' artist='%s' album='%s' }\n", 
                        ++removed, resolved_path, track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "");

                g_unlink(resolved_path);
                stats.rm_bytes += track->size;
            }
            else
            {
                g_print("ORPHN  %s -> { title='%s' artist='%s' album='%s' }\n", 
                        resolved_path, track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "");
                ++orphaned;
                stats.orphan_bytes += track->size;
            }

            itdb_track_free(track);
            track = NULL;
        }
    }

    g_slist_free_full(files, g_free);
    files = NULL;
    g_hash_table_destroy(hash);
    hash = NULL;

    if (supported && (added || removed)) {
        g_print("sync'ing iPod ...\n");
        itdb_write(itdb, &error);

        if (error) {
            g_printerr("failed to write iPod database - %s\n", error->message ? error->message : "<unknown error>");
             g_error_free (error);
             ret = 1;
        }
    }


    // work on tracks that are now in master playlist in case of re-adds above
    const guint  cksum_then = g_get_monotonic_time();
    if ( supported && (opts.mode & GPOD_MODE_CKSUM | opts.mode & GPOD_MODE_CKSUM_REGEN) )
    {
	// reset everything
	mpl = itdb_playlist_mpl(itdb);
	GList*  i = mpl->members;

	struct _cksum_pool_args  pool_args = { 
	    .mountpoint = mountpoint,
	    .itdb = itdb,
	    .sync_limit = opts.sync_limit,
	    .stats = &stats,
	    .checksumed = &checksumed,
	    .ttl = -1
	};
	g_mutex_init(&pool_args.lck);

	GThreadPool*  cksum_tp = g_thread_pool_new((GFunc)_cksum_thread, (void*)&pool_args, opts.threads, TRUE, NULL);

	if (argc == optind)
	{
	    pool_args.ttl = g_list_length(i);
	    for (; i!=NULL; i=i->next) {
		_cksum_q(i->data, cksum_tp, opts.mode);
	    }
	}
	else
	{
	    pool_args.ttl = argc - optind;
	    GTree*  tree = itdb_track_id_tree_create(itdb);
	    while (optind < argc)
	    {
		const unsigned  track_id = atol(argv[optind++]);
		track = itdb_track_id_tree_by_id(tree, track_id);
		if (track) {
		    _cksum_q(track, cksum_tp, opts.mode);
		}
	    }
	    itdb_track_id_tree_destroy(tree);
	}

	g_thread_pool_free(cksum_tp, FALSE, TRUE);
	g_mutex_clear(&pool_args.lck);

	if (checksumed) {
	    g_print("sync'ing iPod ...\n");
	    itdb_write(itdb, &error);

	    if (error) {
		g_printerr("failed to write iPod database - %s\n", error->message ? error->message : "<unknown error>");
		 g_error_free (error);
		 ret = 1;
	    }
	}
    }
    const guint  cksum_now = g_get_monotonic_time();


    char  add_size[32] = { 0 };
    gpod_bytes_to_human(add_size, sizeof(add_size), stats.add_bytes, true);
    char  rm_size[32] = { 0 };
    gpod_bytes_to_human(rm_size, sizeof(rm_size), stats.rm_bytes, true);
    char  orphan_size[32] = { 0 };
    gpod_bytes_to_human(orphan_size, sizeof(orphan_size), stats.orphan_bytes, true);

    char cksum_duration[32] = { 0 };
    gpod_duration(cksum_duration, 0, stats.cksum_time);

    char cksum_elapsed[32] = { 0 };
    gpod_duration(cksum_elapsed, cksum_then, cksum_now);


    g_print("iPod total tracks=%u  orphaned %u %s, removed %u %s, added %u %s, checksumed %u (total %s, elapsed %s)\n", g_list_length(itdb_playlist_mpl(itdb)->members), orphaned, orphan_size, removed, rm_size, added, add_size, checksumed, cksum_duration, cksum_elapsed);

    if (itdev) {
        itdb_device_free(itdev);
    }
    itdb_free(itdb);

    return ret;
}
