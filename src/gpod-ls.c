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
 *
 * based on libgpod/tests/tests-ls.c
 * Copyright (C) 2002-2003 Jorg Schuler <jcsjcs at users.sourceforge.net>
 * Copyright (C) 2006 Christophe Fergeau  <teuf@gnome.org>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <getopt.h>

#include <glib.h>
#include <gmodule.h>
#include <json.h>
#include <gpod/itdb.h>
#ifdef HAVE_SQLITE3
#include <sqlite3.h>
#else
typedef void* sqlite3;
#endif

#include "gpod-db.h"
#include "gpod-utils.h"


#ifdef HAVE_SQLITE3
bool  db_create(sqlite3 *hdl_)
{
    int  ret;
    char*  err = NULL;

    const char**  p = db_init_queries;
    while (*p) {
        if ((ret = sqlite3_exec(hdl_, *p, NULL, NULL, &err)) != SQLITE_OK) {
            g_printerr ("failed to create db objects - %s\n", err);
            sqlite3_free(err);
            return false;
        }
        ++p;
    }
    return true;
}

bool  db_add_track(sqlite3 *hdl_, const Itdb_Track* track_)
{
#define QADD_TMPL \
  "INSERT INTO tracks (" \
    "id, ipod_path, mediatype," \
    "title, artist, album, genre, filetype, composer, grouping, albumartist, sort_artist, sort_title, sort_album, sort_albumartist, sort_composer," \
    "size, tracklen, cd_nr, cds, track_nr, tracks, bitrate, samplerate, year, time_added, time_modified, time_played, rating, playcount, playcount2, recent_playcount," \
    "checksum, " \
    "unk126, unk132, unk144, unk148, unk152, unk179, unk180, unk196, unk204, unk220, unk224, unk228, unk232, unk236, unk240, unk244, unk252" \
    "    )" \
    "  VALUES (%d, '%q', %d," \
    "          %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q," \
    "          %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d," \
    "          %u," \
    "          %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u" \
    "         );"

  char*  err = NULL;
  char*  query = sqlite3_mprintf(QADD_TMPL, 
                                track_->id, track_->ipod_path, track_->mediatype,
                                track_->title, track_->artist, track_->album, track_->genre, track_->filetype, track_->composer, track_->grouping, track_->albumartist, track_->sort_artist, track_->sort_title, track_->sort_album, track_->sort_albumartist, track_->sort_composer, 
                                track_->size, track_->tracklen, track_->cd_nr, track_->cds, track_->track_nr, track_->tracks, track_->bitrate, track_->samplerate, track_->year, track_->time_added, track_->time_modified, track_->time_played, track_->rating, track_->playcount, track_->playcount2, track_->recent_playcount,
				gpod_saved_cksum(track_),
				track_->unk126, track_->unk132, track_->unk144, track_->unk148, track_->unk152, track_->unk179, track_->unk180, track_->unk196, track_->unk204, track_->unk220, track_->unk224, track_->unk228, track_->unk232, track_->unk236, track_->unk240, track_->unk244, track_->unk252
				);

#undef QADD_TMPL

  sqlite3_stmt*  stmt = NULL;

  int  ret;
  int  n = 5;
  while (n--) 
  {
      if ( (ret = sqlite3_prepare_v2(hdl_, query, -1, &stmt, NULL)) != SQLITE_OK) {
          sqlite3_free(query);
          g_printerr("failed to prepare DB query - %s (%s)\n", sqlite3_errmsg(hdl_), sqlite3_errstr(ret));
          return false;
      }

      while (ret = sqlite3_step(stmt) == SQLITE_ROW) {
          ;   // keep going...
      }
      sqlite3_finalize(stmt);

      if (ret != SQLITE_SCHEMA)
        break;
    }

    sqlite3_free(err);
    sqlite3_free(query);
    if (ret != SQLITE_OK) {
        g_printerr("failed to insert data - %s (%s)\n", sqlite3_errmsg(hdl_), sqlite3_errstr(ret));
        return false;
    }
    return true;
}
#endif

