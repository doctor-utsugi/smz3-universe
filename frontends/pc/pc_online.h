/* The online ladder (https://smz3.indieviral.com), the PC twin of OnlineLadder.kt. Requests go through the curl program, so
 * nothing is linked; without curl or a connection the run simply stays off the ladder. Slots are the 1-based numbers of the run files. */
#ifndef PC_ONLINE_H
#define PC_ONLINE_H

#include <stdint.h>
#include "smz3_ui.h"
#include "smz3_daily.h"

/* The 1-based run slot of the daily challenge's attempt; it is never listed with the others. */
#define DAILY_SLOT (UI_SLOTS + 1)

/* Loads the stored profile into the menu and enables its Player row. */
void online_init(SmzUi *u);
/* Writes the profile when the menu changed it. */
void online_save_profile(SmzUi *u);
/* 0 offline, 1 original-rules ladder, 2 monthly seed, 3 weekly run. */
int online_slot_kind(int slot);
/* Records what the menu answered for the run just generated. */
void online_mark_run(int slot, int online, int kind);   /* kind as online_slot_kind (1 when online is set and nothing else applies) */
/* This month's label and seed (fetched, or the copy of the same month); 0 on none. Blocks up to a few seconds. */
int online_monthly(char label[16], int *seed);
int online_days_left(void);
int online_weekly(char label[16], int *seed);
/* The server's answer for one ladder table (tab 0 original, 1 monthly, 2 weekly): 1 with the JSON in out, 0 when it failed. */
int online_ladder_json(int tab, char *out, size_t cap);
/* The world goals of the month (/api/goals for this install). */
int online_goals_json(char *out, size_t cap);
/* Sends a run's world goal counters (smz_counters_text); the run is told apart by its slot and when it was made. Blocks. */
int online_goals_report(int slot, int64_t created, int online, const char *counters);
int online_weekly_days_left(void);
/* Asks for the token of an online run once; 1 when the slot has one (or is not online). Blocks. */
int online_ensure_token(int slot);
/* seed is sent for the original ladder, whose seed the server cannot know; the proof of run is read from the slot's .proof file. */
void online_queue_finish(int slot, int64_t time_ms, int seed);
/* Sends the finished runs waiting (the daily attempt's too). Blocks. */
void online_flush(void);

/* The daily challenge; each call blocks for one request. The attempt is kept in runs/daily.txt beside the daily slot. */
int online_daily_info(SmzDailyInfo *d);
/* Starts (or resumes) the attempt of a window: 0 with the run's settings JSON, or -1 with the reason in err. */
int online_daily_start(const char *window, SmzDailyRun *r, char *settings, size_t cap, char *err, size_t errcap);
int online_daily_hints(SmzDailyRun *r);
int online_daily_load(SmzDailyRun *r);
void online_daily_save(const SmzDailyRun *r);
/* Queues the attempt's finish (real time from Start); online_flush sends it with the slot's proof. */
void online_daily_queue_finish(int64_t time_ms);
/* What the server answered to the finish: 1 with a line for the player, 0 while nothing came back. */
int online_daily_result(char *out, size_t cap);
/* The device clock in ms. */
int64_t online_now_ms(void);

#endif
