#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>

#include "gpod-ffmpeg.h"
#include "test-ff-wav.h"
#include <libavutil/log.h>
#include <libavutil/error.h>
#include <libavutil/hash.h>
#include <libavformat/avformat.h>


char  path[PATH_MAX] = { 0 };

#define TEST_FF_XCODE_WAV_SAMPLE "test-ff-xcode.wav"
int _generate_sample()
{
    int  fd;
    if ( (fd=open(TEST_FF_XCODE_WAV_SAMPLE, O_CREAT | O_TRUNC | O_WRONLY, 0600)) < 0) {
        return -errno;
    }

    if (write(fd, sample_wav, sizeof(sample_wav)) != sizeof(sample_wav)) {
	close(fd);
        return 1;
    }
    close(fd);

    strcpy(path, TEST_FF_XCODE_WAV_SAMPLE);
    return 0;
}

static char* ff_hash_fin(struct AVHashContext*  hash_ctx_, bool want_b64_)
{
    char  res[2 * AV_HASH_MAX_SIZE + 4] = { 0 };

    if (want_b64_) {
        av_hash_final_b64(hash_ctx_, res, sizeof(res));
    } else {
        av_hash_final_hex(hash_ctx_, res, sizeof(res));
    }

    return strdup(res);
}


int  ff_hash(char** hash_, const char* file_)
{
    AVFormatContext *ctx = NULL;
    const AVCodec *codec = NULL;
    AVCodecContext *codec_ctx = NULL;
    AVPacket *pkt = NULL;
    AVFrame  *frame = NULL;
    int64_t pktnum  = 0;
    int err;
    int audio_stream_idx = -1;
    int ret = -1;
    bool  has_finished;

    struct AVHashContext *hash;

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

    for (int i=0; i<ctx->nb_streams; ++i)
    {
        if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_idx = i;
            break;
        }
    }

    if (audio_stream_idx < 0) {
        fprintf(stderr, "invalid stream on input\n");
	goto cleanup;
    }

    codec = avcodec_find_decoder(ctx->streams[audio_stream_idx]->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "failed to find decoding context\n");
        goto cleanup;
    }
    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, ctx->streams[audio_stream_idx]->codecpar);

    err = avcodec_open2(codec_ctx, codec, NULL);
    if (err < 0) {
        fprintf(stderr, "failed to find decoding context\n");
        goto cleanup;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "failed to alloc pkt\n");
        goto cleanup;
    }

    frame = av_frame_alloc();

    err = av_hash_alloc(&hash, "sha256");
    if (err < 0) {
        fprintf(stderr, "failed to alloc hash - %s\n", err == EINVAL ? "unknown hash" : strerror(err));
        goto cleanup;
    }
    av_hash_init(hash);

    has_finished = false;
    while (!has_finished) 
    {
	while ((err = av_read_frame(ctx, pkt)) >= 0)
	{
	    if (pkt->stream_index != audio_stream_idx) {
		av_packet_unref(pkt);
		continue;
	    }

	    // ask decoder for an uncompressed audio frame
	    avcodec_send_packet(codec_ctx, pkt);

	    err = avcodec_receive_frame(codec_ctx, frame);
	    break;
	}
	if (err == AVERROR(EAGAIN)) {
	    av_packet_unref(pkt);
	    continue;
	}

	if (err == AVERROR_EOF) {
	    has_finished = true;
	}
	else {
	    fprintf(stderr, ".");
	    av_hash_update(hash, pkt->data, pkt->size);
	    av_packet_unref(pkt);
	    av_frame_unref(frame);
	    pktnum++;
	}
    }

    *hash_ = ff_hash_fin(hash, false);
    ret = 0;

cleanup:
    if (pkt)        av_packet_free(&pkt);
    if (frame)      av_frame_free(&frame);
    if (ctx)      { avformat_close_input(&ctx); avformat_close_input(&ctx); }
    if (codec_ctx)  avcodec_free_context(&codec_ctx);
    if (hash)       av_hash_freep(&hash);

    return ret;
}


