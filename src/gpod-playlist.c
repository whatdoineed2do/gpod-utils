/*
 *  Copyright (C) 2021-2026 Ray <whatdoineed2do @ gmail com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * iTunes and iPod are trademarks of Apple
 *
 */

#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>

#include <glib.h>
#include <gpod/itdb.h>

#include "lib/gpod-utils.h"


void  _usage(char* argv0_)
{
    char *basename = g_path_get_basename (argv0_);
    g_print ("%s: %s-%s\n", basename, GIT_TAG, GIT_COMMIT);

    GSList*  supported = gpod_supported();
    g_print("  supported:\n");
    for (GSList* s=supported; s; s=s->next) {
        g_print("    %s\n", (const char*)s->data);
    }
    g_slist_free(supported);

    g_print ("usage: %s -M <dir ipod mount | file iTunesDB>  <operation>\n"
             "\n"
             "    playlist CRUD on iPod/iTunesDB\n"
             "\n"
             "    -l              list playlists; with -p <name>, list that playlist's tracks/rules\n"
             "    -c <name>       create new playlist\n"
             "    -c <name> -S    create smart playlist:\n"
             "       -e '<field> <op> <value>'  rule (repeatable); default match ALL rules\n"
             "       -A                         match ANY rule instead of ALL\n"
             "       -L <n>:<type>[:<sort>]     limit, eg 25:songs:recent\n"
             "    -U              refresh smart playlists' member lists against library\n"
             "    -d <name>       delete playlist\n"
             "    -p <name> -R <newname>       rename playlist (renaming master playlist renames iPod)\n"
             "    -p <name> -C                 clear playlist (remove all tracks)\n"
             "    -p <name> -a <path|id> ...   add tracks to playlist\n"
             "    -p <name> -r <path|id> ...   remove tracks from playlist\n"
             "    -u [-n limit] [-3]           update 'recently added' playlists\n"
             "                                 (0wk/1wk/1month/3months/6months/12months)\n"
             "                                 -n limits albums (default 50), -3 also writes m3u\n"
             "\n"
             "    smart playlist rules '<field> <op> <value>':\n"
             "      string:  title album artist albumartist genre composer comment grouping\n"
             "               ops:  = != ~ (contains) !~ ^ (starts with) $ (ends with)\n"
             "      int:     rating (stars) playcount skipcount year track disc bitrate\n"
             "               samplerate bpm size time (seconds)\n"
             "               ops:  = != < <= > >=, range as '= a..b'\n"
             "      bool:    compilation purchased; value 'set' or 'unset'\n"
             "      date:    added modified played skipped; '< 4w' in the last 4 weeks,\n"
             "               '> 4w' not in the last; units h/d/w/m/y\n"
             "      limit:   types songs|minutes|hours|mb|gb  sorts random|name|album|\n"
             "               artist|genre|recent|least-recent|most-played|least-played|\n"
             "               recently-played|least-recently-played|highest-rated|lowest-rated\n"
             "    eg: -c 'Top Rock' -S -e 'genre ~ Rock' -e 'rating >= 4' -L 50:songs:recent\n"
             "\n"
             "    Track ids/ipod paths can be determined using gpod-ls\n"
             "\n"
            , basename);
    g_free (basename);
    exit(-1);
}


enum gpod_pl_op {
    OP_NONE = 0,
    OP_LIST,
    OP_CREATE,
    OP_DELETE,
    OP_RENAME,
    OP_CLEAR,
    OP_ADD,
    OP_REMOVE,
    OP_RECENT,
    OP_SPL_REFRESH
};

static const char*  _pl_type(Itdb_Playlist* pl_)
{
    if (itdb_playlist_is_mpl(pl_))       return "master";
    if (itdb_playlist_is_podcasts(pl_))  return "podcasts";
    return "playlist";
}

/* smart playlist vocabulary - shared by the -e/-L parsers and the rule
 * renderer so that listing a smart playlist echoes valid input syntax
 */
struct spl_name_map { const char*  name; guint32  value; };

static const struct spl_name_map  _spl_fields[] = {
    { "title",       ITDB_SPLFIELD_SONG_NAME },
    { "album",       ITDB_SPLFIELD_ALBUM },
    { "artist",      ITDB_SPLFIELD_ARTIST },
    { "albumartist", ITDB_SPLFIELD_ALBUMARTIST },
    { "genre",       ITDB_SPLFIELD_GENRE },
    { "composer",    ITDB_SPLFIELD_COMPOSER },
    { "comment",     ITDB_SPLFIELD_COMMENT },
    { "grouping",    ITDB_SPLFIELD_GROUPING },
    { "rating",      ITDB_SPLFIELD_RATING },
    { "playcount",   ITDB_SPLFIELD_PLAYCOUNT },
    { "skipcount",   ITDB_SPLFIELD_SKIPCOUNT },
    { "year",        ITDB_SPLFIELD_YEAR },
    { "track",       ITDB_SPLFIELD_TRACKNUMBER },
    { "disc",        ITDB_SPLFIELD_DISC_NUMBER },
    { "bitrate",     ITDB_SPLFIELD_BITRATE },
    { "samplerate",  ITDB_SPLFIELD_SAMPLE_RATE },
    { "bpm",         ITDB_SPLFIELD_BPM },
    { "size",        ITDB_SPLFIELD_SIZE },
    { "time",        ITDB_SPLFIELD_TIME },
    { "compilation", ITDB_SPLFIELD_COMPILATION },
    { "purchased",   ITDB_SPLFIELD_PURCHASE },
    { "added",       ITDB_SPLFIELD_DATE_ADDED },
    { "modified",    ITDB_SPLFIELD_DATE_MODIFIED },
    { "played",      ITDB_SPLFIELD_LAST_PLAYED },
    { "skipped",     ITDB_SPLFIELD_LAST_SKIPPED },
    { NULL, 0 }
};

