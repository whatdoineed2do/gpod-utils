#
# Regular cron jobs for the gpod-utils package.
#
0 4	* * *	root	[ -x /usr/bin/gpod-utils_maintenance ] && /usr/bin/gpod-utils_maintenance
