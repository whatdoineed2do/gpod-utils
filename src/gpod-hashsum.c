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

    if (argc == 1 || argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
	printf("usage:  %s: <files>\n"
	       "\n"
	       "   reports for each input:\n"
	       "       file DBJ hash file sha1 sum | audio hash | audio DJB hash\n"
	       "\n"
	       "   Idneitfiers used by gpod utils to identify duplicate files.\n"
	       "\n"
	       , argv0);
	return -1;
    }

    gpod_ff_init();

    int  ret;
    char* err = NULL;
    char*  streamhash = NULL;
    int  arg = 1;
    while (arg < argc)
    {
	const char*  path = argv[arg++];
	struct gpod_hash_digest  res = { 0 };

	err = NULL;
	streamhash = NULL;

	ret = gpod_hash_digest_file(&res, path);
	gpod_ff_audio_hash(&streamhash, path, &err);

	if (err) {
	    printf("%s - %s\n", err, path);
	}
	else {
	    printf("%-11" PRIu64 "  %s   %-11" PRIu32 " %s  %s\n",
		   ret == 0 ? res.hash : 0, ret == 0 ? res.digest : "",
		   streamhash ? gpod_djbhash(streamhash) : 0, streamhash ? streamhash : err, path);
	}
	free(streamhash);
	free(err);
    }

    return 0;
}
