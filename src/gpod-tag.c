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
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>

#include <glib.h>
#include <gpod/itdb.h>

#include "gpod-utils.h"



struct gpod_opts {
    char*  artist;
    char*  album;
    char*  albumartist;
    char*  composer;
    char*  title;
    char*  genre;
    int  year;
    int  track;
    short  rating;

    bool  sanitize;
};

static void  gpod_opts_init(struct gpod_opts* opts_)
{
    memset(opts_, 0, sizeof(struct gpod_opts));
    opts_->year = -1;
    opts_->track = -1;
    opts_->rating = -1;

    opts_->sanitize = false;
}

static void  gpod_opts_free(struct gpod_opts* opts_)
{
    free(opts_->artist);
    free(opts_->album);
    free(opts_->albumartist);
    free(opts_->composer);
    free(opts_->title);
    free(opts_->genre);
    memset(opts_, 0, sizeof(struct gpod_opts));
}

static void  _sanitize(struct gpod_opts* opts_)
{
    if (!opts_->sanitize) {
        return;
    }

    char*  what[] = {
	opts_->artist, opts_->album, opts_->title, opts_->genre, opts_->albumartist, opts_->composer, NULL
    };
    char**  p = what;
    while (*p) {
	if (*p) {
	    gpod_sanitize_text(*p, true);
	}
        ++p;
    }
}

struct gpod_arg {
    union {
        const char*  ipod_path;
        uint64_t  id;
    } u;
};

void  _usage(const char* argv_)
{
    char *basename = g_path_get_basename (argv_);
    g_print ("%s\n", PACKAGE_STRING);
    g_print ("usage: %s  OPTIONS  <file id/ipod path> [...]\n"
	     "    -t  --title    <title>\n"
	     "    -a  --artist   <artist>\n"
	     "    -R  --albumartist   <albumartist>\n"
	     "    -c  --composer <composer>\n"
	     "    -A  --album    <album>\n"
	     "    -g  --genre    <genre>\n"
	     "    -T  --track    <track>\n"
	     "    -y  --year     <year>\n"
	     "    -r  --rating   <rating 0-5>\n"
	     "\n"
             "  update meta tags for files as known in iPod/iTunesDB\n"
	     "  empty string (\"\") or -1 to unset string and numeric flds repsectively\n"
             "  use gpod-ls to determine ipod path/id\n"
	     "\n"
	     "    -M  --mount-point  <iPod mount>>\n"
	     "    -S  --santize  [Y|N]    sanitize text tags, chars like ’ to '\n"
             , basename);
    g_free (basename);
    exit(-1);
}

#define TRACK_ASSIGN(tag_, new_, dup_) \
{\
    if (new_) {\
	if (tag_) { \
	    g_free(tag_);\
	    tag_ = NULL;\
	}\
	tag_ = dup_ ? g_strdup(new_) : new_;\
    }\
}

