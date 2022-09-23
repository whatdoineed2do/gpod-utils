#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <inttypes.h>

#include <gpod-utils.h>
#include <gpod-ffmpeg.h>


int main(int argc, char* argv[])
{
    const char* argv0 = basename(argv[0]);

    if (argc == 1) {
	printf("%s: no files\n", argv0);
	return -1;
    }

    gpod_ff_init();

    struct gpod_hash_digest  res;
    int  ret;
    char* err = NULL;
    char*  streamhash = NULL;
    int  arg = 1;
    while (arg < argc)
    {
	const char*  path = argv[arg++];
	memset(&res, 0, sizeof(res));

	err = NULL;
	streamhash = NULL;

	ret = gpod_hash_digest_file(&res, path);
	gpod_ff_audio_hash(&streamhash, path, &err);
	printf("%-11" PRIu64 "  %s   %-11" PRIu32 " %s %s\n", ret == 0 ? res.hash : 0, ret == 0 ? res.digest : "", streamhash ? gpod_djbhash(streamhash) : 0, streamhash ? streamhash : err, path);
	free(streamhash);
	free(err);
    }

    return 0;
}
