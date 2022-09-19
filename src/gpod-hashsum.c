#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <inttypes.h>

#include <gpod-utils.h>
#ifdef HAVE_FFMPEG
#include <gpod-ffmpeg.h>
#endif


int main(int argc, char* argv[])
{
    const char* argv0 = basename(argv[0]);

    if (argc == 1) {
	printf("%s: no files\n", argv0);
	return -1;
    }

#ifdef HAVE_FFMPEG
    gpod_ff_init();
#endif

    struct gpod_hash_digest  res;
    int  ret;
    char*  streamhash = NULL;
    int  arg = 1;
    while (arg < argc)
    {
	const char*  path = argv[arg++];
	memset(&res, 0, sizeof(res));

	ret = gpod_hash_digest_file(&res, path);
#ifdef HAVE_FFMPEG
	gpod_ff_audio_hash(&streamhash, path);
	printf("%-11" PRIu64 "  %s   %-11" PRIu32 " %s %s\n", ret == 0 ? res.hash : 0, ret == 0 ? res.digest : "", streamhash ? gpod_djbhash(streamhash) : 0, streamhash ? streamhash : "", path);
#else
	printf("%-11" PRIu64 "  %s  %s\n", ret == 0 ? res.hash : 0, ret == 0 ? res.digest : "", path);
#endif
	free(streamhash);
    }

    return 0;
}
