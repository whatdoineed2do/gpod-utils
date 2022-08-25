#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <inttypes.h>

#include <gpod-utils.h>


int main(int argc, char* argv[])
{
    const char* argv0 = basename(argv[0]);

    if (argc == 1) {
	printf("%s: no files\n", argv0);
	return -1;
    }

    struct gpod_hash_digest  res;
    int  ret;
    int  arg = 1;
    while (arg < argc)
    {
	const char*  path = argv[arg++];
	memset(&res, 0, sizeof(res));

	ret = gpod_hash_digest_file(&res, path);
	printf("%-11" PRIu64 "  %s  %s\n", ret == 0 ? res.hash : 0, ret == 0 ? res.digest : "", path);
    }

    return 0;
}
