/*
 * Copyright (c) 2021 Ray whatdoineed2do at gmail com
 * 'remux' the input file to output, sync'ing up metadata with iTunesDB
 *
 * based on https://ffmpeg.org/doxygen/trunk/remuxing_8c-example.html
 * Copyright (c) 2013 Stefano Sabatini
 */


#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>

#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>

#include <glib/gstdio.h>

#include <gpod/itdb.h>
#include "gpod-utils.h"


#define track_offsetof(field) offsetof(Itdb_Track, field)

struct Metadata {
    const char *key;  // as recognised by ffmpeg
    bool    int_fld;
    size_t  offset;   // correspoding element in Itdb_Track
};

static const struct Metadata  metadata_map[] = {
    { "title",        false, track_offsetof(title) },
    { "artist",       false, track_offsetof(artist) },
    { "album_artist", false, track_offsetof(albumartist) },
    { "album",        false, track_offsetof(album) },
    { "genre",        false, track_offsetof(genre) },
    { "composer",     false, track_offsetof(composer) },
    { "track",        true,  track_offsetof(track_nr) },
    { "disc",         true,  track_offsetof(cd_nr) },
    { "date",         true,  track_offsetof(year) },

    { NULL, false, 0 }
};

// extra tags that are not directly settable by the generic ffmpeg tags
// but these get set as user defined strings
static const struct Metadata  metadata_mp3_map[] = {
    { "POPM",  true, track_offsetof(rating) },

    { NULL, false, 0 }
};

static const struct Metadata  metadata_aac_map[] = {
    // { "rtng",  true, track_offsetof(rating) }, // not set??

    { NULL, false, 0 }
};



static int  _set_metadata(AVDictionary* meta_, const Itdb_Track* track_, const struct Metadata* p_)
{
    /* this is a little messy - the 'track_' is a ptr an obj
     * which in itself bunch of char* or ints, ie
     *    struct ItDb_Track { char* title; int track_nr; ... }
     * we step over the mem and find its now a ptr the original
     * data, hence const char**
     */
    char  buf[128] = { 0 };
    const char*  value = NULL;
    if (p_->int_fld) {
	const gint32  tmp = *(const gint32*) ((void*)track_ + p_->offset);
	snprintf(buf, sizeof(buf), "%u", tmp);
	value = buf;
    }
    else {
	value = *(const char**) ((void*)track_ + p_->offset);
    }

    av_dict_set(&meta_, p_->key, value, 0);
}


static int  _sync_metadata(AVDictionary* ctx_meta_, AVDictionary* stream_meta_, const AVCodecParameters* codecpar_, const Itdb_Track* track_)
{
    const struct Metadata*  p = NULL;

    if (ctx_meta_)
    {
	p = metadata_map;
	while (p->key) {
	    _set_metadata(ctx_meta_, track_, p);
	    ++p;
	}
    }

    if (stream_meta_)
    {
	switch (codecpar_->codec_id) {
	    case AV_CODEC_ID_MP3:
		p = metadata_mp3_map;
		break;

	    case AV_CODEC_ID_AAC:
		p = metadata_aac_map;
		break;

	    default:
		p = NULL;
	}

	while (p && p->key) {
	    _set_metadata(ctx_meta_, track_, p);
	    ++p;
	}
    }
}

