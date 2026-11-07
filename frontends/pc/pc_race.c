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

/* Sends one request and files its answer; a create or join answer brings the room's token. */
static void request(const char *what, const char *fields, int with_token, int find, char *buf)
{
    if (!strcmp(what, "forget")) { pthread_mutex_lock(&mu); token[0] = 0; since = 0; progress = 0; ne = nc = 0; slow = 0; pthread_mutex_unlock(&mu); return; }
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
    if (!strcmp(what, "leave")) { pthread_mutex_lock(&mu); token[0] = 0; since = 0; pthread_mutex_unlock(&mu); return; }   /* gone either way */
    if (code < 0) { answer(RACE_ANS_ERROR, "No connection"); return; }
    if (code != 200) {
        char err[64];
        smz_race_error(buf, err, sizeof err);
        answer(RACE_ANS_ERROR, !strcmp(err, "version") ? "Update the app to join this room" : err[0] ? err : "The server said no");
        return;
    }
    if (find) { answer(RACE_ANS_ROOMS, buf); return; }
    const char *t = strstr(buf, "\"token\":\"");
    if (t && (!strcmp(what, "create") || !strcmp(what, "join"))) {
        t += 9;
        size_t n = strcspn(t, "\"");
        pthread_mutex_lock(&mu);
        if (n < sizeof token) { memcpy(token, t, n); token[n] = 0; }
        pthread_mutex_unlock(&mu);
        return;   /* the room itself comes with the next poll, at once */
    }
    answer(RACE_ANS_ROOM, buf);
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
            request(what, fields ? fields : "", tk, find, buf);
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
        json_list(ev, sizeof ev, events[0], ne, sizeof events[0]);
        json_list(ch, sizeof ch, chats[0], nc, sizeof chats[0]);
        ne = nc = 0;
        snprintf(fields, sizeof fields, "\"since\":%lld,\"progress\":%d,\"events\":%s,\"chat\":%s", (long long)since, progress, ev, ch);
        last = now;
        pthread_mutex_unlock(&mu);
        request("poll", fields, 1, 0, buf);
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