int main(int argc, char* argv[])
{
    const struct option  long_opts[] = {
        { "verbose", 0, 0, 'v' },
        { "debug",   0, 0, 'd' },
        { "info",    0, 0, 'i' },
        { "quiet",   0, 0, 'q' },
        { "help",    0, 0, 'h' },

        {0, 0, 0,  0 }
    };
    char  opt_args[sizeof(long_opts)*2] = { 0 };
    {
        char*  og = opt_args;
        const struct option* op = long_opts;
        while (op->name) {
            *og++ = op->val;
            if (op->has_arg != no_argument) {
                *og++ = ':';
            }
            ++op;
        }
    }


    bool  default_gpod_log_level = true;
    int  c;
    while ( (c=getopt_long(argc, argv, opt_args, long_opts, NULL)) != -1)
    {
        switch (c) {
	    case 'v':  av_log_set_level(AV_LOG_VERBOSE); default_gpod_log_level = false; break;
	    case 'd':  av_log_set_level(AV_LOG_DEBUG);   default_gpod_log_level = false; break;
	    case 'i':  av_log_set_level(AV_LOG_INFO);    default_gpod_log_level = false; break;
	    case 'q':  av_log_set_level(AV_LOG_QUIET);   default_gpod_log_level = false; break;

	    case 'h':
	    default:
		break;
	}
    }

    if (default_gpod_log_level) {
        gpod_ff_init();
    }

    if (argc == optind) {
	if (_generate_sample() != 0) {
	    fprintf(stderr, "unabled to generated data sample %s - %s\n", TEST_FF_XCODE_WAV_SAMPLE, strerror(errno));
	    return -1;
	}
    }
    else {
	strcpy(path, argv[optind]);
    }


    const struct Fmt {
        enum gpod_ff_enc  enc;
        enum gpod_ff_transcode_quality  quality;
        const char*  name;
    } fmts[] = {
        { GPOD_FF_ENC_FDKAAC, GPOD_FF_XCODE_VBR1,   "test-ff-aac-%d-vbr1.aac" },
        { GPOD_FF_ENC_FDKAAC, GPOD_FF_XCODE_VBR1,   "test-ff-fdkaac-%d-vbr1.m4a" },
        { GPOD_FF_ENC_AAC,    GPOD_FF_XCODE_VBR1,   "test-ff-aac-%d-vbr1.m4a" },
        { GPOD_FF_ENC_FDKAAC, GPOD_FF_XCODE_VBR9,   "test-ff-fdkaac-%d-vbr9.m4a" },
        { GPOD_FF_ENC_AAC,    GPOD_FF_XCODE_VBR9,   "test-ff-aac-%d-vbr9.m4a" },

        { GPOD_FF_ENC_FDKAAC, GPOD_FF_XCODE_CBR256, "test-ff-fdkaac-%d-cbr256.m4a" },
        { GPOD_FF_ENC_FDKAAC, GPOD_FF_XCODE_CBR160, "test-ff-fdkaac-%d-cbr160.m4a" },
        { GPOD_FF_ENC_AAC,    GPOD_FF_XCODE_CBR256, "test-ff-aac-%d-cbr256.m4a" },
        { GPOD_FF_ENC_AAC,    GPOD_FF_XCODE_CBR160, "test-ff-aac-%d-cbr160.m4a" },

        { GPOD_FF_ENC_MP3,    GPOD_FF_XCODE_VBR1,   "test-ff-mp3-%d-vbr1.mp3" },
        { GPOD_FF_ENC_MP3,    GPOD_FF_XCODE_CBR160, "test-ff-mp3-%d-cbr160.mp3" },

        { GPOD_FF_ENC_ALAC,   GPOD_FF_XCODE_MAX,    "test-ff-alac-%d.m4a" },

        { 0, 0, NULL }
    };

    struct gpod_ff_media_info  mi;
    gpod_ff_media_info_init(&mi);
    strcpy(mi.path, path);


    printf("testing xcode with %s\n"
           "  ffmpeg %s:\n"
	   "    libavutil:     %d.%d.%d\n"
	   "    libavcodec:    %d.%d.%d\n"
	   "    libavformat:   %d.%d.%d\n"
	   "    libswresample: %d.%d.%d\n",
	       mi.path,
	       av_version_info(),
	       AV_VERSION_MAJOR(avutil_version()), AV_VERSION_MINOR(avutil_version()), AV_VERSION_MICRO(avutil_version()),
	       AV_VERSION_MAJOR(avcodec_version()), AV_VERSION_MINOR(avcodec_version()), AV_VERSION_MICRO(avcodec_version()),
	       AV_VERSION_MAJOR(avformat_version()), AV_VERSION_MINOR(avformat_version()), AV_VERSION_MICRO(avformat_version()),
	       AV_VERSION_MAJOR(swresample_version()), AV_VERSION_MINOR(swresample_version()), AV_VERSION_MICRO(swresample_version()));

    struct gpod_ff_transcode_ctx  xcode;
    char*  err;

    const unsigned  sample_rates[] = { 44100, 22050, 0 };
    const unsigned*  sample_rate = sample_rates;
    while (*sample_rate)
    {
	const struct Fmt*  p = fmts;
	while (p->name)
	{
	    err = NULL;
	    gpod_ff_transcode_ctx_init(&xcode, p->enc, p->quality, true);
	    xcode.audio_opts.samplerate = *sample_rate;
	    sprintf(xcode.path, p->name, xcode.audio_opts.samplerate);

	    ++p;

	    printf("xcoding %s.. ", xcode.path);
	    fflush(stdout);
	    if (gpod_ff_transcode(&mi, &xcode, &err) < 0) {
		if (err) {
		    fprintf(stderr, "failed xcode '%s' - %s\n", xcode.path, err);
		    free(err);
		}
	    }
	    else {
		char*  hash = NULL;
		int  ret = ff_hash(&hash, xcode.path);
		printf(" audio hash=%s\n", ret < 0 ? "n/a" : hash);
		free(hash);
	    }
	    putchar('\n');
	}
	++sample_rate;
    }


    return 0;
}
