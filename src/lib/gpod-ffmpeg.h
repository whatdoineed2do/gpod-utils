#ifndef GPOD_FFMPEG_H
#define GPOD_FFMPEG_H

/* Copyright 2021 Ray whatdoineed2do @ gmail com
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
#include <libswscale/swscale.h>

#include <glib.h>

#ifndef GPOD_FF_STANDALONE
#include <gpod/itdb.h>
#endif

/* ffmpeg 7+ (libavcodec 61) renamed FF_PROFILE_* to AV_PROFILE_* and removed
 * the old names in libavcodec 62 */
#ifndef FF_PROFILE_UNKNOWN
# define FF_PROFILE_UNKNOWN                   AV_PROFILE_UNKNOWN
# define FF_PROFILE_H264_BASELINE             AV_PROFILE_H264_BASELINE
# define FF_PROFILE_H264_CONSTRAINED_BASELINE AV_PROFILE_H264_CONSTRAINED_BASELINE
# define FF_PROFILE_H264_MAIN                 AV_PROFILE_H264_MAIN
# define FF_PROFILE_H264_EXTENDED             AV_PROFILE_H264_EXTENDED
# define FF_PROFILE_H264_HIGH                 AV_PROFILE_H264_HIGH
# define FF_PROFILE_H264_HIGH_10              AV_PROFILE_H264_HIGH_10
# define FF_PROFILE_H264_HIGH_10_INTRA        AV_PROFILE_H264_HIGH_10_INTRA
# define FF_PROFILE_H264_MULTIVIEW_HIGH       AV_PROFILE_H264_MULTIVIEW_HIGH
# define FF_PROFILE_H264_HIGH_422             AV_PROFILE_H264_HIGH_422
# define FF_PROFILE_H264_HIGH_422_INTRA       AV_PROFILE_H264_HIGH_422_INTRA
# define FF_PROFILE_H264_STEREO_HIGH          AV_PROFILE_H264_STEREO_HIGH
# define FF_PROFILE_H264_HIGH_444             AV_PROFILE_H264_HIGH_444
# define FF_PROFILE_H264_HIGH_444_PREDICTIVE  AV_PROFILE_H264_HIGH_444_PREDICTIVE
# define FF_PROFILE_H264_HIGH_444_INTRA       AV_PROFILE_H264_HIGH_444_INTRA
# define FF_PROFILE_H264_CAVLC_444            AV_PROFILE_H264_CAVLC_444
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

struct gpod_ff_coverart {
    gsize size;
    guchar *data;
};

struct gpod_ff_media_info
{
    char  path[PATH_MAX];
    int64_t  file_size;

    const char*  type;  // m4a, mp3...
    const char*  description;

    bool  has_video; // is video file
    bool  has_audio; // is audio file
    bool  supported_ipod_fmt;  // mp3, m4a, mp4/m4v

    struct gpod_ff_audio  audio;
    struct gpod_ff_video  video;
    struct gpod_ff_meta  meta;
    struct gpod_ff_coverart coverart;
};

enum gpod_ff_enc {
    GPOD_FF_ENC_MP3,
    GPOD_FF_ENC_FDKAAC,
    GPOD_FF_ENC_AAC,
    GPOD_FF_ENC_AAC_AT,
    GPOD_FF_ENC_ALAC,

    GPOD_FF_ENC_MAX
};

struct gpod_ff_enc_support {
    const enum gpod_ff_enc  enc;
    const char* const  name;      // what we call this
    const char* const  enc_name;  // the libavcodec encoder name
    bool  supported;              // does the libavcodec library support this (namely, libfdk_aac)
};

extern const struct  gpod_ff_enc_support*  gpod_ff_encoders;


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


const struct gpod_ff_enc_support*  gpod_ff_enc_supported(enum gpod_ff_enc  enc_);

void  gpod_ff_transcode_ctx_init(struct gpod_ff_transcode_ctx* obj_,
                                 enum gpod_ff_enc enc_, enum gpod_ff_transcode_quality quality_, bool sync_meta_);

int  gpod_ff_transcode(struct gpod_ff_media_info *info_, struct gpod_ff_transcode_ctx* target, char** err_);

/* On success, returns 0 and hash_ is non-NULL and must be freeed
 */
int  gpod_ff_audio_hash(char** hash_, const char* file_, char** err_);

void  gpod_ff_init();

#ifdef __cplusplus
}
#endif
#endif
