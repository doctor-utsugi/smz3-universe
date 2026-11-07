/* The daily timed challenge on the client side: what the server's /api/daily answers say, kept in plain C types for every
 * host. Server times are turned into the local clock (ms since 1970) with the server's "now", so a wrong device clock
 * does not move the window. */
#ifndef SMZ3_DAILY_H
#define SMZ3_DAILY_H

#include <stddef.h>
#include <stdint.h>
#include "smz3_race.h"

#define SMZ_DAILY_HINTS 24
#define SMZ_DAILY_HINT_LEN 160
#define SMZ_DAILY_START 8

typedef enum { DAILY_NONE, DAILY_OPEN, DAILY_STARTED, DAILY_DONE, DAILY_LATE, DAILY_FAILED } SmzDailyState;

/* GET /api/daily: the window open now (or the next one), the player's state in it and the token balance. */
typedef struct {
    int loaded;                           /* 0 not yet, 1 ok, -1 failed (offline) */
    char window[16];                      /* the open window's id ("2026-10-30-1"); "" when none is open */
    char goal[96], difficulty[8];
    char start[SMZ_DAILY_START][32];      /* the items the run starts with */
    int n_start, est_min;
    SmzDailyState state;
    int64_t opens_at, closes_at, next_opens_at, started_at, time_ms;
    int tokens, streak;
    int players_now, players_done;        /* playing it now and finished, -1 until the server says */
} SmzDailyInfo;

/* An attempt in progress (POST /api/daily/start, then /api/daily/hints). */
typedef struct {
    char window[16], token[320], goal[96], check[32];   /* check: the goal as the hosts read it (smz3_goal.h) */
    int64_t started_at, closes_at, next_hint_at;   /* next_hint_at 0: every hint is out */
    int64_t began_at;   /* when the game itself started (the file loaded), as the ladder times its runs; 0 until then */
    int players_known, players_now, players_started, players_done;   /* who is on the window, from the last answer (not kept) */
    int n_hints;
    int64_t hint_at[SMZ_DAILY_HINTS];
    char hint[SMZ_DAILY_HINTS][SMZ_DAILY_HINT_LEN];
} SmzDailyRun;

/* Each parser returns 0, or -1 when the answer is not what the server sends. local_now_ms is the device clock at the answer. */
int smz_daily_parse_info(const char *json, int64_t local_now_ms, SmzDailyInfo *d);
/* POST /api/daily/start: the run settings go to settings (the run JSON to generate from). */
int smz_daily_parse_start(const char *json, int64_t local_now_ms, SmzDailyRun *r, char *settings, size_t settings_cap);
/* GET /api/daily/hints: replaces the hints and the time of the next one. */
int smz_daily_parse_hints(const char *json, int64_t local_now_ms, SmzDailyRun *r);
/* The attempt as text lines the host keeps beside the run, and back. */
int smz_daily_run_text(const SmzDailyRun *r, char *out, size_t cap);
/* Where the own clock counts from: the game's start, or Start on the menu while the game has not begun. */
int64_t smz_daily_clock_from(const SmzDailyRun *r);
/* The game has started (the tracker sees a file in play): sets began_at once. A start more than 10 minutes after Start (an
 * attempt resumed on a new install) keeps Start, so losing the files never resets the clock. 1 when it changed. */
int smz_daily_note_began(SmzDailyRun *r, int64_t now_ms);
int smz_daily_run_load(const char *text, SmzDailyRun *r);
/* "2h 05m", "12m 30s", "45s" for a span of ms (0 or less: "0s"). */
void smz_daily_span(int64_t ms, char *out, size_t cap);
/* Adds "challenge":true, the goal and the prepared start of the server's settings (server_settings, may be NULL) to a run's
 * settings JSON in place (the generator's JSON has none of them). */
int smz_daily_mark_settings(char *json, size_t cap, const char *goal_check, const char *server_settings);
/* A mid-game start: the Super Metroid bosses already dead (bit 0 Kraid, 1 Phantoon, 2 Draygon, 3 Ridley), from a settings JSON. */
int smz_daily_prepared_bosses(const char *settings);

/* The window's chat (GET /api/daily/chat): its lines kept as a race's, RACE_LINE_CHAT and RACE_LINE_FINISH (name finished,
 * text "finished in 1:02:03, place 2"), oldest first; since is the newest line read, fresh how many the last answer added. */
typedef struct { int64_t since; SmzRaceLine line[RACE_LINES]; int lines, fresh; } SmzDailyChat;
/* Adds the answer's new lines to c and its player counts to r (may be NULL). 0, or -1 when it is not the chat's answer. */
int smz_daily_parse_chat(const char *json, SmzDailyChat *c, SmzDailyRun *r);
/* The body of POST /api/daily/chat for the attempt r (out gets at most cap bytes); -1 when it does not fit. */
int smz_daily_chat_body(const SmzDailyRun *r, const char *player, const char *country, const char *text, char *out, size_t cap);
/* Quick lines the chat offers to send with one press, emotes among them. */
#define SMZ_DAILY_PHRASES 8
extern const char *const SMZ_DAILY_PHRASE[SMZ_DAILY_PHRASES];

#endif
