#ifndef GPOD_FFMPEG_H
#define GPOD_FFMPEG_H

/* Copyright 2021 Ray whatdoineed2do @ gmail com
 *
 * based on forked-daapd filescanner_ffmpeg.c
 * Copyright (C) 2009-2011 Julien BLACHE <jb@jblache.org
 */

#ifdef __cplusplus
extern "C" {
#endif


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <limits.h>
#include <stdint.h>
#include <stdbool.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#ifndef GPOD_FF_STANDALONE
#include <gpod/itdb.h>
#endif

struct gpod_ff_meta {
    bool  has_meta;
    char *title;
    char *artist;
    char *album;
    char *album_artist;
    char *genre;
    char *comment;
    char *composer;
    char *grouping;

    uint32_t year;         /* TDRC */
    uint32_t date_released;

    uint32_t track;        /* TRCK */
    uint32_t total_tracks;

    uint32_t disc;         /* TPOS */
    uint32_t total_discs;

    uint32_t compilation;
    char *title_sort;
    char *artist_sort;
    char *album_sort;
    char *album_artist_sort;
    char *composer_sort;
}; 

struct gpod_ff_audio {
    enum AVCodecID  codec_id;

    uint32_t  bitrate;
    uint32_t  samplerate;
    uint32_t  channels;
    uint32_t  song_length;
    uint32_t  bits_per_sample;
};

struct gpod_ff_video {
    enum AVCodecID  codec_id;

    uint32_t  width;
    uint32_t  height;
    int       profile;
    uint32_t  length;
    uint32_t  bitrate;
    float     fps;
};


struct gpod_ff_media_info
{
    char  path[PATH_MAX];
    int64_t  file_size;

    const char*  type;  // m4a, mp3...
    const char*  codectype;  // alac...
    const char*  description;

    bool  has_video; // is video file
    bool  has_audio; // is audio file
    bool  supported_ipod_fmt;  // mp3, m4a, mp4/m4v

    struct gpod_ff_audio  audio;
    struct gpod_ff_video  video;
    struct gpod_ff_meta  meta;
};

enum gpod_ff_enc {
    GPOD_FF_ENC_MP3,
    GPOD_FF_ENC_AAC,
    GPOD_FF_ENC_FDKAAC,
    GPOD_FF_ENC_ALAC,

    GPOD_FF_ENC_MAX
};

enum gpod_ff_transcode_quality { 
    GPOD_FF_XCODE_VBR0 = 0,
    GPOD_FF_XCODE_VBR1,
    GPOD_FF_XCODE_VBR2,
    GPOD_FF_XCODE_VBR3,
    GPOD_FF_XCODE_VBR4,
    GPOD_FF_XCODE_VBR5,
    GPOD_FF_XCODE_VBR6,
    GPOD_FF_XCODE_VBR7,
    GPOD_FF_XCODE_VBR8,
    GPOD_FF_XCODE_VBR9,
    GPOD_FF_XCODE_VBR_MAX = GPOD_FF_XCODE_VBR9,

    GPOD_FF_XCODE_CBR96 = 96000,
    GPOD_FF_XCODE_CBR128 = 128000,
    GPOD_FF_XCODE_CBR160 = 160000,
    GPOD_FF_XCODE_CBR192 = 192000,
    GPOD_FF_XCODE_CBR256 = 256000,
    GPOD_FF_XCODE_CBR320 = 320000,

    GPOD_FF_XCODE_MAX
};

struct gpod_ff_transcode_ctx {
    struct {
        enum AVCodecID  codec_id;
        const char*  enc_name;  // if set use this over codec_id
        uint8_t  channels;
        uint32_t  samplerate;
        enum AVSampleFormat  samplefmt;
        enum gpod_ff_transcode_quality  quality;
        float  quality_scale_factor;
    } audio_opts;

    bool  sync_meta;

    const char*  extn;
    char  path[PATH_MAX];
    char  tmpprfx[PATH_MAX];
};

void  gpod_ff_meta_free(struct gpod_ff_meta*  obj_);
void  gpod_ff_media_info_free(struct gpod_ff_media_info*  obj_);
void  gpod_ff_media_info_init(struct gpod_ff_media_info*  obj_);

#ifndef GPOD_FF_STANDALONE
int  gpod_ff_scan(struct gpod_ff_media_info *info_, const char *file_, Itdb_IpodGeneration target_, char** err_);

Itdb_Track*  gpod_ff_meta_to_track(const struct gpod_ff_media_info* meta_, time_t time_added_, bool sanitize_);
#endif


bool  gpod_ff_enc_supported(enum gpod_ff_enc  enc_);

void  gpod_ff_transcode_ctx_init(struct gpod_ff_transcode_ctx* obj_,
                                 enum gpod_ff_enc enc_, enum gpod_ff_transcode_quality quality_, bool sync_meta_);

int  gpod_ff_transcode(struct gpod_ff_media_info *info_, struct gpod_ff_transcode_ctx* target, char** err_);


void  gpod_ff_init();

#ifdef __cplusplus
}
#endif
#endif
