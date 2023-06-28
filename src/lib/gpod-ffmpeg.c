/* Copyright 2021 Ray whatdoineed2do @ gmail com
 *
 * based on forked-daapd filescanner_ffmpeg.c
 * Copyright (C) 2009-2011 Julien BLACHE <jb@jblache.org
 */

#include "gpod-ffmpeg.h"

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/hash.h>
#include <libavutil/log.h>

#include "gpod-utils.h"


void gpod_ff_meta_free(struct gpod_ff_meta*  obj_)
{
    free(obj_->title);
    free(obj_->artist);
    free(obj_->album);
    free(obj_->album_artist);
    free(obj_->genre);
    free(obj_->comment);
    free(obj_->composer);
    free(obj_->grouping);
    free(obj_->title_sort);
    free(obj_->artist_sort);
    free(obj_->album_sort);
    free(obj_->album_artist_sort);
    free(obj_->composer_sort);

    memset(obj_, 0, sizeof(struct gpod_ff_meta));
}

void  gpod_ff_media_info_free(struct gpod_ff_media_info*  obj_)
{
    gpod_ff_meta_free(&obj_->meta);
}

void  gpod_ff_media_info_init(struct gpod_ff_media_info*  obj_)
{
    memset(obj_, 0, sizeof(struct gpod_ff_media_info));
    obj_->type = obj_->description = "unknown";
}


#define meta_offsetof(field) offsetof(struct gpod_ff_meta, field)


/* Mapping between the metadata name(s) and the offset
 * of the equivalent metadata field in struct gpod_ff_meta 
 */
struct metadata_map
{
    char *key;
    int as_int;
    size_t offset;
    int (*handler_function) (struct gpod_ff_meta*, char *);
};

static char errbuf[64];

static inline char *
err2str (int errnum)
{
    av_strerror (errnum, errbuf, sizeof (errbuf));
    return errbuf;
}

static int
parse_genre(struct gpod_ff_meta *mfi, char *genre_string)
{
    char **genre = (char**)((char *) mfi + meta_offsetof(genre));
    char *ptr;

    if (*genre) {
	return 0;
    }

    *genre = strdup(genre_string);
    ptr = strchr(*genre, ';');
    if (ptr) {
	*ptr = '\0';
    }

    return 1;
}

static int
parse_slash_separated_ints (char *string, uint32_t * firstval, uint32_t * secondval)
{
    int numvals = 0;
    char *ptr;

    ptr = strchr (string, '/');
    if (ptr)
    {
        *ptr = '\0';
        *secondval = atol(ptr + 1);
        numvals++;
    }

    *firstval = atol(string);
    numvals++;

    return numvals;
}

static int
parse_track (struct gpod_ff_meta *mfi, char *track_string)
{
    uint32_t *track = (uint32_t *) ((char *) mfi + meta_offsetof (track));
    uint32_t *total_tracks =
        (uint32_t *) ((char *) mfi + meta_offsetof (total_tracks));

    return parse_slash_separated_ints (track_string, track, total_tracks);
}

static int
parse_disc (struct gpod_ff_meta *mfi, char *disc_string)
{
    uint32_t *disc = (uint32_t *) ((char *) mfi + meta_offsetof (disc));
    uint32_t *total_discs =
        (uint32_t *) ((char *) mfi + meta_offsetof (total_discs));

    return parse_slash_separated_ints (disc_string, disc, total_discs);
}

static int
parse_date (struct gpod_ff_meta *mfi, char *date_string)
{
    char year_string[32];
    uint32_t *year = (uint32_t *) ((char *) mfi + meta_offsetof (year));
    uint32_t *date_released =
        (uint32_t *) ((char *) mfi + meta_offsetof (date_released));
    struct tm tm = { 0 };
    int ret = 0;

    if ((*year == 0) && (atoll(date_string) == 0))
        ret++;

    if (strptime (date_string, "%FT%T%z", &tm)	// ISO 8601, %F=%Y-%m-%d, %T=%H:%M:%S
            || strptime (date_string, "%F %T", &tm)
            || strptime (date_string, "%F %H:%M", &tm)
            || strptime (date_string, "%F", &tm))
    {
        *date_released = (uint32_t) mktime (&tm);
        ret++;
    }

    if ((*date_released == 0) && (*year != 0))
    {
        snprintf (year_string, sizeof (year_string),
                "%" PRIu32 "-01-01T12:00:00", *year);
        if (strptime (year_string, "%FT%T", &tm))
        {
            *date_released = (uint32_t) mktime (&tm);
            ret++;
        }
    }

    return ret;
}


