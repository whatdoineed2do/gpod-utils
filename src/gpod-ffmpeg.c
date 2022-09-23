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
    obj_->type = obj_->codectype = obj_->description = "unknown";
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
    {"title", 0, meta_offsetof (title), NULL},
    {"artist", 0, meta_offsetof (artist), NULL},
    {"author", 0, meta_offsetof (artist), NULL},
    {"album_artist", 0, meta_offsetof (album_artist), NULL},
    {"album", 0, meta_offsetof (album), NULL},
    {"genre", 0, meta_offsetof (genre), NULL},
    {"composer", 0, meta_offsetof (composer), NULL},
    {"grouping", 0, meta_offsetof (grouping), NULL},
    {"comment", 0, meta_offsetof (comment), NULL},
    {"description", 0, meta_offsetof (comment), NULL},
    {"track", 1, meta_offsetof (track), parse_track},
    {"disc", 1, meta_offsetof (disc), parse_disc},
    {"year", 1, meta_offsetof (year), NULL},
    {"date", 1, meta_offsetof (date_released), parse_date},
    {"title-sort", 0, meta_offsetof (title_sort), NULL},
    {"artist-sort", 0, meta_offsetof (artist_sort), NULL},
    {"album-sort", 0, meta_offsetof (album_sort), NULL},
    {"compilation", 1, meta_offsetof (compilation), NULL},

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
    AVDictionary *options;
    const struct metadata_map*  extra_md_map = NULL;
    enum AVMediaType codec_type;
    enum AVCodecID codec_id;
    enum AVCodecID video_codec_id;
    enum AVCodecID audio_codec_id;
    enum AVSampleFormat sample_fmt;
    AVStream *video_stream;
    AVStream *audio_stream;

    int sample_rate;
    int channels;
    int i;
    int ret;

    ctx = NULL;
    options = NULL;

    strcpy(info_->path, file_);

    ret = avformat_open_input(&ctx, file_, NULL, &options);
    if (options) {
        av_dict_free(&options);
    }
    if (ret != 0) {
        char  err[1024];
        snprintf(err, 1024, "%s", av_err2str(ret));
        *err_ = strdup(err);
        return -1;
    }

    struct stat  st;
    stat(file_, &st);
    info_->file_size = st.st_size;

    if ( (ret = avformat_find_stream_info(ctx, NULL)) < 0) {
        *err_ = strdup("failed to find audio/data stream");
        avformat_close_input(&ctx);
        return -1;
    }

    /* Extract codec IDs, check for video */
    video_codec_id = AV_CODEC_ID_NONE;
    video_stream = NULL;

    audio_codec_id = AV_CODEC_ID_NONE;
    audio_stream = NULL;

    for (i=0; i<ctx->nb_streams; ++i)
    {
        codec_type  = ctx->streams[i]->codecpar->codec_type;
        codec_id    = ctx->streams[i]->codecpar->codec_id;
        sample_rate = ctx->streams[i]->codecpar->sample_rate;
        sample_fmt  = ctx->streams[i]->codecpar->format;
#ifdef HAVE_FF5_CH_LAYOUT
        channels    = (ctx->streams[i]->codecpar->ch_layout.nb_channels);
#else
        channels    = ctx->streams[i]->codecpar->channels;
#endif
        switch (codec_type)
        {
	    case AVMEDIA_TYPE_VIDEO:
            {
                // only care about h264
                switch (codec_id)
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
            } break;

            /* WARN -- only consider the file's FIRST audio stream - if normal 
             * siutations this is fine
             */
            case AVMEDIA_TYPE_AUDIO:
            {
                info_->has_audio = true;
                if (!audio_stream)
                {
                    audio_stream = ctx->streams[i];

                    info_->audio.codec_id = audio_codec_id = codec_id;
                    if (info_->audio.samplerate == 0) {
			info_->audio.samplerate = sample_rate;
		    }
                    info_->audio.bits_per_sample = 8 * av_get_bytes_per_sample(sample_fmt);

                    if (info_->audio.bits_per_sample == 0) {
                        info_->audio.bits_per_sample = av_get_bits_per_sample(codec_id);
                    }
                    info_->audio.channels = channels;
                    info_->audio.song_length = ctx->duration / (AV_TIME_BASE / 1000); /* ms */
                    if (ctx->bit_rate > 0) {
                        info_->audio.bitrate = ctx->bit_rate / 1000;
                    }
                    else if (ctx->duration > AV_TIME_BASE) /* guesstimate */ {
                        info_->audio.bitrate = ((info_->file_size * 8) / (ctx->duration / AV_TIME_BASE)) / 1000;
                    }

                } 
            } break;

            default:
                break;
        }
    }

    if (video_codec_id == AV_CODEC_ID_NONE && audio_codec_id == AV_CODEC_ID_NONE) {
        info_->has_audio = info_->has_video = false;
        avformat_close_input(&ctx);
        return -1;
    }

    /* Check codec */
    info_->supported_ipod_fmt = false;
    if (info_->has_video)
    {
        // its a real vid file (not jsut an audio file with cover art)
        info_->codectype = "h264";
        info_->description = "H264 video";
	info_->supported_ipod_fmt = device_support_video(idevice_, info_);
        switch (info_->video.profile)
        {
            case FF_PROFILE_H264_BASELINE:
                info_->type = "h264 (baseline)";
                break;
            case FF_PROFILE_H264_CONSTRAINED_BASELINE:
                info_->type = "h264 (constrained baseline)";
                break;

            case FF_PROFILE_H264_MAIN:
                info_->type = "h264 (main)";
                break;
            case FF_PROFILE_H264_EXTENDED:
                info_->type = "h264 (extended)";
                break;
            case FF_PROFILE_H264_HIGH:
                info_->type = "h264 (high)";
                break;
            case FF_PROFILE_H264_HIGH_10:
                info_->type = "h264 (high 10)";
                break;
            case FF_PROFILE_H264_HIGH_10_INTRA:
                info_->type = "h264 (high 10 intra)";
                break;
            case FF_PROFILE_H264_MULTIVIEW_HIGH:
                info_->type = "h264 (high multiview)";
                break;
            case FF_PROFILE_H264_HIGH_422:
                info_->type = "h264 (high 422)";
                break;
            case FF_PROFILE_H264_HIGH_422_INTRA:
                info_->type = "h264 (high 442 intra)";
                break;
            case FF_PROFILE_H264_STEREO_HIGH:
                info_->type = "h264 (high stereo)";
                break;
            case FF_PROFILE_H264_HIGH_444:
                info_->type = "h264 (high 444)";
                break;
            case FF_PROFILE_H264_HIGH_444_PREDICTIVE:
                info_->type = "h264 (high 444 predictive)";
                break;
            case FF_PROFILE_H264_HIGH_444_INTRA:
                info_->type = "h264 (hgh 444 intra)";
                break;
            case FF_PROFILE_H264_CAVLC_444:
                info_->type = "h264 (high cavlc 444)";
                break;

            default:
                info_->type = "h264 (unknown)";
        }

        if (video_stream->metadata) {
            info_->meta.has_meta = true;
            extract_metadata(info_, ctx, video_stream, md_map_generic);
        }
    }
    else
    {
        switch (audio_codec_id)
        {
            case AV_CODEC_ID_MP3:
                info_->type = "mp3";
                info_->codectype = "mpeg";
                info_->description = "MPEG audio";

                info_->supported_ipod_fmt = true;

                extra_md_map = md_map_id3;
                break;

            case AV_CODEC_ID_AAC:
                info_->type = "m4a";
                info_->codectype = "mp4a";
                info_->description = "AAC audio";

                info_->supported_ipod_fmt = true;
                break;

            case AV_CODEC_ID_ALAC:
                info_->type = "m4a";
                info_->codectype = "alac";
                info_->description = "Apple Lossless audio";

                info_->supported_ipod_fmt = true;
                break;

    // this block of types will needs transcoding to go onto iPod

            case AV_CODEC_ID_FLAC:
                info_->type = "flac";
                info_->codectype = "flac";
                info_->description = "FLAC audio";

                extra_md_map = md_map_vorbis;
                break;


            case AV_CODEC_ID_APE:
                info_->type = "ape";
                info_->codectype = "ape";
                info_->description = "Monkey's audio";
                break;

            case AV_CODEC_ID_VORBIS:
                info_->type = "ogg";
                info_->codectype = "ogg";
                info_->description = "Ogg Vorbis audio";

                extra_md_map = md_map_vorbis;
                break;

            case AV_CODEC_ID_WMAV1:
            case AV_CODEC_ID_WMAV2:
            case AV_CODEC_ID_WMAVOICE:
                info_->type = "wma";
                info_->codectype = "wmav";
                info_->description = "WMA audio";
                break;

            case AV_CODEC_ID_WMAPRO:
                info_->type = "wmap";
                info_->codectype = "wma";
                info_->description = "WMA audio";
                break;

            case AV_CODEC_ID_WMALOSSLESS:
                info_->type = "wma";
                info_->codectype = "wmal";
                info_->description = "WMA audio";
                break;

            case AV_CODEC_ID_PCM_S16LE ... AV_CODEC_ID_PCM_F64LE:
                if (strcmp(ctx->iformat->name, "aiff") == 0)
                {
                    info_->type = "aif";
                    info_->codectype = "aif";
                    info_->description = "AIFF audio";
                    break;
                }
                else if (strcmp(ctx->iformat->name, "wav") == 0)
                {
                    info_->type = "wav";
                    info_->codectype = "wav";
                    info_->description = "WAV audio";
                    break;
                }
                /* WARNING: will fallthrough to default case, don't move */
                /* FALLTHROUGH */
     
    // everything we're not supporting even if its a valid audio

            default:
                info_->type = "unkn";
                info_->codectype = "unkn";
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

    return track;
}
#endif


bool  gpod_ff_enc_supported(enum gpod_ff_enc enc_)
{
    const char*  codec_name = NULL;
    switch (enc_) {
	case GPOD_FF_ENC_MP3:     codec_name = "libmp3lame";  break;
	case GPOD_FF_ENC_AAC:     codec_name = "aac";         break;
	case GPOD_FF_ENC_FDKAAC:  codec_name = "libfdk_aac";  break;
	case GPOD_FF_ENC_ALAC:    codec_name = "alac";        break;
    }

    return codec_name ? avcodec_find_encoder_by_name(codec_name) : false;
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


int  gpod_ff_audio_hash(char** hash_, const char* file_)
{
    AVFormatContext *ctx = NULL;
    AVPacket *pkt = NULL;
    int64_t pktnum  = 0;
    int err;
    int audio_stream_idx = -1;

    struct AVHashContext *hash;

    *hash_ = NULL;

    err = avformat_open_input(&ctx, file_, NULL, NULL);
    if (err < 0) {
        fprintf(stderr, "cannot open input '%s' - %s\n", av_err2str(err));
        goto cleanup;
    }

    err = avformat_find_stream_info(ctx, NULL);
    if (err < 0) {
        fprintf(stderr, "failed to find stream - %s\n", av_err2str(err));
        goto cleanup;
    }

    audio_stream_idx = av_find_best_stream(ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_stream_idx < 0) {
        fprintf(stderr, "invalid stream on input\n");
	goto cleanup;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "failed to alloc pkt\n");
	err = ENOMEM;
        goto cleanup;
    }

    err = av_hash_alloc(&hash, "sha256");
    if (err < 0) {
        fprintf(stderr, "failed to alloc hash - %s\n", err == EINVAL ? "unknown hash" : strerror(err));
	if (err != EINVAL)  {
	    err = ENOMEM;
	}
        goto cleanup;
    }
    av_hash_init(hash);

    while ((err = av_read_frame(ctx, pkt)) >= 0)
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

    err = 0;

cleanup:
    if (pkt)   av_packet_free(&pkt);
    if (ctx) { avformat_close_input(&ctx); avformat_close_input(&ctx); }
    if (hash)  av_hash_freep(&hash);

    return err;
}


static void  _avlog_callback_null(void *ptr, int level, const char *fmt, va_list vl)
{ }

void  gpod_ff_init()
{
    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    av_log_set_callback(_avlog_callback_null);
}
