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

struct gpod_ff_media_info
{
    char  path[PATH_MAX];
    int64_t  file_size;

    const char*  type;  // m4a, mp3...
    const char*  codectype;  // alac...
    const char*  description;

    bool  has_audio; // is audio file
    bool  supported_ipod_fmt;  // mp3 or m4a

    enum AVCodecID  audio_codec_id;

    struct _audio {
        uint32_t  bitrate;
        uint32_t  samplerate;
        uint32_t  channels;
        uint32_t  song_length;
        uint32_t  bits_per_sample;
    } audio;

    struct gpod_ff_meta  meta;
};

void  gpod_ff_meta_free(struct gpod_ff_meta*  obj_);
void  gpod_ff_media_info_free(struct gpod_ff_media_info*  obj_);
void  gpod_ff_media_info_init(struct gpod_ff_media_info*  obj_);

int  gpod_ff_scan(struct gpod_ff_media_info *info_, const char *file_, char** err_);


#ifdef __cplusplus
}
#endif
#endif
