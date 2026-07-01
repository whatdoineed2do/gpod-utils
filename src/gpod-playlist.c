/*
 *  Copyright (C) 2021-2026 Ray <whatdoineed2do @ gmail com>
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

#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>

#include <glib.h>
#include <gpod/itdb.h>

#include "lib/gpod-utils.h"


void  _usage(char* argv0_)
{
    char *basename = g_path_get_basename (argv0_);
    g_print ("%s: %s-%s\n", basename, GIT_TAG, GIT_COMMIT);

    GSList*  supported = gpod_supported();
    g_print("  supported:\n");
    for (GSList* s=supported; s; s=s->next) {
        g_print("    %s\n", (const char*)s->data);
    }
    g_slist_free(supported);

    g_print ("usage: %s -M <dir ipod mount | file iTunesDB>  <operation>\n"
             "\n"
             "    playlist CRUD on iPod/iTunesDB\n"
             "\n"
             "    -l              list playlists; with -p <name>, list that playlist's tracks\n"
             "    -c <name>       create new playlist\n"
             "    -d <name>       delete playlist\n"
             "    -p <name> -R <newname>       rename playlist (renaming master playlist renames iPod)\n"
             "    -p <name> -C                 clear playlist (remove all tracks)\n"
             "    -p <name> -a <path|id> ...   add tracks to playlist\n"
             "    -p <name> -r <path|id> ...   remove tracks from playlist\n"
             "    -u [-n limit] [-3]           update 'recently added' playlists\n"
             "                                 (0wk/1wk/1month/3months/6months/12months)\n"
             "                                 -n limits albums (default 50), -3 also writes m3u\n"
             "\n"
             "    Track ids/ipod paths can be determined using gpod-ls\n"
             "\n"
            , basename);
    g_free (basename);
    exit(-1);
}


enum gpod_pl_op {
    OP_NONE = 0,
    OP_LIST,
    OP_CREATE,
    OP_DELETE,
    OP_RENAME,
    OP_CLEAR,
    OP_ADD,
    OP_REMOVE,
    OP_RECENT
};

static const char*  _pl_type(Itdb_Playlist* pl_)
{
    if (itdb_playlist_is_mpl(pl_))       return "master";
    if (itdb_playlist_is_podcasts(pl_))  return "podcasts";
    return "playlist";
}

/* resolve a track selector - all digits is an iTunesDB track id, otherwise
 * an ipod path (/iPod_Control/Music/...); lookup structs created lazily
 */
static Itdb_Track*  _resolve_track(Itdb_iTunesDB* itdb_, const char* arg_,
                                   GTree** idtree_, GHashTable** pathhash_)
{
    const char*  d = arg_;
    while (*d && isdigit(*d)) {
        ++d;
    }

    if (*d == '\0' && d != arg_)
    {
        if (*idtree_ == NULL) {
            *idtree_ = itdb_track_id_tree_create(itdb_);
        }
        return itdb_track_id_tree_by_id(*idtree_, atol(arg_));
    }

    if (*pathhash_ == NULL)
    {
        *pathhash_ = g_hash_table_new(g_str_hash, g_str_equal);
        for (GList* it=itdb_playlist_mpl(itdb_)->members; it!=NULL; it=it->next)
        {
            Itdb_Track*  track = (Itdb_Track*)it->data;
            itdb_filename_ipod2fs(track->ipod_path);
            g_hash_table_insert(*pathhash_, track->ipod_path, track);
        }
    }
    return g_hash_table_lookup(*pathhash_, arg_);
}

static void  _list_playlists(Itdb_iTunesDB* itdb_)
{
    unsigned  playlists = 0;
    for (GList* it=itdb_->playlists; it!=NULL; it=it->next)
    {
        Itdb_Playlist*  pl = (Itdb_Playlist*)it->data;
        g_print("'%s' { type=%s count=%u smartpl=%s }\n",
                pl->name ? pl->name : "",
                _pl_type(pl),
                g_list_length(pl->members),
                pl->is_spl ? "yes" : "no");
        ++playlists;
    }
    g_print("playlists=%u\n", playlists);
}

