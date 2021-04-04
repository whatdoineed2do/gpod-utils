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
#define _XOPEN_SOURCE 1
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>



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



int  gpod_ff_scan(struct gpod_ff_media_info *info_, const char *file_, char** err_)
{
    AVFormatContext *ctx;
    AVDictionary *options;
    enum AVMediaType codec_type;
    enum AVCodecID codec_id;
    enum AVCodecID video_codec_id;
    enum AVCodecID audio_codec_id;
    enum AVSampleFormat sample_fmt;
    AVStream *video_stream;
    AVStream *audio_stream;

    int mdcount;
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
        *err_ = strdup("failed to avformat_open_input()");
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
        channels    = ctx->streams[i]->codecpar->channels;
        switch (codec_type)
        {
            /* WARN -- only consider the file's FIRST audio stream - if normal 
             * siutations this is fine
             */
            case AVMEDIA_TYPE_AUDIO:
            {
                info_->has_audio = true;
                if (!audio_stream)
                {
                    audio_stream = ctx->streams[i];

                    info_->audio_codec_id = audio_codec_id = codec_id;
                    info_->audio.samplerate = sample_rate;
                    info_->audio.bits_per_sample = 8 * av_get_bytes_per_sample(sample_fmt);

                    if (info_->audio.bits_per_sample == 0) {
                        info_->audio.bits_per_sample = av_get_bits_per_sample(codec_id);
                    }
                    info_->audio.channels = channels;
                } 
            } break;

            default:
                break;
        }
    }

    if (audio_codec_id == AV_CODEC_ID_NONE) {
        info_->has_audio = false;
        avformat_close_input(&ctx);
        return -1;
    }

    if (ctx->duration > 0) {
        info_->audio.song_length = ctx->duration / (AV_TIME_BASE / 1000); /* ms */
    }

    if (ctx->bit_rate > 0) {
        info_->audio.bitrate = ctx->bit_rate / 1000;
    }
    else if (ctx->duration > AV_TIME_BASE) /* guesstimate */ {
        info_->audio.bitrate = ((info_->file_size * 8) / (ctx->duration / AV_TIME_BASE)) / 1000;
    }


    /* Check codec */
    codec_id = audio_codec_id;
    info_->supported_ipod_fmt = false;
    switch (codec_id)
    {
        case AV_CODEC_ID_MP3:
            info_->type = "mp3";
            info_->codectype = "mpeg";
            info_->description = "MPEG audio file";

            info_->supported_ipod_fmt = true;
            break;

        case AV_CODEC_ID_AAC:
            info_->type = "m4a";
            info_->codectype = "mp4a";
            info_->description = "AAC audio file";

            info_->supported_ipod_fmt = true;
            break;

        case AV_CODEC_ID_ALAC:
            info_->type = "m4a";
            info_->codectype = "alac";
            info_->description = "Apple Lossless audio file";

            info_->supported_ipod_fmt = true;
            break;

// this block of types will needs transcoding to go onto iPod

        case AV_CODEC_ID_FLAC:
            info_->type = "flac";
            info_->codectype = "flac";
            info_->description = "FLAC audio file";
            break;


        case AV_CODEC_ID_APE:
            info_->type = "ape";
            info_->codectype = "ape";
            info_->description = "Monkey's audio";
            break;

        case AV_CODEC_ID_VORBIS:
            info_->type = "ogg";
            info_->codectype = "ogg";
            info_->description = "Ogg Vorbis audio file";
            break;

        case AV_CODEC_ID_WMAV1:
        case AV_CODEC_ID_WMAV2:
        case AV_CODEC_ID_WMAVOICE:
            info_->type = "wma";
            info_->codectype = "wmav";
            info_->description = "WMA audio file";
            break;

        case AV_CODEC_ID_WMAPRO:
            info_->type = "wmap";
            info_->codectype = "wma";
            info_->description = "WMA audio file";
            break;

        case AV_CODEC_ID_WMALOSSLESS:
            info_->type = "wma";
            info_->codectype = "wmal";
            info_->description = "WMA audio file";
            break;

        case AV_CODEC_ID_PCM_S16LE ... AV_CODEC_ID_PCM_F64LE:
            if (strcmp(ctx->iformat->name, "aiff") == 0)
            {
                info_->type = "aif";
                info_->codectype = "aif";
                info_->description = "AIFF audio file";
                break;
            }
            else if (strcmp(ctx->iformat->name, "wav") == 0)
            {
                info_->type = "wav";
                info_->codectype = "wav";
                info_->description = "WAV audio file";
                break;
            }
            /* WARNING: will fallthrough to default case, don't move */
            /* FALLTHROUGH */
 
// everything we're not supporting even if its a valid audio

        default:
            info_->type = "unkn";
            info_->codectype = "unkn";
            info_->description = "Unknown audio file format";
            break;
    }

    info_->meta.has_meta = ((!ctx->metadata) && (!audio_stream->metadata)) ? false : true;
    if (info_->meta.has_meta) {
        ret = extract_metadata(info_, ctx, audio_stream, md_map_generic);
    }

    avformat_close_input(&ctx);
    return 0;
}
