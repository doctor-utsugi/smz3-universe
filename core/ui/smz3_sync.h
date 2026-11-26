/* The run sync (docs/PLAN_ROMM_SYNC.md): a run's ROM, SRAM, states and run file kept in RomM, the same on every device,
 * and the hashes and holds the SMZ3 server keeps so that only what SMZ3 itself wrote counts for the ladders. */
#ifndef SMZ3_SYNC_H
#define SMZ3_SYNC_H

#include <stddef.h>
#include <stdint.h>
#include "smz3_romm.h"

/* The files of a run. The first SMZ_SYNC_FILES go to RomM as a save (srm) or states; the rest travel in the run note
 * (run, help), are the ROM, or are this device's sync record. States are always snes9x 1.63 (#!s9xsnp:0014). */
enum { SMZ_SYNC_SRM, SMZ_SYNC_RESUME, SMZ_SYNC_Q1, SMZ_SYNC_Q2, SMZ_SYNC_Q3, SMZ_SYNC_Q4, SMZ_SYNC_RACE, SMZ_SYNC_FILES,
       SMZ_SYNC_RUN = SMZ_SYNC_FILES, SMZ_SYNC_HELP, SMZ_SYNC_ROM, SMZ_SYNC_RECORD, SMZ_SYNC_KINDS };
extern const char *const SMZ_SYNC_KIND_NAMES[SMZ_SYNC_FILES];   /* "srm", "resume", "q1".."q4", "race" */

/* The host's side: read returns a kind's local bytes, malloc'd (NULL when it has none); write replaces them (data NULL
 * removes the file). SMZ_SYNC_RUN is the run file's text without the local ROM path. */
typedef struct {
    uint8_t *(*read)(void *ctx, int kind, size_t *len);
    int (*write)(void *ctx, int kind, const uint8_t *data, size_t len);
    void *ctx;
} SmzSyncFiles;

/* This device's record of a run (SMZ_SYNC_RECORD): the revision it last synced, the RomM ids, and per file the hash it last
 * synced (base), the hash SMZ3's emulator last wrote (own) and the RomM id. */
typedef struct {
    int rev, rom_id, note_id, unranked;
    struct { char base[33], own[33]; int id; } f[SMZ_SYNC_FILES];
} SmzSyncRecord;
void smz_sync_record_load(const SmzSyncFiles *fs, SmzSyncRecord *rec);
int smz_sync_record_save(const SmzSyncFiles *fs, const SmzSyncRecord *rec);
/* Call whenever SMZ3's emulator writes a run file (kind < SMZ_SYNC_FILES): an upload whose hash differs goes up unranked. */
void smz_sync_own(const SmzSyncFiles *fs, int kind, const uint8_t *data, size_t len);

typedef struct {
    SmzRomm *romm;
    char server[128];      /* the SMZ3 server, "https://..." ("" skips the hash and hold calls) */
    char install[48];      /* this install's id: the server maps it to the player */
    char device[40];       /* this device's name, shown when a run changed in two places */
    int64_t now_ms;        /* the time now, Unix ms */
} SmzSync;

typedef struct {
    const char *run_id;    /* the run's id: its game letter and a UUID */
    const char *rom_name;  /* smz_run_rom_name */
    const char *name;      /* the run's name, for the file names RomM shows */
    int online;            /* a ladder, daily or race run: one device at a time */
    int64_t play_ms;
    const char *proof;     /* the run's proof so far (NULL none) */
    int rom_id;            /* a run listed from RomM and not on this device yet: its ROM's id there (smz_romm_notes) */
} SmzSyncRun;

enum { SMZ_SYNC_OK, SMZ_SYNC_CONFLICT, SMZ_SYNC_OFFLINE, SMZ_SYNC_HELD, SMZ_SYNC_FAILED };
typedef struct {
    int result;
    int changed;               /* pull: files on this device were replaced */
    int unranked;              /* pull: a file came from outside SMZ3: the run carries on unranked */
    char cloud_dev[40], err[160];
    int64_t cloud_at;          /* CONFLICT: who changed the run in RomM, and when */
    int64_t play_ms;           /* pull of an online run: the play time the server recorded with its resume state (0 none) */
} SmzSyncResult;

/* Uploads what changed on this device. When RomM moved on since this device's last sync and the device changed files too,
 * returns CONFLICT unless force (keep this device's). When only RomM moved on it pulls instead. */
int smz_sync_push(SmzSync *s, const SmzSyncRun *run, const SmzSyncFiles *fs, int force, SmzSyncResult *res);
/* Downloads what changed in RomM. CONFLICT when this device changed files too, unless force (keep RomM's). */
int smz_sync_pull(SmzSync *s, const SmzSyncRun *run, const SmzSyncFiles *fs, int force, SmzSyncResult *res);

/* A run's note as listed by smz_romm_notes, read into its parts: name, rom name, revision and run text (caller frees run_text). */
int smz_sync_note_info(const char *content, char *name, size_t name_cap, char *rom_name, size_t rom_cap, int *rev, char **run_text);
/* Makes a new run id for game letter ('s' SMZ3, 'z', 'm'). */
void smz_sync_new_id(char letter, uint64_t entropy, char out[40]);

/* The SMZ3 server's player and hold calls. A code is 6 digits. Each returns 0, or -1 with s->romm->err set; hold returns 1
 * when another device holds the run (holder gets its install id) and force takes it over. */
int smz_sync_player_code(SmzSync *s, char code[8]);
int smz_sync_player_link(SmzSync *s, const char *code);
int smz_sync_hold(SmzSync *s, const char *run_id, int force);
void smz_sync_release(SmzSync *s, const char *run_id);

#endif
