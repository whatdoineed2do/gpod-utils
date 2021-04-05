#ifndef GPOD_DB_H
#define GPOD_DB_H

#define QUERY_DROP \
  "DROP TABLE IF EXISTS tracks;"

#define QUERY_TBL \
  "CREATE TABLE IF NOT EXISTS tracks (" \
    "  id         INTEGER PRIMARY KEY NOT NULL," \
    "  ipod_path  VARCHAR(4096) NOT NULL,"       \
    "  mediatype  INTEGER NOT NULL,"       \
    "  title      VARCHAR(2048)," \
    "  album      VARCHAR(2048)," \
    "  artist     VARCHAR(2048)," \
    "  genre      VARCHAR(2048)," \
    "  filetype   VARCHAR(2048)," \
    "  comment  VARCHAR(2048)," \
    "  category  VARCHAR(2048)," \
    "  composer  VARCHAR(2048)," \
    "  grouping  VARCHAR(2048)," \
    "  description  VARCHAR(2048)," \
    "  podcasturl  VARCHAR(2048)," \
    "  podcastrss  VARCHAR(2048)," \
    "  subtitle  VARCHAR(2048)," \
    "  tvshow  VARCHAR(2048)," \
    "  tvepisode  VARCHAR(2048)," \
    "  tvnetwork  VARCHAR(2048)," \
    "  albumartist  VARCHAR(2048)," \
    "  keywords  VARCHAR(2048)," \
    "  sort_artist  VARCHAR(2048)," \
    "  sort_title  VARCHAR(2048)," \
    "  sort_album  VARCHAR(2048)," \
    "  sort_albumartist  VARCHAR(2048)," \
    "  sort_composer  VARCHAR(2048)," \
    "  sort_tvshow  VARCHAR(2048)," \
    "  size  INTEGER DEFAULT -1," \
    "  tracklen  INTEGER DEFAULT -1," \
    "  cd_nr  INTEGER DEFAULT -1," \
    "  cds  INTEGER DEFAULT -1," \
    "  track_nr  INTEGER DEFAULT -1," \
    "  tracks  INTEGER DEFAULT -1," \
    "  bitrate  INTEGER DEFAULT -1," \
    "  samplerate  INTEGER DEFAULT -1," \
    "  samplerate_low  INTEGER DEFAULT -1," \
    "  year  INTEGER DEFAULT -1," \
    "  volume  INTEGER DEFAULT -1," \
    "  soundcheck  INTEGER DEFAULT -1," \
    "  time_added  INTEGER DEFAULT 0," \
    "  time_modified  INTEGER DEFAULT 0," \
    "  time_played  INTEGER DEFAULT 0," \
    "  rating  INTEGER DEFAULT -1," \
    "  playcount  INTEGER DEFAULT -1," \
    "  playcount2  INTEGER DEFAULT -1," \
    "  recent_playcount  INTEGER DEFAULT -1," \
    "  BPM  INTEGER DEFAULT -1," \
    "  app_rating  INTEGER DEFAULT -1," \
    "  compilation  INTEGER DEFAULT -1," \
    "  starttime  INTEGER DEFAULT -1," \
    "  stoptime  INTEGER DEFAULT -1," \
    "  checked  INTEGER DEFAULT -1," \
    "  artwork_count  INTEGER DEFAULT -1," \
    "  artwork_size  INTEGER DEFAULT -1," \
    "  time_released  INTEGER DEFAULT 0," \
    "  explicit_flag  INTEGER DEFAULT -1," \
    "  skipcount  INTEGER DEFAULT -1," \
    "  recent_skipcount  INTEGER DEFAULT -1," \
    "  last_skipped  INTEGER DEFAULT -1," \
    "  has_artwork  INTEGER DEFAULT -1," \
    "  samplecount  INTEGER DEFAULT -1," \
    "  season_nr  INTEGER DEFAULT -1," \
    "  episode_nr  INTEGER DEFAULT -1" \
    ");"

#define QUERY_IDX \
  "CREATE INDEX IF NOT EXISTS idx_key_path ON tracks(ipod_path);"
#define QUERY_IDX1 \
  "CREATE INDEX IF NOT EXISTS idx_key_flds ON tracks(title, album, artist, genre);"
 
const char*  db_init_queries[] = {
  QUERY_DROP,
  QUERY_TBL,
  QUERY_IDX,
  QUERY_IDX1,
  NULL
};

#undef QUERY_TBL
#undef QUERY_IDX

#endif
