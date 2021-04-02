/*
|   Copyright (C) 2002-2003 Jorg Schuler <jcsjcs at users.sourceforge.net>
|   Copyright (C) 2006 Christophe Fergeau  <teuf@gnome.org>
|   Copyright (C) 2021 Ray <whatdoineed2do @ gmail com>
|
|   This program is free software; you can redistribute it and/or modify
|   it under the terms of the GNU General Public License as published by
|   the Free Software Foundation; either version 2 of the License, or
|   (at your option) any later version.
|
|   This program is distributed in the hope that it will be useful,
|   but WITHOUT ANY WARRANTY; without even the implied warranty of
|   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
|   GNU General Public License for more details.
|
|   You should have received a copy of the GNU General Public License
|   along with this program; if not, write to the Free Software
|   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
|
|  iTunes and iPod are trademarks of Apple
|
|  This product is not supported/written/published by Apple!
|
|  based on libgpod/tests/tests-ls.c
|
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <stdbool.h>

#include <json.h>
#include <gpod/itdb.h>
#include <sqlite3.h>

#include "gpod-db.h"


bool  db_create(sqlite3 *hdl_)
{
    int  ret;
    char*  err = NULL;

    const char**  p = db_init_queries;
    while (*p) {
        if ((ret = sqlite3_exec(hdl_, *p, NULL, NULL, &err)) != SQLITE_OK) {
            g_print ("failed to create db objects - %s\n", err);
            sqlite3_free(err);
            return false;
        }
        ++p;
    }
    return true;
}

int  db_add_track(sqlite3 *hdl_, const Itdb_Track* track_)
{
}


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
json_object_add_boolean(json_object* obj_, const char* tag_, const bool data_)
{
    json_object_object_add(obj_, tag_, json_object_new_boolean(data_));
}


static json_object*
_track (Itdb_Track *track, bool verbose_)
{
    json_object*  jobj = json_object_new_object();

    itdb_filename_ipod2fs(track->ipod_path);
    if (verbose_)
    {
        json_object_add_int(jobj, "id", track->id);
        json_object_add_string(jobj, "ipod_path", track->ipod_path);
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
    }
    else
    {
        json_object_add_int(jobj, "id", track->id);
        json_object_add_string(jobj, "ipod_path", track->ipod_path);
        json_object_add_string(jobj, "title", track->title);
        json_object_add_string(jobj, "artist", track->artist);
        json_object_add_string(jobj, "album", track->album);
        json_object_add_string(jobj, "genre", track->genre);
        json_object_add_string(jobj, "albumartist", track->albumartist);
        json_object_add_int(jobj, "rating", track->rating);
    }
     
    return jobj;
}

static json_object*
_playlist (Itdb_Playlist *playlist)
{
    GList *it;
    const char*  type;
    bool  verbose = false;

    if (itdb_playlist_is_mpl (playlist)) {
        type = "master";
        verbose = true;
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
	json_object_array_add(jtracks, _track(track, verbose));
    }
    json_object_object_add(jobj, "tracks", jtracks);

    return jobj;
}

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
    Itdb_iTunesDB *itdb;
    sqlite3*  hdl = NULL;

    if (argc != 2 && argc != 3)
    {
        char *basename = g_path_get_basename (argv[0]);
        g_print ("usage: %s [ <dir ipod mount> | <file iTunesDB>]  [sqlite3 db outfile]\n\n%s%s%s", basename,
                 "    This utility dumps the iTunesDB as a json object listing:\n",
                 "    playlists (iPod, podcasts, internal and user playlists\n",
                 "    with track info per playlist\n");
        g_free (basename);
        exit(-1);
    }

    _setlocale();

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

    if (argc == 3) {
        if (sqlite3_open(argv[2], &hdl) != SQLITE_OK) {
            g_printerr("failed to open '%s': %s\n", argv[2], sqlite3_errmsg(hdl));
            sqlite3_close(hdl);
            hdl = NULL;
        }
        if (!db_create(hdl)) {
            sqlite3_close(hdl);
            hdl = NULL;
        }
    }


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
    json_object*  jplylists = NULL;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "count", json_object_new_int(g_list_length(itdb->playlists)));

    jplylists = json_object_new_array();

    GList *it;
    for (it = itdb->playlists; it != NULL; it = it->next)
    {
        Itdb_Playlist*  playlist = (Itdb_Playlist *)it->data;
        json_object_array_add(jplylists, _playlist(playlist));
    }
    json_object_object_add(jobj, "playlists", jplylists);

    g_print("%s\n", json_object_to_json_string(jobj)); 
    json_object_put(jobj);

    itdb_free (itdb);
    if (hdl) {
        sqlite3_close(hdl);
    }

    return 0;
}
