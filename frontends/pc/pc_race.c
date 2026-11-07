#include "pc_race.h"
#include "pc_online.h"
#include "smz3_race.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
static void nap(int ms) { Sleep(ms); }
#else
#include <unistd.h>
static void nap(int ms) { usleep(ms * 1000); }
#endif

#define QN 16
#define ANS_CAP 32768

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int started;
static struct { char what[16]; char *fields; int token, find; } queue[QN];
static int qn;
static struct { int kind; char *json; } answers[QN];
static int an;
static char token[96];
static int64_t since;
static int progress, slow;
static int64_t offline_since;   /* ms (monotonic) of the first failed poll in a row; 0 while the server answers */
static char events[8][48], chats[3][84];
static int ne, nc;

static void answer(int kind, const char *json)
{
    pthread_mutex_lock(&mu);
    if (an == QN) { free(answers[0].json); memmove(answers, answers + 1, sizeof answers[0] * (QN - 1)); an--; }
    answers[an].kind = kind;
    answers[an++].json = strdup(json ? json : "");
    pthread_mutex_unlock(&mu);
}

static void json_list(char *out, size_t cap, const char *list, int n, size_t each)
{
    size_t k = (size_t)snprintf(out, cap, "[");
    for (int i = 0; i < n && k < cap; i++) {
        char q[200];
        smz_race_quote(list + i * each, q, sizeof q);
        k += (size_t)snprintf(out + k, cap - k, "%s%s", i ? "," : "", q);
    }
    if (k < cap) snprintf(out + k, cap - k, "]");
}

static int64_t mono_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

/* What can't be lost in a race: sent again until the server answers. */
static int keeps(const char *what) { return !strcmp(what, "finish") || !strcmp(what, "ladder") || !strcmp(what, "back") || !strcmp(what, "vote") || !strcmp(what, "pause"); }

/* Sends one request and files its answer; a create or join answer brings the room's token. Returns -1 when the server
 * could not be reached. */
static int request(const char *what, const char *fields, int with_token, int find, char *buf)
{
    if (!strcmp(what, "forget")) { pthread_mutex_lock(&mu); token[0] = 0; since = 0; progress = 0; ne = nc = 0; slow = 0; offline_since = 0; pthread_mutex_unlock(&mu); return 0; }
    int code;
    if (find) code = online_http("GET", "/api/race/open", NULL, buf, ANS_CAP);
    else {
        char path[48], *body = malloc(ANS_CAP);
        snprintf(path, sizeof path, "/api/race/%s", what);
        pthread_mutex_lock(&mu);
        char tok[100];
        smz_race_quote(token, tok, sizeof tok);
        pthread_mutex_unlock(&mu);
        snprintf(body, ANS_CAP, "{%s%s%s%s}", with_token ? "\"token\":" : "", with_token ? tok : "", with_token && fields[0] ? "," : "", fields);
        code = online_http("POST", path, body, buf, ANS_CAP);
        free(body);
    }
    if (!strcmp(what, "leave")) { pthread_mutex_lock(&mu); token[0] = 0; since = 0; pthread_mutex_unlock(&mu); return 0; }   /* gone either way */
    pthread_mutex_lock(&mu);
    if (code < 0) { if (!offline_since) offline_since = mono_ms(); }
    else offline_since = 0;
    pthread_mutex_unlock(&mu);
    if (code < 0) { if (!keeps(what) && strcmp(what, "poll")) answer(RACE_ANS_ERROR, "No connection"); return -1; }
    if (code != 200) {
        char err[64];
        smz_race_error(buf, err, sizeof err);
        answer(RACE_ANS_ERROR, !strcmp(err, "version") ? "Update the app to join this room" : err[0] ? err : "The server said no");
        return 0;
    }
    if (find) { answer(RACE_ANS_ROOMS, buf); return 0; }
    const char *t = strstr(buf, "\"token\":\"");
    if (t && (!strcmp(what, "create") || !strcmp(what, "join"))) {
        t += 9;
        size_t n = strcspn(t, "\"");
        pthread_mutex_lock(&mu);
        if (n < sizeof token) { memcpy(token, t, n); token[n] = 0; }
        pthread_mutex_unlock(&mu);
        return 0;   /* the room itself comes with the next poll, at once */
    }
    answer(RACE_ANS_ROOM, buf);
    return 0;
}

