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
	        printf("usage: [OPTIONS] [file to xcode]\n"
		       "\n"
		       "    tests the transcode functionality to various formats and sample rates for given input file\n"
		       "    if no data file is provided, use internal dataset\n"
		       "\n"
		       "  logging:\n"
		       "    -v  --verbose\n"
		       "    -d  --debug\n"
		       "    -i  --info\n"
		       "    -q  --quiet\n"
		       "\n");
		return 1;
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


    const unsigned  sample_rates[] = { 44100, 22050, 0 };
    struct sample_hash_pair {
	int sample;
	char* hash;
    };
    const struct Fmt {
        enum gpod_ff_enc  enc;
        enum gpod_ff_transcode_quality  quality;
        const char*  name;
	struct sample_hash_pair  hashes[sizeof(sample_rates)/sizeof(unsigned)];
    } fmts[] = {
        {
	    .enc = GPOD_FF_ENC_FDKAAC,
	    .quality = GPOD_FF_XCODE_VBR1,
	    .name = "test-ff-aac-%d-vbr1.aac",
	    .hashes = {
		{ 44100, "eeb109d3381c0625609f84965610e8f6a1b6d8c337fd3d7a934f804c8a4b8b8f" },
		{ 22050, "941216461eecfd12eff13f5a8be83618f6b56bbbbb425a81fbf7dd02450bdec7" },
		{ 0, NULL }
	    }
	},
        {
	    .enc = GPOD_FF_ENC_FDKAAC,
	    .quality = GPOD_FF_XCODE_VBR1,
	    .name = "test-ff-fdkaac-%d-vbr1.m4a",
	    .hashes = {
	        { 44100, "db64d007d80f834e4f323926153ff9e02d2ecb8e863d1345a04af946b5bd67b9" },
		{ 22050, "a748ccef0e0bbab2f056ef5b06156e603268d1bfa14ed49be4d00d9c4965c981" },
		{ 0, NULL }
	    },
	},
        {
	    .enc = GPOD_FF_ENC_AAC,
	    .quality = GPOD_FF_XCODE_VBR1,
	    .name = "test-ff-aac-%d-vbr1.m4a",
	    .hashes = {
	        { 44100, "f8bede78673ae3b0d22ce66917204700ba015c8f703bdd562c5da26d49df290a" },
		{ 22050, "8c2c29a2831775f5d69fde4fdccb3e500ee354217fde866146ca553863d84d0f" },
		{ 0, NULL }
	    },
	},
	{
	    .enc = GPOD_FF_ENC_FDKAAC,
	    .quality = GPOD_FF_XCODE_VBR9,
	    .name = "test-ff-fdkaac-%d-vbr9.m4a" ,
	    .hashes = {
	        { 44100, "745d5e9537956f9f8bb757b677abd676f4c3d4daf20dd5381ccb2f2e668bedff" },
		{ 22050, "9ba5c7ee60da588d55abdbc30f3da1651c04fda0c1fdc36084dbe8ea33498546" },
		{ 0, NULL }
	    },
	},
	{
	    .enc = GPOD_FF_ENC_AAC,
	    .quality = GPOD_FF_XCODE_VBR9,
	    .name = "test-ff-aac-%d-vbr9.m4a",
	    .hashes = {
	        { 44100, "de4945dacdefe064f03630c9214f044c4f6a5d9c4f347df106ff8f8a0e92920b" },
		{ 22050, "5291ff893c8fba4c07db71edddc21aacfbe7be4486c3e766abbd0233244454d6" },
		{ 0, NULL }
	    },

	},
	{
	    .enc = GPOD_FF_ENC_FDKAAC,
	    .quality = GPOD_FF_XCODE_CBR256,
	    .name = "test-ff-fdkaac-%d-cbr256.m4a",
	    .hashes = {
	        { 44100, "157f7347e4b567a2cd45179c0bbbcbf8bf60e14e443c7b7c3e5ddf3049b8ac6f" },
		{ 22050, "65cdb41db70e1c00d579cc15c74bc2ee459f6b80fc42033efb5b404f1d917e91" },
		{ 0, NULL }
	    },
	},
	{
	    .enc = GPOD_FF_ENC_FDKAAC,
	    .quality = GPOD_FF_XCODE_CBR160,
	    .name = "test-ff-fdkaac-%d-cbr160.m4a",
	    .hashes = {
	        { 44100, "372faf52543a676347d7c7310164be47af796b365106707911e20bb6c592d8f3" },
		{ 22050, "43594d5a0486a22cc19dff11cc0d5c21b445d452f67f671e5bcb0b24e31527b1" },
		{ 0, NULL }
	    },
	},
	{
	    .enc = GPOD_FF_ENC_AAC,
	    .quality = GPOD_FF_XCODE_CBR256,
	    .name = "test-ff-aac-%d-cbr256.m4a",
	    .hashes = {
	        { 44100, "1340fb5970dbdeae3b8ab5e1207d075327005b8d06f982b94769058f62bc8939" },
		{ 22050, "ec5c9d2d45ec7b6aec7da52b3c03f08c8e452a104193e9eff587c1e8bc265ce5" },
		{ 0, NULL }
	    },
	},
	{
	    .enc = GPOD_FF_ENC_AAC,
	    .quality = GPOD_FF_XCODE_CBR160,
	    .name = "test-ff-aac-%d-cbr160.m4a",
	    .hashes = {
	        { 44100, "48106240707d0fb6200616277328cd03c267574fa15714b6dcd8e1f9325c25e8" },
		{ 22050, "3e49dedb4349927af49bfc745c40bd9cba1792b50d5ec0530d15e2a143190510" },
		{ 0, NULL }
	    },
	},
	{
	    .enc = GPOD_FF_ENC_MP3,
	    .quality = GPOD_FF_XCODE_VBR1,
	    .name = "test-ff-mp3-%d-vbr1.mp3",
	    .hashes = {
	        { 44100, "dc3036f0c207f80c90db4d3b5f26509104da46b86b47308761d5501ffbba7007" },
		{ 22050, "9fa889bc0fa6a16140ae04d28ab2342a2875c98976165fd59740a3355c65f9b2" },
		{ 0, NULL }
	    },
	},
	{
	    .enc = GPOD_FF_ENC_MP3,
	    .quality = GPOD_FF_XCODE_CBR160,
	    .name = "test-ff-mp3-%d-cbr160.mp3",
	    .hashes = {
	        { 44100, "c743ed3992512c4d3a4bbaf75fb5a2b709053f22742b0a7c06e32f76dc2253af" },
		{ 22050, "7c1fba46071b81d50c8e85ff7d868c8c8c0a523d2c5b60676e09c1418bf983ed" },
		{ 0, NULL }
	    },
	},
	{
	    .enc = GPOD_FF_ENC_ALAC,
	    .quality = GPOD_FF_XCODE_MAX,
	    .name = "test-ff-alac-%d.m4a",
	    .hashes = {
	        { 44100, "b6a34dff1a12b3dd16179d486757655a691b2488e630a33d2749e1bf1e52bdf3" },
		{ 22050, "5ab134097408762316f9219341b59a8dc5c379d795ab94def72efcd1f557c98c" },
		{ 0, NULL }
	    },
	},

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

    char*  errb = NULL;
    char*  hash = NULL;
    int  ret = gpod_ff_audio_hash(&hash, path, &errb);
    printf("original %' 33s..  audio hash %s\n", path, ret < 0 ? "n/a" : hash);
    free(hash);
    free(errb);

    struct gpod_ff_transcode_ctx  xcode;
    char*  err;

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

	    printf("xcoding  %' 33s.. ", xcode.path);
	    fflush(stdout);
	    if (gpod_ff_transcode(&mi, &xcode, &err) < 0) {
		if (err) {
		    fprintf(stderr, "failed xcode '%s' - %s\n", xcode.path, err);
		    free(err);
		}
	    }
	    else {
		errb = NULL;
		ret = gpod_ff_audio_hash(&hash, xcode.path, &errb);
		const char*  hash_result = "n/a";
		if (ret == 0 && hash) {
		    const struct sample_hash_pair*  shp = p->hashes;
		    while (shp->hash) {
			 if (shp->sample == *sample_rate) {
			     hash_result = strcmp(hash, shp->hash) == 0 ? "ok" : shp->hash;
			     break;
			 }
		         ++shp;
		    }
		}
		printf(" audio hash %s  [%s]", ret < 0 ? "n/a" : hash, hash_result);
		free(hash);
		free(errb);
	    }
	    putchar('\n');

	    ++p;
	}
	++sample_rate;
    }


    return 0;
}
