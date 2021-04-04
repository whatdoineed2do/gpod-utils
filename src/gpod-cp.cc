/*
 * Copyright (C) 2021 Ray whatdoineed2do @ gmail com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * based on libgpod/tests/test-cp.cc
 * Copyright (C) 2006, 2009 Christophe Fergeau
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <typeinfo>
#include <unistd.h>

#include <glib/gstdio.h>
#include <gpod/itdb.h>

#include "gpod-ffmpeg.h"

// taglib
#include <id3v2tag.h>
#include <mpegfile.h>


static Itdb_Track*
_track(const char* file_)
{
#if 0
    struct gpod_ff_media_info  mi;
    gpod_ff_media_info_init(&mi);
    char*  err = NULL;
    if (gpod_ff_scan(&mi, file_, &err) < 0) {
        g_print("failed to ff scan - %s\n", err ? err : "<no err>");
        free(err);
    }
    else {
#define str_iff_null(x) (x ? x : "")
        g_print("%s size=%u type=%s codec=%s has_audio=%d supported=%d codecid=%d bitrate=%u samplerate=%u chanels=%u length=%u bps=%u   has_meta=%d [ title=%s artist=%s album=%s ]\n", 
            mi.path, mi.file_size, mi.type, mi.codectype, mi.has_audio, mi.supported_ipod_fmt, mi.audio.bitrate, mi.audio.samplerate, mi.audio.channels, mi.audio.song_length, mi.audio.bits_per_sample,

            mi.meta.has_meta, mi.meta.title, mi.meta.artist, str_iff_null(mi.meta.album));
    } 
    gpod_ff_media_info_free(&mi);
#endif

    Itdb_Track*  track = nullptr;
    try
    {
        TagLib::MPEG::File  file(file_);

        const TagLib::AudioProperties*  props = file.audioProperties();

        /* this isn't the best test - the ctr does not know if the file is
         * really an mp3 or not.  however, it tries to determine the audio
         * properties .. and if these are junk (all 0) then we're going to
         * assume either the mp3 file is junk or its not really an mp3
         * file
         */
        if (props == nullptr || (props && props->lengthInSeconds() == 0 && props->bitrate() == 0 && props->sampleRate() == 0)) {
            return nullptr;
        }

        TagLib::Tag*  tag = file.ID3v2Tag();
        track = itdb_track_new();
        
        track->filetype = g_strdup ("MPEG audio file");
        struct stat  st;
        if (g_stat (file_, &st) == 0) {
            track->size = st.st_size;
        }
        track->tracklen = props->lengthInSeconds();
        track->bitrate = props->bitrate();
        track->samplerate = props->sampleRate();

        if (tag) {
            track->title = g_strdup(tag->title().toCString(true));
            track->album = g_strdup(tag->album().toCString(true));
            track->artist = g_strdup(tag->artist().toCString(true));
            track->genre = g_strdup(tag->genre().toCString(true));
            track->comment = g_strdup(tag->comment().toCString(true));
            track->track_nr = tag->track();
            track->year = tag->year();
        }
    }
    catch (const std::exception& ex)
    {
        g_printerr("%s: failed to parse MP3 data - %s\n", file_, ex.what());
    }
    return track;
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

int main (int argc, char *argv[])
{
    GError *error = nullptr;
    Itdb_iTunesDB*  itdb = nullptr;
    Itdb_Device*  itdev = nullptr;
    int  ret = 0;

    if (argc < 3)
    {
        char *basename = g_path_get_basename(argv[0]);
        g_print ("usage: %s [ <dir ipod mount> | <file iTunesDB>]  <file0.mp3> [<file1.mp3> ...]\n\n%s%s", basename,
                 "    This utility adds specified files to iPod/iTunesDB\n");
        g_free (basename);
        exit(-1);
    }

    _setlocale();

    char  mountpoint[PATH_MAX];
    strcpy(mountpoint, argv[1]);

    itdev = itdb_device_new();

    const char*  argtype = "unknown";
    if (g_file_test(argv[1], G_FILE_TEST_IS_DIR)) {
        itdb = itdb_parse (argv[1], &error);
        argtype = "directroy";
        itdb_device_set_mountpoint(itdev, argv[1]);
    }
    else {
        if (g_file_test(argv[1], G_FILE_TEST_EXISTS)) {
            itdb = itdb_parse_file(argv[1], &error);
            argtype = "file";
        }
        // location /mnt/iPod_Control/iTunes/iTunesDB we can figure this out
        char*  mp;
        if ( (mp = strstr(mountpoint, "iPod_Control/"))) {
            *mp = '\0';
            itdb_device_set_mountpoint(itdev, mountpoint);
        }
        else {
            return -1;
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

    // unlink track and remove from all playlists

    Itdb_Playlist*  mpl = itdb_playlist_mpl(itdb);

    char  path[PATH_MAX];
    Itdb_Track*  track;
    Itdb_Track*  tmptrack;
    GList*  it;

    bool  first = true;
    uint32_t  added = 0;
    uint32_t  requested = 0;
    struct tm  tm;
    char dt[20];

    char**  p = &argv[2];
    const uint32_t  N = argc-2;

    const Itdb_IpodInfo*  ipodinfo = itdb_device_get_ipod_info(itdev);
    const uint32_t  current = g_list_length(mpl->members);
    g_print("copying %u tracks to iPod %s %s, currently %u tracks\n", N, ipodinfo->model_number, itdb_info_get_ipod_generation_string(ipodinfo->ipod_generation), current);
    while (*p)
    {
        ++requested;
        const char*  path = *p++;

        if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
            g_printerr("[%3u/%u]  %s -> No such file or directory\n", requested, N, path);
            continue;
        }

        error = NULL;

        Itdb_Track*  track = nullptr;
        bool  ok = true;
        if ( (track = _track(path)) == nullptr) {
            ok = false;
        }
        else {
            itdb_track_add(itdb, track, -1);
            itdb_playlist_add_track(mpl, track, -1);
            ok = itdb_cp_track_to_ipod (track, path, &error);
        }

        if (ok) {
            itdb_filename_ipod2fs(track->ipod_path);
            g_print("[%3u/%u]  %s -> { ipod_path=%s title='%s' artist='%s' album='%s' }\n", requested, N, path, track->ipod_path ? track->ipod_path : "", track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "");
            ++added;
        }
        else {
            g_printerr("[%3u/%u]  %s -> { failed - %s }\n", requested, N, path, error && error->message ? error->message : "<unknown error>");
            if (track) {
                itdb_playlist_add_track(mpl, track, -1);
                itdb_track_remove(track);
            }
            if (error) g_error_free(error);
        }
    }


    if (added)
    {
        g_print("sync'ing iPod ... adding %d/%d\n", added, requested);
        itdb_write(itdb, &error);

        if (error) {
            g_printerr("failed to write iPod database, %d files NOT added- %s\n", requested, error->message ? error->message : "<unknown error>");
            g_error_free (error);
            ret = 1;
        }
        g_print("updated iPod, new total tracks=%u (originally=%u)\n", g_list_length(itdb_playlist_mpl(itdb)->members), current);
    }
    else {
        g_printerr("failed to add %d\n", requested);
    }
    itdb_device_free(itdev);
    itdb_free(itdb);

    return 0;
}