static const struct spl_name_map  _spl_string_ops[] = {
    { "=",  ITDB_SPLACTION_IS_STRING },
    { "!=", ITDB_SPLACTION_IS_NOT },
    { "~",  ITDB_SPLACTION_CONTAINS },
    { "!~", ITDB_SPLACTION_DOES_NOT_CONTAIN },
    { "^",  ITDB_SPLACTION_STARTS_WITH },
    { "$",  ITDB_SPLACTION_ENDS_WITH },
    { NULL, 0 }
};

static const struct spl_name_map  _spl_int_ops[] = {
    { "=",  ITDB_SPLACTION_IS_INT },
    { "!=", ITDB_SPLACTION_IS_NOT_INT },
    { ">",  ITDB_SPLACTION_IS_GREATER_THAN },
    { "<",  ITDB_SPLACTION_IS_LESS_THAN },
    { NULL, 0 }
};

/* newer libgpod headers define these alongside ItdbSPLActionLast */
#ifndef ITDB_SPLACTION_LAST_HOURS_VALUE
#define ITDB_SPLACTION_LAST_HOURS_VALUE  3600
#endif
#ifndef ITDB_SPLACTION_LAST_YEARS_VALUE
#define ITDB_SPLACTION_LAST_YEARS_VALUE  31536000
#endif

/* value is seconds-per-unit as used in Itdb_SPLRule.fromunits */
static const struct spl_name_map  _spl_date_units[] = {
    { "h", ITDB_SPLACTION_LAST_HOURS_VALUE },
    { "d", ITDB_SPLACTION_LAST_DAYS_VALUE },
    { "w", ITDB_SPLACTION_LAST_WEEKS_VALUE },
    { "m", ITDB_SPLACTION_LAST_MONTHS_VALUE },
    { "y", ITDB_SPLACTION_LAST_YEARS_VALUE },
    { NULL, 0 }
};

static const struct spl_name_map  _spl_limit_types[] = {
    { "songs",   ITDB_LIMITTYPE_SONGS },
    { "minutes", ITDB_LIMITTYPE_MINUTES },
    { "hours",   ITDB_LIMITTYPE_HOURS },
    { "mb",      ITDB_LIMITTYPE_MB },
    { "gb",      ITDB_LIMITTYPE_GB },
    { NULL, 0 }
};

static const struct spl_name_map  _spl_limit_sorts[] = {
    { "random",                ITDB_LIMITSORT_RANDOM },
    { "name",                  ITDB_LIMITSORT_SONG_NAME },
    { "album",                 ITDB_LIMITSORT_ALBUM },
    { "artist",                ITDB_LIMITSORT_ARTIST },
    { "genre",                 ITDB_LIMITSORT_GENRE },
    { "recent",                ITDB_LIMITSORT_MOST_RECENTLY_ADDED },
    { "least-recent",          ITDB_LIMITSORT_LEAST_RECENTLY_ADDED },
    { "most-played",           ITDB_LIMITSORT_MOST_OFTEN_PLAYED },
    { "least-played",          ITDB_LIMITSORT_LEAST_OFTEN_PLAYED },
    { "recently-played",       ITDB_LIMITSORT_MOST_RECENTLY_PLAYED },
    { "least-recently-played", ITDB_LIMITSORT_LEAST_RECENTLY_PLAYED },
    { "highest-rated",         ITDB_LIMITSORT_HIGHEST_RATING },
    { "lowest-rated",          ITDB_LIMITSORT_LOWEST_RATING },
    { NULL, 0 }
};

static const struct spl_name_map*  _spl_map_by_name(const struct spl_name_map* map_, const char* name_, size_t len_)
{
    for (const struct spl_name_map* m=map_; m->name; ++m) {
        if (strlen(m->name) == len_ && strncmp(m->name, name_, len_) == 0) {
            return m;
        }
    }
    return NULL;
}

static const struct spl_name_map*  _spl_map_by_value(const struct spl_name_map* map_, guint32 value_)
{
    for (const struct spl_name_map* m=map_; m->name; ++m) {
        if (m->value == value_) {
            return m;
        }
    }
    return NULL;
}

/* user-facing units -> stored units: rating in stars, time in seconds */
static guint64  _spl_value_scale(guint32 field_)
{
    switch (field_) {
        case ITDB_SPLFIELD_RATING:  return ITDB_RATING_STEP;
        case ITDB_SPLFIELD_TIME:    return 1000;
        default:                    return 1;
    }
}

static bool  _spl_parse_uint(const char* s_, const char** end_, guint64* val_)
{
    char*  end;
    *val_ = g_ascii_strtoull(s_, &end, 10);
    if (end == s_) {
        return false;
    }
    *end_ = end;
    return true;
}

