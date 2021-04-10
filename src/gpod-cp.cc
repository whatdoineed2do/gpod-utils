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

#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>

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
            if (*err_) {
                const char*  err = "no audio - ";
                char*  tmp = (char*)malloc(strlen(*err_) + strlen(err) +1);
                sprintf(tmp, "%s%s", err, *err_);
                free(*err_);
                *err_ = tmp;
            }
            else {
                *err_ = strdup("no audio");
            }
	}
        gpod_ff_media_info_free(&mi);
        return NULL;
    }

    Itdb_Track*  track = NULL;
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
int  gpod_write_db(Itdb_iTunesDB* itdb, const char* mountpoint, GSList** pending)
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

    return ret ? 0 : -1;
}

static void  walk_dir(const gchar *dir, GSList **l) 
{
    GDir*  dir_handle;
    const gchar*  filename;
    gchar*  path;

    if (!g_file_test(dir, G_FILE_TEST_IS_DIR)) {
        *l = g_slist_append(*l, g_strdup(dir));
        return;
    }

    if ( (dir_handle = g_dir_open(dir, 0, NULL)) == NULL) {
        return;
    }

    while ((filename = g_dir_read_name(dir_handle)))
    {
        path = g_build_filename(dir, filename, NULL);

        if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
            walk_dir(path, l);
            g_free(path);
        }
        else {
            *l = g_slist_append(*l, path);
        }
    }

    g_dir_close(dir_handle);
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


static bool  gpod_stop = false;
static void  _sighandler(const int sig_)
{
    gpod_stop = true;
}

#define GPOD_CP_LOCKFILE  "/tmp/.gpod-cp.pid"
static int  gpod_lockfd;
int  gpod_cp_init()
{
    struct sigaction  act, oact;

    memset(&act,  0, sizeof(struct sigaction));
    memset(&oact, 0, sizeof(struct sigaction));

    act.sa_handler = _sighandler;
    sigemptyset(&act.sa_mask);

    const int   sigs[] = { SIGINT, SIGTERM, SIGHUP, -1 };
    const int*  p = sigs;
    while (*p != -1) {
        sigaddset(&act.sa_mask, *p++);
    }

    p = sigs;
    while (*p != -1) {
        sigaction(*p++, &act, &oact);
    }


    // this is a cheap attempt to stop multiple updates potentially killing your iTunesDB
    if (access(GPOD_CP_LOCKFILE, F_OK) == 0) {
        return -EEXIST;
    }

    if ( (gpod_lockfd=open(GPOD_CP_LOCKFILE, O_CREAT|O_TRUNC|O_WRONLY, 0660)) < 0 &&
         flock(gpod_lockfd, LOCK_EX) < 0) 
    {
        return -errno;
    }
    char  buf[16];
    sprintf(buf, "%u\n", getpid());
    write(gpod_lockfd, buf, strlen(buf));
    return 0;
}

void  gpod_cp_destroy()
{
    flock(gpod_lockfd, LOCK_UN);
    close(gpod_lockfd);
    unlink(GPOD_CP_LOCKFILE);
}


int main (int argc, char *argv[])
{
    GError *error = NULL;
    Itdb_iTunesDB*  itdb = NULL;
    Itdb_Device*  itdev = NULL;
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

    // everything is ok, writes/updates can start so lock
    if ( (ret = gpod_cp_init() < 0)) {
        g_printerr("unable to obtain process lock on %s (%s) - exitting to avoid concurrent iTunesDB update\n", GPOD_CP_LOCKFILE, strerror(-ret));
        return 2;
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

    GSList*  files = NULL;
    char**  pa = &argv[2];
    while (*pa) {
        walk_dir(*pa++, &files);
    }
    const uint32_t  N = g_slist_length(files);

    struct {
	uint32_t  music;
	uint32_t  video;
	uint32_t  other;
    } stats = { 0, 0, 0 };

    struct gpod_ff_transcode_ctx  xfrm;

    GSList*  pending = NULL;
    const Itdb_IpodInfo*  ipodinfo = itdb_device_get_ipod_info(itdev);
    const uint32_t  current = g_list_length(mpl->members);
    g_print("copying %u tracks to iPod %s %s, currently %u tracks\n", N, ipodinfo->model_number, itdb_info_get_ipod_generation_string(ipodinfo->ipod_generation), current);

    const guint  then = g_get_monotonic_time();
    GSList*  p = files;
    while (p && !gpod_stop)
    {
        ++requested;
        const char*  path = (const char*)(p->data);
        p = p->next;

        g_print("[%3u/%u]  %s -> ", requested, N, path);

        if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
            g_print("{ } No such file or directory\n", path);
            continue;
        }

        error = NULL;

        gpod_ff_transcode_ctx_init(&xfrm);

        Itdb_Track*  track = NULL;
        bool  ok = true;
        if ( (track = _track(path, &xfrm, &err)) == NULL) {
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

            if (ok)
	    {
                ++added;
                itdb_filename_ipod2fs(track->ipod_path);
                g_print("'%s' }\n", track->ipod_path);

		pending = g_slist_append(pending, g_strdup(track->ipod_path));

		switch (track->mediatype) {
		    case ITDB_MEDIATYPE_AUDIO:  ++stats.music;  break;
		    case ITDB_MEDIATYPE_MOVIE:  ++stats.video;  break;
		    default: ++stats.other;
		}

		if (added%10 == 0) {
		    // force a upd of the db and clear down pending list 
		    if (!gpod_write_db(itdb, mountpoint, &pending)) {
			break;
		    }
		}
            }
            else {
                g_print("N/A } %s\n", error->message ? error->message : "<unknown err>");
                itdb_playlist_remove_track(mpl, track);
                itdb_track_remove(track);
            }
        }

        if (error) {
	    g_error_free(error);
	    error = NULL;
	}
    }
    g_slist_free_full(files, g_free);
    files = NULL;


    if (added) {
        g_print("sync'ing iPod ...\n");  // even though we may have nothing left...
    }

    if (g_slist_length(pending)) {
	ret = gpod_write_db(itdb, mountpoint, &pending);
    }

    char duration[32];
    {
        const guint  now = g_get_monotonic_time();
        const unsigned  sec = (now-then)/1000000;
        unsigned  h, m, s;
        h = (sec/3600);
        m = (sec -(3600*h))/60;
        s = (sec -(3600*h)-(m*60));
        if (sec < 60) {
            sprintf(duration, "%.3f secs", (now-then)/1000000.0);
        }
        else
        {
            if (sec < 3600) {
                sprintf(duration, "%02d:%02d mins:secs", m,s);
            }
            else {
                if (sec < 3600*60) {
                    sprintf(duration, "%02d:%02d:%02d", h,m,s);
                }
                else {
                    strcpy(duration, "inf");
                }
            }
        }
    }

    char  userterm[128];
    if (gpod_stop) {
	snprintf(userterm, sizeof(userterm), " -- user terminated, %u items ignored", N-added);
    }
    else {
	userterm[0] = '\0';
    }
    g_print("iPod total tracks=%u  (+%u/%u items music=%u video=%u other=%u  in %s%s)\n", g_list_length(itdb_playlist_mpl(itdb)->members), ret < 0 ? 0 : added, N, stats.music, stats.video, stats.other, duration, userterm);

    itdb_device_free(itdev);
    itdb_free(itdb);

    gpod_cp_destroy();

    return ret;
}