/* Lookup is case-insensitive, first occurrence takes precedence */
static const struct metadata_map   md_map_generic[] = {
    {"title",		0, meta_offsetof (title), 		NULL },
    {"artist",		0, meta_offsetof (artist), 		NULL },
    {"author",		0, meta_offsetof (artist), 		NULL },
    {"album_artist",	0, meta_offsetof (album_artist), 	NULL },
    {"album",		0, meta_offsetof (album), 		NULL },
    {"genre", 		0, meta_offsetof (genre), 		parse_genre },
    {"composer", 	0, meta_offsetof (composer), 		NULL },
    {"grouping", 	0, meta_offsetof (grouping), 		NULL },
    {"comment", 	0, meta_offsetof (comment), 		NULL },
    {"description", 	0, meta_offsetof (comment), 		NULL },
    {"track", 		1, meta_offsetof (track), 		parse_track},
    {"disc", 		1, meta_offsetof (disc), 		parse_disc},
    {"year", 		1, meta_offsetof (year), 		NULL },
    {"date", 		1, meta_offsetof (date_released),	parse_date},
    {"title-sort", 	0, meta_offsetof (title_sort),		NULL },
    {"artist-sort", 	0, meta_offsetof (artist_sort), 	NULL },
    {"album-sort", 	0, meta_offsetof (album_sort), 		NULL },
    {"compilation", 	1, meta_offsetof (compilation), 	NULL },

    {NULL, 0, 0, NULL}
};

static const struct metadata_map md_map_vorbis[] = {
    { "albumartist",  0, meta_offsetof(album_artist),      NULL },
    { "album artist", 0, meta_offsetof(album_artist),      NULL },
    { "tracknumber",  1, meta_offsetof(track),             NULL },
    { "tracktotal",   1, meta_offsetof(total_tracks),      NULL },
    { "totaltracks",  1, meta_offsetof(total_tracks),      NULL },
    { "discnumber",   1, meta_offsetof(disc),              NULL },
    { "disctotal",    1, meta_offsetof(total_discs),       NULL },
    { "totaldiscs",   1, meta_offsetof(total_discs),       NULL },

    { NULL,           0, 0,                               NULL }
};

static const struct metadata_map  md_map_id3[] = {
    { "TT1",          0, meta_offsetof(grouping),              NULL },              /* ID3v2.2 */
    { "TIT1",         0, meta_offsetof(grouping),              NULL },              /* ID3v2.3 */
    { "GP1",          0, meta_offsetof(grouping),              NULL },              /* unofficial iTunes */
    { "GRP1",         0, meta_offsetof(grouping),              NULL },              /* unofficial iTunes */
    { "TCM",          0, meta_offsetof(composer),              NULL },              /* ID3v2.2 */
    { "TPA",          1, meta_offsetof(disc),                  parse_disc },        /* ID3v2.2 */
    { "XSOA",         0, meta_offsetof(album_sort),            NULL },              /* ID3v2.3 */
    { "XSOP",         0, meta_offsetof(artist_sort),           NULL },              /* ID3v2.3 */
    { "XSOT",         0, meta_offsetof(title_sort),            NULL },              /* ID3v2.3 */
    { "TS2",          0, meta_offsetof(album_artist_sort),     NULL },              /* ID3v2.2 */
    { "TSO2",         0, meta_offsetof(album_artist_sort),     NULL },              /* ID3v2.3 */
    { "ALBUMARTISTSORT",     0, meta_offsetof(album_artist_sort),     NULL },              /* ID3v2.x */
    { "TSC",          0, meta_offsetof(composer_sort),         NULL },              /* ID3v2.2 */
    { "TSOC",         0, meta_offsetof(composer_sort),         NULL },              /* ID3v2.3 */

    { NULL,           0, 0,                                   NULL }
};



