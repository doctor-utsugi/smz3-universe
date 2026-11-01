/* In-game dialogs: the Zelda string table and the texts that depend on the seed. */
#ifndef SMZ3_TEXT_H
#define SMZ3_TEXT_H

#include <stddef.h>
#include "smz3_fill.h"
#include "smz3_hints.h"

#define SMZ_TEXT_TABLE_SIZE 0x7355

/* Compiles dialog markup (UTF-8) into game bytes. Returns the byte count, or -1 on an unknown command. */
int smz_dialog_compiled(const char *text, uint8_t *out, int cap);
/* The simple 3 row format used by the fixed text slots. */
int smz_dialog_simple(const char *text, uint8_t *out, int cap);
/* Wraps text to the 19 columns of the dialog box. */
void smz_game_safe_string(const char *text, char *out, size_t cap);

typedef void (*SmzTextEmit)(void *user, uint32_t snes_address, const uint8_t *data, int n);

/* Emits every text patch of the seed (fixed slots and the whole string table). Text variants are picked from
 * the seed so the same seed gives the same texts. */
int smz_text_patches(const SmzSeed *seed, const SmzSettings *s, const SmzHints *hints, SmzTextEmit emit, void *user);

/* Plando text overrides (PlandoTextConfig): index = the property's position there. */
enum { SMZ_TXT_GanonIntro, SMZ_TXT_GanonIntroGoalsNotMet, SMZ_TXT_BlindIntro, SMZ_TXT_TriforceRoom, SMZ_TXT_KingZora, SMZ_TXT_BottleMerchant,
       SMZ_TXT_GanonSilversHint, SMZ_TXT_GanonsTowerGoalSign, SMZ_TXT_GanonGoalSign, SMZ_TXT_SahasrahlaReveal, SMZ_TXT_BombShopReveal,
       SMZ_TXT_TavernMan, SMZ_TXT_MasterSwordPedestal, SMZ_TXT_EtherTablet, SMZ_TXT_BombosTablet,
       SMZ_TXT_HintTile0,              /* then one per hint tile, in the order of SMZ_HINT_TILES */
       SMZ_TXT_COUNT = SMZ_TXT_HintTile0 + 15 };
typedef struct { const char *v[SMZ_TXT_COUNT]; } SmzTextOverrides;
/* The same with overrides: a non-empty entry replaces the generated text of that slot. ov may be NULL. */
int smz_text_patches_ex(const SmzSeed *seed, const SmzSettings *s, const SmzHints *hints, const SmzTextOverrides *ov, SmzTextEmit emit, void *user);

/* What a hint says, as the hint tiles show it (wording picked from the seed and the hint). */
void smz_hint_text(const SmzSeed *seed, const SmzHint *hint, unsigned wording, char *out, size_t cap);

#define SMZ_HELP_COUNT 15
#define SMZ_HELP_TEXT 320
/* The hints a player can ask for in the limited help mode, in the order they are given: the same hints the hint tiles
 * show in the all helps mode. Each text is a NUL terminated string; costs a few playthrough simulations. */
void smz_help_texts(const SmzSeed *seed, const SmzPatchCfg *cfg, char out[SMZ_HELP_COUNT][SMZ_HELP_TEXT]);

#endif