static void
json_object_add_string(json_object* obj_, const char* tag_, const char* data_)
{
    json_object_object_add(obj_, tag_, data_ ? json_object_new_string(data_) : NULL);
}

static void
json_object_add_int(json_object* obj_, const char* tag_, const int data_)
{
    json_object_object_add(obj_, tag_, json_object_new_int(data_));
}

static void
json_object_add_uint(json_object* obj_, const char* tag_, const uint64_t data_)
{
    json_object_object_add(obj_, tag_, json_object_new_uint64(data_));
}

static void
json_object_add_boolean(json_object* obj_, const char* tag_, const bool data_)
{
    json_object_object_add(obj_, tag_, json_object_new_boolean(data_));
}


typedef struct _TrkHash {
    uint64_t  high;    // file size/len/artist/title/album
    uint64_t  med;     // artist/title
    uint64_t  low;     // file size/len
} TrkHash;

static TrkHash*  hash_trk_init(const Itdb_Track* track_, bool cksum_)
{
   TrkHash*  o = malloc(sizeof(TrkHash)); 
   memset(o, 0, sizeof(TrkHash));

   o->low = track_->size + track_->tracklen + track_->bitrate + track_->samplerate;
   o->med = (track_->artist ? g_str_hash(track_->artist) : 0)  + (track_->title ? g_str_hash(track_->title) : 0);

   if (!cksum_) {
       o->high = o->low + o->med + (track_->album ? g_str_hash(track_->album) : 0);
   }
   else {
       const guint  hash = gpod_saved_cksum(track_);
       o->high = hash > 0 ? hash : gpod_hash(track_);
   }

   return o;
}

static void  hash_trk_free(gpointer p_)
{
    free(p_);
}

static guint  hask_trk_low(gconstpointer v_)
{ return ((const TrkHash*)v_)->low; }

static gboolean  hask_trk_low_equals(gconstpointer v0_, gconstpointer v1_)
{
    return ((const TrkHash*)v0_)->low == ((const TrkHash*)v1_)->low;
}

static guint  hask_trk_med(gconstpointer v_)
{ return ((const TrkHash*)v_)->med; }

static gboolean  hask_trk_med_equals(gconstpointer v0_, gconstpointer v1_)
{
    return ((const TrkHash*)v0_)->med == ((const TrkHash*)v1_)->med;
}

static guint  hask_trk_high(gconstpointer v_)
{ return ((const TrkHash*)v_)->high; }

static gboolean  hask_trk_high_equals(gconstpointer v0_, gconstpointer v1_)
{
    return ((const TrkHash*)v0_)->high == ((const TrkHash*)v1_)->high;
}

typedef struct _TrkHashes {
    GHashTable*  high;
    GHashTable*  med;
    GHashTable*  low;
} TrkHashTbl;

static void  hash_tbl_init(TrkHashTbl* o_)
{
    o_->high = g_hash_table_new(hask_trk_high, hask_trk_high_equals);
    o_->med = g_hash_table_new(hask_trk_med, hask_trk_med_equals);
    o_->low = g_hash_table_new(hask_trk_low, hask_trk_low_equals);
}

static void  hash_tbl_val_destroy(gpointer k_, gpointer v_, gpointer d_)
{
    g_slist_free(v_);
}

static void  hash_tbl_free(TrkHashTbl* o_)
{
    g_hash_table_foreach(o_->high, hash_tbl_val_destroy, NULL);
    g_hash_table_destroy(o_->high);
    g_hash_table_foreach(o_->med, hash_tbl_val_destroy, NULL);
    g_hash_table_destroy(o_->med);
    g_hash_table_foreach(o_->low, hash_tbl_val_destroy, NULL);
    g_hash_table_destroy(o_->low);
    memset(o_, 0, sizeof(TrkHashTbl));
}