static void  _list_pl_tracks(Itdb_Playlist* pl_)
{
    const unsigned  N = g_list_length(pl_->members);
    unsigned  i = 0;
    for (GList* it=pl_->members; it!=NULL; it=it->next)
    {
        Itdb_Track*  track = (Itdb_Track*)it->data;
        itdb_filename_ipod2fs(track->ipod_path);
        g_print("[%3u/%u]  id=%u ipod_path='%s' { title='%s' artist='%s' album='%s' }\n",
                ++i, N,
                track->id,
                track->ipod_path ? track->ipod_path : "",
                track->title ? track->title : "",
                track->artist ? track->artist : "",
                track->album ? track->album : "");
    }
    g_print("'%s' tracks=%u\n", pl_->name ? pl_->name : "", N);
}

/* op functions return -1 on refusal/error, otherwise number of changes */

static int  _pl_create(Itdb_iTunesDB* itdb_, const char* name_)
{
    if (itdb_playlist_by_name(itdb_, (gchar*)name_)) {
        g_printerr("playlist '%s' already exists\n", name_);
        return -1;
    }

    Itdb_Playlist*  pl = itdb_playlist_new(name_, false);
    itdb_playlist_add(itdb_, pl, -1);
    g_print("created playlist '%s'\n", name_);
    return 1;
}

static int  _pl_delete(Itdb_Playlist* pl_)
{
    if (itdb_playlist_is_mpl(pl_)) {
        g_printerr("cannot delete master playlist\n");
        return -1;
    }
    if (itdb_playlist_is_podcasts(pl_)) {
        g_printerr("cannot delete podcasts playlist\n");
        return -1;
    }

    g_print("deleting playlist '%s' with %u tracks (tracks remain on iPod)\n",
            pl_->name ? pl_->name : "", g_list_length(pl_->members));
    itdb_playlist_remove(pl_);
    return 1;
}

static int  _pl_rename(Itdb_iTunesDB* itdb_, Itdb_Playlist* pl_, const char* newname_)
{
    if (itdb_playlist_is_podcasts(pl_)) {
        g_printerr("cannot rename podcasts playlist\n");
        return -1;
    }
    if (itdb_playlist_by_name(itdb_, (gchar*)newname_)) {
        g_printerr("playlist '%s' already exists\n", newname_);
        return -1;
    }

    if (itdb_playlist_is_mpl(pl_)) {
        g_print("renaming iPod to '%s'\n", newname_);
    }
    else {
        g_print("renaming playlist '%s' to '%s'\n", pl_->name ? pl_->name : "", newname_);
    }
    g_free(pl_->name);
    pl_->name = g_strdup(newname_);
    return 1;
}

static int  _pl_clear(Itdb_Playlist* pl_)
{
    if (itdb_playlist_is_mpl(pl_)) {
        g_printerr("cannot clear master playlist\n");
        return -1;
    }
    if (pl_->is_spl) {
        g_printerr("cannot clear smart playlist '%s'\n", pl_->name ? pl_->name : "");
        return -1;
    }
    if (itdb_playlist_is_podcasts(pl_)) {
        g_print("clearing podcasts playlist\n");
    }

    int  removed = 0;
    while (pl_->members) {
        itdb_playlist_remove_track(pl_, (Itdb_Track*)pl_->members->data);
        ++removed;
    }
    g_print("cleared '%s', removed %d tracks\n", pl_->name ? pl_->name : "", removed);
    return removed;
}

static int  _pl_add_tracks(Itdb_iTunesDB* itdb_, Itdb_Playlist* pl_, char* argv_[], unsigned N_)
{
    if (itdb_playlist_is_mpl(pl_)) {
        g_printerr("cannot add tracks to master playlist\n");
        return -1;
    }
    if (itdb_playlist_is_podcasts(pl_)) {
        g_printerr("cannot add tracks to podcasts playlist - use gpod-cp\n");
        return -1;
    }
    if (pl_->is_spl) {
        g_printerr("cannot add tracks to smart playlist '%s'\n", pl_->name ? pl_->name : "");
        return -1;
    }

    GTree*  idtree = NULL;
    GHashTable*  pathhash = NULL;
    int  added = 0;
    unsigned  requested = 0;

    for (char** p=argv_; *p; ++p)
    {
        ++requested;
        Itdb_Track*  track = _resolve_track(itdb_, *p, &idtree, &pathhash);

        if (track == NULL) {
            g_print("[%3u/%u]  %s -> { Not on iPod/iTunesDB }\n", requested, N_, *p);
            continue;
        }

        if (itdb_playlist_contains_track(pl_, track)) {
            g_print("[%3u/%u]  %s -> { already in playlist }\n", requested, N_, *p);
            continue;
        }

        itdb_playlist_add_track(pl_, track, -1);
        g_print("[%3u/%u]  %s -> { id=%u title='%s' artist='%s' }\n",
                requested, N_, *p,
                track->id,
                track->title ? track->title : "",
                track->artist ? track->artist : "");
        ++added;
    }

    if (idtree)    itdb_track_id_tree_destroy(idtree);
    if (pathhash)  g_hash_table_destroy(pathhash);

    g_print("added %d/%u tracks to '%s', now count=%u\n",
            added, requested, pl_->name ? pl_->name : "", g_list_length(pl_->members));
    return added;
}

