# `gpod utils`
Command line tools using [libgpod](https://sourceforge.net/p/gtkpod/libgpod/ci/master/tree/) to access iPod data.

Whilst `libgpod` appears to be in sunset mode (last release in 2015) recent 2021 Fedora and Debian distros still provide `gtkpod` in their standard repos.  However there are still many old iPods in the wild with a mini resurrgence of popularity for the iPod 4/5/5.5/Classic units given the relative ease in replacing batteries and swapping out their power hungry harddisks for larger capacity SD cards.

## `gpod-ls`
Simple utility that parses an iPod db and generates a `json` output of the internal playlists (main playlist `iPod`) as well as the user generated playlists - the main playlist will list most of the available track information and the other playlists will contain less verbose data.

Optionally an `SQLite3` db can be generated for easier investigation.

This utility can work on a mounted iPod or directly pointing the `iTunesDB` file - the following works on an old iPod Video 5G.
```
$ gpod-ls /run/media/ray/IPOD | tee ipod.json
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
To examine the main iPod playlist where all tracks are stored:
```
$ cat ipod.json | jq '.ipod_data.playlists.items[] | select(.type == "master")'
```
Find all tracks for artist _Foo_ but only get filename, title and id
```
$ cat ipod.json | jq '.ipod_data.playlists.items[] | select(.type == "master") | .tracks[] | select(.artist=="Foo") | {id, ipod_path, title, album}'
```

## `gpod-rm`
Removes track from iPod.  Requires the filename as known in the `iTunesDB` - see the output from `gpod-ls`.
```
$ gpod-rm /run/media/ray/IPOD   /iPod_Control/Music/F41/ZNUF.mp3
/iPod_Control/Music/F41/ZNUF.mp3 -> { id=1366 title='foo' artist='Foo&Bar' album='9492' time_added=161672437 }
sync'ing iPod ... removing 1/1
```

## `gpod-cp`
Copies track(s) to iPod, accepting `mp3`, `m4a/aac` and `h264` videos..  For audio files not supported by `iPod` and automatic conversions to mp3 is made.
```
$ gpod-cp /run/media/ray/IPOD   nothere.mp3 foo.flac foo.mp3 
copying 3 tracks to iPod 9725 Shuffle (1st Gen.), currently 27 tracks
[  1/3]  nothere.mp3 -> { } No such file or directory
[  2/3]  foo.flac -> { title='Flac file' artist='Foo' album='Test tracks' ipod_path='/iPod_Control/Music/F00/libgpod325022.mp3' }
[  3/3]  foo.mp3 -> { title='mp3 file' artist='Foo' album='Test tracks' ipod_path='/iPod_Control/Music/F01/libgpod211429.mp3' }
sync'ing iPod ... adding 2/3
updated iPod, new total tracks=29 (originally=27)
```
Note that the classic `iPods` (5th-7th generation) can only accept video files conforming to `h264 baseline`, 30fps, bitrate up to 2.5Mbbps and `aac` stereo audio up to 160kbps.  Furthermore, iTunes will not accept video files that have not had a special `uuid` atom encoded into the video file - however this does NOT prevent such files from being copied and played onto the iPod.

To test this, you can generate your own `h264` files using `ffmpeg -f rawvideo -video_size 640x320 -pixel_format yuv420p -framerate 23.976 -i /dev/random -f lavfi -i 'anoisesrc=color=brown' -c:a aac -b:a 96k -ar 44100 -t 10  -c:v libx264 -profile baseline -b:v 1.8M foo.mp4`

## `gpod-tag`
Simple metadata tool to modify the `iTunesDB`.  The underlying media files on the device are NOT updated.  The internal `id` or `ipod_path` of the files are required and can be determined from `gpod-ls`
```
$ gpod-tag -M /run/media/ray/IPOD -A "new album name" -y 2021  \
    9999 521 /iPod_Control/Music/F01/libgpod211429.mp3
updating iPod track meta { title='<nul>' artist='<nul>' album='new album name' genre='<nul>' track=-1 year=2021 } ...
[  1/3]  9999 { } - No such track
[  2/3]  521 { id=521 ipod_path='/iPod_Control/Music/F02/libgpod886634.m4a' { title='foo bar sings' artist='Foo&Bar' album='' genre='' track=2 year=0 time_modified=2021-04-01T11:43:29 } }
[  3/3]  /iPod_Control/Music/F01/libgpod211429.mp3 { id=534 ipod_path='/iPod_Control/Music/F01/libgpod211429.mp3' { title='A Song' artist='Foo&Bar' album='' genre='' track=2 year=0 time_modified=2021-04-01T13:03:09 } }
sync'ing iPod ... updated 2/3
updated iPod, total tracks=29
```
The metadata shown for each tracks is the *existing* data - the new metadata is show at the start of processing.