/* parse '<field> <op> <value>' into rule_; value may contain spaces */
static bool  _spl_parse_rule(Itdb_SPLRule* rule_, const char* expr_)
{
    const char*  p = expr_;
    while (*p && isspace(*p))  ++p;
    const char*  field = p;
    while (*p && !isspace(*p))  ++p;
    const size_t  fieldlen = p - field;
    while (*p && isspace(*p))  ++p;
    const char*  op = p;
    while (*p && !isspace(*p))  ++p;
    const size_t  oplen = p - op;
    while (*p && isspace(*p))  ++p;
    const char*  value = p;

    if (fieldlen == 0 || oplen == 0 || *value == '\0') {
        g_printerr("bad rule '%s' - expect '<field> <op> <value>'\n", expr_);
        return false;
    }

    const struct spl_name_map*  fm = _spl_map_by_name(_spl_fields, field, fieldlen);
    if (fm == NULL) {
        g_printerr("bad rule '%s' - unknown field '%.*s'\n", expr_, (int)fieldlen, field);
        return false;
    }
    rule_->field = fm->value;

    const struct spl_name_map*  om;
    const guint64  scale = _spl_value_scale(rule_->field);
    guint64  v;
    const char*  end;

    switch (itdb_splr_get_field_type(rule_))
    {
        case ITDB_SPLFT_STRING:
            om = _spl_map_by_name(_spl_string_ops, op, oplen);
            if (om == NULL) {
                g_printerr("bad rule '%s' - invalid string op '%.*s'\n", expr_, (int)oplen, op);
                return false;
            }
            rule_->action = om->value;
            g_free(rule_->string);
            rule_->string = g_strdup(value);
            break;

        case ITDB_SPLFT_INT:
            if (!_spl_parse_uint(value, &end, &v)) {
                g_printerr("bad rule '%s' - invalid numeric value '%s'\n", expr_, value);
                return false;
            }
            v *= scale;

            if (oplen == 1 && *op == '=' && strncmp(end, "..", 2) == 0)
            {
                guint64  to;
                if (!_spl_parse_uint(end+2, &end, &to) || *end != '\0') {
                    g_printerr("bad rule '%s' - invalid range value '%s'\n", expr_, value);
                    return false;
                }
                rule_->action = ITDB_SPLACTION_IS_IN_THE_RANGE;
                rule_->fromvalue = v;
                rule_->tovalue = to * scale;
                break;
            }
            if (*end != '\0') {
                g_printerr("bad rule '%s' - invalid numeric value '%s'\n", expr_, value);
                return false;
            }

            /* libgpod has no >=/<= actions; exact for ints as >v-1 / <v+1 */
            if (oplen == 2 && strncmp(op, ">=", 2) == 0) {
                if (v == 0) {
                    g_printerr("bad rule '%s' - '>= 0' always matches\n", expr_);
                    return false;
                }
                rule_->action = ITDB_SPLACTION_IS_GREATER_THAN;
                rule_->fromvalue = v - 1;
                break;
            }
            if (oplen == 2 && strncmp(op, "<=", 2) == 0) {
                rule_->action = ITDB_SPLACTION_IS_LESS_THAN;
                rule_->fromvalue = v + 1;
                break;
            }

            om = _spl_map_by_name(_spl_int_ops, op, oplen);
            if (om == NULL) {
                g_printerr("bad rule '%s' - invalid numeric op '%.*s'\n", expr_, (int)oplen, op);
                return false;
            }
            rule_->action = om->value;
            rule_->fromvalue = v;
            break;

        case ITDB_SPLFT_BOOLEAN:
            if (oplen != 1 || *op != '=') {
                g_printerr("bad rule '%s' - boolean fields take '= set' or '= unset'\n", expr_);
                return false;
            }
            if (g_ascii_strcasecmp(value, "set") == 0) {
                rule_->action = ITDB_SPLACTION_IS_INT;
            }
            else if (g_ascii_strcasecmp(value, "unset") == 0) {
                rule_->action = ITDB_SPLACTION_IS_NOT_INT;
            }
            else {
                g_printerr("bad rule '%s' - boolean fields take '= set' or '= unset'\n", expr_);
                return false;
            }
            break;

        case ITDB_SPLFT_DATE:
        {
            if (oplen != 1 || (*op != '<' && *op != '>')) {
                g_printerr("bad rule '%s' - date fields take '< <n><unit>' (in the last) or '> <n><unit>' (not in the last)\n", expr_);
                return false;
            }
            if (!_spl_parse_uint(value, &end, &v) || v == 0) {
                g_printerr("bad rule '%s' - invalid date value '%s'\n", expr_, value);
                return false;
            }
            const struct spl_name_map*  um = _spl_map_by_name(_spl_date_units, end, strlen(end));
            if (um == NULL) {
                g_printerr("bad rule '%s' - invalid date unit '%s' (h/d/w/m/y)\n", expr_, end);
                return false;
            }
            rule_->action = *op == '<' ? ITDB_SPLACTION_IS_IN_THE_LAST
                                       : ITDB_SPLACTION_IS_NOT_IN_THE_LAST;
            rule_->fromdate = -(gint64)v;
            rule_->fromunits = um->value;
        } break;

        default:
            g_printerr("bad rule '%s' - field '%s' not supported\n", expr_, fm->name);
            return false;
    }

    if (itdb_splr_get_action_type(rule_) == ITDB_SPLAT_INVALID) {
        g_printerr("bad rule '%s' - op not valid for field '%s'\n", expr_, fm->name);
        return false;
    }
    itdb_splr_validate(rule_);
    return true;
}

