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

#ifndef GPOD_UTILS_H
#define GPOD_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include <glib.h>
#include <gpod/itdb.h>


const char*  gpod_setlocale();
char*  gpod_trim(const char* what_);

bool  gpod_write_supported(const Itdb_IpodInfo* ipi_);

// recursively walk dir, adding files as strings to the list
void  gpod_walk_dir(const gchar* dir_, GSList **l_);


// replace some special char/strings to ascii like compatriots
char*  gpod_sanitize_text(char* what_, bool sanitize_);

void   gpod_bytes_to_human(char* buf_, unsigned bufsz_, size_t  bytes_, bool wrap_);


// mountpoint is alternative
guint  gpod_hash(const Itdb_Track* track_);
guint  gpod_hash_file(const char* track_);


struct gpod_track_fs_hash {
    GHashTable*  tbl;
};

void  gpod_track_fs_hash_init(struct gpod_track_fs_hash* htbl_, Itdb_iTunesDB* itdb_);
void  gpod_track_fs_hash_destroy(struct gpod_track_fs_hash* htbl_);

bool  gpod_track_fs_hash_contains(const struct gpod_track_fs_hash* htbl_, const Itdb_Track* track_, const char* path_);

GTree*       gpod_track_key_tree_create(Itdb_iTunesDB *itdb_);
void         gpod_track_key_tree_destroy(GTree* tree_);

GHashTable*  gpod_track_htbl_create(Itdb_iTunesDB* itdb_);
void         gpod_track_htbl_destroy(GHashTable* htbl_);

// create recent playlists from given date
void  gpod_playlist_recent(unsigned* playlists_, unsigned* tracks_,
	                   Itdb_iTunesDB* itdb_, unsigned album_limit_, gint64  when_);


// raspberry pi buster ships 2.58
#ifndef GLIB_VERSION_2_60
gboolean g_strv_equal(const gchar* const *strv1, const gchar* const *strv2);
#endif

#ifndef GLIB_VERSION_2_62
gchar* g_date_time_format_iso8601(GDateTime *datetime);
#endif

#ifdef __cplusplus
}
#endif

#endif