static int
extract_metadata_core (struct gpod_ff_meta *mfi, AVDictionary * md,
        const struct metadata_map *md_map)
{
    AVDictionaryEntry *mdt;
    char **strval;
    uint32_t *intval;
    int mdcount;
    int i;
    int ret;

    mdcount = 0;

    /* Extract actual metadata */
    for (i = 0; md_map[i].key != NULL; i++)
    {
        mdt = av_dict_get (md, md_map[i].key, NULL, 0);
        if (mdt == NULL)
            continue;

        if ((mdt->value == NULL) || (strlen (mdt->value) == 0))
            continue;

        if (md_map[i].handler_function)
        {
            mdcount += md_map[i].handler_function (mfi, mdt->value);
            continue;
        }

        mdcount++;

        if (!md_map[i].as_int)
        {
            strval = (char **) ((char *) mfi + md_map[i].offset);

            if (*strval == NULL)
                *strval = strdup (mdt->value);
        }
        else
        {
            intval = (uint32_t *) ((char *) mfi + md_map[i].offset);

            if (*intval == 0)
            {
                *intval = atoll(mdt->value);
                if (ret < 0)
                    continue;
            }
        }
    }

    return mdcount;
}

static int
extract_metadata (struct gpod_ff_media_info* info_, AVFormatContext* ctx,
        AVStream * audio_stream, const struct metadata_map* md_map)
{
    int mdcount;
    int ret;

    mdcount = 0;
    if (ctx->metadata) {
        ret = extract_metadata_core(&info_->meta, ctx->metadata, md_map);
        mdcount += ret;
    }

    if (audio_stream->metadata) {
        ret = extract_metadata_core(&info_->meta, audio_stream->metadata, md_map);
        mdcount += ret;
    }

    return mdcount;
}

const struct gpod_video_support {
    unsigned  max_width;
    unsigned  max_height;
    unsigned  max_vbit_rate;
    float     max_fps;
    unsigned  max_abit_rate;
    short     channels;
    unsigned  sample_rate;

    int*  profile;
    Itdb_IpodGeneration*  device;
}  video_support[] = {
    {
	.max_width = 640,
	.max_height = 480,
	.max_vbit_rate = 2500000,
	.max_fps = 30, 
	.max_abit_rate = 1600,
	.channels = 2,
	.sample_rate = 48000,

	.profile = (int[]){
	    FF_PROFILE_H264_BASELINE,
	    FF_PROFILE_H264_CONSTRAINED_BASELINE,
	    FF_PROFILE_UNKNOWN
	},
	.device = (Itdb_IpodGeneration[]){ 
	    ITDB_IPOD_GENERATION_VIDEO_1,
	    ITDB_IPOD_GENERATION_VIDEO_2,
	    ITDB_IPOD_GENERATION_UNKNOWN }
    },

    // in reality, this is redundant since the only ipods supporting video that we can update (does not need iTuneCDB is the ipod video
    {
	.max_width = 1280,
	.max_height = 720, 
	.max_vbit_rate = 2500000,
	.max_fps = 30,
	.max_abit_rate = 1600,
	.channels = 2,
	.sample_rate = 48000,

	.profile = (int[]){
	    FF_PROFILE_H264_BASELINE,
	    FF_PROFILE_H264_CONSTRAINED_BASELINE,
	    FF_PROFILE_H264_MAIN,
	    FF_PROFILE_UNKNOWN
	},
	.device = (Itdb_IpodGeneration[]){
	    ITDB_IPOD_GENERATION_UNKNOWN
	}
    },

    { .profile = NULL, .device = NULL }
};

#ifndef GPOD_FF_STANDALONE
static bool  device_support_video(Itdb_IpodGeneration idevice_, const struct gpod_ff_media_info* mi_)
{
    const struct gpod_video_support*  p = video_support;

    while (p->profile)
    {
	if (mi_->video.height <= p->max_height &&
            mi_->video.width  <= p->max_width  &&
	    mi_->video.bitrate <= p->max_vbit_rate &&
	    mi_->video.fps <= p->max_fps &&
	    mi_->audio.samplerate <= p->sample_rate &&
	    mi_->audio.channels <= p->channels)
	{
	    int*  q = p->profile;
	    while (*q != FF_PROFILE_UNKNOWN)
	    {
		if (*q == mi_->video.profile) {
		    Itdb_IpodGeneration*  r = p->device;
		    while (*r != ITDB_IPOD_GENERATION_UNKNOWN) {
			if (*r == idevice_) {
			    return true;
			}
			++r;
		    }
		}
		++q;
	    }
	}
	++p;
    }
    return false;
}