/* parse '<n>:<type>[:<sort>]' eg 25:songs:recent */
static bool  _spl_parse_limit(Itdb_SPLPref* pref_, const char* spec_)
{
    bool  ret = false;
    char**  tok = g_strsplit(spec_, ":", 3);
    const unsigned  ntok = g_strv_length(tok);

    const char*  end;
    guint64  v;
    const struct spl_name_map*  tm;
    const struct spl_name_map*  sm = _spl_limit_sorts;  /* random */

    if (ntok < 2 || !_spl_parse_uint(tok[0], &end, &v) || *end != '\0' || v == 0) {
        g_printerr("bad limit '%s' - expect '<n>:<type>[:<sort>]'\n", spec_);
        goto done;
    }
    if ( (tm = _spl_map_by_name(_spl_limit_types, tok[1], strlen(tok[1]))) == NULL) {
        g_printerr("bad limit '%s' - unknown type '%s'\n", spec_, tok[1]);
        goto done;
    }
    if (ntok == 3 && (sm = _spl_map_by_name(_spl_limit_sorts, tok[2], strlen(tok[2]))) == NULL) {
        g_printerr("bad limit '%s' - unknown sort '%s'\n", spec_, tok[2]);
        goto done;
    }

    pref_->checklimits = 1;
    pref_->limitvalue = v;
    pref_->limittype = tm->value;
    pref_->limitsort = sm->value;
    ret = true;

done:
    g_strfreev(tok);
    return ret;
}

/* render a rule back in the syntax _spl_parse_rule() accepts */
static void  _spl_render_rule(GString* out_, Itdb_SPLRule* rule_)
{
    const struct spl_name_map*  fm = _spl_map_by_value(_spl_fields, rule_->field);
    if (fm == NULL) {
        g_string_append_printf(out_, "<unsupported field=0x%x action=0x%x>", rule_->field, rule_->action);
        return;
    }

    const struct spl_name_map*  om;
    const guint64  scale = _spl_value_scale(rule_->field);

    switch (itdb_splr_get_field_type(rule_))
    {
        case ITDB_SPLFT_STRING:
            om = _spl_map_by_value(_spl_string_ops, rule_->action);
            if (om) {
                g_string_append_printf(out_, "%s %s %s", fm->name, om->name, rule_->string ? rule_->string : "");
                return;
            }
            break;

        case ITDB_SPLFT_INT:
            if (rule_->action == ITDB_SPLACTION_IS_IN_THE_RANGE) {
                g_string_append_printf(out_, "%s = %" G_GUINT64_FORMAT "..%" G_GUINT64_FORMAT,
                                       fm->name, rule_->fromvalue/scale, rule_->tovalue/scale);
                return;
            }
            om = _spl_map_by_value(_spl_int_ops, rule_->action);
            if (om) {
                g_string_append_printf(out_, "%s %s %" G_GUINT64_FORMAT,
                                       fm->name, om->name, rule_->fromvalue/scale);
                return;
            }
            break;

        case ITDB_SPLFT_BOOLEAN:
            if (rule_->action == ITDB_SPLACTION_IS_INT || rule_->action == ITDB_SPLACTION_IS_NOT_INT) {
                g_string_append_printf(out_, "%s = %s", fm->name,
                                       rule_->action == ITDB_SPLACTION_IS_INT ? "set" : "unset");
                return;
            }
            break;

        case ITDB_SPLFT_DATE:
            if (rule_->action == ITDB_SPLACTION_IS_IN_THE_LAST || rule_->action == ITDB_SPLACTION_IS_NOT_IN_THE_LAST)
            {
                const struct spl_name_map*  um = _spl_map_by_value(_spl_date_units, rule_->fromunits);
                if (um) {
                    g_string_append_printf(out_, "%s %c %" G_GINT64_FORMAT "%s",
                                           fm->name,
                                           rule_->action == ITDB_SPLACTION_IS_IN_THE_LAST ? '<' : '>',
                                           -rule_->fromdate, um->name);
                    return;
                }
            }
            break;

        default:
            break;
    }
    g_string_append_printf(out_, "<unsupported field=%s action=0x%x>", fm->name, rule_->action);
}

static void  _spl_dump(Itdb_Playlist* pl_)
{
    GString*  out = g_string_new(NULL);
    g_string_append_printf(out, "smart { match=%s liveupdate=%s",
                           pl_->splrules.match_operator == ITDB_SPLMATCH_OR ? "any" : "all",
                           pl_->splpref.liveupdate ? "yes" : "no");
    if (pl_->splpref.checklimits)
    {
        const struct spl_name_map*  tm = _spl_map_by_value(_spl_limit_types, pl_->splpref.limittype);
        const struct spl_name_map*  sm = _spl_map_by_value(_spl_limit_sorts, pl_->splpref.limitsort);
        g_string_append_printf(out, " limit=%u:%s:%s",
                               pl_->splpref.limitvalue,
                               tm ? tm->name : "?", sm ? sm->name : "?");
    }
    g_string_append(out, " }\n");

    if (!pl_->splpref.checkrules || pl_->splrules.rules == NULL) {
        g_string_append(out, "  rules: none (matches all)\n");
    }
    else {
        for (GList* it=pl_->splrules.rules; it!=NULL; it=it->next) {
            g_string_append(out, "  rule: ");
            _spl_render_rule(out, (Itdb_SPLRule*)it->data);
            g_string_append_c(out, '\n');
        }
    }
    g_print("%s", out->str);
    g_string_free(out, TRUE);
}

