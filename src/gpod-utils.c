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

bool  gpod_write_supported(const Itdb_IpodInfo* ipi_)
{
    /* anything that is not on this list requires a hash/cksum'd
     * iTunesDB/iTunesCDB and sqlite3 db for the ipod which doesn't
     * work well
     */
    static const int  supported[] = { 
	ITDB_IPOD_GENERATION_FIRST,
	ITDB_IPOD_GENERATION_SECOND,
	ITDB_IPOD_GENERATION_THIRD,
	ITDB_IPOD_GENERATION_FOURTH,
	ITDB_IPOD_GENERATION_PHOTO,
	ITDB_IPOD_GENERATION_VIDEO_1,
	ITDB_IPOD_GENERATION_VIDEO_2,
	-1,
    };

    const int*  p = supported;
    while (*p)
    {
	if (*p == ipi_->ipod_generation) {
	    return true;
	}
	++p;
    }
    return false;
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

static const float  BYTES_KB  = 1024.0;
static const float  BYTES_MB  = BYTES_KB * 1024.0;
static const float  BYTES_GB  = BYTES_MB * 1024.0;

void  gpod_bytes_to_human(char* buf_, unsigned bufsz_, size_t  bytes_, bool wrap_)
{
    if      (bytes_ >= BYTES_GB)  snprintf(buf_, bufsz_, "%s%.1fG%s", wrap_ ? "(" : "", bytes_/BYTES_GB, wrap_ ? ")" : "" );
    else if (bytes_ >= BYTES_MB)  snprintf(buf_, bufsz_, "%s%.3fM%s", wrap_ ? "(" : "", bytes_/BYTES_MB, wrap_ ? ")" : "" );
    else                          snprintf(buf_, bufsz_, "%s%.2fK%s", wrap_ ? "(" : "", bytes_/BYTES_KB, wrap_ ? ")" : "" );
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


static int  _track_key_cmp(gconstpointer a_, gconstpointer b_)
{
    const Itdb_Track*  a = (const Itdb_Track*)a_;
    const Itdb_Track*  b = (const Itdb_Track*)b_;

    const char*  akeys[] = { a->title, a->album, a->artist, NULL };
    const char*  bkeys[] = { b->title, b->album, b->artist, NULL };

    if (g_strv_equal(akeys, bkeys)) return 0;

    const unsigned  na = a->title ? strlen(a->title) : 0 + a->album ? strlen(a->album) : 0  + a->artist ? strlen(a->artist) : 0;
    const unsigned  nb = b->title ? strlen(b->title) : 0 + b->album ? strlen(b->album) : 0  + b->artist ? strlen(b->artist) : 0;
    return na < nb ? -1 : 1;
}

GTree*  gpod_track_key_tree_create(Itdb_iTunesDB *itdb_)
{
    g_return_val_if_fail(itdb_, NULL);

    GTree *idtree = g_tree_new(_track_key_cmp);
    Itdb_Playlist*  mpl = itdb_playlist_mpl(itdb_);
    for (GList* gl=mpl->members; gl!=NULL; gl=gl->next)
    {
	Itdb_Track *tr = gl->data;
	g_return_val_if_fail (tr, NULL);
	g_tree_insert(idtree, tr, tr);
    }
    return idtree;
}

void  gpod_track_key_tree_destroy(GTree* tree_)
{
    g_return_if_fail(tree_);
    g_tree_destroy(tree_);
}


static guint _track_key_hash(gconstpointer p_)
{
    const Itdb_Track*  t = (const Itdb_Track*)p_;
    const char*  elems[] = { t->title, t->artist, t->album, NULL };

    guint  hash = 0;
    const char**  p = elems;
    while (*p) {
        hash += (**p) ? g_str_hash(*p) : 0;
        ++p;
    }
    return hash;
}

static gboolean  _track_key_equal(gconstpointer a_, gconstpointer b_)
{
    return _track_key_cmp(a_,b_) == 0;
}

static void  _track_htbl_val_destroy(gpointer p_)
{
    if (p_) {
        g_slist_free((GSList*)p_);
    }
}

GHashTable*  gpod_track_htbl_create(Itdb_iTunesDB* itdb_)
{
    // GHashTable*  htbl = g_hash_table_new_full(_track_key_hash, _track_key_equal, NULL, _track_htbl_val_destroy);
    GHashTable*  htbl = g_hash_table_new(_track_key_hash, _track_key_equal);

    Itdb_Track*  track;
    Itdb_Playlist*  mpl = itdb_playlist_mpl(itdb_);
    for (GList* i=mpl->members; i!=NULL; i=i->next)
    {
        track = (Itdb_Track*)i->data;
        g_hash_table_insert(htbl, track,
                            g_slist_append(g_hash_table_lookup(htbl, track), track)
                           );
    }

    return htbl;
}

void  gpod_track_htbl_val_destroy(gpointer key_, gpointer value_, gpointer data_)
{
    g_slist_free(value_);
}

void  gpod_track_htbl_destroy(GHashTable* htbl_)
{
    g_hash_table_foreach(htbl_, gpod_track_htbl_val_destroy, NULL);
    g_hash_table_destroy(htbl_);
}
#endif


// recent playlists creation
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


GSList*  gpod_recents_new(gint64  now_, const char* extra_)
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
    when->name = g_strdup_printf("Recent%s: 0d", extra_ ? extra_ : "");
    when->range.to   = g_date_time_new_utc(dmy.y, dmy.m, dmy.d, 23, 59, 59);
    when->range.from = g_date_time_new_utc(dmy.y, dmy.m, dmy.d,  0,  0,  0);
    when->from = g_date_time_to_unix(when->range.from);
    when->to   = g_date_time_to_unix(when->range.to);
    l = g_slist_append(l, when);
    when_last = when;
    when_now = when;

    when = gpod_recent_new();
    when->name = g_strdup_printf("Recent%s: 0d..7d", extra_ ? extra_ : "");
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
    when->name = g_strdup_printf("Recent%s: 7d..last mth", extra_ ? extra_ : "");
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
    when->name = g_strdup_printf("Recent%s: 1..3mth", extra_ ? extra_ : "");
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
    when->name = g_strdup_printf("Recent%s: 3..6mth", extra_ ? extra_ : "");
    g_date_time_get_ymd(when_last->range.from, &dmy.y, &dmy.m, &dmy.d);
    tmp = g_date_time_new_utc(dmy.y, dmy.m, dmy.d, 23, 59, 59);
    when->range.to   = g_date_time_add_days(tmp, -1);
    when->range.from = g_date_time_add_months(when_now->range.from, -6);
    when->from = g_date_time_to_unix(when->range.from);
    when->to   = g_date_time_to_unix(when->range.to);
    l = g_slist_append(l, when);
    when_last = when;
    g_date_time_unref(tmp);

    when = gpod_recent_new();
    when->name = g_strdup_printf("Recent%s: 6..12mth", extra_ ? extra_ : "");
    g_date_time_get_ymd(when_last->range.from, &dmy.y, &dmy.m, &dmy.d);
    tmp = g_date_time_new_utc(dmy.y, dmy.m, dmy.d, 23, 59, 59);
    when->range.to   = g_date_time_add_days(tmp, -1);
    when->range.from = g_date_time_add_months(when_now->range.from, -12);
    when->from = g_date_time_to_unix(when->range.from);
    when->to   = g_date_time_to_unix(when->range.to);
    l = g_slist_append(l, when);
    when_last = when;
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

    char  extra[32] = { 0 };
    g_snprintf(extra, sizeof(extra), " (%u)", album_limit_);
    GSList*  recents = gpod_recents_new(when_, extra);  // define the playlist of interest and time ranges
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
		    Itdb_Track*  track = cp->data;
		    if (track->time_added >= recent->from && track->time_added <= recent->to) {
			recent->tracks = g_slist_append(recent->tracks, track);
		    }
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

// modified glib2.0 impl from 2.66.2

#ifndef GLIB_VERSION_2_60
gboolean g_strv_equal(const gchar* const *strv1, const gchar* const *strv2)
{
    if (strv1 == NULL || strv2 == NULL) return false;
    if (strv1 == strv2) return true;

    for (; *strv1 != NULL && *strv2 != NULL; strv1++, strv2++) {
	if (!g_str_equal(*strv1, *strv2)) {
	    return false;
	}
    }
    return (*strv1 == NULL && *strv2 == NULL);
}
#endif

#ifndef GLIB_VERSION_2_62
gchar* g_date_time_format_iso8601(GDateTime *datetime)
{
    GString *outstr = NULL;
    gchar *main_date = NULL;
    gint64 offset;
    gchar *format = "%Y-%m-%dT%H:%M:%S";

    main_date = g_date_time_format (datetime, format);
    outstr = g_string_new (main_date);
    g_free (main_date);
    offset = g_date_time_get_utc_offset (datetime);
    if (offset == 0)
    {
	g_string_append_c (outstr, 'Z');
    }
    else
    {
	gchar *time_zone = g_date_time_format (datetime, "%:::z");
	g_string_append (outstr, time_zone);
	g_free (time_zone);
    }
    return g_string_free (outstr, FALSE);
}
#endif