static void  hash_tbl_json(gpointer k_, gpointer v_, gpointer d_)
{
    GSList*  l = (GSList*)v_;
    const int  count = g_slist_length(l);
    if (count < 2) {
        return;
    }

    json_object*  jarray = (json_object*)d_;

    json_object*  jobj = json_object_new_object();
    json_object*  jtracks = json_object_new_array();

    json_object_add_int(jobj, "size", ((Itdb_Track*)l->data)->size);
    json_object_add_int(jobj, "tracklen", ((Itdb_Track*)l->data)->tracklen);
    json_object_add_int(jobj, "bitrate", ((Itdb_Track*)l->data)->bitrate);
    json_object_add_int(jobj, "samplerate", ((Itdb_Track*)l->data)->samplerate);

    struct tm  tm;
    char dt[20];
    for (GSList* i=l; i!= NULL; i=i->next) {
        json_object*  jtrack = json_object_new_object();

        json_object_add_int(jtrack, "id", ((Itdb_Track*)i->data)->id);
        json_object_add_string(jtrack, "ipod_path", ((Itdb_Track*)i->data)->ipod_path);
        json_object_add_int(jtrack, "mediatype", ((Itdb_Track*)i->data)->mediatype);

        json_object_add_string(jtrack, "title", ((Itdb_Track*)i->data)->title);
        json_object_add_string(jtrack, "artist", ((Itdb_Track*)i->data)->artist);
        json_object_add_string(jtrack, "album", ((Itdb_Track*)i->data)->album);
        json_object_add_string(jtrack, "genre", ((Itdb_Track*)i->data)->genre);

        gmtime_r(&(((Itdb_Track*)i->data)->time_added), &tm);
        strftime(dt, 20, "%Y-%m-%dT%H:%M:%S", &tm);
        json_object_add_string(jtrack, "date_added", dt);

        json_object_add_uint(jtrack, "size", ((Itdb_Track*)i->data)->size);
        json_object_add_uint(jtrack, "checksum", gpod_saved_cksum((Itdb_Track*)i->data));

        json_object_array_add(jtracks, jtrack);
    }
    json_object_add_int(jobj, "count", count-1);
    json_object_object_add(jobj, "items", jtracks);

    json_object_array_add(jarray, jobj);
}

