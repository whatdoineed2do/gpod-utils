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

#include "gpod-utils.h"

#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <gpod/itdb.h>

#include "sha1.h"


const char*  gpod_setlocale()
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


char*  gpod_trim(const char* what_)
{
    if (what_ == NULL) {
	return NULL;
    }

    const char*  p0 = what_;
    while (*p0 && isspace(*p0)) {
	++p0;
    }

    unsigned  p1 = strlen(p0);
    while (p1 && isspace(p0[p1-1])) { 
	--p1;
    }

    char*  buf = malloc(p1+1);
    buf[p1] = '\0';
    memcpy(buf , p0, p1);
    return buf;
}

void  gpod_walk_dir(const gchar *dir_, GSList **l_) 
{
    GDir*  dir_handle;
    const gchar*  filename;
    gchar*  path;

    if (!g_file_test(dir_, G_FILE_TEST_IS_DIR)) {
        *l_ = g_slist_append(*l_, g_strdup(dir_));
        return;
    }

    if ( (dir_handle = g_dir_open(dir_, 0, NULL)) == NULL) {
        return;
    }

    while ((filename = g_dir_read_name(dir_handle)))
    {
        path = g_build_filename(dir_, filename, NULL);

        if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
            gpod_walk_dir(path, l_);
            g_free(path);
        }
        else {
            *l_ = g_slist_append(*l_, path);
        }
    }

    g_dir_close(dir_handle);
}

char*  gpod_sanitize_text(char* what_, bool sanitize_)
{
    if (!sanitize_ || what_ == NULL) {
        return what_;
    }

    struct sanitize_map {
        const char   tgt;
	const char*  xlate;  // anything in this array to map to c
    };

    const struct sanitize_map  maps[] = {
        //{ 'X',  "aeiou" },
        { '-',   "‐", },
	{ '\'',  "’" }, // 3 bytes!!

	{ '\0', NULL }
    };

    const struct sanitize_map*  p = maps;
    while (p->xlate)
    {
        const char*  q = p->xlate;
	{
	    char*  r = what_;
	    while ( (r = strstr(r, q)) ) {
	        *r = p->tgt;
		memmove(r+1, r+strlen(q), 1+strlen(r+strlen(q)) );
	    }
	}
        ++p;
    }
    return what_;
}



#ifdef WANT_GPOD_HASH
guint  gpod_hash(const Itdb_Track* track_)
{ 
    char  path[PATH_MAX];
    sprintf(path, "%s/%s", itdb_get_mountpoint(track_->itdb), track_->ipod_path);
    itdb_filename_ipod2fs(path);

    return gpod_hash_file(path);
}

guint  gpod_hash_file(const char* path_)
{
    FILE*  f;
    if ( (f=fopen(path_, "r")) == NULL) {
        return 0;
    }

    unsigned char  sha1[20];  // hex buffer
    char  sha1str[41];
    sha1_stream(f, sha1);
    fclose(f);
    f = NULL;

    sprintf(sha1str, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", sha1[0], sha1[1], sha1[2], sha1[3], sha1[4], sha1[5], sha1[6], sha1[7], sha1[8], sha1[9], sha1[10], sha1[11], sha1[12], sha1[13], sha1[14], sha1[15], sha1[16], sha1[17], sha1[18], sha1[19]);

    return g_str_hash(sha1str);
}



static guint  _track_mkhash(Itdb_Track* track_)
{ 
    const guint  hash = gpod_hash(track_);
    track_->userdata = malloc(sizeof(guint));
    *((guint*)track_->userdata) = hash;
    track_->userdata_destroy = free;

    return hash;
}

static guint  _track_hash(gconstpointer v_)
{ 
    return *((guint*)(v_));
}


static gboolean  _track_hash_equals(gconstpointer v0_, gconstpointer v1_)
{
    return *((guint*)v0_) == *((guint*)v1_);
}

static gint  _track_guintp_cmp(gconstpointer a_, gconstpointer b_)
{
    const Itdb_Track*  x = (Itdb_Track*)a_;
    const Itdb_Track*  y = (Itdb_Track*)b_;

    return x->time_added  < y->time_added ? -1 :
           x->time_added == y->time_added ?  0 : 1;
}

static void  _track_destroy(gpointer k_, gpointer v_, gpointer d_)
{
    g_slist_free(v_);
}


void  gpod_track_fs_hash_init(struct gpod_track_fs_hash* htbl_, Itdb_iTunesDB* itdb_)
{
    memset(htbl_, 0, sizeof(struct gpod_track_fs_hash));
    htbl_->tbl = g_hash_table_new(_track_hash, _track_hash_equals);

    GHashTable*  htbl = htbl_->tbl;

    Itdb_Track*  track;

    track = NULL;
    Itdb_Playlist*  mpl = itdb_playlist_mpl(itdb_);
    for (GList* i=mpl->members; i!=NULL; i=i->next)
    {
        track = (Itdb_Track*)i->data;
        _track_mkhash(track);

        g_hash_table_insert(htbl,
                            track->userdata,
                            g_slist_insert_sorted(g_hash_table_lookup(htbl, track->userdata),
                                                  track,
                                                  _track_guintp_cmp)
                           );
    }
}

void  gpod_track_fs_hash_destroy(struct gpod_track_fs_hash* htbl_)
{
    g_hash_table_foreach(htbl_->tbl, _track_destroy, NULL);
    g_hash_table_destroy(htbl_->tbl);

    memset(htbl_, 0, sizeof(struct gpod_track_fs_hash));
}

static gint  _track_find_elem(gconstpointer x_, gconstpointer y_)
{
    const Itdb_Track*  track = (const Itdb_Track*)x_;
    const char*  path = (const char*)y_;

    return strcmp(track->ipod_path, path);
}

bool  gpod_track_fs_hash_contains(const struct gpod_track_fs_hash* htbl_, const Itdb_Track* track_, const char* path_)
{
    const char*  path = track_->itdb ? track_->ipod_path : path_;
    const guint  hash = track_->itdb ? gpod_hash(track_) : gpod_hash_file(path_);

    GSList*  what = g_hash_table_lookup(htbl_->tbl, &hash);

    if (what == NULL) {
        return false;
    }

    if (g_slist_length(what) == 1) {
        return true;
    }

    return g_slist_find_custom(what, path, _track_find_elem);
}

#endif

