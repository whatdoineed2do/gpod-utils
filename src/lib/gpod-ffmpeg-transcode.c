/*
 * Copyright 2021-22 Ray whatdoineed2do at gmail com
 *
 * based on ffmpeg/doc/examples/transcode_aac.c
 * Copyright (c) 2013-2022 Andreas Unterweger
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Simple audio converter
 *
 * @example transcode_aac.c
 * Convert an input audio file to AAC in an MP4 container using FFmpeg.
 * Formats other than MP4 are supported based on the output file extension.
 * @author Andreas Unterweger (dustsigns@gmail.com)
 */

#include "gpod-ffmpeg.h"


#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>

#include <libavformat/avformat.h>
#include <libavformat/avio.h>

#include <libavcodec/avcodec.h>

#include <libavutil/audio_fifo.h>
#include <libavutil/avassert.h>
#include <libavutil/avstring.h>
#ifdef HAVE_FF5_CH_LAYOUT
#include <libavutil/channel_layout.h>
#endif
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>

#include <libswresample/swresample.h>

#define GPOD_MAX_SAMPLERATE  48000
#define GPOD_PREF_SAMPLERATE 44100


/**
 * Open an input file and the required decoder.
 * @param      filename             File to be opened
 * @param[out] input_format_context Format context of opened file
 * @param[out] input_codec_context  Codec context of opened file
 * @return Error code (0 if successful)
 */
static int open_input_file(const char *filename,
                           AVFormatContext **input_format_context,
                           AVCodecContext **input_codec_context, int* audio_stream_idx, char** err_)
{
    AVCodecContext *avctx;
#if LIBAVFORMAT_VERSION_MAJOR > 58
    const
#endif
    AVCodec *input_codec;
    const AVStream *stream;
    int error;

    /* Open the input file to read from it. */
    if ((error = avformat_open_input(input_format_context, filename, NULL,
                                     NULL)) < 0) {
        char  err[1024];
        snprintf(err, 1024,"Could not open input file '%s' (error '%s')",
                filename, av_err2str(error));
            *err_ = strdup(err);
        *input_format_context = NULL;
        return error;
    }

    /* Get information on the input file (number of streams etc.). */
    if ((error = avformat_find_stream_info(*input_format_context, NULL)) < 0) {
        char  err[1024];
        snprintf(err, 1024,"Could not open find stream info (error '%s')",
                av_err2str(error));
            *err_ = strdup(err);
        avformat_close_input(input_format_context);
        return error;
    }

    /* Make sure that there is an audio stream in the input file. */
    *audio_stream_idx = av_find_best_stream(*input_format_context, AVMEDIA_TYPE_AUDIO, -1, -1, &input_codec, 0);
    if (*audio_stream_idx < 0)
    {
        char  err[1024];
        snprintf(err, 1024,"Expected an audio input stream, but found none in %d streams",
                (*input_format_context)->nb_streams);
            *err_ = strdup(err);
        avformat_close_input(input_format_context);
        return AVERROR_EXIT;
    }

    stream = (*input_format_context)->streams[*audio_stream_idx];

    /* Allocate a new decoding context. */
    avctx = avcodec_alloc_context3(input_codec);
    if (!avctx) {
        char  err[1024];
        snprintf(err, 1024,"Could not allocate a decoding context");
            *err_ = strdup(err);
        avformat_close_input(input_format_context);
        return AVERROR(ENOMEM);
    }

    /* Initialize the stream parameters with demuxer information. */
    error = avcodec_parameters_to_context(avctx, stream->codecpar);
    if (error < 0) {
        avformat_close_input(input_format_context);
        avcodec_free_context(&avctx);
        return error;
    }

    /* Open the decoder for the audio stream to use it later. */
    if ((error = avcodec_open2(avctx, input_codec, NULL)) < 0) {
        char  err[1024];
        snprintf(err, 1024,"Could not open input codec (error '%s')",
                av_err2str(error));
            *err_ = strdup(err);
        avcodec_free_context(&avctx);
        avformat_close_input(input_format_context);
        return error;
    }

    /* Set the packet timebase for the decoder. */
    avctx->pkt_timebase = stream->time_base;

#ifdef HAVE_FF5_CH_LAYOUT
    av_channel_layout_copy(&avctx->ch_layout, &(stream->codecpar->ch_layout));
#else
    avctx->channel_layout = av_get_default_channel_layout(avctx->channels);
#endif

    /* Save the decoder context for easier access later. */
    *input_codec_context = avctx;

    return 0;
}

static unsigned  _select_samplerate(const struct AVCodec* output_codec_, unsigned input_samplerate_)
{
    const int*  output_samplerates = output_codec_->supported_samplerates;
    if (output_samplerates == NULL) {
	return input_samplerate_;
    }

    unsigned  sr = 0;
    unsigned  min_sr = *output_samplerates;

    while (*output_samplerates) {
	if (*output_samplerates <= GPOD_MAX_SAMPLERATE && *output_samplerates >= sr) {
	    sr = *output_samplerates;
	}
	if (*output_samplerates < min_sr) {
	    min_sr = *output_samplerates;
	}
	++output_samplerates;
    }

    // input > max sr
    if (input_samplerate_ >= sr) {
	return sr;
    }

    // validate that this sample rate is supported, or find the nearest one down
    output_samplerates = output_codec_->supported_samplerates;
    sr = min_sr;

    while (*output_samplerates) {
	if (input_samplerate_ >= *output_samplerates && sr < *output_samplerates) {
	    sr = *output_samplerates;
	}
	++output_samplerates;
    }
    return sr;
}

