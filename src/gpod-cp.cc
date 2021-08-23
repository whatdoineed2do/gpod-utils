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
#include <stdarg.h>

#include <glib/gstdio.h>
#include <gpod/itdb.h>

#include "gpod-ffmpeg.h"
#include "gpod-utils.h"

struct {
    const char*  itdb_path;
    bool cksum;
    bool  force;
    enum gpod_ff_enc  enc;
    bool enc_fallback;
    enum gpod_ff_transcode_quality  xcode_quality;
    bool  sanitize;
    struct {
      const char*  pl;
      unsigned  limit;
    } recent;
    unsigned short  max_threads;
} opts = { NULL, false, false, GPOD_FF_ENC_FDKAAC, true, GPOD_FF_XCODE_VBR1, true };

struct {
    uint32_t  music;
    uint32_t  video;
    uint32_t  other;
    size_t    bytes;
} stats = { 0, 0, 0, 0 };

struct gpod_cp_log_ctx {
    uint32_t  requested;
    uint32_t  N;
    const char* path;
};

static void  gpod_cp_log(const struct gpod_cp_log_ctx* ctx_, const char* fmt_, ...)
{
    char*  tmp = NULL;

    va_list  args;
    va_start(args, fmt_);
    g_vasprintf(&tmp, fmt_, args);
    va_end(args);

    g_print("[%3u/%u]  %s -> %s", ctx_->requested, ctx_->N, ctx_->path, tmp);
    g_free(tmp);
}

/* parse the track info to make sure its a compatible format, if not supported 
 * attempt transcode otherwise NULL retruned
 */
static Itdb_Track*
_track(const char* file_, struct gpod_ff_transcode_ctx* xfrm_, Itdb_IpodGeneration idevice_, bool sanitize_, char** err_)
{
    struct gpod_ff_media_info  mi;
    gpod_ff_media_info_init(&mi);

    if (gpod_ff_scan(&mi, file_, idevice_, err_) < 0) {
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
	if (mi.has_audio && !mi.has_video)
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
	else
	{
	    if (mi.has_video)
	    {
		char err[1024];
		snprintf(err, 1024, "unsupported iPod video file: %s %ux%u @ %i kb/s, %.3f fps, %i channels @ %i",
                                mi.type, mi.video.width, mi.video.height, mi.video.bitrate/1000, mi.video.fps, mi.audio.channels, mi.audio.bitrate);

		*err_ = g_strdup(err);
	    }
	}
    }

    track = gpod_ff_meta_to_track(&mi, sanitize_);

    gpod_ff_media_info_free(&mi);
    return track;
}


static bool  _track_exists(const Itdb_Track* track_, const struct gpod_track_fs_hash*  tfsh_, const char* path_)
{
    return gpod_track_fs_hash_contains(tfsh_, track_, path_);
}


/* writes the itunedb and clears pending list
 * if the itunes write fails, rollback all the files listed in pending
 */
static int  gpod_write_db(Itdb_iTunesDB* itdb, const char* mountpoint, GSList** pending)
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

