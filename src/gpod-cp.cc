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
#include <limits.h>
#include <locale.h>

#include <glib/gstdio.h>
#include <gpod/itdb.h>

#include "gpod-ffmpeg.h"


/* parse the track info to make sure its a compatible format, if not supported 
 * attempt transcode otherwise NULL retruned
 */
static Itdb_Track*
_track(const char* file_, struct gpod_ff_transcode_ctx* xfrm_, char** err_)
{
    struct gpod_ff_media_info  mi;
    gpod_ff_media_info_init(&mi);

    if (gpod_ff_scan(&mi, file_, err_) < 0) {
	if (!mi.has_audio) {
	    *err_ = strdup("no audio");
	}
        gpod_ff_media_info_free(&mi);
        return nullptr;
    }

    Itdb_Track*  track = nullptr;
    if (!mi.supported_ipod_fmt)
    {
        /* generate a tmp transcoded file name - having this set is also the
         * indicator a on-the-fly transcoded file
         */
        snprintf(xfrm_->path, PATH_MAX, "%s-%u-%u.%s", xfrm_->tmpprfx, xfrm_->audio_opts.codec_id, time(NULL), xfrm_->extn);

        if (gpod_ff_transcode(&mi, xfrm_, err_) < 0) {
            char err[1024];
            snprintf(err, 1024, "unsupported iPod file type %u bytes %s (%s) - %s", mi.file_size, mi.type, mi.codectype, *err_ ? *err_ : "");
            if (*err_) {
                free(*err_);
            }
            *err_ = g_strdup(err);
        }
        else {
            mi.supported_ipod_fmt = true;
            mi.description = "audio (transcoded)";
        }
    }


    if (mi.supported_ipod_fmt)
    {
        track = itdb_track_new();
        
        track->mediatype = mi.has_video ? ITDB_MEDIATYPE_MOVIE : ITDB_MEDIATYPE_AUDIO;
        track->time_added = time(NULL);
        track->time_modified = track->time_added;

        track->filetype = g_strdup(mi.description);
        track->size = mi.file_size;
        track->tracklen = mi.audio.song_length;
        track->bitrate = mi.audio.bitrate;
        track->samplerate = mi.audio.samplerate;

        track->title = g_strdup(mi.meta.title);
        track->album = g_strdup(mi.meta.album);
        track->artist = g_strdup(mi.meta.artist);
        track->genre = g_strdup(mi.meta.genre);
        track->comment = g_strdup(mi.meta.comment);
        track->track_nr = mi.meta.track;
        track->year = mi.meta.year;
    }
    gpod_ff_media_info_free(&mi);
    return track;
}


/* writes the itunedb and clears pending list
 * if the itunes write fails, rollback all the files listed in pending
 */
bool  gpod_write_db(Itdb_iTunesDB* itdb, const char* mountpoint, GSList** pending)
{
    GError*  error = NULL;
    itdb_write(itdb, &error);

    bool  ret = true;

    if (error) {
	g_printerr("failed write iPod database, %u files NOT added- %s\n", g_slist_length(*pending), error->message ? error->message : "<unknown error>");
	g_error_free(error);
	error = NULL;

	// try to clean up the copied data that will dangle
	GSList*  pi = *pending;
	for (; pi; pi = pi->next) {
	    char  rollback[PATH_MAX];
	    sprintf(rollback, "%s%s", mountpoint, pi->data);
	    g_unlink(rollback);
	}

	ret = false;
    }

    g_slist_free_full(*pending, g_free);
    *pending = NULL;

    return ret;
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
        g_print ("usage: %s [ <dir ipod mount> | <file iTunesDB>]  <file0.mp3> [<file1.flac> ...]\n\n"
                 "       This utility adds specified files to iPod/iTunesDB\n"
                 "       Will automatically transcode unsupported audio (flac,wav etc) to .aac\n", basename);
        g_free (basename);
        exit(-1);
    }

    _setlocale();
    gpod_ff_init();

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
            g_printerr("failed to find iTunesDB structure under %s\n", argv[1]);
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


    Itdb_Playlist*  mpl = itdb_playlist_mpl(itdb);

    char  path[PATH_MAX];
    Itdb_Track*  track;
    Itdb_Track*  tmptrack;
    GList*  it;
    char*  err = NULL;

    bool  first = true;
    uint32_t  added = 0;
    uint32_t  requested = 0;
    struct tm  tm;
    char dt[20];

    char**  p = &argv[2];
    const uint32_t  N = argc-2;

    struct gpod_ff_transcode_ctx  xfrm;

    GSList*  pending = NULL;
    const Itdb_IpodInfo*  ipodinfo = itdb_device_get_ipod_info(itdev);
    const uint32_t  current = g_list_length(mpl->members);
    g_print("copying %u tracks to iPod %s %s, currently %u tracks\n", N, ipodinfo->model_number, itdb_info_get_ipod_generation_string(ipodinfo->ipod_generation), current);
    while (*p)
    {
        ++requested;
        const char*  path = *p++;

        g_print("[%3u/%u]  %s -> ", requested, N, path);

        if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
            g_print("{ } No such file or directory\n", path);
            continue;
        }

        error = NULL;

        gpod_ff_transcode_ctx_init(&xfrm);

        Itdb_Track*  track = nullptr;
        bool  ok = true;
        if ( (track = _track(path, &xfrm, &err)) == nullptr) {
            ok = false;
            g_print("{ } track err - %s\n", err ? err : "<>");
            g_free(err);
	    err = NULL;
        }
        else
        {
            itdb_track_add(itdb, track, -1);
            itdb_playlist_add_track(mpl, track, -1);

            g_print("{ title='%s' artist='%s' album='%s' ipod_path=", track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "");

            ok = itdb_cp_track_to_ipod (track, xfrm.path[0] ? xfrm.path : path, &error);
            if (xfrm.path[0]) {
                g_unlink(xfrm.path);
            }

            if (ok) {
                ++added;
                itdb_filename_ipod2fs(track->ipod_path);
                g_print("'%s' }\n", track->ipod_path);

		pending = g_slist_append(pending, g_strdup(track->ipod_path));

		if (added%10 == 0) {
		    // force a upd of the db and clear down pending list 
		    if (!gpod_write_db(itdb, mountpoint, &pending)) {
			break;
		    }
		}
            }
            else {
                g_print("N/A } %s\n", error->message ? error->message : "<???>");
                itdb_playlist_remove_track(mpl, track);
                itdb_track_remove(track);
            }
        }

        if (error) {
	    g_error_free(error);
	    error = NULL;
	}
    }


    if (added) {
        g_print("sync'ing iPod ...\n");  // even though we may have nothing left...
    }

    if (g_slist_length(pending)) {
	ret = gpod_write_db(itdb, mountpoint, &pending);
    }

    g_print("iPod total tracks=%u  (+%u new items)\n", g_list_length(itdb_playlist_mpl(itdb)->members), added);

    itdb_device_free(itdev);
    itdb_free(itdb);

    return ret;
}
