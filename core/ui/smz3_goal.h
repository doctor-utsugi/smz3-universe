/* The goal of a daily challenge, read from game memory. smz3daily writes it as a short text in the run settings ("goal"):
 *   loc:<n>       location n of smz3_locations is cleared (the goal item was picked up)
 *   zboss:<hex>   the ALttP boss of that room is beaten (bit 11 of its room word, as the SMZ3 tracker reads it)
 *   aga           Agahnim is beaten (progress byte $7EF3C5 >= 3)
 *   smboss:<n>    Super Metroid boss n is beaten (0 Kraid, 1 Phantoon, 2 Draygon, 3 Ridley)
 *   crystals:<n>  at least n crystals
 *   smbosses:<n>  at least n of the four Super Metroid bosses
 *   zdungeon:<hex> Link is inside that dungeon ($7E040C)
 *   smarea:<n>    Samus is in that area ($7E079F: 0 Crateria, 1 Brinstar, 2 Norfair, 3 Wrecked Ship, 4 Maridia, 5 Tourian) */
#ifndef SMZ3_GOAL_H
#define SMZ3_GOAL_H

#include <stddef.h>
#include <stdint.h>

typedef enum { GOAL_NONE, GOAL_LOC, GOAL_ZBOSS, GOAL_AGA, GOAL_SMBOSS, GOAL_CRYSTALS, GOAL_SMBOSSES, GOAL_ZDUNGEON, GOAL_SMAREA } SmzGoalKind;

typedef struct { SmzGoalKind kind; int value; } SmzGoal;

/* Parses the text form; returns 0, or -1 (and GOAL_NONE) when it is not a goal. */
int smz_goal_parse(const char *text, SmzGoal *g);
/* The "goal" field of a run's settings JSON, parsed; GOAL_NONE when the run has none. */
int smz_goal_from_settings(const char *settings_json, SmzGoal *g);
/* 1 when the goal is met in this memory. */
int smz_goal_done(const SmzGoal *g, const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len);

#endif
