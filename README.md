# `gpod utils`
Command line tools using [`libgpod`](https://sourceforge.net/p/gtkpod/libgpod/ci/master/tree/) to access `iPod` data.

Whilst `libgpod` appears to be in sunset mode (last release in 2015), recent 2021 Fedora and Debian distros still provide `gtkpod` in their standard repos.  However there are still many old iPods in the wild with a mini resurrgence of popularity for the `iPod` 4/5/5.5/Classic units given the relative ease in replacing batteries and swapping out their power hungry harddisks for larger capacity SD cards.

As of 2021, the last `libgpod` release is 0.8.3 - their docs suggests the library supports all classic `iPods`, `iPod Touches` and early `iPhones`.  Whilst testing this codebase, only `iPods` were supported with `iPod Touch 1G` and onwards not compatible due to the singing requirement of the `iTunesDB` file on these devices.

### Supported / Tested
|Model|OS|Supported|Comments
---|:---:|---:|---
`iPod 5G` MA002LL|1.3|Yes|
`iPod 5.5G` MA446FB|1.3|Yes|
`iPod Touch 1G`|5.1.1|No|tools appear to be success and updates the `iTunesDB`.  Data not reflected one rescan/app.  Remove appears success but next scan file exists, underlying file removed but listing on app exists.  Other tracks continue to be playable
`iPhone 1` MB213B|3.1.3|No|tools appear to be success and updates the `iTunesDB`.  However once a sync has been complete (cp/rm) none of the audio files are playable on the `iPod` app

The underlying support is provided by `libgpod`.

### mount points
Most modern Linux distros and window managers will try to automount old `iPod`'s filesystem to a location such as `/run/media/${USER}/<name of iPod>/`.  However this is not a given and I've seen this fail for `iPhones` and `iPod touch` even though the distros mount items through `gvfs`.  If your `iPod` is not automounted, try the following to mount `mkdir -p /tmp/ipod && ifuse /tmp/ipod` and this to unmount `fusermoumt -u /tmp/ipod` when done.

## `gpod-ls`
Simple utility that parses an `iPod` db and generates a `json` output of the internal playlists (main playlist `iPod`) as well as the user generated playlists - the main playlist will list most of the available track information and the other playlists will contain less verbose data.

Optionally an `SQLite3` db can be generated for easier investigation.

This utility can work on a mounted `iPod` or directly pointing the `iTunesDB` file - the following works on an old `iPod Video 5G`.
```
$ gpod-ls -M /run/media/ray/IPOD | tee ipod.json | jq '.'
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
$ gpod-ls \
    -M /run/media/ray/IPOD/iPod_Control/iTunes/iTunesDB \
    -Q /tmp/ipod.sqlite3
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
To examine the main `iPod` playlist where all tracks are stored:
```
$ jq '.ipod_data.playlists.items[] | select(.type == "master")' ipod.json
```
Find all tracks for artist _Foo_ but only get filename, title and id
```
$ jq '.ipod_data.playlists.items[] | select(.type == "master") | .tracks[] | select(.artist=="Foo") | {id, ipod_path, title, album}' ipod.json
```

## `gpod-rm`
Removes track(s) from `iPod`.  Requires the filename as known in the `iTunesDB` - see the output from `gpod-ls`.
```
$ gpod-rm -M /run/media/ray/IPOD \
    /iPod_Control/Music/F41/ZNUF.mp3
removing tracks from iPod Video (1st Gen.) A002, currently 88 tracks
/iPod_Control/Music/F41/ZNUF.mp3 -> { id=1366 title='foo' artist='Foo&Bar' album='9492' time_added=161672437 }
sync'ing iPod ... removing 1/1
iPod total tracks=87 (originally=88)
```
The `-a` flag can be specified before any other files to force removal of duplicates files based on `iPod` filesystem checksums, leaving the earliest added instance of the track.

## `gpod-cp`
Copies track(s) to `iPod`, accepting `mp3`, `m4a/aac` and `h264` videos..  For audio files not supported by `iPod` an automatic conversion is performed.  Using the `-c` switch will perform checksum generation/analysis of files on `iPod` to prevent duplicates being copied.
```
$ gpod-cp -M /run/media/ray/IPOD -c \
    nothere.mp3 foo.flac foo.mp3 
copying 3 tracks to iPod 9725 Shuffle (1st Gen.), currently 27 tracks
[  1/3]  nothere.mp3 -> { } No such file or directory
[  2/3]  foo.flac -> { title='Flac file' artist='Foo' album='Test tracks' ipod_path='/iPod_Control/Music/F00/libgpod325022.mp3' }
[  3/3]  foo.mp3 -> { title='mp3 file' artist='Foo' album='Test tracks' ipod_path='/iPod_Control/Music/F01/libgpod211429.mp3' }
sync'ing iPod ... 
iPod total tracks=29  2/3 items (3.44M)  music=2 video=0 other=0  in 0.572 secs
```
The quality of automatic audio conversions can be controlled by `-q` with values 0 (best) ..9 for VBR and 96,128,.320 for CBR.  The default conversion is to high quality vbr AAC (equivalent to `ffmpeg -c:a libfdk_aac -vbr 5`) but conversions to MP3 and ALAC is also available via `-e` flag.  Note that the AAC conversion is dependant on `ffmpeg` supporting `libfdk_aac` (auto fallback conversion to MP3 if FDK not available) - we avoid conversion using `ffmpeg`'s internal `aac` encoder as it appears older `iPod`'s can't play the files without glitches/artifacts.

Note that the classic `iPods` (5th-7th generation) can only accept video files conforming to a `h264 baseline` in a `m4v` or `mp4` container, up to 30fps, bitrate up to 2.5Mbbps and `aac` stereo audio up to 160kbps.  Furthermore, iTunes will not copy video files to the `iPod 5/5.5G` that do not contain a special `uuid` atom encoded into the video file - however this does NOT prevent such files from being copied using `gpod-cp` and played on the `iPod`.

To test this, you can generate your own `h264` files using `ffmpeg -f rawvideo -video_size 640x320 -pixel_format yuv420p -framerate 23.976 -i /dev/random -f lavfi -i 'anoisesrc=color=brown' -c:a aac -b:a 96k -ar 44100 -t 10  -c:v libx264 -profile baseline -b:v 1.8M foo.mp4`.  This video will not contain the `uuid` atom.

To convert an existing video file for the `iPod` classics, you can use `handbrake` or `ffmpeg` directly:
```
# example transcode using a cuda/nvidia enabled ffmpeg
ffmpeg -hwaccel cuda  -hwaccel_output_format cuda  \
  -i foo.mp4 \
    -c:a aac -b:a 128k -ar 44100  \
    -f ipod \
    -c:v h264_nvenc -rc vbr_hq -minrate 1M -maxrate 2.5M \
    -profile:v baseline  \
    -vf scale_npp=640:-1 \
  bar.mp4
```
The `-f ipod` flag will add the `uuid` attom.  If the video file will be sync'd to your `iPod 5G` using `gpod-cp` then this flag is not necessary but *is* required if you with to use iTunes to perform the copy to the device.

## `gpod-tag`
Simple metadata tool to modify the `iTunesDB`.  The underlying media files on the device are NOT updated.  The internal `id` or `ipod_path` of the files are required and can be determined from `gpod-ls`.  Use empty string (`""`) or `-1` to unset the string and int tags respectively
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

## `gpod-extract`
Extracts all or select files from `iPod` and optionally sync'ing metadata (with `-s` flag) on the copied files to the `iTunesDB` values.  No transcoding will be performed on the files, only generic metadata updates (as limited by `ffmpeg`).
```
$ gpod-extract -M /run/media/ray/IPOD -o /export/public/music/ -s /iPod_Control/Music/F02/libgpod886634.m4a ...
extracting 3 tracks from iPod Video (1st Gen.), currently 27 tracks
[  1/3]  id=521 /iPod_Control/Music/F02/libgpod886634.m4a -> '/export/pubic/music/Foo&Bar - foobar sings.m4a'
iPod total tracks=27  3/3 items (990.68K) in 0.107 secs
```
### `exiftool`
`exiftool` can be further used to automatically organise files into directory structures if required.
```
# rename based on artist
$ exiftool '-filename<$Artist - $Title.%le' -r -ext mp3 -ext m4a .

# rename based on albums, creating the directory structure as necessary
$ exiftool '-filename<$Album/$Artist - $Title.%le' -r -ext mp3 -ext m4a .
```
## `gpod-verify`
Verifies the `iPod` db aginst the files on the device.  Three areas:
|mode|DB|filesystem|Comments
---|---|---|---
_clean_|x||not recoverable, delete from db
_add_||x|(`-a`) sync with filesystem, add to db
_remove_||x|(`-d`) sync with db, remove from filesystem
```
$ gpod-verify-M /run/media/ray/IPOD -a
validating tracks from iPod Video (2nd Gen.) A446, currently 4/4 db/filesystem tracks
CLEAN [  1]  /iPod_Control/Music/F13/libgpod031826.mp3 -> { id=52 title='some title' artist='foo' album='' time_added=1619260556 }
ADD   [  1]  /iPod_Control/Music/F00/foo.mp3 -> { title='Sine' artist='ffmpeg' album='' }
sync'ing iPod ...
iPod total tracks=4  orphaned 0 removed 1 added 1 items
```
#