/**
 * Open an output file and the required encoder.
 * Also set some basic encoder parameters.
 * Some of these parameters are based on the input file's parameters.
 * @param      filename              File to be opened
 * @param      input_codec_context   Codec context of input file
 * @param[out] output_format_context Format context of output file
 * @param[out] output_codec_context  Codec context of output file
 * @return Error code (0 if successful)
 */
static int open_output_file(struct gpod_ff_transcode_ctx* target_,
                            AVCodecContext *input_codec_context,
                            AVFormatContext **output_format_context,
                            AVCodecContext **output_codec_context, char** err_)
{
    AVCodecContext *avctx          = NULL;
    AVIOContext *output_io_context = NULL;
    AVStream *stream               = NULL;
    const AVCodec *output_codec    = NULL;
    int error;
    const char*  filename = target_->path;

    /* Find the encoder to be used by its name. */
    if (target_->audio_opts.enc_name == NULL) {
	*err_ = strdup("encoder not specified");
	return AVERROR_ENCODER_NOT_FOUND;
    }

    if ( (output_codec = avcodec_find_encoder_by_name(target_->audio_opts.enc_name)) == NULL) {
	char  err[256];
	snprintf(err, 256,"Could not find encoder %s.", target_->audio_opts.enc_name);
	*err_ = strdup(err);
	return AVERROR_ENCODER_NOT_FOUND;
    }

    /* Open the output file to write to it. */
    if ((error = avio_open(&output_io_context, filename,
                           AVIO_FLAG_WRITE)) < 0) {
        char  err[1024];
        snprintf(err, 1024,"Could not open output file '%s' (error '%s')",
                filename, av_err2str(error));
            *err_ = strdup(err);
        return error;
    }

    /* Create a new format context for the output container format. */
    if (!(*output_format_context = avformat_alloc_context())) {
        char  err[1024];
        snprintf(err, 1024,"Could not allocate output format context");
            *err_ = strdup(err);
        return AVERROR(ENOMEM);
    }

    /* Associate the output file (pointer) with the container format context. */
    (*output_format_context)->pb = output_io_context;

    /* Guess the desired container format based on the file extension. */
    if (!((*output_format_context)->oformat = av_guess_format(NULL, filename,
                                                              NULL))) {
        char  err[1024];
        snprintf(err, 1024,"Could not find output file format");
            *err_ = strdup(err);
        goto cleanup;
    }

    if (!((*output_format_context)->url = av_strdup(filename))) {
        char  err[1024];
        snprintf(err, 1024,"Could not allocate url.");
            *err_ = strdup(err);
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    /* Create a new audio stream in the output file container. */
    if (!(stream = avformat_new_stream(*output_format_context, NULL))) {
        char  err[1024];
        snprintf(err, 1024,"Could not create new stream");
            *err_ = strdup(err);
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    avctx = avcodec_alloc_context3(output_codec);
    if (!avctx) {
        char  err[1024];
        snprintf(err, 1024,"Could not allocate an encoding context");
            *err_ = strdup(err);
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    /* Set the basic encoder parameters.
     * validate the sample rate is not higher than max supported / setup for resample
     */
#ifdef HAVE_FF5_CH_LAYOUT
    av_channel_layout_default(&avctx->ch_layout, target_->audio_opts.channels);
#else
    avctx->channels       = target_->audio_opts.channels;
    avctx->channel_layout = av_get_default_channel_layout(avctx->channels);
#endif
    avctx->sample_rate    = _select_samplerate(output_codec, target_->audio_opts.samplerate ? target_->audio_opts.samplerate : input_codec_context->sample_rate);
    avctx->sample_fmt     = target_->audio_opts.samplefmt == AV_SAMPLE_FMT_NONE ? output_codec->sample_fmts[0] : target_->audio_opts.samplefmt;
    const int  quality = (int)(target_->audio_opts.quality);
    if (quality != GPOD_FF_XCODE_MAX)
    {
	if (quality > GPOD_FF_XCODE_VBR_MAX) {
	    avctx->bit_rate = quality;
	}
	else {
	    // vbr
	    avctx->flags         |= AV_CODEC_FLAG_QSCALE;
	    avctx->global_quality = quality * target_->audio_opts.quality_scale_factor;
	}
    }

#ifdef HAVE_FF5_CH_LAYOUT
    /* Allow the use of the experimental AAC encoder. */
    avctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
#endif

    /* Set the sample rate for the container. */
    stream->time_base.den = avctx->sample_rate;
    stream->time_base.num = 1;

    /* Some container formats (like MP4) require global headers to be present.
     * Mark the encoder so that it behaves accordingly. */
    if ((*output_format_context)->oformat->flags & AVFMT_GLOBALHEADER)
        avctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

#ifdef GPOD_XCODE_DEBUG
    // xcoding test-ff-aac-vbr1.m4a.. channels=2 channel_layout=3 bit_rate=0 flags=4194306 global_quality=118 compression_level=%ld sample_rate=44100 qcompress=0 request channel_layout=4294967295 request_sample_fmt=4204882
    printf("channels=%u channel_layout=%u bit_rate=%u flags=%u global_quality=%u compression_level=%ld codec_tag=%u sample_rate=%u qcompress=%u request channel_layout=%u request_sample_fmt=%u\n",
	   avctx->channels, avctx->channel_layout, avctx->bit_rate, avctx->flags, avctx->global_quality, avctx->compression_level, avctx->codec_tag, avctx->sample_rate, avctx->qcompress, avctx->request_channel_layout, avctx->request_sample_fmt);
#endif

    /* Open the encoder for the audio stream to use it later. */
    if ((error = avcodec_open2(avctx, output_codec, NULL)) < 0) {
        char  err[1024];
        snprintf(err, 1024,"Could not open output codec (error '%s')",
                av_err2str(error));
            *err_ = strdup(err);
        goto cleanup;
    }

    error = avcodec_parameters_from_context(stream->codecpar, avctx);
    if (error < 0) {
        char  err[1024];
        snprintf(err, 1024,"Could not initialize stream parameters");
            *err_ = strdup(err);
        goto cleanup;
    }

    /* Save the encoder context for easier access later. */
    *output_codec_context = avctx;

    return 0;

cleanup:
    avcodec_free_context(&avctx);
    avio_closep(&(*output_format_context)->pb);
    avformat_free_context(*output_format_context);
    *output_format_context = NULL;
    return error < 0 ? error : AVERROR_EXIT;
}

/**
 * Initialize one data packet for reading or writing.
 * @param[out] packet Packet to be initialized
 * @return Error code (0 if successful)
 */
static int init_packet(AVPacket **packet, char** err_)
{
    if (!(*packet = av_packet_alloc())) {
        char  err[1024];
        snprintf(err, 1024,"Could not allocate packet");
            *err_ = strdup(err);
        return AVERROR(ENOMEM);
    }
    return 0;
}

/**
 * Initialize one audio frame for reading from the input file.
 * @param[out] frame Frame to be initialized
 * @return Error code (0 if successful)
 */
static int init_input_frame(AVFrame **frame, char** err_)
{
    if (!(*frame = av_frame_alloc())) {
        char  err[1024];
        snprintf(err, 1024,"Could not allocate input frame");
            *err_ = strdup(err);
        return AVERROR(ENOMEM);
    }
    return 0;
}

/**
 * Initialize the audio resampler based on the input and output codec settings.
 * If the input and output sample formats differ, a conversion is required
 * libswresample takes care of this, but requires initialization.
 * @param      input_codec_context  Codec context of the input file
 * @param      output_codec_context Codec context of the output file
 * @param[out] resample_context     Resample context for the required conversion
 * @return Error code (0 if successful)
 */
static int init_resampler(AVCodecContext *input_codec_context,
                          AVCodecContext *output_codec_context,
                          SwrContext **resample_context, char** err_)
{
        int error;

        /*
         * Create a resampler context for the conversion.
         * Set the conversion parameters.
         * Default channel layouts based on the number of channels
         * are assumed for simplicity (they are sometimes not detected
         * properly by the demuxer and/or decoder).
         */
#ifdef HAVE_FF5_CH_LAYOUT
        error = swr_alloc_set_opts2(resample_context,
                                             &output_codec_context->ch_layout,
                                              output_codec_context->sample_fmt,
                                              output_codec_context->sample_rate,
                                             &input_codec_context->ch_layout,
                                              input_codec_context->sample_fmt,
                                              input_codec_context->sample_rate,
                                              0, NULL);
        if (error < 0) {
	    char  err[1024];
            snprintf(err, 1024,"Could not allocate resample context - %s", av_err2str(error));
            *err_ = strdup(err);
            return error;
        }
#else
        *resample_context = swr_alloc_set_opts(NULL,
                                              av_get_default_channel_layout(output_codec_context->channels),
                                              output_codec_context->sample_fmt,
                                              output_codec_context->sample_rate,
                                              av_get_default_channel_layout(input_codec_context->channels),
                                              input_codec_context->sample_fmt,
                                              input_codec_context->sample_rate,
                                              0, NULL);

        if (!*resample_context) {
	    char  err[1024];
            snprintf(err, 1024,"Could not allocate resample context");
            *err_ = strdup(err);
            return AVERROR(ENOMEM);
        }
#endif

        /* Open the resampler with the specified parameters. */
        if ((error = swr_init(*resample_context)) < 0) {
            char  err[1024];
            snprintf(err, 1024,"Could not open resample context\n");
            *err_ = strdup(err);

            swr_free(resample_context);
            return error;
        }
    return 0;
}

/**
 * Initialize a FIFO buffer for the audio samples to be encoded.
 * @param[out] fifo                 Sample buffer
 * @param      output_codec_context Codec context of the output file
 * @return Error code (0 if successful)
 */
static int init_fifo(AVAudioFifo **fifo, AVCodecContext *output_codec_context, char** err_)
{
    /* Create the FIFO buffer based on the specified output sample format. */
    if (!(*fifo = av_audio_fifo_alloc(output_codec_context->sample_fmt,
#ifdef HAVE_FF5_CH_LAYOUT
				      output_codec_context->ch_layout.nb_channels,
#else
                                      output_codec_context->channels,
#endif
				      1))) {
        char  err[1024];
        snprintf(err, 1024, "Could not allocate FIFO");
        *err_ = strdup(err);
        return AVERROR(ENOMEM);
    }
    return 0;
}

/**
 * Write the header of the output file container.
 * @param output_format_context Format context of the output file
 * @return Error code (0 if successful)
 */
static int write_output_file_header(AVFormatContext *output_format_context, char** err_)
{
    int error;
    if ((error = avformat_write_header(output_format_context, NULL)) < 0) {
        char  err[1024];
        snprintf(err, 1024, "Could not write output file header (error '%s')",
                av_err2str(error));
        return error;
    }
    return 0;
}

/**
 * Decode one audio frame from the input file.
 * @param      frame                Audio frame to be decoded
 * @param      input_format_context Format context of the input file
 * @param      input_codec_context  Codec context of the input file
 * @param[out] data_present         Indicates whether data has been decoded
 * @param[out] finished             Indicates whether the end of file has
 *                                  been reached and all data has been
 *                                  decoded. If this flag is false, there
 *                                  is more data to be decoded, i.e., this
 *                                  function has to be called again.
 * @return Error code (0 if successful)
 */
static int decode_audio_frame(AVFrame *frame,
                              AVPacket *input_packet,
                              AVFormatContext *input_format_context,
                              AVCodecContext *input_codec_context,
			      const int audio_stream_idx,
                              int *data_present, int *finished, char** err_)
{
    /* Packet used for temporary storage. */
    int error;

    av_packet_unref(input_packet);

    *data_present = 0;
    *finished = 0;
    /* Read one audio frame from the input file into a temporary packet. */
    input_packet->stream_index = -1;
    while (!*finished && input_packet->stream_index != audio_stream_idx) {
	if ((error = av_read_frame(input_format_context, input_packet)) < 0) {
	    /* If we are at the end of the file, flush the decoder below. */
	    if (error == AVERROR_EOF)
		*finished = 1;
	    else {
		char  err[1024];
		snprintf(err, 1024, "Could not read frame (error '%s')",
			av_err2str(error));
		*err_ = strdup(err);
		goto cleanup;
	    }
	}
    }

    /* Send the audio frame stored in the temporary packet to the decoder.
     * The input audio stream decoder is used to do this. */
    if ((error = avcodec_send_packet(input_codec_context, input_packet)) < 0) {
        char  err[1024];
        snprintf(err, 1024, "Could not send packet for decoding (error '%s')",
                av_err2str(error));
        *err_ = strdup(err);
        goto cleanup;
    }

    /* Receive one frame from the decoder. */
    error = avcodec_receive_frame(input_codec_context, frame);
    /* If the decoder asks for more data to be able to decode a frame,
     * return indicating that no data is present. */
    if (error == AVERROR(EAGAIN)) {
        error = 0;
        goto cleanup;
    /* If the end of the input file is reached, stop decoding. */
    } else if (error == AVERROR_EOF) {
        *finished = 1;
        error = 0;
        goto cleanup;
    } else if (error < 0) {
        char  err[1024];
        snprintf(err, 1024, "Could not decode frame (error '%s')", av_err2str(error));
        *err_ = strdup(err);
        goto cleanup;
    /* Default case: Return decoded data. */
    } else {
        *data_present = 1;
        goto cleanup;
    }

cleanup:
    return error;
}

/**
 * Initialize a temporary storage for the specified number of audio samples.
 * The conversion requires temporary storage due to the different format.
 * The number of audio samples to be allocated is specified in frame_size.
 * @param[out] converted_input_samples Array of converted samples. The
 *                                     dimensions are reference, channel
 *                                     (for multi-channel audio), sample.
 * @param      output_codec_context    Codec context of the output file
 * @param      frame_size              Number of samples to be converted in
 *                                     each round
 * @return Error code (0 if successful)
 */
static int init_converted_samples(uint8_t ***converted_input_samples,
                                  AVCodecContext *output_codec_context,
                                  int frame_size, char** err_)
{
    int error;

    /* Allocate as many pointers as there are audio channels.
     * Each pointer will point to the audio samples of the corresponding
     * channels (although it may be NULL for interleaved formats).
     * Allocate memory for the samples of all channels in one consecutive
     * block for convenience. */
    if ((error = av_samples_alloc_array_and_samples(converted_input_samples, NULL,
#ifdef HAVE_FF5_CH_LAYOUT
				  output_codec_context->ch_layout.nb_channels,
#else
                                  output_codec_context->channels,
#endif
                                  frame_size,
                                  output_codec_context->sample_fmt, 0)) < 0) {
        char  err[1024];
        snprintf(err, 1024,
                "Could not allocate converted input samples (error '%s')",
                av_err2str(error));
        *err_ = strdup(err);
        return error;
    }
    return 0;
}

/**
 * Convert the input audio samples into the output sample format.
 * The conversion happens on a per-frame basis, the size of which is
 * specified by frame_size.
 * @param      input_data       Samples to be decoded. The dimensions are
 *                              channel (for multi-channel audio), sample.
 * @param[out] converted_data   Converted samples. The dimensions are channel
 *                              (for multi-channel audio), sample.
 * @param      frame_size       Number of samples to be converted
 * @param      resample_context Resample context for the conversion
 * @return Error code (converted samnples if successful)
 */
static int convert_samples(const uint8_t **input_data, const int in_samples,
                           uint8_t **converted_data, const int out_samples,
                           SwrContext *resample_context, char** err_)
{
    int error;

/*
 * https://ffmpeg.org/doxygen/3.4/group__lswr.html

    uint8_t **input;
    int in_samples;
    while (get_input(&input, &in_samples)) {
	uint8_t *output;
	int out_samples = av_rescale_rnd(swr_get_delay(swr, 48000) +
					 in_samples, 44100, 48000, AV_ROUND_UP);
	av_samples_alloc(&output, NULL, 2, out_samples,
			 AV_SAMPLE_FMT_S16, 0);
	out_samples = swr_convert(swr, &output, out_samples,
				  input, in_samples);
	handle_output(output, out_samples);
	av_freep(&output);
    }
 */

    /* Convert the samples using the resampler. */
    if ((error = swr_convert(resample_context,
                             converted_data, out_samples,
                             input_data    , in_samples)) < 0) {
        char  err[1024];
        snprintf(err, 1024, "Could not convert input samples (error '%s')", av_err2str(error));
        *err_ = strdup(err);
    }

    return error;
}

/**
 * Add converted input audio samples to the FIFO buffer for later processing.
 * @param fifo                    Buffer to add the samples to
 * @param converted_input_samples Samples to be added. The dimensions are channel
 *                                (for multi-channel audio), sample.
 * @param frame_size              Number of samples to be converted
 * @return Error code (0 if successful)
 */
static int add_samples_to_fifo(AVAudioFifo *fifo,
                               uint8_t **converted_input_samples,
                               const int frame_size, char** err_)
{
    int error;

    /* Make the FIFO as large as it needs to be to hold both,
     * the old and the new samples. */
    if ((error = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + frame_size)) < 0) {
        *err_ = strdup("Could not write data to FIFO");
        return error;
    }

    /* Store the new samples in the FIFO buffer. */
    if (av_audio_fifo_write(fifo, (void **)converted_input_samples,
                            frame_size) < frame_size) {
        *err_ = strdup("Could not write data to FIFO");
        return AVERROR_EXIT;
    }
    return 0;
}

/**
 * Read one audio frame from the input file, decode, convert and store
 * it in the FIFO buffer.
 * @param      fifo                 Buffer used for temporary storage
 * @param      input_format_context Format context of the input file
 * @param      input_codec_context  Codec context of the input file
 * @param      output_codec_context Codec context of the output file
 * @param      resampler_context    Resample context for the conversion
 * @param[out] finished             Indicates whether the end of file has
 *                                  been reached and all data has been
 *                                  decoded. If this flag is false,
 *                                  there is more data to be decoded,
 *                                  i.e., this function has to be called
 *                                  again.
 * @return Error code (0 if successful)
 */
static int read_decode_convert_and_store(AVAudioFifo *fifo,
                                         AVFrame* input_frame,
                                         AVPacket* input_packet,
                                         AVFormatContext *input_format_context,
                                         AVCodecContext *input_codec_context,
					 const int audio_stream_idx,
                                         AVCodecContext *output_codec_context,
                                         SwrContext *resampler_context,
                                         int *finished, char** err_)
{
    /* Temporary storage for the converted input samples. */
    uint8_t **converted_input_samples = NULL;
    int data_present;
    int ret = AVERROR_EXIT;

    av_frame_unref(input_frame);

    /* Decode one frame worth of audio samples. */
    if (decode_audio_frame(input_frame, input_packet, input_format_context,
                           input_codec_context, audio_stream_idx, &data_present, finished, err_))
        goto cleanup;
    /* If we are at the end of the file and there are no more samples
     * in the decoder which are delayed, we are actually finished.
     * This must not be treated as an error. */
    if (*finished) {
        ret = 0;
        goto cleanup;
    }
    /* If there is decoded data, convert and store it. */
    if (data_present) {
        /* Initialize the temporary storage for the converted input samples. */
        if (init_converted_samples(&converted_input_samples, output_codec_context,
                                   input_frame->nb_samples, err_))
            goto cleanup;

        /* Convert the input samples to the desired output sample format.
         * This requires a temporary storage provided by converted_input_samples. */
        if (convert_samples((const uint8_t**)input_frame->extended_data, input_frame->nb_samples,
		            converted_input_samples,
		            input_frame->nb_samples, resampler_context, err_) < 0)
            goto cleanup;

        /* Add the converted input samples to the FIFO buffer for later processing. */
        if (add_samples_to_fifo(fifo, converted_input_samples,
                                input_frame->nb_samples, err_))
            goto cleanup;
        ret = 0;
    }
    ret = 0;

cleanup:
    if (converted_input_samples)
        av_freep(&converted_input_samples[0]);
    av_freep(&converted_input_samples);

    return ret;
}

static int read_decode_and_store(AVAudioFifo *fifo,
                                 AVFrame *input_frame,
                                 AVPacket *input_packet,
				 AVFormatContext *input_format_context,
				 AVCodecContext *input_codec_context,
				 const int audio_stream_idx,
				 int *finished, char** err_)
{
    int data_present;
    int ret = AVERROR_EXIT;

    av_frame_unref(input_frame);

    if (decode_audio_frame(input_frame, input_packet, input_format_context,
                           input_codec_context, audio_stream_idx, &data_present, finished, err_))
        goto cleanup;

    if (*finished) {
        ret = 0;
        goto cleanup;
    }

    if (data_present) {
        /* Add the converted input samples to the FIFO buffer for later processing. */
        if (add_samples_to_fifo(fifo, (uint8_t**)input_frame->extended_data, input_frame->nb_samples, err_))
            goto cleanup;
    }
    ret = 0;

cleanup:

    return ret;
}

/**
 * Initialize one input frame for writing to the output file.
 * The frame will be exactly frame_size samples large.
 * @param[out] frame                Frame to be initialized
 * @param      output_codec_context Codec context of the output file
 * @param      frame_size           Size of the frame
 * @return Error code (0 if successful)
 */
static int init_output_frame(AVFrame **frame,
                             AVCodecContext *output_codec_context,
                             int frame_size, char** err_)
{
    int error;

    /* Create a new frame to store the audio samples. */
    if (*frame == NULL && !(*frame = av_frame_alloc())) {
        *err_ = strdup("Could not allocate output frame");
        return AVERROR_EXIT;
    }

    /* Set the frame's parameters, especially its size and format.
     * av_frame_get_buffer needs this to allocate memory for the
     * audio samples of the frame.
     * Default channel layouts based on the number of channels
     * are assumed for simplicity. */
    (*frame)->nb_samples     = frame_size;
#ifdef HAVE_FF5_CH_LAYOUT
    av_channel_layout_copy(&(*frame)->ch_layout, &output_codec_context->ch_layout);
#else
    (*frame)->channel_layout = output_codec_context->channel_layout;
#endif
    (*frame)->format         = output_codec_context->sample_fmt;
    (*frame)->sample_rate    = output_codec_context->sample_rate;

    /* Allocate the samples of the created frame. This call will make
     * sure that the audio frame can hold as many samples as specified. */
    if ((error = av_frame_get_buffer(*frame, 0)) < 0) {
        char  err[1024];
	snprintf(err, 1024, "Could not allocate output frame samples (error '%s')", av_err2str(error));
        *err_ = strdup(err);
        av_frame_free(frame);
        return error;
    }

    return 0;
}


/**
 * Encode one frame worth of audio to the output file.
 * @param      frame                 Samples to be encoded
 * @param      output_format_context Format context of the output file
 * @param      output_codec_context  Codec context of the output file
 * @param[out] data_present          Indicates whether data has been
 *                                   encoded
 * @return Error code (0 if successful)
 */
static int encode_audio_frame(AVFrame *frame,
                              AVPacket *output_packet,
                              AVFormatContext *output_format_context,
                              AVCodecContext *output_codec_context,
                              int64_t* pts, int *data_present, char** err_)
{
    int error;

    av_packet_unref(output_packet);

    /* Set a timestamp based on the sample rate for the container. */
    if (frame) {
        frame->pts = *pts;
        *pts += frame->nb_samples;
    }

    *data_present = 0;
    /* Send the audio frame stored in the temporary packet to the encoder.
     * The output audio stream encoder is used to do this. */
    error = avcodec_send_frame(output_codec_context, frame);
    /* Check for errors, but proceed with fetching encoded samples if the
     *  encoder signals that it has nothing more to encode. */
    if (error < 0 && error != AVERROR_EOF) {
        char  err[1024];
        snprintf(err, 1024, "Could not send packet for encoding (error '%s')",
                av_err2str(error));
        *err_ = strdup(err);
        goto cleanup;
    }

    /* Receive one encoded frame from the encoder. */
    error = avcodec_receive_packet(output_codec_context, output_packet);
    /* If the encoder asks for more data to be able to provide an
     * encoded frame, return indicating that no data is present. */
    if (error == AVERROR(EAGAIN)) {
        error = 0;
        goto cleanup;
    /* If the last frame has been encoded, stop encoding. */
    } else if (error == AVERROR_EOF) {
        error = 0;
        goto cleanup;
    } else if (error < 0) {
        char  err[1024];
        snprintf(err, 1024, "Could not encode frame (error '%s')\n", av_err2str(error));
        *err_ = strdup(err);
        goto cleanup;
    /* Default case: Return encoded data. */
    } else {
        *data_present = 1;
    }

    /* Write one audio frame from the temporary packet to the output file. */
    if (*data_present &&
        (error = av_write_frame(output_format_context, output_packet)) < 0) {
        char  err[1024];
        snprintf(err, 1024, "Could not write frame (error '%s')", av_err2str(error));
        *err_ = strdup(err);
        goto cleanup;
    }

cleanup:
    return error;
}

static int  load_convert_and_store(AVAudioFifo* output_samples_fifo, AVFrame** frame, const AVFormatContext* output_context, AVCodecContext* output_codec_context, int output_frame_size,
	                           AVAudioFifo* input_samples_fifo,  const AVFormatContext* input_context, AVCodecContext* input_codec_context,
				   SwrContext* resample_context, char** err_)
{
    uint8_t **converted_input_samples = NULL;
    int  ret = AVERROR_EXIT;

    const int frame_size = FFMIN(av_audio_fifo_size(input_samples_fifo),
                                 output_frame_size);

    // yes this is init_output_frame
    if (init_output_frame(frame, input_codec_context, frame_size, err_))
        return AVERROR_EXIT;

    AVFrame *output_frame = *frame;

    /* Read as many samples from the FIFO buffer as required to fill the frame.
     * The samples are stored in the frame temporarily. */
    if (av_audio_fifo_read(input_samples_fifo, (void **)output_frame->data, frame_size) < frame_size) {
        *err_ = strdup("Could not read data from input samples FIFO");
        return AVERROR_EXIT;
    }

    int  nb_samples = (output_codec_context->sample_rate == input_codec_context->sample_rate) ?
	output_frame->nb_samples :
	av_rescale_rnd(swr_get_delay(resample_context, input_codec_context->sample_rate) + output_frame->nb_samples, output_codec_context->sample_rate, input_codec_context->sample_rate, AV_ROUND_DOWN);

#ifdef GPOD_XCODE_SWR_DEBUG
    printf("load/convert  frame size=%d  -> nb_samples=%d   out ctx frame=%d\n", frame_size, output_frame->nb_samples, output_codec_context->frame_size);
#endif

    /* Initialize the temporary storage for the converted input samples. */
    if (init_converted_samples(&converted_input_samples, output_codec_context,
		nb_samples, err_))
	goto cleanup;

    /* Convert the input samples to the desired output sample format.
     * This requires a temporary storage provided by converted_input_samples. */
    if ( (nb_samples = convert_samples((const uint8_t**)output_frame->extended_data, output_frame->nb_samples,
		converted_input_samples, output_codec_context->frame_size,
		resample_context, err_)) < 0)
	goto cleanup;

    /* Add the converted input samples to the FIFO buffer for later processing. */
    if (add_samples_to_fifo(output_samples_fifo, converted_input_samples,
		nb_samples, err_))
	goto cleanup;

    ret = 0;

cleanup:
    if (converted_input_samples) {
        av_freep(&converted_input_samples[0]);
        free(converted_input_samples);
    }
    av_frame_unref(output_frame);

    return ret;
}

/**
 * Load one audio frame from the FIFO buffer, encode and write it to the
 * output file.
 * @param fifo                  Buffer used for temporary storage
 * @param output_format_context Format context of the output file
 * @param output_codec_context  Codec context of the output file
 * @return Error code (0 if successful)
 */
static int load_encode_and_write(AVAudioFifo *fifo,
                                 AVFrame **frame,
                                 AVPacket *output_packet,
                                 AVFormatContext *output_format_context,
                                 AVCodecContext *output_codec_context, int64_t* pts, char** err_)
{
    /* Temporary storage of the output samples of the frame written to the file. */ 

    /* Use the maximum number of possible samples per frame.
     * If there is less than the maximum possible frame size in the FIFO
     * buffer use this number. Otherwise, use the maximum possible frame size. */
    const int frame_size = FFMIN(av_audio_fifo_size(fifo),
                                 output_codec_context->frame_size);
    int data_written;

    /* Initialize temporary storage for one output frame. */
    if (init_output_frame(frame, output_codec_context, frame_size, err_))
        return AVERROR_EXIT;

    AVFrame *output_frame = *frame;

    /* Read as many samples from the FIFO buffer as required to fill the frame.
     * The samples are stored in the frame temporarily. */
    if (av_audio_fifo_read(fifo, (void **)output_frame->data, frame_size) < frame_size) {
        *err_ = strdup("Could not read data from FIFO");
        return AVERROR_EXIT;
    }

    /* Encode one frame worth of audio samples. */
    if (encode_audio_frame(output_frame, output_packet, output_format_context,
                           output_codec_context, pts, &data_written, err_)) {
        return AVERROR_EXIT;
    }
    av_frame_unref(output_frame);
    return 0;
}

/**
 * Write the trailer of the output file container.
 * @param output_format_context Format context of the output file
 * @return Error code (0 if successful)
 */
static int write_output_file_trailer(AVFormatContext *output_format_context, char** err_)
{
    int error;
    if ((error = av_write_trailer(output_format_context)) < 0) {
        char  err[1024];
        snprintf(err, 1024, "Could not write output file trailer (error '%s')",
                av_err2str(error));
        *err_ = strdup(err);
        return error;
    }
    return 0;
}


int  gpod_ff_transcode(struct gpod_ff_media_info *info_, struct gpod_ff_transcode_ctx* target_, char** err_)
{
    AVFormatContext  *input_format_context = NULL, *output_format_context = NULL;
    AVCodecContext  *input_codec_context = NULL, *output_codec_context = NULL;
    SwrContext  *resample_context = NULL;
    AVAudioFifo  *fifo = NULL;
    AVAudioFifo  *input_samples_fifo = NULL;
    /* Temporary storage of the input samples of the frame read from the file. */
    AVFrame *input_frame = NULL;
    AVFrame *output_frame = NULL;
    AVPacket *input_packet = NULL;
    AVPacket *output_packet = NULL;
    int ret = AVERROR_EXIT;
    int audio_stream_idx;

    /* timestamp for the audio frames. */
    int64_t pts = 0;


    /* Open the input file for reading. */
    if (open_input_file(info_->path, &input_format_context,
                        &input_codec_context, &audio_stream_idx, err_))
        goto cleanup;

    /* Open the output file for writing. */
    if (open_output_file(target_, input_codec_context,
                         &output_format_context, &output_codec_context, err_))
        goto cleanup;

    if (target_->sync_meta) {
	av_dict_copy(&output_format_context->metadata, input_format_context->metadata, 0);
    }

#ifdef GPOD_XCODE_SWR_DEBUG
    printf("input: file=%s  channels=%d  layout=%d  format=%s  sample_rate=%d  output: channels=%d  layout=%d  format=%s  sample_rate=%d\n",
	    info_->path, input_codec_context->channels, input_codec_context->channel_layout, av_get_sample_fmt_name(input_codec_context->sample_fmt), input_codec_context->sample_rate,
	    output_codec_context->channels, output_codec_context->channel_layout, av_get_sample_fmt_name(output_codec_context->sample_fmt), output_codec_context->sample_rate
	    );
#endif

    /* Initialize the resampler to be able to convert audio sample formats. */
    if (init_resampler(input_codec_context, output_codec_context,
                       &resample_context, err_))
        goto cleanup;
    /* Initialize the FIFO buffer to store audio samples to be encoded. */
    if (init_fifo(&fifo, output_codec_context, err_))
        goto cleanup;

    if (init_fifo(&input_samples_fifo, input_codec_context, err_))
        goto cleanup;

    /* Write the header of the output file container. */
    if (write_output_file_header(output_format_context, err_))
        goto cleanup;

    /* Initialize temporary storage for one input frame. */
    if (init_input_frame(&input_frame, err_))
        goto cleanup;

    if (init_packet(&input_packet, err_) < 0)
        goto cleanup;
    if (init_packet(&output_packet, err_) < 0)
        goto cleanup;

    /* Loop as long as we have input samples to read or output samples
     * to write; abort as soon as we have neither. */
    while (1) {
        /* Use the encoder's desired frame size for processing. */
        const int output_frame_size = output_codec_context->frame_size;
        int finished                = 0;

        /* Make sure that there is one frame worth of samples in the FIFO
         * buffer so that the encoder can do its work.
         * Since the decoder's and the encoder's frame size may differ, we
         * need to FIFO buffer to store as many frames worth of input samples
         * that they make up at least one frame worth of output samples. */
	if (output_codec_context->sample_rate == input_codec_context->sample_rate)
	{
	    while (av_audio_fifo_size(fifo) < output_frame_size) {
		/* Decode one frame worth of audio samples, convert it to the
		 * output sample format and put it into the FIFO buffer. */
		if (read_decode_convert_and_store(fifo, input_frame, input_packet, input_format_context,
			    input_codec_context,
			    audio_stream_idx,
			    output_codec_context,
			    resample_context, &finished, err_))
		    goto cleanup;

		/* If we are at the end of the input file, we continue
		 * encoding the remaining audio samples to the output file. */
		if (finished)
		    break;
	    }
	}
	else
	{
	    // sample rate conversion case
	    /* Re: Resample frame to specified number of samples
	     * https://ffmpeg.org/pipermail/libav-user/2017-July/010496.html
	     * Yes, you need to buffer sufficient audio frames to feed to the encoder.
	     *
	     * Calculate the number of in samples:
		    in_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, c->sample_rate) +
		out_nb_samples,
				    in_sample_rate, c->sample_rate, AV_ROUND_DOWN);

		then allocate buffers to concatenate the in samples until you have enough
		to pass to swr_ctx.
	     */
	    while (av_audio_fifo_size(input_samples_fifo) < output_frame_size) {
		if (read_decode_and_store(input_samples_fifo,
                            input_frame,
                            input_packet,
			    input_format_context, input_codec_context,
			    audio_stream_idx,
			    &finished, err_))
		    goto cleanup;

		if (finished)
		    break;
	    }
#ifdef GPOD_XCODE_SWR_DEBUG
	    printf("fifo buf can convert:  input samples=%d  frame size=%d  input frame size=%d  channels=%d  channel_layout=%d sample fmt=%s  sample rate=%d  -> output codec frame size=%d  channels=%d  layout=%d  sample format=%s  sample rate=%d  swr delay=%d  finished=%d\n", av_audio_fifo_size(input_samples_fifo),
		    output_frame_size,
		    input_codec_context->frame_size, input_codec_context->channels, input_codec_context->channel_layout, av_get_sample_fmt_name(input_codec_context->sample_fmt), input_codec_context->sample_rate, 
		    output_codec_context->frame_size, output_codec_context->channels, output_codec_context->channel_layout, av_get_sample_fmt_name(output_codec_context->sample_fmt), output_codec_context->sample_rate, 
		    swr_get_delay(resample_context, input_codec_context->sample_rate), finished);
#endif

	    while (av_audio_fifo_size(input_samples_fifo) >= output_frame_size ||
		    (finished && av_audio_fifo_size(input_samples_fifo) > 0)) {
		/* take all input samples and convert them before handing off to encoder
		*/
		if (load_convert_and_store(fifo, &output_frame,
			    output_format_context, output_codec_context, output_frame_size,
			    input_samples_fifo, input_format_context, input_codec_context,
			    resample_context, err_))
		    goto cleanup;
	    }
	}


        /* If we have enough samples for the encoder, we encode them.
         * At the end of the file, we pass the remaining samples to
         * the encoder. */
        while (av_audio_fifo_size(fifo) >= output_frame_size ||
               (finished && av_audio_fifo_size(fifo) > 0))
            /* Take one frame worth of audio samples from the FIFO buffer,
             * encode it and write it to the output file. */
            if (load_encode_and_write(fifo, &output_frame, output_packet, output_format_context,
                                      output_codec_context, &pts, err_))
                goto cleanup;

        /* If we are at the end of the input file and have encoded
         * all remaining samples, we can exit this loop and finish. */
        if (finished) {
            int data_written;
            /* Flush the encoder as it may have delayed frames. */
            do {
                if (encode_audio_frame(NULL, output_packet, output_format_context,
                                       output_codec_context, &pts, &data_written, err_))
                    goto cleanup;
            } while (data_written);
            break;
        }
    }

    /* Write the trailer of the output file container. */
    if (write_output_file_trailer(output_format_context, err_))
        goto cleanup;

    struct stat  st;
    stat(info_->path, &st);
    info_->file_size = st.st_size;

    ret = 0;

cleanup:
    if (input_samples_fifo)
        av_audio_fifo_free(input_samples_fifo);
    if (fifo)
        av_audio_fifo_free(fifo);
    swr_free(&resample_context);
    if (output_codec_context)
        avcodec_free_context(&output_codec_context);
    if (output_format_context) {
        avio_closep(&output_format_context->pb);
        avformat_free_context(output_format_context);
    }
    if (input_codec_context)
        avcodec_free_context(&input_codec_context);
    if (input_format_context)
        avformat_close_input(&input_format_context);
    if (input_frame)
        av_frame_free(&input_frame);
    if (input_packet)
        av_packet_free(&input_packet);
    if (output_frame)
        av_frame_free(&output_frame);
    if (output_packet)
        av_packet_free(&output_packet);

    return ret;
}
