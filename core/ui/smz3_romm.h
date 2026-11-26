/* A RomM client (romm.app, API 5.4) for the run sync: device-code login, ROMs, run notes, saves and states.
 * The host only moves bytes: it hands in one HTTP function, and everything here is plain C on every device. */
#ifndef SMZ3_ROMM_H
#define SMZ3_ROMM_H

#include <stddef.h>
#include <stdint.h>

/* One request to a full URL. headers holds "Name: value" lines, each ending in "\n" (may be NULL). Returns the HTTP status,
 * or -1 when the server was not reached. *out gets the answer, malloc'd with a NUL after it; the caller frees it. */
typedef int (*SmzHttpFn)(void *ctx, const char *method, const char *url, const char *headers,
                         const uint8_t *body, size_t len, uint8_t **out, size_t *out_len);

typedef struct {
    SmzHttpFn http;
    void *ctx;
    char base[160];      /* "https://romm.example.com", no slash at the end */
    char token[1024];    /* the device's access token ("" until signed in) */
    char device[64];     /* the RomM device id the token belongs to */
    int platform;        /* the snes platform's id, looked up on first use */
    int status;          /* the last request's HTTP status */
    char err[160];       /* why the last call failed */
} SmzRomm;

#define SMZ_ROMM_SNES "snes"
#define SMZ_ROMM_EMULATOR "snes9x"

/* Device-code login: start shows the user a code to approve on the RomM page; poll every interval seconds until it
 * returns 0 (r->token and r->device are set) or -1 (denied or expired). 1: not approved yet. */
typedef struct { char device_code[160], user_code[24], url[256]; int interval, expires_in; } SmzRommLogin;
int smz_romm_login_start(SmzRomm *r, const char *client_id, const char *device_name, const char *platform, const char *version, SmzRommLogin *l);
int smz_romm_login_poll(SmzRomm *r, const SmzRommLogin *l);
/* 0 when the token still works, 1 when RomM refuses it (sign in again), -1 when RomM is not reached. user gets the name. */
int smz_romm_whoami(SmzRomm *r, char *user, size_t cap);

/* A ROM by its MD5 (hex): 0 and *rom_id, 1 when RomM has none, -1 on error. */
int smz_romm_rom_find(SmzRomm *r, const char *md5, int *rom_id);
/* Uploads a ROM to the snes platform under file_name and waits for RomM to list it (up to about a minute). */
int smz_romm_rom_upload(SmzRomm *r, const char *file_name, const uint8_t *rom, size_t len, const char *md5, int *rom_id);
int smz_romm_rom_download(SmzRomm *r, int rom_id, const char *file_name, uint8_t **out, size_t *len);

/* Every note titled "smz3-run <id>" on the account's snes ROMs. */
typedef struct { int rom_id, note_id; char run_id[48], rom_name[96], updated[40]; char *content; } SmzRommNote;
int smz_romm_notes(SmzRomm *r, SmzRommNote **notes, int *count);
/* One ROM's run note by run id: 0 found, 1 none, -1 error. */
int smz_romm_note_get(SmzRomm *r, int rom_id, const char *run_id, SmzRommNote *n);
/* One ROM's run note by note id: 0 found, 1 none, -1 error. */
int smz_romm_note_by_id(SmzRomm *r, int rom_id, int note_id, SmzRommNote *n);
void smz_romm_note_free(SmzRommNote *n);
void smz_romm_notes_free(SmzRommNote *notes, int count);
/* Creates the note when *note_id is 0, else replaces its content. The note is private. */
int smz_romm_note_put(SmzRomm *r, int rom_id, int *note_id, const char *run_id, const char *content);
int smz_romm_note_delete(SmzRomm *r, int rom_id, int note_id);

/* The run's SRAM: a save with slot = run id. *save_id 0 creates it, else replaces it. */
int smz_romm_save_put(SmzRomm *r, int rom_id, const char *run_id, const char *file_name, const uint8_t *data, size_t len,
                      const char *md5, int *save_id);
/* A state with the labels run id and kind ("resume", "q1".."q4", "race"). *state_id 0 creates it, else replaces it. */
int smz_romm_state_put(SmzRomm *r, int rom_id, const char *run_id, const char *kind, const char *file_name,
                       const uint8_t *data, size_t len, int *state_id);
/* kind 's' a save, 't' a state: its bytes. 1 when RomM no longer has it. */
int smz_romm_file_get(SmzRomm *r, int kind, int id, uint8_t **out, size_t *len);
int smz_romm_file_delete(SmzRomm *r, int kind, int id);

/* Any request with the token, for the rest: path starts with "/api/". Returns the status; *out as SmzHttpFn's. */
int smz_romm_call(SmzRomm *r, const char *method, const char *path, const char *content_type, const uint8_t *body, size_t len,
                  uint8_t **out, size_t *out_len);

#endif