/* resolve a track selector - all digits is an iTunesDB track id, otherwise
 * an ipod path (/iPod_Control/Music/...); lookup structs created lazily
 */
static Itdb_Track*  _resolve_track(Itdb_iTunesDB* itdb_, const char* arg_,
                                   GTree** idtree_, GHashTable** pathhash_)
{
    const char*  d = arg_;
    while (*d && isdigit(*d)) {
        ++d;
    }

    if (*d == '\0' && d != arg_)
    {
        if (*idtree_ == NULL) {
            *idtree_ = itdb_track_id_tree_create(itdb_);
        }
        return itdb_track_id_tree_by_id(*idtree_, atol(arg_));
    }

    if (*pathhash_ == NULL)
    {
        *pathhash_ = g_hash_table_new(g_str_hash, g_str_equal);
        for (GList* it=itdb_playlist_mpl(itdb_)->members; it!=NULL; it=it->next)
        {
            Itdb_Track*  track = (Itdb_Track*)it->data;
            itdb_filename_ipod2fs(track->ipod_path);
            g_hash_table_insert(*pathhash_, track->ipod_path, track);
        }
    }
    return g_hash_table_lookup(*pathhash_, arg_);
}

static void  _list_playlists(Itdb_iTunesDB* itdb_)
{
    unsigned  playlists = 0;
    for (GList* it=itdb_->playlists; it!=NULL; it=it->next)
    {
        Itdb_Playlist*  pl = (Itdb_Playlist*)it->data;
        g_print("'%s' { type=%s count=%u smartpl=%s",
                pl->name ? pl->name : "",
                _pl_type(pl),
                g_list_length(pl->members),
                pl->is_spl ? "yes" : "no");
        if (pl->is_spl) {
            g_print(" rules=%u liveupdate=%s",
                    g_list_length(pl->splrules.rules),
                    pl->splpref.liveupdate ? "yes" : "no");
        }
        g_print(" }\n");
        ++playlists;
    }
    g_print("playlists=%u\n", playlists);
}

static void  _list_pl_tracks(Itdb_Playlist* pl_)
{
    if (pl_->is_spl) {
        _spl_dump(pl_);
    }

    const unsigned  N = g_list_length(pl_->members);
    unsigned  i = 0;
    for (GList* it=pl_->members; it!=NULL; it=it->next)
    {
        Itdb_Track*  track = (Itdb_Track*)it->data;
        itdb_filename_ipod2fs(track->ipod_path);
        g_print("[%3u/%u]  id=%u ipod_path='%s' { title='%s' artist='%s' album='%s' }\n",
                ++i, N,
                track->id,
                track->ipod_path ? track->ipod_path : "",
                track->title ? track->title : "",
                track->artist ? track->artist : "",
                track->album ? track->album : "");
    }
    g_print("'%s' tracks=%u\n", pl_->name ? pl_->name : "", N);
}

/* op functions return -1 on refusal/error, otherwise number of changes */

static int  _pl_create(Itdb_iTunesDB* itdb_, const char* name_)
{
    if (itdb_playlist_by_name(itdb_, (gchar*)name_)) {
        g_printerr("playlist '%s' already exists\n", name_);
        return -1;
    }

    Itdb_Playlist*  pl = itdb_playlist_new(name_, false);
    itdb_playlist_add(itdb_, pl, -1);
    g_print("created playlist '%s'\n", name_);
    return 1;
}

static int  _pl_create_smart(Itdb_iTunesDB* itdb_, const char* name_,
                             char** rules_, unsigned nrules_,
                             bool match_any_, const char* limit_)
{
    if (itdb_playlist_by_name(itdb_, (gchar*)name_)) {
        g_printerr("playlist '%s' already exists\n", name_);
        return -1;
    }

    Itdb_Playlist*  pl = itdb_playlist_new(name_, true);

    /* itdb_playlist_new() seeds a default artist-contains rule; build from args */
    itdb_splr_remove(pl, (Itdb_SPLRule*)pl->splrules.rules->data);

    pl->splrules.match_operator = match_any_ ? ITDB_SPLMATCH_OR : ITDB_SPLMATCH_AND;

    for (unsigned i=0; i<nrules_; ++i)
    {
        Itdb_SPLRule*  rule = itdb_splr_add_new(pl, -1);
        if (!_spl_parse_rule(rule, rules_[i])) {
            itdb_playlist_free(pl);
            return -1;
        }
    }
    if (nrules_ == 0) {
        pl->splpref.checkrules = 0;
    }

    if (limit_ && !_spl_parse_limit(&pl->splpref, limit_)) {
        itdb_playlist_free(pl);
        return -1;
    }

    itdb_playlist_add(itdb_, pl, -1);
    itdb_spl_update(pl);

    g_print("created smart playlist '%s' with members=%u\n", name_, g_list_length(pl->members));
    _spl_dump(pl);
    return 1;
}

