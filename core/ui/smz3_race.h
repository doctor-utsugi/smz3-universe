/* An online race as the server tells it (server/ladder/race.js): the room, its players and the latest log lines, read from
 * the answers of the /api/race/ calls. No network here: each host sends the requests and hands the answers in. */
#ifndef SMZ3_RACE_H
#define SMZ3_RACE_H

#include <stdint.h>
#include <stddef.h>

#define RACE_MAX 8
#define RACE_LINES 24
#define RACE_ROOMS 12

enum { RACE_NONE, RACE_OPEN, RACE_CLOSED, RACE_RUNNING, RACE_FINISHED, RACE_CANCELLED, RACE_PAUSED };
enum { RACE_LINE_SYSTEM, RACE_LINE_EVENT, RACE_LINE_CHAT, RACE_LINE_FINISH };

typedef struct {
    char pid[12], name[25], country[3], link[32], samus[32];
    int ready, left, progress, place, held, away, back;   /* back: in the game again after a pause */
    int64_t time_ms;             /* -1 until they finish */
} SmzRacePlayer;

typedef struct { int64_t seq, at; int kind; char pid[12], name[25], text[84]; } SmzRaceLine;

typedef struct { char code[8], format[8], creator[25]; int size, players; } SmzRaceRoom;   /* a row of the public list */

typedef struct {
    int state;
    char token[96], code[8], format[8], you[12], creator[12], end_why[24];
    int size, is_public, n;
    SmzRacePlayer p[RACE_MAX];
    int64_t start_at, end_at;    /* server time; 0 when not set */
    int64_t go_at;               /* when play starts or starts again after a pause (server time) */
    int64_t resume_at;           /* a paused race: the time the vote set */
    int pauses;
    char vote_by[12];            /* a pause asked for and not settled: who asked ("" none), until when, who said yes */
    int64_t vote_at;
    char vote_yes[RACE_MAX][12];
    int vote_n;
    int64_t offset;              /* server time minus this device's time, from the last poll */
    char seed[16], settings[512];   /* once the room is closed */
    int64_t since;               /* the last log line read */
    SmzRaceLine line[RACE_LINES];   /* the newest lines, oldest first */
    int lines, fresh;            /* lines kept / lines that came with the last answer */
} SmzRace;

/* Reads an answer holding "room" (and for /poll also "now", "you" and "log"); local_now is this device's clock in ms.
 * 0 when it held a room. */
int smz_race_read(SmzRace *r, const char *json, int64_t local_now);
/* The rows of /api/race/open; returns how many. */
int smz_race_rooms(const char *json, SmzRaceRoom *rows, int max);
/* The "error" of an answer, or "" (out gets at most cap bytes). */
void smz_race_error(const char *json, char *out, size_t cap);
/* This player, or NULL. */
const SmzRacePlayer *smz_race_me(const SmzRace *r);
/* Server time now, from this device's clock. */
int64_t smz_race_server_now(const SmzRace *r, int64_t local_now);
/* The format's name for people ("Full game", "Sprint", "Half", "Draft"). */
const char *smz_race_format_name(const char *format);
/* Whether this player said yes to the pause being voted. */
int smz_race_voted(const SmzRace *r);
/* A server time as this device's local day and hour ("Sat 18:30"). */
void smz_race_when(int64_t ms, char *out, size_t cap);
/* Marks a race run's settings JSON (as the host generated it from the room's) as a challenge, with the room's Draft goal
 * and start when it has them. 0, or -1 when they do not fit in cap. */
int smz_race_mark_settings(char *json, size_t cap, const char *room_settings);
/* Writes a Draft's start ("start": "sm:<area>:<station>" or "z3:<exit>") into the ROM and fixes its checksum; 0 when the
 * settings have no start or it was written, -1 when the ROM refused it. */
int smz_race_apply_start(uint8_t *rom, const char *settings);
/* A JSON string with quotes and escapes, for the requests (out gets at most cap bytes). */
void smz_race_quote(const char *s, char *out, size_t cap);

#endif
