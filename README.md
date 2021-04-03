# `gpod utils`
Command line tools using [libgpod](https://sourceforge.net/p/gtkpod/libgpod/ci/master/tree/) to access iPod data.

Whilst `libgpod` appears to be in sunset mode (last release in 2015) recent 2021 Fedora and Debian distros still provide `gtkpod` in their standard repos.  However there are still many old iPods in the wild with a mini resurrgence of popularity for the iPod 4/5/5.5/Classic units given the relative ease in replacing batteries and swapping out their power hungry harddisks for larger capacity SD cards.

## `gpod-ls`
Simple utility that parses an iPod db and generates a `json` output of the internal playlists (main playlist `iPod`) as well as the user generated playlists - the main playlist will list most of the available track information and the other playlists will contain less verbose data.

Optionally an `SQLite3` db can be generated for easier investigation.

This utility can work on a mounted iPod or directly pointing the `iTunesDB` file - the following works on an old iPod Video 5G.
```
$ gpod-ls /run/media/ray/IPOD
{
  "ipod_data" {
    "playlists": {
      "items": [
        {
          "name": "iPod",
          "type": "master",
          "count": 1720,
          "smartpl": false,
          "timestamp": 1568200601,
          "tracks": [
            {
              "id": 52,
              "ipod_path": "/iPod_Control/Music/F43/SGWQ.mp3",
              "title": "foo bar",
              "artist": "Foo&Bar",
              "album": null,
              "genre": "Pop",
              "filetype": "MPEG audio file",
              "composer": "Unknown",
              "grouping": null,
              "albumartist": null,
              "sort_artist": null,
              "sort_title": null,
              "sort_album": null,
              "sort_albumartist": null,
              "sort_composer": null,
              "size": 3246551,
              "tracklen": 202840,
              "cd_nr": 0,
              "cds": 0,
              "track_nr": 0,
              "tracks": 0,
              "bitrate": 128,
              "samplerate": 44100,
              "year": 2017,
              "time_added": 1616007149,
              "time_modified": 1616872918,
              "time_played": 1616698504,
              "rating": 20,
              "playcount": 3,
              "playcount2": 0,
              "recent_playcount": 0
            },
            ...
          ]
        },
        {
          "name": "Genius",
          "type": "playlist",
          "count": 0,
          "smartpl": false,
          "timestamp": 1616771596,
          "tracks": []
        },
        {
          "name": "Podcasts",
          "type": "podcasts",
          "count": 0,
          "smartpl": false,
          "timestamp": 1617250954,
          "tracks": []
        },
        {
          "name": "[Orphaned]",
          "type": "playlist",
          "count": 0,
          "smartpl": false,
          "timestamp": 1617251149,
          "tracks": []
        }
      ],
      "count": 4,
    },
    ...
  }
}
```
Directly on the db file and generating a standalone db
```
$ gpod-ls /run/media/ray/IPOD/iPod_Control/iTunes/iTunesDB /tmp/ipod.sqlite3
```
The `json` output is not pretty printed but rather you can use other tools, such as [`jq`](https://stedolan.github.io/jq/) to perform simple queries or to use the generated DB file.

Whilst both `gtkpod` and `Rhythmbox` provide good graphical interfaces for adding/removing music, they are less useful for data mining.  Of particular use is identifying potentially duplicate tracks.  Note the `duplicates` object - this contains 3 further objects, `high`, `med`, `low` which in turn contains a list of potentially duplicate tracks.  The difference between these objects is the manner in which they determine _matches_ - using basic filesize track length and then increasing to equivalence in some metadata fields.  Note that it is highly recommended that you examine/listen to the underlying tracks detailed before purging.
```
{
  "ipod_data": {
    "playlists": {
      ...
    }
  },
  "ipod_analysis": {
    "duplicates": [
      {
        "match": "high",
        "tracks": []
      },
      {
        "match": "med",
        "tracks": []
      },
      {
        "match": "low",
        "tracks": [
          {
            "size": 8827352,
            "tracklen": 220000,
            "count": 1,
            "items": [
              {
                "id": 1361,
                "ipod_path": "/iPod_Control/Music/F08/NCQQ.mp3",
                "title": "foo",
                "artist": "Singer",
                "album": null,
                "genre": "Pop",
                "date_added": "2021-03-25T18:06:12"
              },
              {
                "id": 1366,
                "ipod_path": "/iPod_Control/Music/F41/ZNUF.mp3",
                "title": "bar",
                "artist": "Singer",
                "album": null,
                "genre": "Pop",
                "date_added": "2021-03-25T18:06:12"
              }
            ]
          }
        ]
      }
    ]
  }
}
```