static json_object*
_track (Itdb_Track *track, bool verbose_, sqlite3* hdl_, TrkHashTbl* htbl_, bool cksum_)
{
    static unsigned  inscnt = 0;
    json_object*  jobj = json_object_new_object();

    itdb_filename_ipod2fs(track->ipod_path);
    if (verbose_)
    {
        json_object_add_int(jobj, "id", track->id);
        json_object_add_string(jobj, "ipod_path", track->ipod_path);
        json_object_add_int(jobj, "mediatype", track->mediatype);
        json_object_add_string(jobj, "title", track->title);
        json_object_add_string(jobj, "artist", track->artist);
        json_object_add_string(jobj, "album", track->album);
        json_object_add_string(jobj, "genre", track->genre);
        json_object_add_string(jobj, "filetype", track->filetype);
        json_object_add_string(jobj, "composer", track->composer);
        json_object_add_string(jobj, "grouping", track->grouping);
        json_object_add_string(jobj, "albumartist", track->albumartist);
        json_object_add_string(jobj, "sort_artist", track->sort_artist);
        json_object_add_string(jobj, "sort_title", track->sort_title);
        json_object_add_string(jobj, "sort_album", track->sort_album);
        json_object_add_string(jobj, "sort_albumartist", track->sort_albumartist);
        json_object_add_string(jobj, "sort_composer", track->sort_composer);
        json_object_add_int(jobj, "size", track->size);
        json_object_add_int(jobj, "tracklen", track->tracklen);
        json_object_add_int(jobj, "cd_nr", track->cd_nr);
        json_object_add_int(jobj, "cds", track->cds);
        json_object_add_int(jobj, "track_nr", track->track_nr);
        json_object_add_int(jobj, "tracks", track->tracks);
        json_object_add_int(jobj, "bitrate", track->bitrate);
        json_object_add_int(jobj, "samplerate", track->samplerate);
        json_object_add_int(jobj, "year", track->year);
        json_object_add_int(jobj, "time_added", track->time_added);
        json_object_add_int(jobj, "time_modified", track->time_modified);
        json_object_add_int(jobj, "time_played", track->time_played);
        json_object_add_int(jobj, "rating", track->rating);
        json_object_add_int(jobj, "playcount", track->playcount);
        json_object_add_int(jobj, "playcount2", track->playcount2);
        json_object_add_int(jobj, "recent_playcount", track->recent_playcount);
        json_object_add_uint(jobj, "checksum", gpod_saved_cksum(track));
    }
    else
    {
        json_object_add_int(jobj, "id", track->id);
        json_object_add_string(jobj, "ipod_path", track->ipod_path);
        json_object_add_int(jobj, "mediatype", track->mediatype);
        json_object_add_string(jobj, "title", track->title);
        json_object_add_string(jobj, "artist", track->artist);
        json_object_add_string(jobj, "album", track->album);
        json_object_add_string(jobj, "genre", track->genre);
        json_object_add_string(jobj, "albumartist", track->albumartist);
        json_object_add_int(jobj, "rating", track->rating);
        json_object_add_uint(jobj, "checksum", gpod_saved_cksum(track));
    }

#ifdef HAVE_SQLITE3
    if (hdl_)
    {
        char*  err = NULL;
        if (inscnt == 0) {
            if (sqlite3_exec(hdl_, "BEGIN TRANSACTION", NULL, NULL, &err) != SQLITE_OK) {
                g_printerr ("failed to start txn- %s\n", err);
                sqlite3_free(err);
            }
        }

        db_add_track(hdl_, track);

        if (++inscnt == 500) {
            if (sqlite3_exec(hdl_, "COMMIT TRANSACTION", NULL, NULL, &err) != SQLITE_OK) {
                g_printerr ("failed to commit txn - %s\n", err);
                sqlite3_free(err);
            }
            inscnt = 0;
        }
    }
#endif

    if (htbl_)
    {
        /* generate the hash info for this track and store it with the track
         *
         * store corresponding items with the hash tables
         */
        track->userdata = hash_trk_init(track, cksum_);
        track->userdata_destroy = hash_trk_free;

        g_hash_table_insert(htbl_->high,
                            track->userdata,
                            g_slist_append(g_hash_table_lookup(htbl_->high, track->userdata), track));
        g_hash_table_insert(htbl_->med,
                            track->userdata,
                            g_slist_append(g_hash_table_lookup(htbl_->med, track->userdata), track));
        g_hash_table_insert(htbl_->low,
                            track->userdata,
                            g_slist_append(g_hash_table_lookup(htbl_->low, track->userdata), track));
    }
     
    return jobj;
}

static json_object*
_playlist (Itdb_Playlist *playlist, sqlite3* hdl_, TrkHashTbl* htbl_, bool cksum_)
{
    GList *it;
    const char*  type;
    bool  master = false;

    if (itdb_playlist_is_mpl (playlist)) {
        type = "master";
        master = true;
    }
    else if (itdb_playlist_is_podcasts (playlist)) type = "podcasts";
    else type = "playlist";

    json_object*  jobj = json_object_new_object();

    json_object_add_string(jobj, "name", playlist->name);
    json_object_add_string(jobj, "type", type);
    json_object_add_int(jobj, "count", g_list_length (playlist->members));
    json_object_add_boolean(jobj, "smartpl", playlist->is_spl);
    json_object_add_int(jobj, "timestamp", playlist->timestamp);

    json_object*  jtracks = json_object_new_array();
    for (it = playlist->members; it != NULL; it = it->next) {
	Itdb_Track *track;
	
	track = (Itdb_Track *)it->data;
	json_object_array_add(jtracks, _track(track, master, master ? hdl_ : NULL, master ? htbl_ : NULL, cksum_));
    }
    json_object_object_add(jobj, "tracks", jtracks);

    return jobj;
}