/* iPod displays the member list stored in iTunesDB, it does not evaluate
 * rules itself - refresh members after library changes (gpod-cp/gpod-rm)
 */
static int  _pl_spl_refresh(Itdb_iTunesDB* itdb_)
{
    unsigned  spls = 0;
    for (GList* it=itdb_->playlists; it!=NULL; it=it->next)
    {
        Itdb_Playlist*  pl = (Itdb_Playlist*)it->data;
        if (!pl->is_spl) {
            continue;
        }

        const unsigned  before = g_list_length(pl->members);
        itdb_spl_update(pl);
        g_print("'%s' members %u -> %u\n",
                pl->name ? pl->name : "", before, g_list_length(pl->members));
        ++spls;
    }

    if (spls == 0) {
        g_print("no smart playlists\n");
        return 0;
    }
    g_print("refreshed %u smart playlists\n", spls);
    return spls;
}

static int  _pl_delete(Itdb_Playlist* pl_)
{
    if (itdb_playlist_is_mpl(pl_)) {
        g_printerr("cannot delete master playlist\n");
        return -1;
    }
    if (itdb_playlist_is_podcasts(pl_)) {
        g_printerr("cannot delete podcasts playlist\n");
        return -1;
    }

    g_print("deleting playlist '%s' with %u tracks (tracks remain on iPod)\n",
            pl_->name ? pl_->name : "", g_list_length(pl_->members));
    itdb_playlist_remove(pl_);
    return 1;
}

static int  _pl_rename(Itdb_iTunesDB* itdb_, Itdb_Playlist* pl_, const char* newname_)
{
    if (itdb_playlist_is_podcasts(pl_)) {
        g_printerr("cannot rename podcasts playlist\n");
        return -1;
    }
    if (itdb_playlist_by_name(itdb_, (gchar*)newname_)) {
        g_printerr("playlist '%s' already exists\n", newname_);
        return -1;
    }

    if (itdb_playlist_is_mpl(pl_)) {
        g_print("renaming iPod to '%s'\n", newname_);
    }
    else {
        g_print("renaming playlist '%s' to '%s'\n", pl_->name ? pl_->name : "", newname_);
    }
    g_free(pl_->name);
    pl_->name = g_strdup(newname_);
    return 1;
}

static int  _pl_clear(Itdb_Playlist* pl_)
{
    if (itdb_playlist_is_mpl(pl_)) {
        g_printerr("cannot clear master playlist\n");
        return -1;
    }
    if (pl_->is_spl) {
        g_printerr("cannot clear smart playlist '%s'\n", pl_->name ? pl_->name : "");
        return -1;
    }
    if (itdb_playlist_is_podcasts(pl_)) {
        g_print("clearing podcasts playlist\n");
    }

    int  removed = 0;
    while (pl_->members) {
        itdb_playlist_remove_track(pl_, (Itdb_Track*)pl_->members->data);
        ++removed;
    }
    g_print("cleared '%s', removed %d tracks\n", pl_->name ? pl_->name : "", removed);
    return removed;
}

static int  _pl_add_tracks(Itdb_iTunesDB* itdb_, Itdb_Playlist* pl_, char* argv_[], unsigned N_)
{
    if (itdb_playlist_is_mpl(pl_)) {
        g_printerr("cannot add tracks to master playlist\n");
        return -1;
    }
    if (itdb_playlist_is_podcasts(pl_)) {
        g_printerr("cannot add tracks to podcasts playlist - use gpod-cp\n");
        return -1;
    }
    if (pl_->is_spl) {
        g_printerr("cannot add tracks to smart playlist '%s'\n", pl_->name ? pl_->name : "");
        return -1;
    }

    GTree*  idtree = NULL;
    GHashTable*  pathhash = NULL;
    int  added = 0;
    unsigned  requested = 0;

    for (char** p=argv_; *p; ++p)
    {
        ++requested;
        Itdb_Track*  track = _resolve_track(itdb_, *p, &idtree, &pathhash);

        if (track == NULL) {
            g_print("[%3u/%u]  %s -> { Not on iPod/iTunesDB }\n", requested, N_, *p);
            continue;
        }

        if (itdb_playlist_contains_track(pl_, track)) {
            g_print("[%3u/%u]  %s -> { already in playlist }\n", requested, N_, *p);
            continue;
        }

        itdb_playlist_add_track(pl_, track, -1);
        g_print("[%3u/%u]  %s -> { id=%u title='%s' artist='%s' }\n",
                requested, N_, *p,
                track->id,
                track->title ? track->title : "",
                track->artist ? track->artist : "");
        ++added;
    }

    if (idtree)    itdb_track_id_tree_destroy(idtree);
    if (pathhash)  g_hash_table_destroy(pathhash);

    g_print("added %d/%u tracks to '%s', now count=%u\n",
            added, requested, pl_->name ? pl_->name : "", g_list_length(pl_->members));
    return added;
}

