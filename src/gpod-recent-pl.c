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

struct gpod_album_key {
    const char*  album;
    const char*  artist;
};

struct gpod_album {
    struct gpod_album_key  key;

    time_t  time_added;  // most recent as of tracks
    GSList*  tracks;     // refs to Itdb_Track objs we DONT own
};

struct gpod_album*  gpod_album_new(Itdb_Track* track_)
{
    struct gpod_album*  obj = (struct gpod_album*)g_malloc(sizeof(struct gpod_album));
    memset(obj, 0, sizeof(struct gpod_album));

    obj->key.album  = track_->album;
    obj->key.artist = track_->artist;

    return obj;
}

static void  dump_album(gpointer a_, gpointer na_)
{
    struct gpod_album*  album = (struct gpod_album*)a_;
    g_print("%u  %s - %s [ %u ]\n", album->time_added, album->key.album, album->key.artist, g_slist_length(album->tracks));
}


static void  gpod_album_free(struct gpod_album*  obj_)
{
    g_slist_free(obj_->tracks);
    g_free(obj_);
}


static gint album_cmp(gconstpointer a_, gconstpointer b_)
{
    struct gpod_album*  a = (struct gpod_album*)a_;
    struct gpod_album_key*  b = (struct gpod_album_key*)b_;

    if (a->key.album == NULL || b->album == NULL) {
	if (a->key.artist && b->artist && strcmp(a->key.artist, b->artist) == 0 ) {
	    return 0;
	}
	return 1;
    }

    return strcmp(a->key.album,  b->album)  == 0 ? 0 : 1;

#if 0
    if (a->key.artist == NULL && b->artist == NULL && a->key.album == NULL || b->album == NULL) {
	return 0;
    }
    if (a->key.artist == NULL || b->artist == NULL || a->key.album == NULL || b->artist == NULL) {
	return 1;
    }

    return strcmp(a->key.artist, b->artist) == 0 && 
           strcmp(a->key.album,  b->album)  == 0 ? 0 : 1;
#endif
}


static void  albums_index(gpointer track_, gpointer albums_)
{
    Itdb_Track*  track = (Itdb_Track*)track_;
    GSList**  albums = (GSList**)albums_;  // gpod_album

    if (track->mediatype != ITDB_MEDIATYPE_AUDIO) {
	return;
    }

    // find the album entry if already created.. if not create
    const struct gpod_album_key  key = { track->album, track->artist };
    GSList*  elem = g_slist_find_custom(*albums, &key, album_cmp);
    struct gpod_album*  album;
    if (elem) {
	album = elem->data;
    }
    else {
	album = gpod_album_new(track);
	*albums = g_slist_append(*albums, album);
    }

    // add the track and update album's time_added marker
    album->tracks = g_slist_append(album->tracks, track);
    if (track->time_added >= album->time_added) {
	album->time_added = track->time_added;
    }
}

static gint  album_time_added_sort(gconstpointer a_, gconstpointer b_)
{
    const struct gpod_album*  a = (const struct gpod_album*)a_;
    const struct gpod_album*  b = (const struct gpod_album*)b_;

    return a->time_added  < b->time_added ? 1 :
           a->time_added == b->time_added ? 0 : -1;
}

struct gpod_recent
{
    char* name;
    struct {
	GDateTime*  from;
	GDateTime*  to;
    } range;
    gint64  from;
    gint64  to;

    GSList*  tracks;  // ref to other Itdb_Track
};

static struct gpod_recent*  gpod_recent_new()
{
    struct gpod_recent*  obj = (struct gpod_recent*)g_malloc(sizeof( struct gpod_recent));

    memset(obj, 0, sizeof( struct gpod_recent));
    return obj;
}

static void  gpod_recent_free(struct gpod_recent* obj_)
{
    if (obj_->range.from) g_date_time_unref(obj_->range.from);
    if (obj_->range.to)   g_date_time_unref(obj_->range.to);

    g_free(obj_->name);
    if (obj_->tracks)  g_slist_free(obj_->tracks);
    g_free(obj_);
}

static void  dump_track(gpointer data_, gpointer user_data_)
{
    Itdb_Track*  obj = (Itdb_Track*)data_;
    g_print(" %u %s/%s/%s ", obj->id, obj->album, obj->artist, obj->title);
}