int  gpod_ff_scan(struct gpod_ff_media_info *info_, const char *file_, Itdb_IpodGeneration idevice_, char** err_)
{
    AVFormatContext *ctx;
    const struct metadata_map*  extra_md_map = NULL;
    enum AVCodecID codec_id;
    enum AVCodecID video_codec_id;
    enum AVCodecID audio_codec_id;
    AVStream *video_stream;
    AVStream *audio_stream;

    int i;
    int ret;

    ctx = NULL;

    if ( (ret = avformat_open_input(&ctx, file_, NULL, NULL)) < 0) {
        char  err[1024];
        snprintf(err, 1024, "%s", av_err2str(ret));
        *err_ = strdup(err);
        return -1;
    }

    if ( (ret = avformat_find_stream_info(ctx, NULL)) < 0) {
        *err_ = strdup("failed to find audio/data stream");
        avformat_close_input(&ctx);
        return -1;
    }

    strcpy(info_->path, file_);

    struct stat  st;
    stat(file_, &st);
    info_->file_size = st.st_size;


    /* Extract codec IDs, check for video */
    video_codec_id = AV_CODEC_ID_NONE;
    video_stream = NULL;

    audio_codec_id = AV_CODEC_ID_NONE;
    audio_stream = NULL;

    i = av_find_best_stream(ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (i >= 0)
    {
	info_->has_audio = true;
	audio_stream = ctx->streams[i];

	info_->audio.codec_id = audio_codec_id = audio_stream->codecpar->codec_id;
	info_->audio.samplerate = audio_stream->codecpar->sample_rate;
	info_->audio.bits_per_sample = 8 * av_get_bytes_per_sample(audio_stream->codecpar->format);

	info_->audio.bits_per_sample = av_get_bits_per_sample(audio_stream->codecpar->codec_id);

#ifdef HAVE_FF5_CH_LAYOUT
	info_->audio.channels = audio_stream->codecpar->ch_layout.nb_channels;
#else
	info_->audio.channels = audio_stream->codecpar->channels;
#endif
	info_->audio.song_length = ctx->duration / (AV_TIME_BASE / 1000); /* ms */
	if (ctx->bit_rate > 0) {
	    info_->audio.bitrate = ctx->bit_rate / 1000;
	}
	else if (ctx->duration > AV_TIME_BASE) /* guesstimate */ {
	    info_->audio.bitrate = ((info_->file_size * 8) / (ctx->duration / AV_TIME_BASE)) / 1000;
	}
    }

    for (i=0; i<ctx->nb_streams; ++i)
    {
        if (ctx->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
	    continue;
	}

	// only care about h264
        switch (ctx->streams[i]->codecpar->codec_id)
	{
	    case AV_CODEC_ID_H264:
	    {
		switch (ctx->streams[i]->codecpar->profile)
		{
		    // only believe in
		    case FF_PROFILE_H264_BASELINE:
		    case FF_PROFILE_H264_CONSTRAINED_BASELINE:
		    case FF_PROFILE_H264_MAIN:
		    case FF_PROFILE_H264_EXTENDED:
		    case FF_PROFILE_H264_HIGH:
		    case FF_PROFILE_H264_HIGH_10:
		    case FF_PROFILE_H264_HIGH_10_INTRA:
		    case FF_PROFILE_H264_MULTIVIEW_HIGH:
		    case FF_PROFILE_H264_HIGH_422:
		    case FF_PROFILE_H264_HIGH_422_INTRA:
		    case FF_PROFILE_H264_STEREO_HIGH:
		    case FF_PROFILE_H264_HIGH_444:
		    case FF_PROFILE_H264_HIGH_444_PREDICTIVE:
		    case FF_PROFILE_H264_HIGH_444_INTRA:
		    case FF_PROFILE_H264_CAVLC_444:
		    {
			info_->has_video = true;
			if (!video_stream)
			{
			    video_stream = ctx->streams[i];
			    info_->video.codec_id = video_codec_id = codec_id;
			    info_->video.height = video_stream->codecpar->height;
			    info_->video.width = video_stream->codecpar->width;
			    info_->video.profile = video_stream->codecpar->profile;
			    info_->video.bitrate = video_stream->codecpar->bit_rate;
			    info_->video.length = video_stream->duration/AV_TIME_BASE;
			    info_->video.fps = video_stream->avg_frame_rate.den ? video_stream->avg_frame_rate.num/(float)video_stream->avg_frame_rate.den : 0;
			}
		    } break;

		    default:
			break;
		}
	    } break;

	    case AV_CODEC_ID_MJPEG:
	    case AV_CODEC_ID_MJPEGB:
		// embedded artwork, not video
		break;
	}
    }

    if (video_codec_id == AV_CODEC_ID_NONE && audio_codec_id == AV_CODEC_ID_NONE) {
        info_->has_audio = info_->has_video = false;
        avformat_close_input(&ctx);
        return -1;
    }

    const AVCodecDescriptor*  avc_desc = NULL;

    /* Check codec */
    info_->supported_ipod_fmt = false;
    if (info_->has_video)
    {
        // its a real vid file (not jsut an audio file with cover art)
	avc_desc = avcodec_descriptor_get(video_codec_id);

	info_->type = avc_desc->name;
	info_->description = avc_desc->long_name;

	info_->supported_ipod_fmt = device_support_video(idevice_, info_);
	info_->type = avcodec_profile_name(video_codec_id, info_->video.profile);

        if (video_stream->metadata) {
            info_->meta.has_meta = true;
            extract_metadata(info_, ctx, video_stream, md_map_generic);
        }
    }
    else
    {
	avc_desc = avcodec_descriptor_get(audio_codec_id);

	info_->type = avc_desc->name;
	info_->description = avc_desc->long_name;

        switch (audio_codec_id)
        {
            case AV_CODEC_ID_MP3:
                info_->supported_ipod_fmt = true;

                extra_md_map = md_map_id3;
                break;

            case AV_CODEC_ID_AAC:
                info_->supported_ipod_fmt = true;
                break;

            case AV_CODEC_ID_ALAC:
                info_->supported_ipod_fmt = true;
                break;

    // this block of types will needs transcoding to go onto iPod

            case AV_CODEC_ID_FLAC:

                extra_md_map = md_map_vorbis;
                break;


            case AV_CODEC_ID_APE:
                break;

            case AV_CODEC_ID_VORBIS:

                extra_md_map = md_map_vorbis;
                break;

            case AV_CODEC_ID_WMAV1:
            case AV_CODEC_ID_WMAV2:
            case AV_CODEC_ID_WMAVOICE:
                break;

            case AV_CODEC_ID_WMAPRO:
                break;

            case AV_CODEC_ID_WMALOSSLESS:
                break;

            case AV_CODEC_ID_PCM_S16LE ... AV_CODEC_ID_PCM_F64LE:
                if (strcmp(ctx->iformat->name, "aiff") == 0)
                {
                    info_->type = "aif";
                    info_->description = "AIFF audio";
                    break;
                }
                else if (strcmp(ctx->iformat->name, "wav") == 0)
                {
                    info_->type = "wav";
                    info_->description = "WAV audio";
                    break;
                }
                /* WARNING: will fallthrough to default case, don't move */
                /* FALLTHROUGH */
     
    // everything we're not supporting even if its a valid audio

            default:
                info_->type = "unkn";
                info_->description = "Unknown audio format";
                break;
        }

        if (ctx->metadata || audio_stream->metadata) {
            info_->meta.has_meta = true;
            if (extra_md_map) {
                extract_metadata(info_, ctx, audio_stream, extra_md_map);
            }
            extract_metadata(info_, ctx, audio_stream, md_map_generic);
        }
    }

    avformat_close_input(&ctx);
    return 0;
}


Itdb_Track*  gpod_ff_meta_to_track(const struct gpod_ff_media_info* meta_, time_t time_added_, bool sanitize_)
{
    if (!meta_->supported_ipod_fmt) {
        return NULL;
    }

    Itdb_Track*  track = itdb_track_new();
    
    track->mediatype = meta_->has_video ? ITDB_MEDIATYPE_MOVIE : ITDB_MEDIATYPE_AUDIO;
    track->time_added = time_added_ ? time_added_ : time(NULL);
    track->time_modified = track->time_added;

    track->filetype = gpod_sanitize_text(gpod_trim(meta_->description), sanitize_);
    track->size = meta_->file_size;
    track->tracklen = meta_->audio.song_length;
    track->bitrate = meta_->audio.bitrate;
    track->samplerate = meta_->audio.samplerate;

    track->title = gpod_sanitize_text(gpod_trim(meta_->meta.title), sanitize_);
    track->album = gpod_sanitize_text(gpod_trim(meta_->meta.album), sanitize_);
    track->artist = gpod_sanitize_text(gpod_trim(meta_->meta.artist), sanitize_);
    track->genre = gpod_sanitize_text(gpod_trim(meta_->meta.genre), sanitize_);
    track->comment = gpod_sanitize_text(gpod_trim(meta_->meta.comment), sanitize_);
    track->track_nr = meta_->meta.track;
    track->year = meta_->meta.year;

    track->sort_artist      = gpod_sortname(track->artist);
    track->sort_title       = gpod_sortname(track->title);
    track->sort_album       = gpod_sortname(track->album);
    track->sort_albumartist = gpod_sortname(track->albumartist);
    track->sort_composer    = gpod_sortname(track->composer);

    return track;
}
#endif


const struct gpod_ff_enc_support*  gpod_ff_enc_supported(enum gpod_ff_enc enc_)
{
    const struct gpod_ff_enc_support*  p = gpod_ff_encoders;
    while (p->name) {
        if (p->enc == enc_) {
	    return p;
	}
	++p;
    }
    return NULL;
}

void  gpod_ff_transcode_ctx_init(struct gpod_ff_transcode_ctx* obj_,
                                 enum gpod_ff_enc enc_, enum gpod_ff_transcode_quality quality_, bool sync_meta_)
{
    memset(obj_, 0, sizeof(struct gpod_ff_transcode_ctx));

    // default the transcode params
    obj_->audio_opts.channels = 2;
    obj_->audio_opts.quality = quality_;
    obj_->audio_opts.quality_scale_factor = FF_QP2LAMBDA;
    obj_->audio_opts.samplefmt = AV_SAMPLE_FMT_NONE;
    switch (enc_)
    {
      /* maybe better NOT to support the ffmpeg inbuilt aac enc since it generates
       * files that the ipod plays with glitches but fine on other media players
       */
      case GPOD_FF_ENC_AAC:
	obj_->audio_opts.codec_id = AV_CODEC_ID_AAC;
	obj_->audio_opts.enc_name = "aac";
	obj_->extn = ".m4a";
        break;

      case GPOD_FF_ENC_FDKAAC:
	obj_->audio_opts.codec_id = AV_CODEC_ID_AAC;
	obj_->audio_opts.enc_name = "libfdk_aac";
	obj_->extn = ".m4a";
        obj_->audio_opts.quality_scale_factor = 1.0;

        /* fdk-aac encoder only accepts vbr 1-5 (best), rather than the ffmpeg 
         * -q:a 1 (best)-9 so fudge it
         */
        if (quality_ <= GPOD_FF_XCODE_VBR_MAX) {
            int  tmp = -1 * ( ((int)quality_)/2 - 5);
            obj_->audio_opts.quality = tmp;
        }
        break;

      case GPOD_FF_ENC_ALAC:
	obj_->audio_opts.codec_id = AV_CODEC_ID_ALAC;
	obj_->audio_opts.enc_name = "alac";
	obj_->extn = ".m4a";
        obj_->audio_opts.quality_scale_factor = 0;
	obj_->audio_opts.samplefmt = AV_SAMPLE_FMT_S16P;
        break;

      case GPOD_FF_ENC_MP3:
      default:
	obj_->audio_opts.codec_id = AV_CODEC_ID_MP3;
	obj_->audio_opts.enc_name = "libmp3lame";
	obj_->extn = ".mp3";
    }

    obj_->sync_meta = sync_meta_;


    const char*  tmpdir = getenv("TMPDIR");
    tmpdir = tmpdir == NULL ? "/tmp" : tmpdir;
    sprintf(obj_->tmpprfx, "/%s/.gpod-%d", tmpdir, getpid());
}


int  gpod_ff_audio_hash(char** hash_, const char* file_, char** err_)
{
    AVFormatContext *ctx = NULL;
    AVPacket *pkt = NULL;
    int64_t pktnum  = 0;
    int ret;
    int audio_stream_idx = -1;
    char  err[1024];

    struct AVHashContext *hash = NULL;

    *hash_ = NULL;

    ret = avformat_open_input(&ctx, file_, NULL, NULL);
    if (ret < 0) {
        snprintf(err, sizeof(err), "unable to open input - %s", av_err2str(ret));
        *err_ = strdup(err);
        goto cleanup;
    }

    ret = avformat_find_stream_info(ctx, NULL);
    if (ret < 0) {
        snprintf(err, sizeof(err), "unable to find streams - %s", av_err2str(ret));
        *err_ = strdup(err);
        goto cleanup;
    }

    audio_stream_idx = av_find_best_stream(ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_stream_idx < 0) {
        *err_ = strdup("unable to find audio stream");
	goto cleanup;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        *err_ = strdup("unable to alloc pkt");
	ret = ENOMEM;
        goto cleanup;
    }

    ret = av_hash_alloc(&hash, "sha256");
    if (ret < 0) {
        snprintf(err, sizeof(err), "failed to alloc hash - %s\n", ret == EINVAL ? "unknown hash" : strerror(ret));
        *err_ = strdup(err);
	if (ret != EINVAL)  {
	    ret = ENOMEM;
	}
        goto cleanup;
    }
    av_hash_init(hash);

    while ((ret = av_read_frame(ctx, pkt)) >= 0)
    {
	if (pkt->stream_index != audio_stream_idx) {
	    av_packet_unref(pkt);
	    continue;
	}

	av_hash_update(hash, pkt->data, pkt->size);
	av_packet_unref(pkt);
	pktnum++;
    }

    char  res[2 * AV_HASH_MAX_SIZE + 4] = { 0 };
    av_hash_final_hex(hash, res, sizeof(res));

    *hash_ = strdup(res);

    ret = 0;

cleanup:
    if (pkt)   av_packet_free(&pkt);
    if (ctx) { avformat_close_input(&ctx); avformat_close_input(&ctx); }
    if (hash)  av_hash_freep(&hash);

    return ret;
}


static void  _avlog_callback_null(void *ptr, int level, const char *fmt, va_list vl)
{ }

static struct  gpod_ff_enc_support  _gpod_ff_encoders[] = {
  { .enc = GPOD_FF_ENC_MP3,    .name = "mp3",        .enc_name = "libmp3lame", .supported = false },
  { .enc = GPOD_FF_ENC_FDKAAC, .name = "aac",        .enc_name = "libfdk_aac", .supported = false },
  { .enc = GPOD_FF_ENC_AAC,    .name = "aac-ffmpeg", .enc_name = "aac",        .supported = false },
  { .enc = GPOD_FF_ENC_AAC_AT, .name = "aac_at",     .enc_name = "aac_at",     .supported = false },
  { .enc = GPOD_FF_ENC_ALAC,   .name = "alac",       .enc_name = "alac",       .supported = false },
  { .enc = GPOD_FF_ENC_MAX,    .name = NULL,         .enc_name = NULL,         .supported = false }
};
const struct  gpod_ff_enc_support*  gpod_ff_encoders = NULL;


void  gpod_ff_init()
{
    if (gpod_ff_encoders) {
        return;
    }
    gpod_ff_encoders = _gpod_ff_encoders;

    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    av_log_set_callback(_avlog_callback_null);

    struct gpod_ff_enc_support*  p = _gpod_ff_encoders;
    while (p->name) {
	p->supported = avcodec_find_encoder_by_name(p->enc_name) != NULL;
	++p;
    }
}
