/* Where the PC frontend keeps its files, and the base ROMs the generator needs. */
#ifndef PC_STORE_H
#define PC_STORE_H

#include <stddef.h>
#include <stdint.h>
#include "smz3_run.h"
#include "smz3_ui.h"

/* Data folder: "<exe folder>/data" when a file named portable.txt sits beside the program, else ~/.local/share/smz3. */
const char *store_dir(void);
const char *store_exe_dir(void);
/* "<dir>/<rel>" in a static buffer, one per thread (the folders are created on demand). */
const char *store_path(const char *fmt, ...);

/* Paths of the two base ROMs, kept in config.txt. */
void store_roms_get(char sm[512], char z3[512]);
void store_roms_set(const char *sm, const char *z3);
/* Loads and checks the base ROMs (3 MB Super Metroid JU, 1 MB ALttP JP v1.0). Returns 0 or -1 with the reason in err. */
int store_load_base(uint8_t **sm, uint8_t **z3, char *err, size_t errcap);

uint8_t *store_read(const char *path, size_t *len);
int store_write(const char *path, const void *data, size_t len);

/* The embedded IPS patch the generator asks for. */
const uint8_t *store_ips(void *user, const char *name, size_t *len);

/* Fills the menu's slots from the run files. */
void store_fill_slots(SmzUi *u);
int store_run_load(int slot, SmzRun *r);
int store_run_save(int slot, const SmzRun *r);
void store_run_delete(int slot);
/* First free slot or -1. */
int store_free_slot(void);

#endif