static int  gpod_cp_track(const struct gpod_cp_log_ctx* lctx_,
                          Itdb_iTunesDB* itdb, Itdb_Playlist* mpl_, Itdb_Track** track_, const char* mountpoint, uint32_t* added_,
                          struct gpod_ff_transcode_ctx* xfrm_, const char* path_,
                          GSList** pending_,
                          struct gpod_track_fs_hash*  tfsh_,
                          Itdb_Playlist**  recentpl_,
                          GError** error_)
{
    Itdb_Track*  track = *track_;
    Itdb_Playlist*  recentpl = *recentpl_;


    const bool  dupl = opts.cksum && _track_exists(track, tfsh_, xfrm_->path[0] ? xfrm_->path : path_);

    if (dupl) {
        gpod_cp_log(lctx_, "{ title='%s' artist='%s' album='%s' ipod_path= *** DUPL *** }\n", track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "");
        itdb_track_free(*track_);
        *track_ = NULL;
    }
    else
    {
        itdb_track_add(itdb, track, -1);
        itdb_playlist_add_track(mpl_, track, -1);

        bool  ok = itdb_cp_track_to_ipod (track, xfrm_->path[0] ? xfrm_->path : path_, error_);

        if (ok)
        {
            ++(*added_);
            itdb_filename_ipod2fs(track->ipod_path);
            gpod_cp_log(lctx_, "{ title='%s' artist='%s' album='%s' ipod_path='%s' }\n", track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "", track->ipod_path);

            *pending_ = g_slist_append(*pending_, g_strdup(track->ipod_path));

            switch (track->mediatype) {
                case ITDB_MEDIATYPE_AUDIO:  ++stats.music;  break;
                case ITDB_MEDIATYPE_MOVIE:  ++stats.video;  break;
                default: ++stats.other;
            }
            stats.bytes += track->size;

            // req'd to add to playlist
            if (opts.recent.pl && recentpl == NULL)
            {
                recentpl = itdb_playlist_by_name(itdb, (gchar*)opts.recent.pl);
                if (recentpl == NULL) {
                    recentpl = itdb_playlist_new(opts.recent.pl, false);
                    itdb_playlist_add(itdb, recentpl, -1);
                }
            }

            if (recentpl)
            {
                // always add at the top of playlist and drop off any tracks past imposed limit
                itdb_playlist_add_track(recentpl, track, 0);
                if (g_list_length(recentpl->members) > opts.recent.limit)
                {
                    int  plcnt = 0;
                    for (GList* plelem=recentpl->members; plelem; plelem=plelem->next) {
                        if (plcnt++ >= opts.recent.limit) {
                            itdb_playlist_remove_track(recentpl, (Itdb_Track*)plelem->data);
                        }
                    }
                }
            }

            if ((*added_)%10 == 0) {
                // force a upd of the db and clear down pending list 
                if (gpod_write_db(itdb, mountpoint, pending_) < 0) {
                    return -1;
                }
            }
        }
        else {
            gpod_cp_log(lctx_, "{ title='%s' artist='%s' album='%s' ipod_path=N/A } %s\n", track->title ? track->title : "", track->artist ? track->artist : "", track->album ? track->album : "", (*error_)->message ? (*error_)->message : "<unknown err>");
            itdb_playlist_remove_track(mpl_, track);
            itdb_track_remove(track);
        }
    }

    if (xfrm_->path[0]) {
        g_unlink(xfrm_->path);
    }
    return 0;
}

static bool  gpod_stop = false;

struct gpod_cp_pool_args {
    unsigned  fatal;
    GMutex  cp_lck;

    GSList*  failed;
    GMutex  failed_lck;
    const Itdb_IpodInfo* ipodinfo;
};

struct gpod_cp_pool_args*  gpod_cp_pa_init(const Itdb_IpodInfo* ipodinfo_, GSList* failed_)
{
    struct gpod_cp_pool_args*  args = (gpod_cp_pool_args*)malloc(sizeof(struct gpod_cp_pool_args));
    memset(args, 0, sizeof(struct gpod_cp_pool_args));

    args->ipodinfo = ipodinfo_;

    args->failed = failed_;

    g_mutex_init(&args->failed_lck);
    g_mutex_init(&args->cp_lck);

    return args;
}

void  gpod_cp_pa_free(struct gpod_cp_pool_args*  args_)
{
    g_mutex_clear(&args_->failed_lck);
    g_mutex_clear(&args_->cp_lck);
}

struct gpod_cp_thread_args {
    Itdb_iTunesDB* itdb;
    Itdb_Playlist* mpl;
    const char* mountpoint;
    uint32_t* added;
    GSList* pending;
    struct gpod_track_fs_hash*  tfsh;
    Itdb_Playlist*  recentpl;

    char*     path;
    unsigned  N;
    uint32_t  requested;
};