void  _usage(char* argv0_)
{
    char *basename = g_path_get_basename (argv0_);
    g_print ("%s\n", PACKAGE_STRING);
    g_print ("usage: %s  OPTIONS\n"
             "\n"
             "    dumps the iTunesDB as a json object listing internal (iPod,\n"
             "    podcasts) and user (smartpl, normal) playlists\n"
             "\n"
             "    Each playlist will display track information but fully on 'master'\n"
             "\n"
             "    -M  --mount-point <dir | file>   location of iPod data, as directory mount point or\n"
             "                                     as a iTunesDB file  \n"
#ifdef HAVE_SQLITE3
             "    -Q  --db-file     <sqlite3 db>   generate sqlite3 with a 'tracks' db, representing\n"
             "                                     all tracks in iTunesDB\n"
#else
             "    -Q   --db-file    <sqlite3 db>   IGNORED, disabled at build time\n"
#endif
             "    -c   --enable-checksum           generate checksum of each file in iTunesDB for \n"
             "        --disable-checksum           analysis - this can be slow if checksums not stored\n"
             "\n"
             "\n"
             "    Use 'jq' for basic data mining and sqlite3 for more involved work\n"
             "\n"
             "    # create a subject json object from data naming artist name \n"
             "      jq   '.ipod_data.playlists.items[] | select(.type == \"master\") |\\\n"
             "        .tracks[] | select(.artist==\"Foo\") | {id, ipod_path, title}'\\\n"
             "      foo.json\n"
             "\n"
             "    # unquoted json strings of output matching artist name - useful as data\n"
             "    # inputs to gpod-rm or gpod-tag\n"
             "      jq -r '.ipod_data.playlists.items[] | select(.type == \"master\") |\\\n"
             "        .tracks[] | select(.artist==\"Foo\") | .ipod_path, .id'\\\n"
             "      foo.json\n"
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
    sqlite3*  hdl = NULL;
    struct {
        const char*  itdb_path;
        const char*  db_path;
        bool cksum;
    } opts = { NULL, NULL, true };

    const struct option  long_opts[] = {
        { "mount-point",        1, 0, 'M' },

        { "db-file",            1, 0, 'Q' },
        { "enable-checksum",    0, 0, 'c' },
        { "disable-checksum",   0, 0, 'c'+255 },
        { "help",               0, 0, 'h' },
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
            case 'Q':  opts.db_path = optarg;  break;
            case 'c':  opts.cksum = true;  break;
            case 'c'+255:  opts.cksum = false;  break;

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

#ifdef HAVE_SQLITE3
    if (opts.db_path) {
        if (g_file_test(opts.db_path, G_FILE_TEST_EXISTS)) {
            g_printerr("requested DB file exists, NOT overwritting '%s'\n", opts.db_path);
	    return -1;
        }

        if (sqlite3_open(opts.db_path, &hdl) != SQLITE_OK) {
            g_printerr("failed to open '%s': %s\n", opts.db_path, sqlite3_errmsg(hdl));
            sqlite3_close(hdl);
            hdl = NULL;
        }
        if (!db_create(hdl)) {
            sqlite3_close(hdl);
            hdl = NULL;
        }
    }
#endif

/*
    { 
     "playlists": [
       {
         "name": "iPod",
         "count": ...,
         "tracks": [
           {
             "title":
             "artist":
             ....
           },
           ...
         ]
       }
     ],
     "count": ..
    }

*/

    json_object*  jobj = NULL;
    json_object*  jpodobj = NULL;
    json_object*  jplylists = NULL;
    json_object*  jplylistitems = NULL;

    jobj = json_object_new_object();
    jpodobj = json_object_new_object();

    jplylists = json_object_new_object();
    jplylistitems = json_object_new_array();

    TrkHashTbl  htbl;
    hash_tbl_init(&htbl);

    GList *it;
    for (it = itdb->playlists; it != NULL; it = it->next)
    {
        Itdb_Playlist*  playlist = (Itdb_Playlist *)it->data;
        json_object_array_add(jplylistitems, _playlist(playlist, hdl, &htbl, opts.cksum));
    }
    json_object_object_add(jplylists, "items", jplylistitems);
    json_object_object_add(jplylists, "count", json_object_new_int(g_list_length(itdb->playlists)));

    {
        json_object*  jdevice = json_object_new_object();
        if (itdev) {
            const Itdb_IpodInfo*  ipodinfo = itdb_device_get_ipod_info(itdev);

            json_object_add_string(jdevice, "generation", itdb_info_get_ipod_generation_string (ipodinfo->ipod_generation));
            json_object_add_int(jdevice, "capacity", ipodinfo->capacity);
            json_object_add_string(jdevice, "model_name", itdb_info_get_ipod_model_name_string(ipodinfo->ipod_model));
            json_object_add_string(jdevice, "model_number", ipodinfo->model_number);
            json_object_add_string(jdevice, "uuid", itdb_device_get_uuid(itdev));
            json_object_add_string(jdevice, "serial_number", itdb_device_get_sysinfo(itdev, "SerialNumber"));
            json_object_add_string(jdevice, "format", itdb_device_get_sysinfo(itdev, "VolumeFormat"));
            json_object_add_string(jdevice, "ram", itdb_device_get_sysinfo(itdev, "RAM"));
            json_object_add_string(jdevice, "itunes_version", itdb_device_get_sysinfo(itdev, "MinITunesVersion"));
            json_object_add_string(jdevice, "product_type", itdb_device_get_sysinfo(itdev, "ProductType"));
        }
        json_object_object_add(jpodobj, "device", jdevice);
    }
    json_object_object_add(jpodobj, "playlists", jplylists);
    json_object_object_add(jobj, "ipod_data", jpodobj);

    {
        struct _HtblItems {
            const char*  name;
            GHashTable*  htbl;
        } htblItem[] = {
            { "high", htbl.high },
            { "med", htbl.med},
            { "low", htbl.low},
            { NULL, NULL }
        };

        json_object*  janalysis = json_object_new_object();
        json_object*  jduplicates = json_object_new_array();
        json_object*  jahtbl;


        struct _HtblItems*  hp = htblItem;
        while (hp->name)
        {
            jahtbl = json_object_new_object();
            json_object*  jarray = json_object_new_array();
            json_object_add_string(jahtbl, "match", hp->name);

            GHashTableIter  hiter;
            gpointer  key;
            gpointer  value;

            g_hash_table_iter_init (&hiter, hp->htbl);
            while (g_hash_table_iter_next(&hiter, &key, &value)) {
                hash_tbl_json(key, value, jarray);
            }

            json_object_object_add(jahtbl, "tracks", jarray);
            json_object_array_add(jduplicates, jahtbl);

            ++hp;
        }

        json_object_object_add(janalysis, "duplicates", jduplicates);
        json_object_object_add(jobj, "ipod_analysis", janalysis);
    }

    g_print("%s\n", json_object_to_json_string(jobj)); 
    json_object_put(jobj);

    if (itdev) {
        itdb_device_free(itdev);
    }
    itdb_free (itdb);
#ifdef HAVE_SQLITE3
    if (hdl) {
        sqlite3_exec(hdl, "COMMIT TRANSACTION", NULL, NULL, NULL);
        sqlite3_close(hdl);
    }
#endif
    hash_tbl_free(&htbl);

    return 0;
}