int
main (int argc, char *argv[])
{
    GError *error = NULL;
    Itdb_iTunesDB*  itdb = NULL;
    Itdb_Device*  itdev = NULL;
    int  ret = 0;

    struct gpod_opts  opts;
    gpod_opts_init(&opts);

    const char*  mpt = NULL;

    // no_argument = 0, required_argument = 1, optional_argument = 2 (has arg)
    const struct option  long_opts[] = {
	{ "mount-point", 	1, 0, 'M' },

	{ "artist", 		1, 0, 'a' },
	{ "album",		1, 0, 'A' },
	{ "albumartist",	1, 0, 'R' },
	{ "composer",		1, 0, 'C' },
	{ "title",		1, 0, 't' },
	{ "genre", 		1, 0, 'g' },
	{ "year", 		1, 0, 'Y' },
	{ "track-number", 	1, 0, 'T' },
	{ "rating", 		1, 0, 'r' },

	{ "santize", 		2, 0, 'S' },
	{ "help", 		0, 0, 'h' },
	{ 0, 0, 0,  0 }
    };
    char  opt_args[1 + sizeof(long_opts)*2] = { 0 };
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
            case 'M':  mpt = optarg;  break;

            case 'a':  opts.artist = gpod_trim(optarg);  break;
            case 'A':  opts.album  = gpod_trim(optarg);  break;
            case 'R':  opts.albumartist  = gpod_trim(optarg);  break;
            case 'C':  opts.composer = gpod_trim(optarg);  break;
            case 't':  opts.title  = gpod_trim(optarg);  break;
            case 'g':  opts.genre  = gpod_trim(optarg);  break;
            case 'y':  opts.year   = atol(optarg);  break;
            case 'T':  opts.track  = atol(optarg);  break;
            case 'r': 
		opts.rating = atol(optarg);
		if (opts.rating > 5) {
		    opts.rating = 5;
		}
	        break;

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


    if (opts.title == NULL && opts.artist == NULL && opts.album == NULL && opts.albumartist == NULL && opts.composer == NULL && opts.genre == NULL && opts.year < 0 && opts.track < 0 && opts.rating < 0) {
        g_printerr("invalid/unspecified tagging options\n");
        _usage(argv[0]);
    }

    char  mountpoint[PATH_MAX] = { 0 };
    if (mpt == NULL) {
        mpt = gpod_default_mountpoint(mountpoint, sizeof(mountpoint));
    }
    else {
	strcpy(mountpoint, mpt);
    }

    if ( !(optind < argc) ) {
        g_printerr("no inputs\n");
        _usage(argv[0]);
    }

    _sanitize(&opts);

    gpod_setlocale();

    if (g_file_test(mpt, G_FILE_TEST_IS_DIR)) {
	itdb = itdb_parse (mpt, &error);
	itdev = itdb_device_new();
	itdb_device_set_mountpoint(itdev, mpt);
    }

    if (error)
    {
        if (error->message) {
            g_printerr("failed to prase iTunesDB via %s - %s\n", mpt, error->message);
        }
        g_error_free (error);
        error = NULL;
        return -1;
    }

    if (itdb == NULL) {
        g_print("failed to open iTunesDB via %s\n", mpt);
        _usage(argv[0]);
    }

    const Itdb_IpodInfo*  ipodinfo = itdb_device_get_ipod_info(itdev);
    const bool  supported = gpod_write_supported(ipodinfo);
 
    g_print("updating iPod %s track meta %s", itdb_info_get_ipod_generation_string(ipodinfo->ipod_generation), supported ? "{" : " - device NOT supported\n");

    if (!supported) {
        goto cleanup;
        ret = -1;
    }
      if (opts.title)       g_print(" title='%s'",  opts.title);
      if (opts.artist)      g_print(" artist='%s'", opts.artist);
      if (opts.album)       g_print(" album='%s'",  opts.album);
      if (opts.albumartist) g_print(" albumartist='%s'",  opts.albumartist);
      if (opts.composer)    g_print(" composer='%s'",  opts.composer);
      if (opts.genre)       g_print(" genre='%s'",  opts.genre);
      if (opts.track  >= 0) g_print(" track=%u",    opts.track);
      if (opts.year   >= 0) g_print(" year=%u",     opts.year);
      if (opts.rating >= 0) g_print(" rating=%u",   opts.rating);
    g_print(" }\n");

    opts.rating *= ITDB_RATING_STEP;

    Itdb_Playlist*  mpl = itdb_playlist_mpl(itdb);

    Itdb_Track*  track;
    GList*  it;

    uint64_t  updated = 0;
    uint64_t  requested = 0;
    unsigned  N = argc - optind;

    GTree*  idtree = itdb_track_id_tree_create(itdb);

    struct tm  tm;
    char  dt[21];
    char  path[PATH_MAX];

    struct gpod_arg  arg;
    const char*  p = NULL;
    GHashTable*  hash = NULL;
    int  i = optind;
    while (i < argc)
    {
        ++requested;
        p = argv[i++];

        g_print("[%3u/%u]  %s ", requested, N, p);

        track = NULL;

        if (strncmp(p, "/iPod_Control/", 14) == 0)
        {
            arg.u.ipod_path = p;

            sprintf(path, "%s/%s", mountpoint, p);

            if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
                g_printerr("{ } No such file or directory '%s'\n", path);
                continue;
            }

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
	    track = g_hash_table_lookup(hash, p);
         }
        else
        {
            arg.u.id = (uint32_t)atoll(p);
            
            if ((track = itdb_track_id_tree_by_id(idtree, arg.u.id)) ) {
                itdb_filename_ipod2fs(track->ipod_path);
            }
        }

        if (!track) {
            g_print("{ } - No such track\n");
            continue;
        }

        gmtime_r(&(track->time_modified), &tm);
        strftime(dt, 20, "%Y-%m-%dT%H:%M:%S", &tm);

        g_print("{ id=%u ipod_path='%s' { rating=%d title='%s' artist='%s' album='%s' albumartist='%s' composer='%s' genre='%s' track=%u year=%u time_modified=%s } }\n",
               track->id,
               track->ipod_path,
               track->rating/ITDB_RATING_STEP,
               track->title ? track->title : "",
               track->artist ? track->artist : "",
               track->albumartist ? track->albumartist : "",
               track->composer ? track->composer : "",
               track->album ? track->album : "",
               track->genre ? track->genre : "",
               track->track_nr, track->year,
               dt);
 
        TRACK_ASSIGN(track->title,            opts.title,       1);
        TRACK_ASSIGN(track->artist,           opts.artist,      1);
        TRACK_ASSIGN(track->albumartist,      opts.albumartist, 1);
        TRACK_ASSIGN(track->composer,         opts.composer,    1);
        TRACK_ASSIGN(track->album,            opts.album,       1);
        TRACK_ASSIGN(track->genre,            opts.genre,       1);

        TRACK_ASSIGN(track->sort_title,       gpod_sortname(opts.title),       0);
        TRACK_ASSIGN(track->sort_artist,      gpod_sortname(opts.artist),      0);
        TRACK_ASSIGN(track->sort_albumartist, gpod_sortname(opts.albumartist), 0);
        TRACK_ASSIGN(track->sort_composer,    gpod_sortname(opts.composer),    0);
        TRACK_ASSIGN(track->sort_album,       gpod_sortname(opts.album),       0);

        if (opts.rating >= 0) track->rating = opts.rating; 
        if (opts.track >= 0) track->track_nr = opts.track; 
        if (opts.year >= 0) track->year = opts.year;

        ++updated;
    }
    itdb_track_id_tree_destroy(idtree);
    if (hash) {
	g_hash_table_destroy(hash);
	hash = NULL;
    }

    if (updated)
    {
        g_print("sync'ing iPod ... updated %d/%d\n", updated, requested);
        itdb_write(itdb, &error);

        if (error) {
            g_printerr("failed to write iPod database, %d files NOT updated - %s\n", requested, error->message ? error->message : "<unknown error>");
             g_error_free (error);
             ret = 1;
        }
        g_print("updated iPod %u items, total tracks=%u\n", ret == 0 ? updated : 0, g_list_length(itdb_playlist_mpl(itdb)->members));
    }
    else {
        g_printerr("failed to update\n");
    }

cleanup:
    itdb_device_free(itdev);
    itdb_free (itdb);
    gpod_opts_free(&opts);

    return ret;
}