static void *worker(void *arg)
{
    (void)arg;
    char *buf = malloc(ANS_CAP);
    struct timespec last = {0};
    for (;;) {
        pthread_mutex_lock(&mu);
        if (qn) {
            char what[16], *fields = queue[0].fields;
            int tk = queue[0].token, find = queue[0].find;
            snprintf(what, sizeof what, "%s", queue[0].what);
            memmove(queue, queue + 1, sizeof queue[0] * (QN - 1));
            qn--;
            pthread_mutex_unlock(&mu);
            if (request(what, fields ? fields : "", tk, find, buf) < 0 && keeps(what)) {   /* first in the queue again, a little later */
                pthread_mutex_lock(&mu);
                if (qn < QN) {
                    memmove(queue + 1, queue, sizeof queue[0] * qn);
                    snprintf(queue[0].what, sizeof queue[0].what, "%s", what);
                    queue[0].fields = fields; queue[0].token = tk; queue[0].find = find;
                    qn++;
                    fields = NULL;
                }
                pthread_mutex_unlock(&mu);
                nap(2000);
            }
            free(fields);
            memset(&last, 0, sizeof last);   /* a poll right after, so the room shows the change */
            continue;
        }
        int joined = token[0] != 0;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long gap = (now.tv_sec - last.tv_sec) * 1000 + (now.tv_nsec - last.tv_nsec) / 1000000;
        if (!joined || gap < (slow ? 2000 : 1000)) { pthread_mutex_unlock(&mu); nap(50); continue; }
        char ev[512], ch[400], fields[1024];
        int sent_e = ne, sent_c = nc;
        json_list(ev, sizeof ev, events[0], ne, sizeof events[0]);
        json_list(ch, sizeof ch, chats[0], nc, sizeof chats[0]);
        snprintf(fields, sizeof fields, "\"since\":%lld,\"progress\":%d,\"events\":%s,\"chat\":%s", (long long)since, progress, ev, ch);
        last = now;
        pthread_mutex_unlock(&mu);
        if (request("poll", fields, 1, 0, buf) < 0) continue;   /* the news waits for the next poll */
        pthread_mutex_lock(&mu);   /* sent: off the lists, keeping what came in meanwhile */
        memmove(events, events + sent_e, sizeof events[0] * (ne - sent_e)); ne -= sent_e;
        memmove(chats, chats + sent_c, sizeof chats[0] * (nc - sent_c)); nc -= sent_c;
        pthread_mutex_unlock(&mu);
    }
    return NULL;
}

static void start(void)
{
    if (started) return;
    started = 1;
    pthread_t t;
    if (!pthread_create(&t, NULL, worker, NULL)) pthread_detach(t);
}

static void enqueue(const char *what, const char *fields, int with_token, int find)
{
    start();
    pthread_mutex_lock(&mu);
    if (qn < QN) {
        snprintf(queue[qn].what, sizeof queue[qn].what, "%s", what);
        queue[qn].fields = fields ? strdup(fields) : NULL;
        queue[qn].token = with_token;
        queue[qn++].find = find;
    }
    pthread_mutex_unlock(&mu);
}

void race_send(const char *what, const char *fields, int with_token) { enqueue(what, fields, with_token, 0); }
void race_find(void) { enqueue("open", NULL, 0, 1); }

int race_take(char *out, size_t cap)
{
    pthread_mutex_lock(&mu);
    if (!an) { pthread_mutex_unlock(&mu); return -1; }
    int kind = answers[0].kind;
    snprintf(out, cap, "%s", answers[0].json);
    free(answers[0].json);
    memmove(answers, answers + 1, sizeof answers[0] * (QN - 1));
    an--;
    pthread_mutex_unlock(&mu);
    return kind;
}

void race_forget(void) { enqueue("forget", NULL, 0, 0); }   /* in the queue, so the requests before it still go out with the token */
int race_joined(void) { return token[0] != 0; }
int64_t race_offline_ms(void) { pthread_mutex_lock(&mu); int64_t t = offline_since ? mono_ms() - offline_since : 0; pthread_mutex_unlock(&mu); return t; }
void race_token(char *out, size_t cap) { pthread_mutex_lock(&mu); snprintf(out, cap, "%s", token); pthread_mutex_unlock(&mu); }
void race_resume(const char *t)
{
    start();
    pthread_mutex_lock(&mu);
    for (int i = 0; i < qn; i++) if (!strcmp(queue[i].what, "forget")) { free(queue[i].fields); memmove(queue + i, queue + i + 1, sizeof queue[0] * (qn - i - 1)); qn--; i--; }   /* it would drop this token */
    snprintf(token, sizeof token, "%s", t);
    since = 0; progress = 0; ne = nc = 0; offline_since = 0;
    pthread_mutex_unlock(&mu);
}
void race_set_since(int64_t s) { pthread_mutex_lock(&mu); since = s; pthread_mutex_unlock(&mu); }
void race_set_progress(int p) { pthread_mutex_lock(&mu); progress = p < 0 ? 0 : p > 100 ? 100 : p; pthread_mutex_unlock(&mu); }
void race_set_slow(int s) { slow = s; }

void race_event(const char *text)
{
    pthread_mutex_lock(&mu);
    if (ne < 8) snprintf(events[ne++], sizeof events[0], "%s", text);
    pthread_mutex_unlock(&mu);
}

void race_chat(const char *text)
{
    pthread_mutex_lock(&mu);
    if (nc < 3) snprintf(chats[nc++], sizeof chats[0], "%s", text);
    pthread_mutex_unlock(&mu);
}