static void  dump_recent_elem(gpointer data_, gpointer user_data_)
{
    struct gpod_recent*  obj = (struct gpod_recent*)data_;
    gchar*  from = g_date_time_format_iso8601(obj->range.from);
    gchar*  to = g_date_time_format_iso8601(obj->range.to);
    g_print("%s #%.4d (%s %lu .. %s %lu) [", obj->name, g_slist_length(obj->tracks), from, obj->from, to, obj->to);
    g_free(from);
    g_free(to);
    g_slist_foreach(obj->tracks, dump_track, NULL);
    g_print("]\n");
}

struct recent_create_pl_args {
    Itdb_iTunesDB*  itdb;
    struct {
	unsigned  pl;
	unsigned  tracks;
    } stats;
};

static void gpod_recent_create_playlists(gpointer recent_, gpointer args_)
{
    struct gpod_recent*  recent = (struct gpod_recent*)recent_;
    struct recent_create_pl_args*  args = (struct recent_create_pl_args*)args_;

    Itdb_Playlist*  pl = itdb_playlist_by_name(args->itdb, recent->name);
    if (pl) {
	itdb_playlist_remove(pl);
    }

    if (g_slist_length(recent->tracks) == 0) {
	return;
    }
    ++(args->stats.pl);

    pl = itdb_playlist_new(recent->name, false);
    itdb_playlist_add(args->itdb, pl, -1);

    for (GSList* p=recent->tracks; p!=NULL; p=p->next) {
	itdb_playlist_add_track(pl, p->data, -1);
	++(args->stats.tracks);
    }
}


struct gpod_dmy {
    gint d, m, y;
};

static void  dump_recent_dt(gpointer data, gpointer user_data)
{
    struct gpod_recent*  obj = (struct gpod_recent*)data;

    gchar*  from = g_date_time_format(obj->range.from, "%Y-%m-%d %H:%M:%S");
    gchar*  to = g_date_time_format(obj->range.to, "%Y-%m-%d %H:%M:%S");

    g_print("%.10s  %s to %s\n", obj->name, from, to);

    g_free(from);
    g_free(to);
}


GSList*  gpod_recents_new(gint64  now_)
{
    GSList*  l = NULL;

    GDateTime*  tmp = now_ == 0 ? g_date_time_new_now_utc() : g_date_time_new_from_unix_utc(now_);
    struct gpod_dmy  dmy;
    g_date_time_get_ymd(tmp, &dmy.y, &dmy.m, &dmy.d);
    g_date_time_unref(tmp);

    struct gpod_recent*  when;
    const struct gpod_recent*  when_last;
    const struct gpod_recent*  when_now;

    when = gpod_recent_new();
    when->name = g_strdup("Recent: 0d");
    when->range.to   = g_date_time_new_utc(dmy.y, dmy.m, dmy.d, 23, 59, 59);
    when->range.from = g_date_time_new_utc(dmy.y, dmy.m, dmy.d,  0,  0,  0);
    when->from = g_date_time_to_unix(when->range.from);
    when->to   = g_date_time_to_unix(when->range.to);
    l = g_slist_append(l, when);
    when_last = when;
    when_now = when;

    when = gpod_recent_new();
    when->name = g_strdup("Recent: last wk");
    when->range.to = g_date_time_add_days(when_last->range.to, -1);
    g_date_time_get_ymd(when->range.to, &dmy.y, &dmy.m, &dmy.d);
    tmp = g_date_time_new_utc(dmy.y, dmy.m, dmy.d, 0, 0, 0);
    when->range.from   = g_date_time_add_weeks(tmp, -1);
    when->from = g_date_time_to_unix(when->range.from);
    when->to   = g_date_time_to_unix(when->range.to);
    l = g_slist_append(l, when);
    when_last = when;
    g_date_time_unref(tmp);

    when = gpod_recent_new();
    when->name = g_strdup("Recent: last mth");
    g_date_time_get_ymd(when_last->range.from, &dmy.y, &dmy.m, &dmy.d);
    tmp = g_date_time_new_utc(dmy.y, dmy.m, dmy.d, 23, 59, 59);
    when->range.to   = g_date_time_add_days(tmp, -1);
    when->range.from = g_date_time_add_months(when_now->range.from, -1);
    when->from = g_date_time_to_unix(when->range.from);
    when->to   = g_date_time_to_unix(when->range.to);
    l = g_slist_append(l, when);
    when_last = when;
    g_date_time_unref(tmp);

    when = gpod_recent_new();
    when->name = g_strdup("Recent: last 3mth");
    g_date_time_get_ymd(when_last->range.from, &dmy.y, &dmy.m, &dmy.d);
    tmp = g_date_time_new_utc(dmy.y, dmy.m, dmy.d, 23, 59, 59);
    when->range.to   = g_date_time_add_days(tmp, -1);
    when->range.from = g_date_time_add_months(when_now->range.from, -3);
    when->from = g_date_time_to_unix(when->range.from);
    when->to   = g_date_time_to_unix(when->range.to);
    l = g_slist_append(l, when);
    when_last = when;
    g_date_time_unref(tmp);

    when = gpod_recent_new();
    when->name = g_strdup("Recent: last 6mth");
    g_date_time_get_ymd(when_last->range.from, &dmy.y, &dmy.m, &dmy.d);
    tmp = g_date_time_new_utc(dmy.y, dmy.m, dmy.d, 23, 59, 59);
    when->range.to   = g_date_time_add_days(tmp, -1);
    when->range.from = g_date_time_add_months(when_now->range.from, -6);
    when->from = g_date_time_to_unix(when->range.from);
    when->to   = g_date_time_to_unix(when->range.to);
    l = g_slist_append(l, when);
    g_date_time_unref(tmp);

    return l;
}

