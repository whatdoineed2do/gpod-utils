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

#include <glib.h>
#include <gmodule.h>
#include <glib/gstdio.h>

#include <gpod/itdb.h>

#include "gpod-utils.h"
#include "gpod-ffmpeg.h"


#define GPOD_MODE_LS  1<<0
#define GPOD_MODE_DB  1<<1
#define GPOD_MODE_FS  1<<2


static gint  _track_path_cmp(gconstpointer x_, gconstpointer y_)
{
    return strcmp((const char*)x_, (const char*)y_);
}

static Itdb_Track*  _track(const char* file_, char** err_, bool sanitize_)
{
    struct gpod_ff_media_info  mi;
    gpod_ff_media_info_init(&mi);

    if (gpod_ff_scan(&mi, file_, err_) < 0) {
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

    Itdb_Track*  track = gpod_ff_meta_to_track(&mi, sanitize_);

    gpod_ff_media_info_free(&mi);
    return track;
}


void  _usage(char* argv0_)
{
    char *basename = g_path_get_basename (argv0_);
    g_print ("usage: %s -M <dir ipod mount> [ -a | -d ] [ -S ]\n"
             "\n"
             "    validates the integrity of the iTunesDB (entries in iTunesDB compared to filessystem)\n"
             "    will [CLEAN] db of entries that don't have filesystem entries and optionally add/remove\n"
             "    files on filesystem but not in db\n"
             "\n"
             "    -M <dir>   location of iPod data, as directory mount point or\n"
             "               as a iTunesDB file  \n"
             "    -a         [ADD]   sync iTunesDB as files on device\n"
             "               all files on device will have entry to db\n"
             "    -d         [REMVE] sync files iTunesDB as files on device\n"
             "               all db entries must have corresponding file on device\n"
             "               db entries with no files are removed\n"
	     "    -S         disable text sanitization; chars like ’ to '\n"
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
    } opts = { NULL, 0, true };

    int  c;
    while ( (c=getopt(argc, argv, "M:daS")) != EOF)
    {
        switch (c) {
            case 'M':  opts.itdb_path = optarg;  break;
            case 'a':  opts.mode |= GPOD_MODE_FS;  break;
            case 'd':  opts.mode |= GPOD_MODE_DB;  break;

            case 'S':  opts.sanitize = false;  break;

            case 'h':
            default:
                _usage(argv[0]);
        }
    }

    if (opts.itdb_path == NULL) {
        _usage(argv[0]);
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

    char  mountpoint[PATH_MAX] = { 0 };
    strcpy(mountpoint, opts.itdb_path);
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
    g_print("validating tracks from iPod %s %s, currently %u/%u db/filesystem tracks\n",
             itdb_info_get_ipod_generation_string(ipodinfo->ipod_generation),
             ipodinfo->model_number,
             dbcount, fscount);

    struct Stats {
	unsigned  ttl;
	size_t  rm_bytes;
	size_t  add_bytes;
	size_t  orphan_bytes;
    } stats = { 0, 0, 0, 0 };


    bool  first = true;
    uint32_t  removed = 0;
    uint32_t  added = 0;
    uint32_t  orphaned = 0;
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
	if (!g_hash_table_insert(hash, track->ipod_path, track->ipod_path))
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

        g_print("CLEAN [%3u]  %s -> { id=%d title='%s' artist='%s' album='%s' time_added=%u }\n", 
                ++removed, track->ipod_path, track->id, track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "", track->time_added);

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


    for (GSList* i=files; i!=NULL; i=i->next)
    {
        const char*  resolved_path = i->data;

        if (g_hash_table_contains(hash, resolved_path+strlen(mountpoint)-1)) {
	    // name on fs is in (hash of paths) db
            continue;
        }

        track = NULL;

        /* not in db, on fs .. what to do
         */
        char*  err = NULL;
        track = _track(resolved_path, &err, opts.sanitize);
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

            ++added;
            g_print("ADD   [%3u]  %s -> { title='%s' artist='%s' album='%s' }\n", 
                    added, track->ipod_path, track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "");
            stats.add_bytes += track->size;

            itdb_filename_fs2ipod(track->ipod_path);
            itdb_track_add(itdb, track, -1);
            itdb_playlist_add_track(mpl, track, -1);
        }
        else
        {
            if (opts.mode & GPOD_MODE_DB)
            {
                // trust the db, remove from fs
                g_print("REMVE [%3u]  %s -> { title='%s' artist='%s' album='%s' }\n", 
                        ++removed, resolved_path, track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "");

                g_unlink(resolved_path);
                stats.rm_bytes += track->size;
            }
            else
            {
                g_print("ORPHN [%3u]  %s -> { title='%s' artist='%s' album='%s' }\n", 
                        ++orphaned, resolved_path, track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "");
            }

            itdb_track_free(track);
            track = NULL;
        }
    }
    g_slist_free_full(files, g_free);
    files = NULL;
    g_hash_table_destroy(hash);
    hash = NULL;


    if (added || removed) {
        g_print("sync'ing iPod ...\n");
        itdb_write(itdb, &error);

        if (error) {
            g_printerr("failed to write iPod database - %s\n", error->message ? error->message : "<unknown error>");
             g_error_free (error);
             ret = 1;
        }
    }

    char  add_size[32] = { 0 };
    gpod_bytes_to_human(add_size, sizeof(add_size), stats.add_bytes, true);
    char  rm_size[32] = { 0 };
    gpod_bytes_to_human(rm_size, sizeof(rm_size), stats.rm_bytes, true);
    char  orphan_size[32] = { 0 };
    gpod_bytes_to_human(orphan_size, sizeof(orphan_size), stats.orphan_bytes, true);

    g_print("iPod total tracks=%u  orphaned %u %s, removed %u %s, added %u %s\n", g_list_length(itdb_playlist_mpl(itdb)->members), orphaned, orphan_size, removed, rm_size, added, add_size);


    if (itdev) {
        itdb_device_free(itdev);
    }
    itdb_free(itdb);

    return ret;
}