static int  _pl_remove_tracks(Itdb_iTunesDB* itdb_, Itdb_Playlist* pl_, char* argv_[], unsigned N_)
{
    if (itdb_playlist_is_mpl(pl_)) {
        g_printerr("cannot remove tracks from master playlist - use gpod-rm\n");
        return -1;
    }
    if (pl_->is_spl) {
        g_printerr("cannot remove tracks from smart playlist '%s'\n", pl_->name ? pl_->name : "");
        return -1;
    }

    GTree*  idtree = NULL;
    GHashTable*  pathhash = NULL;
    int  removed = 0;
    unsigned  requested = 0;

    for (char** p=argv_; *p; ++p)
    {
        ++requested;
        Itdb_Track*  track = _resolve_track(itdb_, *p, &idtree, &pathhash);

        if (track == NULL) {
            g_print("[%3u/%u]  %s -> { Not on iPod/iTunesDB }\n", requested, N_, *p);
            continue;
        }

        if (!itdb_playlist_contains_track(pl_, track)) {
            g_print("[%3u/%u]  %s -> { not in playlist }\n", requested, N_, *p);
            continue;
        }

        itdb_playlist_remove_track(pl_, track);
        g_print("[%3u/%u]  %s -> { id=%u title='%s' artist='%s' }\n",
                requested, N_, *p,
                track->id,
                track->title ? track->title : "",
                track->artist ? track->artist : "");
        ++removed;
    }

    if (idtree)    itdb_track_id_tree_destroy(idtree);
    if (pathhash)  g_hash_table_destroy(pathhash);

    g_print("removed %d/%u tracks from '%s', now count=%u\n",
            removed, requested, pl_->name ? pl_->name : "", g_list_length(pl_->members));
    return removed;
}