static int  _pl_remove_tracks(Itdb_iTunesDB* itdb_, Itdb_Playlist* pl_, char* argv_[], unsigned N_)
{
    if (itdb_playlist_is_mpl(pl_)) {
        g_printerr("cannot remove tracks from master playlist - use gpod-rm\n");
        return -1;
    }
    if (pl_->is_spl) {
        g_printerr("cannot remove tracks from smart playlist '%s'\n", pl_->name ? pl_->name : "");
        return -1;
    }

    GTree*  idtree = NULL;
    GHashTable*  pathhash = NULL;
    int  removed = 0;
    unsigned  requested = 0;

    for (char** p=argv_; *p; ++p)
    {
        ++requested;
        Itdb_Track*  track = _resolve_track(itdb_, *p, &idtree, &pathhash);

        if (track == NULL) {
            g_print("[%3u/%u]  %s -> { Not on iPod/iTunesDB }\n", requested, N_, *p);
            continue;
        }

        if (!itdb_playlist_contains_track(pl_, track)) {
            g_print("[%3u/%u]  %s -> { not in playlist }\n", requested, N_, *p);
            continue;
        }

        itdb_playlist_remove_track(pl_, track);
        g_print("[%3u/%u]  %s -> { id=%u title='%s' artist='%s' }\n",
                requested, N_, *p,
                track->id,
                track->title ? track->title : "",
                track->artist ? track->artist : "");
        ++removed;
    }

    if (idtree)    itdb_track_id_tree_destroy(idtree);
    if (pathhash)  g_hash_table_destroy(pathhash);

    g_print("removed %d/%u tracks from '%s', now count=%u\n",
            removed, requested, pl_->name ? pl_->name : "", g_list_length(pl_->members));
    return removed;
}


