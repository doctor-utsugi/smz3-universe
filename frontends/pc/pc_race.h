/* The online race's requests on PC: one thread sends them in order and polls the room (every second in the rooms, every two
 * in the race); the menu and the game take the answers with race_take. Nothing blocks the caller. */
#ifndef PC_RACE_H
#define PC_RACE_H

#include <stddef.h>
#include <stdint.h>

enum { RACE_ANS_ROOM, RACE_ANS_ROOMS, RACE_ANS_ERROR };

/* Queues a POST to /api/race/<what>; fields is the JSON body without braces. With_token adds the room's token. */
void race_send(const char *what, const char *fields, int with_token);
/* Queues the request for the open public rooms. */
void race_find(void);
/* The next answer: its kind (RACE_ANS_*) with the JSON (or the error text) in out; -1 when none is waiting. */
int race_take(char *out, size_t cap);
/* Stops polling and forgets the room, once the requests queued before it are sent. */
void race_forget(void);
int race_joined(void);
/* What the next poll carries: the last log line read, the progress (0..100), a note for the rivals and chat lines. */
void race_set_since(int64_t since);
void race_set_progress(int progress);
void race_event(const char *text);
void race_chat(const char *text);
/* Polls every two seconds instead of every second (in the race). */
void race_set_slow(int slow);

#endif