int
main (int argc, char *argv[])
{
    GError *error = NULL;
    Itdb_iTunesDB*  itdb = NULL;
    Itdb_Device*  itdev = NULL;
    struct {
        const char*  itdb_path;
        const char*  create_name;
        const char*  delete_name;
        const char*  pl_name;
        const char*  rename_to;
        bool  list;
        bool  add;
        bool  remove;
        bool  clear;
        bool  recent;
        unsigned  album_limit;
        bool  with_m3u;
        bool  recent_opts;
        bool  smart;
        bool  match_any;
        const char*  limit;
        bool  spl_refresh;
    } opts = { NULL, NULL, NULL, NULL, NULL, false, false, false, false, false, 50, false, false,
               false, false, NULL, false };

    GPtrArray*  rule_exprs = g_ptr_array_new();

    int  ret = 0;

    int  c;
    while ( (c=getopt(argc, argv, "M:c:d:p:R:n:e:L:larCSAUu3vh")) != EOF)
    {
        switch (c) {
            case 'M':  opts.itdb_path = optarg;  break;
            case 'c':  opts.create_name = optarg;  break;
            case 'd':  opts.delete_name = optarg;  break;
            case 'p':  opts.pl_name = optarg;  break;
            case 'R':  opts.rename_to = optarg;  break;
            case 'l':  opts.list = true;  break;
            case 'a':  opts.add = true;  break;
            case 'r':  opts.remove = true;  break;
            case 'C':  opts.clear = true;  break;
            case 'u':  opts.recent = true;  break;
            case 'n':  opts.album_limit = atol(optarg);  opts.recent_opts = true;  break;
            case '3':  opts.with_m3u = true;  opts.recent_opts = true;  break;
            case 'S':  opts.smart = true;  break;
            case 'A':  opts.match_any = true;  break;
            case 'e':  g_ptr_array_add(rule_exprs, optarg);  break;
            case 'L':  opts.limit = optarg;  break;
            case 'U':  opts.spl_refresh = true;  break;

            case 'v':
            case 'h':
            default:
                _usage(argv[0]);
        }
    }

    enum gpod_pl_op  op = OP_NONE;
    unsigned  nops = 0;
    if (opts.list)         { op = OP_LIST;    ++nops; }
    if (opts.create_name)  { op = OP_CREATE;  ++nops; }
    if (opts.delete_name)  { op = OP_DELETE;  ++nops; }
    if (opts.rename_to)    { op = OP_RENAME;  ++nops; }
    if (opts.clear)        { op = OP_CLEAR;   ++nops; }
    if (opts.add)          { op = OP_ADD;     ++nops; }
    if (opts.remove)       { op = OP_REMOVE;  ++nops; }
    if (opts.recent)       { op = OP_RECENT;  ++nops; }
    if (opts.spl_refresh)  { op = OP_SPL_REFRESH;  ++nops; }

    if (nops == 0) {
        g_printerr("no operation specified\n");
        _usage(argv[0]);
    }
    if (nops > 1) {
        g_printerr("conflicting operations\n");
        _usage(argv[0]);
    }

    switch (op) {
        case OP_ADD:
        case OP_REMOVE:
        case OP_RENAME:
        case OP_CLEAR:
            if (opts.pl_name == NULL) {
                g_printerr("operation requires -p <playlist>\n");
                _usage(argv[0]);
            }
            break;

        default:
            if (op != OP_LIST && opts.pl_name) {
                g_printerr("-p only valid with -l/-a/-r/-R/-C\n");
                _usage(argv[0]);
            }
    }

    if (op == OP_ADD || op == OP_REMOVE) {
        if (optind == argc) {
            g_printerr("no tracks specified\n");
            _usage(argv[0]);
        }
    }
    else if (optind != argc) {
        g_printerr("unexpected arguments\n");
        _usage(argv[0]);
    }

    if (opts.recent_opts && op != OP_RECENT) {
        g_printerr("-n/-3 only valid with -u\n");
        _usage(argv[0]);
    }

    if ((opts.smart || opts.match_any || opts.limit || rule_exprs->len > 0) && op != OP_CREATE) {
        g_printerr("-S/-e/-A/-L only valid with -c\n");
        _usage(argv[0]);
    }
    if ((opts.match_any || opts.limit || rule_exprs->len > 0) && !opts.smart) {
        g_printerr("-e/-A/-L require -c <name> -S\n");
        _usage(argv[0]);
    }

    char  mountpoint[PATH_MAX] = { 0 };
    if (opts.itdb_path == NULL) {
        opts.itdb_path = gpod_default_mountpoint(mountpoint, sizeof(mountpoint));
    }
    else {
	strcpy(mountpoint, opts.itdb_path);
    }

    gpod_setlocale();

    const char*  argtype = "unknown";
    if (g_file_test(opts.itdb_path, G_FILE_TEST_IS_DIR)) {
        itdb = itdb_parse (opts.itdb_path, &error);
        argtype = "directroy";
        itdev = itdb_device_new();
        itdb_device_set_mountpoint(itdev, opts.itdb_path);
    }
    else {
        if (g_file_test(opts.itdb_path, G_FILE_TEST_EXISTS)) {
            itdb = itdb_parse_file(opts.itdb_path, &error);
            argtype = "file";

            // the Device info is /mnt/iPod_Control/Device - if we've been given a db
            // location /mnt/iPod_Control/iTunes/iTunesDB we can figure this out

            char*  dmp;
            if ( (dmp = strstr(mountpoint, "iPod_Control/"))) {
                itdev = itdb_device_new();
                *dmp = '\0';
                itdb_device_set_mountpoint(itdev, mountpoint);
            }
        }
    }

    if (error)
    {
        if (error->message) {
            g_printerr("failed to prase iTunesDB via (%s) %s - %s\n", argtype, opts.itdb_path, error->message);
        }
        g_error_free (error);
        error = NULL;
        return -1;
    }

    if (itdb == NULL) {
        g_print("failed to open iTunesDB via (%s) %s\n", argtype, opts.itdb_path);
        return -1;
    }

    if (op != OP_LIST)
    {
        const Itdb_IpodInfo*  ipodinfo = itdb_device_get_ipod_info(itdev);
        const bool  supported = gpod_write_supported(ipodinfo);
        if (!supported) {
            g_printerr("iPod %s %s not supported\n", itdb_info_get_ipod_generation_string(ipodinfo->ipod_generation), ipodinfo->model_number);
            ret = -1;
            goto cleanup;
        }
    }

    Itdb_Playlist*  pl = NULL;
    const char*  lookup_name = opts.delete_name ? opts.delete_name : opts.pl_name;
    if (lookup_name)
    {
        pl = itdb_playlist_by_name(itdb, (gchar*)lookup_name);
        if (pl == NULL) {
            g_printerr("no such playlist '%s'\n", lookup_name);
            ret = 1;
            goto cleanup;
        }
    }

    int  changes = 0;
    const unsigned  N = argc - optind;

    switch (op)
    {
        case OP_LIST:
            if (pl)  _list_pl_tracks(pl);
            else     _list_playlists(itdb);
            break;

        case OP_CREATE:
            changes = opts.smart ?
                _pl_create_smart(itdb, opts.create_name,
                                 (char**)rule_exprs->pdata, rule_exprs->len,
                                 opts.match_any, opts.limit) :
                _pl_create(itdb, opts.create_name);
            break;

        case OP_SPL_REFRESH:  changes = _pl_spl_refresh(itdb);  break;
        case OP_DELETE:  changes = _pl_delete(pl);  break;
        case OP_RENAME:  changes = _pl_rename(itdb, pl, opts.rename_to);  break;
        case OP_CLEAR:   changes = _pl_clear(pl);  break;
        case OP_ADD:     changes = _pl_add_tracks(itdb, pl, &argv[optind], N);  break;
        case OP_REMOVE:  changes = _pl_remove_tracks(itdb, pl, &argv[optind], N);  break;

        case OP_RECENT:
        {
            unsigned  recent_pl, recent_tracks;
            gpod_playlist_recent(&recent_pl, &recent_tracks, itdb, opts.album_limit, 0, opts.with_m3u);
            g_print("iPod playlists=%u (limited to %d) with tracks=%u\n", recent_pl, opts.album_limit, recent_tracks);
            changes = recent_tracks;
        }
        break;

        default:
            break;
    }

    if (changes < 0) {
        ret = 1;
    }

    if (changes > 0)
    {
        g_print("sync'ing iPod ...\n");
        itdb_write(itdb, &error);

        if (error) {
            g_printerr("failed to write playlists iPod database - %s\n", error->message ? error->message : "<unknown error>");
             g_error_free (error);
             ret = 1;
        }
    }


cleanup:
    g_ptr_array_free(rule_exprs, TRUE);
    itdb_device_free(itdev);
    itdb_free (itdb);

    return ret;
}