static int  _extract(const char* src_, const char* dest_, const Itdb_Track* track_)
{
    int  ret = 0;

    AVFormatContext *in_fmt_ctx = NULL;
    AVFormatContext *out_fmt_ctx = NULL;
    AVPacket pkt;

    int i;
    int stream_idx = 0;
    int *stream_mapping = NULL;
    int number_of_streams = 0;


    if ((ret = avformat_open_input (&in_fmt_ctx, src_, NULL, NULL)) < 0) {
	fprintf (stderr, "Could not open input file '%s'", src_);
        goto end;
    }
    if ((ret = avformat_find_stream_info (in_fmt_ctx, NULL)) < 0) {
	fprintf (stderr, "Failed to retrieve input stream information");
	goto end;
    }

    avformat_alloc_output_context2 (&out_fmt_ctx, NULL, NULL, dest_);
    if (!out_fmt_ctx) {
	fprintf (stderr, "Could not create output context");
	ret = AVERROR_UNKNOWN;
	goto end;
    }

    number_of_streams = in_fmt_ctx->nb_streams;
    stream_mapping = av_calloc(number_of_streams, sizeof (*stream_mapping));

    if (!stream_mapping) {
	ret = AVERROR (ENOMEM);
	goto end;
    }

    // copy the basic/generic meta
    AVDictionaryEntry*  tag = NULL;
    while ((tag = av_dict_get(in_fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        av_dict_set(&(out_fmt_ctx->metadata), tag->key, tag->value, 0);
    }

    for (i = 0; i < in_fmt_ctx->nb_streams; i++)
    {
	AVStream *out_stream;
	AVStream *in_stream = in_fmt_ctx->streams[i];
	AVCodecParameters *in_codecpar = in_stream->codecpar;
	if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
	    in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
	    in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
	{
	    stream_mapping[i] = -1;
	    continue;
	}

	stream_mapping[i] = stream_idx++;
	out_stream = avformat_new_stream (out_fmt_ctx, NULL);
	if (!out_stream)
	{
	    fprintf (stderr, "Failed allocating output stream");
	    ret = AVERROR_UNKNOWN;
	    goto end;
	}
	ret = avcodec_parameters_copy (out_stream->codecpar, in_codecpar);
	if (ret < 0) {
	    fprintf (stderr, "Failed to copy codec parameters");
	    goto end;
	}

	if (in_codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
	{
	    if (in_stream->metadata) {
		while ((tag = av_dict_get(in_stream->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
		    av_dict_set(&(out_stream->metadata), tag->key, tag->value, 0);
	      }
	    }

	    if (track_) {
	        _sync_metadata(out_fmt_ctx->metadata, out_stream->metadata, out_stream->codecpar, track_);
	    }
	}
    }
    av_dump_format (out_fmt_ctx, 0, dest_, 1);

    //if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE))
    {
	ret = avio_open (&out_fmt_ctx->pb, dest_, AVIO_FLAG_WRITE);
	if (ret < 0) {
	    fprintf (stderr, "Could not open output file '%s'", dest_);
	    goto end;
	}
    }
    AVDictionary *opts = NULL;

    ret = avformat_write_header (out_fmt_ctx, &opts);
    if (ret < 0) {
	fprintf (stderr, "Error occurred when writing output header: '%s'", dest_);
	goto end;
    }

    while (1)
    {
	AVStream *in_stream, *out_stream;
	ret = av_read_frame (in_fmt_ctx, &pkt);
	if (ret < 0)
	  break;

	in_stream = in_fmt_ctx->streams[pkt.stream_index];
	if (pkt.stream_index >= number_of_streams || stream_mapping[pkt.stream_index] < 0) {
	    av_packet_unref (&pkt);
	    continue;
	}

	pkt.stream_index = stream_mapping[pkt.stream_index];
	out_stream = out_fmt_ctx->streams[pkt.stream_index];

	/* copy packet */
	pkt.pts = av_rescale_q_rnd (pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
	pkt.dts = av_rescale_q_rnd (pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
	pkt.duration = av_rescale_q (pkt.duration, in_stream->time_base, out_stream->time_base);
	pkt.pos = -1;

	ret = av_interleaved_write_frame (out_fmt_ctx, &pkt); if (ret < 0)
	{
	    fprintf (stderr, "Error muxing pkt\n");
	    break;
	}
	av_packet_unref (&pkt);
    }
    av_write_trailer (out_fmt_ctx);

end:
    avformat_close_input (&in_fmt_ctx);
    if (out_fmt_ctx && !(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
	avio_closep (&out_fmt_ctx->pb);
    }
    avformat_free_context (out_fmt_ctx);
    av_freep (&stream_mapping);
    if (ret < 0 && ret != AVERROR_EOF) {
	fprintf (stderr, " - %s\n", av_err2str(ret));
	return -1;
    }
    return 0;
}


enum FilenameFormat {
    FMT_ORIGINAL = 0,
    FMT_ARTIST_TITLE,
    FMT_ARTIST_ALBUM_TITLE,
    FMT_ALBUM_ARTIST_TITLE,

    FMT_MAX,
    FMT_NULL = -1
};

static void  _output_name(char* dest_, unsigned avail_, enum FilenameFormat ofmt_, const Itdb_Track* track_)
{
    /* we must have the extension.. if the values we want pushes past the size 
     * availabe, we have to trunc
     */
    const char*  ext = strrchr(track_->ipod_path, '.');
    char  extbuf[128];
    const unsigned  avail = avail_  - snprintf(extbuf, sizeof(extbuf), "- %u%s", track_->id, ext);

    char  buf[PATH_MAX] = { 0 };

    static const char*  UNKNOWN = "unknown";

    switch (ofmt_)
    {
	case FMT_ARTIST_ALBUM_TITLE:
	  snprintf (buf, avail, "%s - %s - %s",
		    track_->artist ? track_->artist : UNKNOWN,
		    track_->album  ? track_->album  : UNKNOWN,
		    track_->title  ? track_->title  : UNKNOWN);
	    break;

	case FMT_ALBUM_ARTIST_TITLE:
	  snprintf (buf, avail, "%s - %s - %s",
		    track_->album  ? track_->album  : UNKNOWN,
		    track_->artist ? track_->artist : UNKNOWN,
		    track_->title  ? track_->title  : UNKNOWN);
	    break;

	case FMT_ARTIST_TITLE:
	  snprintf (buf, avail, "%s - %s",
		    track_->artist ? track_->artist : UNKNOWN,
		    track_->title  ? track_->title  : UNKNOWN);

	    break;

	case FMT_ORIGINAL:
	default:
	{
	    const char*  src_basename = strrchr(track_->ipod_path, '/');
	    snprintf(buf, avail, src_basename+1);
	} break;
    }
    snprintf(dest_, avail_, "%s%s", buf, extbuf);
}


static void  _usage(const char* argv0_)
{
    char *basename = g_path_get_basename(argv0_);
    g_print ("%s\n", PACKAGE_STRING);
    g_print ("usage: %s -M  <ipod mount point>  -o <output dir>  [-s]  [ all | <file0> ... ]\n\n"
             "    extracts all or specified files from iPod/iTunesDB and optionally syncs metadata\n"
             "\n"
             "    -M <iPod dir>     location of iPod data, as directory mount point or\n"
             "    -o <output dir>   location to extract\n"
             "    -s                sync (limited) iTunesDB meta to output track\n"
	     "    -f <0-3>          track name format; ipod name =0, artist/title =1, artist/album/title =2, album/artist/title =3\n"
	     "\n"
	     "  Extracted files can be further manipulated: exiftool '-filename<$Artist - $Title.%%le' -ext mp3 -ext m4a -r <output dir>\n"
             ,basename);
    g_free (basename);
    exit(-1);
}


int main (int argc, char **argv)
{
    int  ret = 0;
    struct stat  st;

    struct {
        const char*  itdb_path;
        const char*  output_path;
	char**  p;
	bool  sync_meta;
	bool  extract_all;
	enum FilenameFormat  ofmt;
    } opts = { NULL, NULL, NULL, false, false, FMT_ARTIST_TITLE };

    int  c;
    while ( (c=getopt(argc, argv, "M:o:sf:h")) != EOF)
    {
        switch (c) {
            case 'M':  opts.itdb_path = optarg;  break;
            case 'o':  opts.output_path = optarg;  break;
            case 's':  opts.sync_meta = true;  break;
            case 'f':
	    {
		int  ofmt = atoi(optarg);
		if (ofmt < FMT_MAX) {
		    opts.ofmt = ofmt;
		}
	    } break;

            case 'h':
            default:
                _usage(argv[0]);
        }
    }

    char  mountpoint[PATH_MAX] = { 0 };
    if (opts.itdb_path == NULL) {
        opts.itdb_path = gpod_default_mountpoint(mountpoint, sizeof(mountpoint));
    }

    if (opts.output_path == NULL) {
        _usage(argv[0]);
    }

    if (strcmp(opts.itdb_path, opts.output_path) == 0) {
        g_printerr("output, %s, is same as source\n", opts.output_path);
        _usage(argv[0]);
    }


    stat(opts.output_path, &st);
    if (!S_ISDIR(st.st_mode)) {
        g_printerr("output dir, %s, is not a directory\n", opts.output_path);
	_usage(argv[0]);
    }

    if (access(opts.output_path, W_OK) < 0) {
        g_printerr("output dir, %s, not writable\n", opts.output_path);
	_usage(argv[0]);
    }

    if ( (optind < argc) ) {
        opts.p = &argv[optind];
    }

    gpod_setlocale();

    strcpy(mountpoint, opts.itdb_path);

    GError *error = NULL;
    Itdb_iTunesDB*  itdb = NULL;
    Itdb_Device*  itdev = itdb_device_new();

    if (g_file_test(opts.itdb_path, G_FILE_TEST_IS_DIR)) {
        itdb = itdb_parse(opts.itdb_path, &error);
	itdb_device_set_mountpoint(itdev, opts.itdb_path);
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
	_usage(argv[0]);
    }

    Itdb_Playlist*  mpl = itdb_playlist_mpl(itdb);
    const uint32_t  current = g_list_length(mpl->members);

    char  src[PATH_MAX];
    char  dest_full[PATH_MAX];
    Itdb_Track*  track;
    GList*  it;

    char**  p = &argv[optind];
    if (*p == NULL) {
        g_printerr("no tracks specified for extract\n");
	_usage(argv[0]);
    }

    if (strcmp(*p, "all") == 0) {
	opts.extract_all = true;

	if (optind+1 < argc) {
	    g_printerr("extra args ignored\n");
	}
    }

    const Itdb_IpodInfo*  ipodinfo = itdb_device_get_ipod_info(itdev);
    g_print("extracting %s%stracks from iPod %s %s, currently %u tracks\n",
	        (opts.sync_meta ? "and sync'ing meta " : ""),
	        (opts.extract_all ? "all " : ""),
                itdb_info_get_ipod_generation_string(ipodinfo->ipod_generation),
                ipodinfo->model_number,
                current);

    const unsigned  N = opts.extract_all ? current : argv+argc - p;

    struct Stats {
	unsigned  ttl;
	unsigned  failed;
	size_t  bytes;
    } stats = { 0, 0, 0 };

    GSList*  failed = NULL;

    char*  dest = dest_full + sprintf(dest_full, "%s/", opts.output_path);
    const unsigned  dest_avail = sizeof(dest_full) - strlen(dest_full);

    unsigned  i = 0;
    const guint  then = g_get_monotonic_time();
    for (it = mpl->members; it != NULL; it = it->next)
    {
	++i;

	track = (Itdb_Track *)it->data;
	itdb_filename_ipod2fs(track->ipod_path);

	sprintf(src, "%s/%s", mountpoint, track->ipod_path);

	// extract this one?
	bool  extract = false;
	if (opts.extract_all) {
	    extract = true;
	}
	else
	{
	    p = &argv[optind];
	    while (*p)
	    {
	        if (**p != '\0' && strcmp(*p, track->ipod_path) == 0) {
		    extract = true;
		    *p = "";
		    break;
		}
	        ++p;
	    }
	}

	if (extract)
	{
	    if (src == NULL) {
		// ??? can't happen but ...
	        continue;
	    }
	    _output_name(dest, dest_avail, opts.ofmt, track);

	    g_print("[%3u/%u]  id=%u %s -> %s\n", stats.ttl+1, N, track->id, track->ipod_path, dest_full);
	    if (_extract(src, dest_full, opts.sync_meta ? track : NULL) < 0) {
	        g_printerr("%s - FAILED\n", track->ipod_path);
		g_unlink(dest_full);
		ret = 1;
		++stats.failed;

		failed = g_slist_append(failed, track->ipod_path);
	    }
	    else
	    {
		stat(dest_full, &st);
		++stats.ttl;
		stats.bytes += st.st_size;
	    }
	}
    }

    char duration[32] = { 0 };
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

    char  stats_size[128] = { 0 };
    if (stats.bytes) {
        gpod_bytes_to_human(stats_size, sizeof(stats_size), stats.bytes, true);
    }

    g_print("iPod total tracks=%u  %u/%u items %s in %s\n", g_list_length(itdb_playlist_mpl(itdb)->members), ret < 0 ? 0 : stats.ttl, N, stats_size, duration);
    if (failed)
    {
	g_print("failed tracks:\n");
	for (GSList* f=failed; f!=NULL; f=f->next) {
	    g_print("  %s\n", f->data);
	}
	g_slist_free(failed);
	failed = NULL;
    }


    itdb_device_free(itdev);
    itdb_free(itdb);

    return ret;
}