static void  track_mostrecent(gpointer track_, gpointer when_)
{
    Itdb_Track*  track = (Itdb_Track*)track_;
    gint64*  when = (gint64*)when_;

    if (track->time_added > *when) {
	*when = track->time_added;
    }
}

void  gpod_playlist_recent(unsigned* playlists_, unsigned* tracks_, Itdb_iTunesDB* itdb_, unsigned album_limit_, gint64  when_)
{
    // get the last added track and use that for calcs
    if (when_ == 0) {
	g_list_foreach(itdb_playlist_mpl(itdb_)->members, track_mostrecent, &when_);
    }

    GSList*  recents = gpod_recents_new(when_);  // define the playlist of interest and time ranges
    GSList*  albums = NULL;

    g_list_foreach(itdb_playlist_mpl(itdb_)->members, albums_index, &albums);
    albums = g_slist_sort(albums, album_time_added_sort);  // newest/most recent upd album first
    
    //g_slist_foreach(albums, dump_album, NULL);
    //g_slist_foreach(recents, dump_recent_elem, NULL);

    // walk the sorted album list and generate the playlists
    unsigned  available = album_limit_;
    for (GSList* a=albums; a!=NULL; a=a->next)
    {
	struct gpod_album*  album = (struct gpod_album*)a->data;

	for (GSList* r=recents; r!=NULL; r=r->next)
	{
	    struct gpod_recent*  recent = (struct gpod_recent*)r->data;

	    if (album->time_added >= recent->from && album->time_added <= recent->to) {
		//recent->tracks = g_slist_concat(recent->tracks, album->tracks); // -- get stuck in g_slist_length(recent-tracks)
		for (GSList* cp=album->tracks; cp!=NULL; cp=cp->next) {
		    recent->tracks = g_slist_append(recent->tracks, cp->data);
		}

		--available;
		// g_print("%3u  adding to %s (%u tracks) -> album %s, %u tracks  %lu >= %lu ..  <= %lu\n", available, recent->name, g_slist_length(recent->tracks),  album->key.album, g_slist_length(album->tracks), album->time_added, recent->from, recent->to);
	    }

	    if (available == 0) {
		break;
	    }
	}
    }

    //g_slist_foreach(recents, dump_recent_elem, NULL);
    struct recent_create_pl_args  rcp_args = { 0 };
    rcp_args.itdb = itdb_;
    g_slist_foreach(recents, gpod_recent_create_playlists, &rcp_args);

    g_slist_free_full(albums, (GDestroyNotify)gpod_album_free);
    g_slist_free_full(recents, (GDestroyNotify)gpod_recent_free);

    *tracks_ = rcp_args.stats.tracks;
    *playlists_ = rcp_args.stats.pl;
}


void  _usage(char* argv0_)
{
    char *basename = g_path_get_basename (argv0_);
    g_print ("usage: %s -M <dir ipod mount> | <file iTunesDB> [-n album_limit]<\n"
             "\n"
             "    creates set of playlists of recently added albums\n"
             "\n"
             "    Playlists of: 0wk (today), 1wk, 4wks, 12wks\n"
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

    if (opts.itdb_path == NULL) {
        _usage(argv[0]);
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
            char mountpoint[PATH_MAX];
            strcpy(mountpoint, opts.itdb_path);

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

    g_print("iPod playlists=%u with tracks=%u\n", recent_pl, recent_tracks);


cleanup:
    itdb_device_free(itdev);
    itdb_free (itdb);

    return ret;
}
