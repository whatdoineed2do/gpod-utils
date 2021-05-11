#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "gpod-ffmpeg.h"
#include "test-ff-wav.h"
#include <libavutil/log.h>


#define TEST_FF_XCODE_WAV_SAMPLE "test-ff-xcode.wav"
int _generate_sample()
{
    int  fd;
    if ( (fd=open(TEST_FF_XCODE_WAV_SAMPLE, O_CREAT | O_TRUNC | O_WRONLY, 0600)) < 0) {
        return -errno;
    }

    if (write(fd, sample_wav, sizeof(sample_wav)) != sizeof(sample_wav)) {
        return 1;
    }
    close(fd);
    return 0;
}


int main(int argc, char* argv[])
{
    if (_generate_sample() != 0) {
        fprintf(stderr, "unabled to generated data sample %s - %s\n", TEST_FF_XCODE_WAV_SAMPLE, strerror(errno));
        return -1;
    }

    if (argc == 2) {
        if      (strcmp(argv[1], "-verbose") == 0)  av_log_set_level(AV_LOG_VERBOSE);
        else if (strcmp(argv[1], "-debug")   == 0)  av_log_set_level(AV_LOG_DEBUG);
        else if (strcmp(argv[1], "-info")    == 0)  av_log_set_level(AV_LOG_INFO);
        else if (strcmp(argv[1], "-quiet")    == 0)  av_log_set_level(AV_LOG_QUIET);
    }
    else {
        gpod_ff_init();
    }


    struct gpod_ff_transcode_ctx  xfrm;

    const struct Fmt {
        enum gpod_ff_enc  enc;
        enum gpod_ff_transcode_quality  quality;
        const char*  name;
    } fmts[] = {
        { GPOD_FF_ENC_FDKAAC, GPOD_FF_XCODE_VBR1,   "test-ff-aac-vbr1.aac" },
        { GPOD_FF_ENC_FDKAAC, GPOD_FF_XCODE_VBR1,   "test-ff-fdkaac-vbr1.m4a" },
        { GPOD_FF_ENC_AAC,    GPOD_FF_XCODE_VBR1,   "test-ff-aac-vbr1.m4a" },
        { GPOD_FF_ENC_FDKAAC, GPOD_FF_XCODE_VBR9,   "test-ff-fdkaac-vbr9.m4a" },
        { GPOD_FF_ENC_AAC,    GPOD_FF_XCODE_VBR9,   "test-ff-aac-vbr9.m4a" },

        { GPOD_FF_ENC_FDKAAC, GPOD_FF_XCODE_CBR256, "test-ff-fdkaac-cbr256.m4a" },
        { GPOD_FF_ENC_FDKAAC, GPOD_FF_XCODE_CBR160, "test-ff-fdkaac-cbr160.m4a" },
        { GPOD_FF_ENC_AAC,    GPOD_FF_XCODE_CBR256, "test-ff-aac-cbr256.m4a" },
        { GPOD_FF_ENC_AAC,    GPOD_FF_XCODE_CBR160, "test-ff-aac-cbr160.m4a" },

        { GPOD_FF_ENC_MP3,    GPOD_FF_XCODE_VBR1,   "test-ff-mp3-vbr1.mp3" },
        { GPOD_FF_ENC_MP3,    GPOD_FF_XCODE_CBR160, "test-ff-mp3-cbr160.mp3" },

        { GPOD_FF_ENC_ALAC,   GPOD_FF_XCODE_MAX,    "test-ff-alac.m4a" },

        { 0, 0, NULL }
    };

    struct gpod_ff_media_info  mi;
    gpod_ff_media_info_init(&mi);
    strcpy(mi.path, TEST_FF_XCODE_WAV_SAMPLE);

    struct gpod_ff_transcode_ctx  xcode;
    char*  err;

    const struct Fmt*  p = fmts;
    while (p->name)
    {
        err = NULL;
        gpod_ff_transcode_ctx_init(&xcode, p->enc, p->quality);
        strcpy(xcode.path, p->name);
        xcode.audio_opts.samplerate = 44100;

        ++p;

        printf("xcoding %s.. ", xcode.path);
        fflush(stdout);
        if (gpod_ff_transcode(&mi, &xcode, &err) < 0) {
            if (err) {
                fprintf(stderr, "failed xcode '%s' - %s\n", xcode.path, err);
                free(err);
            }
        }
        putchar('\n');
    }


    return 0;
}