int
main (int argc, char *argv[])
{
    GError *error = NULL;
    Itdb_iTunesDB*  itdb = NULL;
    Itdb_Device*  itdev = NULL;
    struct {
        const char*  itdb_path;
        const char*  create_name;
        const char*  delete_name;
        const char*  pl_name;
        const char*  rename_to;
        bool  list;
        bool  add;
        bool  remove;
        bool  clear;
        bool  recent;
        unsigned  album_limit;
        bool  with_m3u;
        bool  recent_opts;
    } opts = { NULL, NULL, NULL, NULL, NULL, false, false, false, false, false, 50, false, false };

    int  ret = 0;

    int  c;
    while ( (c=getopt(argc, argv, "M:c:d:p:R:n:larCu3vh")) != EOF)
    {
        switch (c) {
            case 'M':  opts.itdb_path = optarg;  break;
            case 'c':  opts.create_name = optarg;  break;
            case 'd':  opts.delete_name = optarg;  break;
            case 'p':  opts.pl_name = optarg;  break;
            case 'R':  opts.rename_to = optarg;  break;
            case 'l':  opts.list = true;  break;
            case 'a':  opts.add = true;  break;
            case 'r':  opts.remove = true;  break;
            case 'C':  opts.clear = true;  break;
            case 'u':  opts.recent = true;  break;
            case 'n':  opts.album_limit = atol(optarg);  opts.recent_opts = true;  break;
            case '3':  opts.with_m3u = true;  opts.recent_opts = true;  break;

            case 'v':
            case 'h':
            default:
                _usage(argv[0]);
        }
    }

    enum gpod_pl_op  op = OP_NONE;
    unsigned  nops = 0;
    if (opts.list)         { op = OP_LIST;    ++nops; }
    if (opts.create_name)  { op = OP_CREATE;  ++nops; }
    if (opts.delete_name)  { op = OP_DELETE;  ++nops; }
    if (opts.rename_to)    { op = OP_RENAME;  ++nops; }
    if (opts.clear)        { op = OP_CLEAR;   ++nops; }
    if (opts.add)          { op = OP_ADD;     ++nops; }
    if (opts.remove)       { op = OP_REMOVE;  ++nops; }
    if (opts.recent)       { op = OP_RECENT;  ++nops; }

    if (nops == 0) {
        g_printerr("no operation specified\n");
        _usage(argv[0]);
    }
    if (nops > 1) {
        g_printerr("conflicting operations\n");
        _usage(argv[0]);
    }

    switch (op) {
        case OP_ADD:
        case OP_REMOVE:
        case OP_RENAME:
        case OP_CLEAR:
            if (opts.pl_name == NULL) {
                g_printerr("operation requires -p <playlist>\n");
                _usage(argv[0]);
            }
            break;

        default:
            if (op != OP_LIST && opts.pl_name) {
                g_printerr("-p only valid with -l/-a/-r/-R/-C\n");
                _usage(argv[0]);
            }
    }

    if (op == OP_ADD || op == OP_REMOVE) {
        if (optind == argc) {
            g_printerr("no tracks specified\n");
            _usage(argv[0]);
        }
    }
    else if (optind != argc) {
        g_printerr("unexpected arguments\n");
        _usage(argv[0]);
    }

    if (opts.recent_opts && op != OP_RECENT) {
        g_printerr("-n/-3 only valid with -u\n");
        _usage(argv[0]);
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

    if (op != OP_LIST)
    {
        const Itdb_IpodInfo*  ipodinfo = itdb_device_get_ipod_info(itdev);
        const bool  supported = gpod_write_supported(ipodinfo);
        if (!supported) {
            g_printerr("iPod %s %s not supported\n", itdb_info_get_ipod_generation_string(ipodinfo->ipod_generation), ipodinfo->model_number);
            ret = -1;
            goto cleanup;
        }
    }

    Itdb_Playlist*  pl = NULL;
    const char*  lookup_name = opts.delete_name ? opts.delete_name : opts.pl_name;
    if (lookup_name)
    {
        pl = itdb_playlist_by_name(itdb, (gchar*)lookup_name);
        if (pl == NULL) {
            g_printerr("no such playlist '%s'\n", lookup_name);
            ret = 1;
            goto cleanup;
        }
    }

    int  changes = 0;
    const unsigned  N = argc - optind;

    switch (op)
    {
        case OP_LIST:
            if (pl)  _list_pl_tracks(pl);
            else     _list_playlists(itdb);
            break;

        case OP_CREATE:  changes = _pl_create(itdb, opts.create_name);  break;
        case OP_DELETE:  changes = _pl_delete(pl);  break;
        case OP_RENAME:  changes = _pl_rename(itdb, pl, opts.rename_to);  break;
        case OP_CLEAR:   changes = _pl_clear(pl);  break;
        case OP_ADD:     changes = _pl_add_tracks(itdb, pl, &argv[optind], N);  break;
        case OP_REMOVE:  changes = _pl_remove_tracks(itdb, pl, &argv[optind], N);  break;

        case OP_RECENT:
        {
            unsigned  recent_pl, recent_tracks;
            gpod_playlist_recent(&recent_pl, &recent_tracks, itdb, opts.album_limit, 0, opts.with_m3u);
            g_print("iPod playlists=%u (limited to %d) with tracks=%u\n", recent_pl, opts.album_limit, recent_tracks);
            changes = recent_tracks;
        }
        break;

        default:
            break;
    }

    if (changes < 0) {
        ret = 1;
    }

    if (changes > 0)
    {
        g_print("sync'ing iPod ...\n");
        itdb_write(itdb, &error);

        if (error) {
            g_printerr("failed to write playlists iPod database - %s\n", error->message ? error->message : "<unknown error>");
             g_error_free (error);
             ret = 1;
        }
    }


cleanup:
    itdb_device_free(itdev);
    itdb_free (itdb);

    return ret;
}
