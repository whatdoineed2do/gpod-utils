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
#include <signal.h>

#include <glib/gstdio.h>
#include <gpod/itdb.h>

#include "gpod-ffmpeg.h"
#include "gpod-utils.h"


/* parse the track info to make sure its a compatible format, if not supported 
 * attempt transcode otherwise NULL retruned
 */
static Itdb_Track*
_track(const char* file_, struct gpod_ff_transcode_ctx* xfrm_, bool sanitize_, char** err_)
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

    track = gpod_ff_meta_to_track(&mi, sanitize_);

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


static bool  _track_exists(const Itdb_Track* track_, const struct gpod_track_fs_hash*  tfsh_, const char* path_)
{
    return gpod_track_fs_hash_contains(tfsh_, track_, path_);
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


void  _usage(const char* argv0_)
{
    char *basename = g_path_get_basename(argv0_);
    g_print ("usage: %s  -M <dir iPod mount>  [-c] [-F] [-e <encoder>] [-q <quality>] [ -S ] <file0.mp3> [<file1.flac> ...]\n\n"
             "    adds specified files to iPod/iTunesDB\n"
             "    Will automatically transcode unsupported audio (flac,wav etc) to .aac\n"
             "\n"
             "    -M <iPod dir>  location of iPod data, as directory mount point or\n"
             "    -c             generate checksum of each file in iTunesDB for \n"
             "                   comparison to prevent duplicate\n"
	     "    -F             libgpod write can corrupt iTunesDB, only allow for tested version.  Use to override\n"
	     "    -e <mp3|aac>  transcode to mp3(default)/fdkaac\n"
	     "    -q <0-9,96,128,160,192,256,320>  VBR level (ffmpeg -q:a 0-9) or CBR 96..320k\n"
	     "    -S             disable text sanitization; chars like ’ to '\n"
             ,basename);
    g_free (basename);
    exit(-1);
}


int main (int argc, char *argv[])
{
    GError *error = NULL;
    Itdb_iTunesDB*  itdb = NULL;
    Itdb_Device*  itdev = NULL;
    int  ret = 0;

    struct {
        const char*  itdb_path;
        bool cksum;
	bool  force;
	enum gpod_ff_enc  enc;
	enum gpod_ff_transcode_quality  xcode_quality;
	bool  sanitize;
    } opts = { NULL, false, false, GPOD_FF_ENC_MP3, GPOD_FF_XCODE_VBR2, true };

    int  c;
    while ( (c=getopt(argc, argv, "M:cFhe:Sq:")) != EOF)
    {
        switch (c) {
            case 'M':  opts.itdb_path = optarg;  break;
            case 'c':  opts.cksum = true;  break;
            case 'F':  opts.force = true;  break;

            case 'e':
	    {
                if      (strcasecmp(optarg, "mp3") == 0)  opts.enc = GPOD_FF_ENC_MP3;
                else if (strcasecmp(optarg, "aac") == 0)  opts.enc = GPOD_FF_ENC_FDKAAC;
                else if (strcasecmp(optarg, "aac-broken") == 0)  opts.enc = GPOD_FF_ENC_AAC;
                else                                      opts.enc = GPOD_FF_ENC_MAX;
            } break;

	    case 'q':
	    {
		const unsigned  q = (unsigned)atol(optarg);
	        if (q < 10) {
		    opts.xcode_quality = (enum gpod_ff_transcode_quality)q;
		}
		else {
		    switch (q) {
			case 96:   opts.xcode_quality = GPOD_FF_XCODE_CBR96  ; break;
			case 128:  opts.xcode_quality = GPOD_FF_XCODE_CBR128 ; break;
			case 160:  opts.xcode_quality = GPOD_FF_XCODE_CBR160 ; break;
			case 192:  opts.xcode_quality = GPOD_FF_XCODE_CBR192 ; break;
			case 256:  opts.xcode_quality = GPOD_FF_XCODE_CBR256 ; break;
			case 320:  opts.xcode_quality = GPOD_FF_XCODE_CBR320 ; break;

		        default:
			   ; // noop
		    }
		}
	    } break;

            case 'S':  opts.sanitize = false;  break;

            case 'h':
            default:
                _usage(argv[0]);
        }
    }

    if (opts.itdb_path == NULL || opts.enc == GPOD_FF_ENC_MAX) {
        _usage(argv[0]);
    }

    if ( !(optind < argc) ) {
        g_printerr("no inputs\n");
        _usage(argv[0]);
    }


    gpod_setlocale();
    gpod_ff_init();

    char  mountpoint[PATH_MAX];


    if (g_file_test(opts.itdb_path, G_FILE_TEST_IS_DIR)) {
        itdb = itdb_parse (opts.itdb_path, &error);
	itdev = itdb_device_new();
        itdb_device_set_mountpoint(itdev, opts.itdb_path);
        strcpy(mountpoint, opts.itdb_path);
    }
 
    if (error)
    {
        if (error->message) {
            g_printerr("failed to prase iTunesDB via %s - %s\n", opts.itdb_path, error->message);
        }
        g_error_free (error);
        error = NULL;
        return -1;
    }

    if (itdb == NULL) {
        g_print("failed to open iTunesDB via %s\n", opts.itdb_path);
        return -1;
    }

    // everything is ok, writes/updates can start so lock
    if ( (ret = gpod_cp_init() < 0)) {
        g_printerr("unable to obtain process lock on %s (%s) - exitting to avoid concurrent iTunesDB update\n", GPOD_CP_LOCKFILE, strerror(-ret));
        return 2;
    }


    Itdb_Playlist*  mpl = itdb_playlist_mpl(itdb);

    GList*  it;
    char*  err = NULL;

    bool  first = true;
    uint32_t  added = 0;
    uint32_t  requested = 0;
    struct tm  tm;
    char dt[20];

    GSList*  files = NULL;
    int  i = optind;
    while (i < argc) {
        gpod_walk_dir(argv[i++], &files);
    }
    const uint32_t  N = g_slist_length(files);

    struct {
	uint32_t  music;
	uint32_t  video;
	uint32_t  other;
	size_t    bytes;
    } stats = { 0, 0, 0, 0 };

    struct gpod_ff_transcode_ctx  xfrm;

    GSList*  pending = NULL;
    const Itdb_IpodInfo*  ipodinfo = itdb_device_get_ipod_info(itdev);

#define SUPPORT_DEVICE  1 << 1
#define SUPPORT_FORCED  1 << 2

    const unsigned  support = gpod_write_supported(ipodinfo) ?
                                SUPPORT_DEVICE : 0 | opts.force ? SUPPORT_FORCED : 0;

    const char*  extra = "";
    if (support & SUPPORT_DEVICE) {
	// noop
    }
    else if (support & SUPPORT_FORCED) {
	extra = " - FORCED support for device";
    }
    else {
	extra = " - UNSUPPORTED device";
	ret = -1;
    }

    const uint32_t  current = g_list_length(mpl->members);
    g_print("copying %u tracks to iPod %s %s, currently %u tracks%s\n", N,
		itdb_info_get_ipod_generation_string(ipodinfo->ipod_generation),
		ipodinfo->model_number, 
		current, extra);

    struct gpod_track_fs_hash  tfsh;
    if (opts.cksum && (support & (SUPPORT_DEVICE|SUPPORT_FORCED) )) {
        gpod_track_fs_hash_init(&tfsh, itdb);
    }

    Itdb_Track*  track = NULL;
    const guint  then = g_get_monotonic_time();
    GSList*  p = files;
    while (p && !gpod_stop && (support & (SUPPORT_DEVICE|SUPPORT_FORCED)) )
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

        gpod_ff_transcode_ctx_init(&xfrm, opts.enc, opts.xcode_quality);

        bool  ok = true;
        if ( (track = _track(path, &xfrm, opts.sanitize, &err)) == NULL) {
            ok = false;
            g_print("{ } track err - %s\n", err ? err : "<>");
            g_free(err);
	    err = NULL;
        }
        else
        {
            g_print("{ title='%s' artist='%s' album='%s' ipod_path=", track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "");

            const bool  dupl = opts.cksum && _track_exists(track, &tfsh, xfrm.path[0] ? xfrm.path : path);

            if (dupl) {
                g_print(" *** DUPL *** }\n");
                itdb_track_free(track);
                track = NULL;
            }
            else
            {
                itdb_track_add(itdb, track, -1);
                itdb_playlist_add_track(mpl, track, -1);

                ok = itdb_cp_track_to_ipod (track, xfrm.path[0] ? xfrm.path : path, &error);


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
		    stats.bytes += track->size;

                    if (added%10 == 0) {
                        // force a upd of the db and clear down pending list 
                        if (gpod_write_db(itdb, mountpoint, &pending) < 0) {
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

            if (xfrm.path[0]) {
                g_unlink(xfrm.path);
            }
        }

        if (error) {
	    g_error_free(error);
	    error = NULL;
	}
    }
    if (opts.cksum) {
        gpod_track_fs_hash_destroy(&tfsh);
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

    char  userterm[128] = { 0 };
    if (gpod_stop) {
	snprintf(userterm, sizeof(userterm), " -- user terminated, %u items ignored", N-added);
    }

    char  stats_size[128] = { 0 };
    if (stats.bytes) {
	gpod_bytes_to_human(stats_size, sizeof(stats_size), stats.bytes, true);
    }


    g_print("iPod total tracks=%u  %u/%u items %s  music=%u video=%u other=%u  in %s%s\n", g_list_length(itdb_playlist_mpl(itdb)->members), ret < 0 ? 0 : added, N, stats_size, stats.music, stats.video, stats.other, duration, userterm);

    itdb_device_free(itdev);
    itdb_free(itdb);

    gpod_cp_destroy();

    return ret;
}
