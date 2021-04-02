# `gpod utils`
Command line tools using [libgpod](https://sourceforge.net/p/gtkpod/libgpod/ci/master/tree/) to access iPod data.

Whilst `libgpod` appears to be in sunset mode (last release in 2015) recent 2021 Fedora and Debian distros still provide `gtkpod` in their standard repos.  However there are still many old iPods in the wild with a mini resurrgence of popularity for the iPod 4/5/5.5/Classic units given the relative ease in replacing batteries and swapping out their power hungry harddisks for larger capacity SD cards.

## `gpod-ls`
Simple utility that parses an iPod db and generates a `json` output of the internal playlists (main playlist `iPod`) as well as the user generated playlists - the main playlist will list most of the available track information and the other playlists will contain less verbose data.

Optionally an `SQLite3` db can be generated for easier investigation.

This utility can work on a mounted iPod or directly pointing the `iTunesDB` file - the following works on an old iPod Video 5G.
```
$ gpod-ls /run/media/ray/IPOD
```
Directly on the db file and generating a standalone db
```
$ gpod-ls /run/media/ray/IPOD/iPod_Control/iTunes/iTunesDB /tmp/ipod.sqlite3
```
The `json` output is not pretty printed but rather you can use other tools, such as [`jq`](https://stedolan.github.io/jq/) to perform simple queries or to use the generated DB file.

Whilst both `gtkpod` and `Rhythmbox` provide good graphical interfaces for adding/removing music, they are less useful for data mining.