struct gpod_cp_thread_args*  gpod_cp_ta_init(
        Itdb_iTunesDB* itdb_, Itdb_Playlist* mpl_, const char* mountpoint_,
        uint32_t* added_, GSList* pending_,
        struct gpod_track_fs_hash*  tfsh_,
        Itdb_Playlist*  recentpl_,
        const char* path_, unsigned N_, uint32_t requested_)
{
    struct gpod_cp_thread_args*  args = (struct gpod_cp_thread_args*)malloc(sizeof(struct gpod_cp_thread_args));
    memset(args, 0, sizeof(struct gpod_cp_thread_args));

    args->itdb = itdb_;
    args->mpl = mpl_;
    args->mountpoint = mountpoint_;
    args->added = added_;
    args->pending = pending_;
    args->tfsh = tfsh_;
    args->recentpl = recentpl_;

    args->path = strdup(path_);
    args->N = N_;
    args->requested = requested_;

    return args;
}

void gpod_cp_ta_free(struct gpod_cp_thread_args* obj_)
{
    free(obj_->path);
    free(obj_);
}

void gpod_cp_thread(gpointer args_, gpointer pool_args_)
{
    struct gpod_cp_thread_args*  args = (struct gpod_cp_thread_args*)args_;
    struct gpod_cp_pool_args*  pargs = (struct gpod_cp_pool_args*)pool_args_;

    GError *error = NULL;
    char*  err = NULL;
    Itdb_Track*  track = NULL;
    struct gpod_ff_transcode_ctx  xfrm;

    const struct gpod_cp_log_ctx  lctx = { 
      args->requested, args->N, args->path
    };

    if (gpod_stop) {
        goto thread_cleanup;
    }

    if (!g_file_test(args->path, G_FILE_TEST_EXISTS)) {
        gpod_cp_log(&lctx, "{ } No such file or directory\n");
        goto thread_cleanup;
    }

    gpod_ff_transcode_ctx_init(&xfrm, opts.enc, opts.xcode_quality);

    if ( (track = _track(args->path, &xfrm, pargs->ipodinfo->ipod_generation, opts.sanitize, &err)) == NULL) {
        gpod_cp_log(&lctx, "{ } track err - %s\n", err ? err : "<>");
        g_free(err);
        err = NULL;

        g_mutex_lock(&pargs->failed_lck);
        pargs->failed = g_slist_append(pargs->failed, (gpointer)args->path);
        g_mutex_unlock(&pargs->failed_lck);
    }
    else
    {
        if (gpod_stop) {
            goto thread_cleanup;
        }

        g_mutex_lock(&pargs->cp_lck);
        if (gpod_cp_track(&lctx,
                          args->itdb, args->mpl, &track, args->mountpoint, args->added,
                          &xfrm, args->path, &args->pending, args->tfsh, &args->recentpl,
                          &error) < 0) {
            ++(pargs->fatal);
        }
        g_mutex_unlock(&pargs->cp_lck);
    }

thread_cleanup:
    if (error) {
        g_error_free(error);
        error = NULL;
    }

    gpod_cp_ta_free(args);
}


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
    g_print ("usage: %s  -M <dir iPod mount>  [-c] [-F] [-e <encoder>] [-q <quality>] [ -S ] [[-P recent playlist name ] [-N playlist limit]] <file0.mp3> [<file1.flac> ...]\n\n"
             "    adds specified files to iPod/iTunesDB\n"
             "    Will automatically transcode unsupported audio (flac,wav etc) to .m4a\n"
             "\n"
             "    -M <iPod dir>  location of iPod data, as directory mount point or\n"
             "    -c             generate checksum of each file in iTunesDB for \n"
             "                   comparison to prevent duplicate\n"
	     "    -F             libgpod write can corrupt iTunesDB, only allow for tested version.  Use to override\n"
	     "    -e <mp3|aac|alac>   transcode to mp3/fdkaac/alac - default to aac\n"
	     "    -E             disable encoding fallback\n"
	     "    -q <0-9,96,128,160,192,256,320>  VBR level (ffmpeg -q:a 0-9) or CBR 96..320k (not applicable for alac)\n"
	     "    -S             disable text sanitization; chars like ’ to '\n"
             "\n"
	     "    -P <name>      generate our 'recently added' playlist\n"
	     "    -N <limit>     'recently added' pl limit'\n"
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

    opts.recent.pl = NULL;
    opts.recent.limit = 50;
    opts.max_threads = sysconf(_SC_NPROCESSORS_ONLN);

    int  c;
    while ( (c=getopt(argc, argv, "M:cFhEe:Sq:P:N:T:")) != EOF)
    {
        switch (c) {
            case 'M':  opts.itdb_path = optarg;  break;
            case 'c':  opts.cksum = true;  break;
            case 'F':  opts.force = true;  break;

	    case 'E':
		opts.enc_fallback = false;
		break;

            case 'e':
	    {
                if      (strcasecmp(optarg, "mp3") == 0)  opts.enc = GPOD_FF_ENC_MP3;
                else if (strcasecmp(optarg, "aac") == 0)  opts.enc = GPOD_FF_ENC_FDKAAC;
                else if (strcasecmp(optarg, "alac") == 0)
		{ 
		    opts.enc = GPOD_FF_ENC_ALAC;
		    opts.xcode_quality = GPOD_FF_XCODE_MAX;
		}
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

            case 'P':  opts.recent.pl = optarg;  break;
            case 'N':  opts.recent.limit = atoi(optarg);  break;

            case 'S':  opts.sanitize = false;  break;
            case 'T':
            {
                unsigned short  req_max_threads = (unsigned short)atoi(optarg);
                if (req_max_threads > opts.max_threads*2) {
                    req_max_threads = opts.max_threads*2;
                }
                opts.max_threads = req_max_threads;
            } break;

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
    GSList*  failed = NULL;
    int  i = optind;
    while (i < argc) {
        gpod_walk_dir(argv[i++], &files);
    }
    const uint32_t  N = g_slist_length(files);


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

    /* validate that the requested xcode encoder is supported; we expect that 
     * mp3 is supported!
     */
    if (!gpod_ff_enc_supported(opts.enc))
    {
	extra = "";
	opts.enc = GPOD_FF_ENC_MAX;
	if (opts.enc_fallback) {
	    opts.enc = GPOD_FF_ENC_MP3;
	    extra = ", falling back to MP3 encoding";
	}
	g_printf("requested transcoding NOT available%s\n", extra);
    }

    struct gpod_track_fs_hash  tfsh;
    if (opts.cksum && (support & (SUPPORT_DEVICE|SUPPORT_FORCED) )) {
        gpod_track_fs_hash_init(&tfsh, itdb);
    }

    Itdb_Playlist*  recentpl = NULL;
    Itdb_Track*  track = NULL;

    // create thread pool and throw all tasks (direct cp and xcode)
    struct gpod_cp_pool_args*  pool_args = gpod_cp_pa_init(ipodinfo, failed);

    GThreadPool*  tp = g_thread_pool_new((GFunc)gpod_cp_thread, (gpointer)pool_args,
                                         opts.max_threads,
                                         TRUE, NULL);

    g_print("processing %u tracks over %u threads\n", N, opts.max_threads);

    const guint  then = g_get_monotonic_time();
    GSList*  p = files;
    while (p && !gpod_stop && (support & (SUPPORT_DEVICE|SUPPORT_FORCED)) )
    {
        ++requested;
        const char*  path = (const char*)(p->data);
        p = p->next;

        struct gpod_cp_thread_args*  args = gpod_cp_ta_init(itdb, mpl, mountpoint, &added, pending, &tfsh, recentpl, path, N, requested);
        g_thread_pool_push(tp, (void*)args, NULL);
    }

    // wait for all tasks
    g_thread_pool_free(tp, FALSE, TRUE);
    gpod_cp_pa_free(pool_args);
    pool_args = NULL;
    tp = NULL;

    if (opts.cksum) {
        gpod_track_fs_hash_destroy(&tfsh);
    }

    if (failed)
    {
	g_print("failed tracks:\n");
	for (p=failed; p!=NULL; p=p->next) {
	    g_print("  %s\n", p->data);
	}
	g_slist_free(failed);
	failed = NULL;
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
