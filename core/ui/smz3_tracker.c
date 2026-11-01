#include <string.h>
#include "smz3_tracker.h"
#include "smz3_generate.h"

#define HOLD_MS 2000

static unsigned u8(const uint8_t *p, size_t len, size_t i) { return i < len ? p[i] : 0; }

static uint32_t hash_bytes(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

void smz_tracker_reset(SmzTracker *t, int64_t base_ms, int64_t save_ms, int save_zelda, int save_metroid)
{
    memset(t, 0, sizeof *t);
    t->base_ms = base_ms;
    t->save_ms = save_ms; t->save_zelda = save_zelda; t->save_metroid = save_metroid;
    t->last_frame = -1;
}

int64_t smz_tracker_pending(const SmzTracker *t) { return t->pending_ms; }

void smz_tracker_commit(SmzTracker *t)
{
    t->base_ms += t->pending_ms;
    t->pending_ms = 0;
}

int smz_boss_mask(const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len, int game)
{
    /* area byte (from $7ED828 / save RAM $2078) and flag of Kraid, Phantoon, Draygon, Ridley */
    static const int area[4] = {1, 3, 4, 2};
    uint8_t flags[8];
    if (game == 2) {
        if (wram_len < 0xD830) return 0;
        memcpy(flags, wram + 0xD828, 8);
    } else {
        if (sram_len < 0x2080) return 0;
        memcpy(flags, sram + 0x2078, 8);
    }
    int mask = 0;
    for (int i = 0; i < 4; i++) if (flags[area[i]] & 1) mask |= 1 << i;
    return mask;
}

int smz_prepared_apply(int dead_mask, uint8_t *wram, size_t wram_len, uint8_t *sram, size_t sram_len)
{
    static const int area[4] = {1, 3, 4, 2};
    if (!dead_mask || sram_len < 0x33FF) return 0;
    int changed = 0;
    if (sram[0x33FE] == 0xFF) {
        /* only in game (state 8 and on): while the file loads the game fills these bytes itself */
        if (wram_len < 0xD830 || (wram[0x998] | wram[0x999] << 8) < 8 || (wram[0x998] | wram[0x999] << 8) > 0x2A) return 0;
        for (int i = 0; i < 4; i++) if ((dead_mask >> i & 1) && !(wram[0xD828 + area[i]] & 1)) { wram[0xD828 + area[i]] |= 1; changed = 1; }
    } else if (sram[0x33FE] == 0x00 && sram_len >= 0x2080) {
        for (int i = 0; i < 4; i++) if ((dead_mask >> i & 1) && !(sram[0x2078 + area[i]] & 1)) { sram[0x2078 + area[i]] |= 1; changed = 1; }
    }
    return changed;
}

int smz_crystal_mask(const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len, int game)
{
    /* bit 0 of $7EF37A is crystal 6, then 1, 5, 7, 2, 4, 3 */
    static const int crystal_of_bit[7] = {6, 1, 5, 7, 2, 4, 3};
    unsigned bits;
    if (game == 2 || game == 3) { if (sram_len < 0x37B) return 0; bits = sram[0x37A]; }   /* parked while Super Metroid (or its credits) runs */
    else { if (wram_len < 0xF37B) return 0; bits = wram[0xF37A]; }
    int mask = 0;
    for (int b = 0; b < 7; b++) if (bits & (1u << b)) mask |= 1 << (crystal_of_bit[b] - 1);
    return mask;
}

static void push_front(int *list, int *n, int value, int dedupe)
{
    int at = *n;
    if (dedupe) for (int i = 0; i < *n; i++) if (list[i] == value) { at = i; break; }
    if (at == *n && *n < SMZ_TRK_RECENT) (*n)++;
    else if (at == *n) at = *n - 1;
    for (int i = at; i > 0; i--) list[i] = list[i - 1];
    list[0] = value;
}

void smz_tracker_tick(SmzTracker *t, const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len,
                      int dt_ms, int speed)
{
    t->now_ms += dt_ms;
    /* both games bump a frame counter every frame (ALttP $1A, Super Metroid $05B6): not moving means paused */
    int frame = wram_len > 0x5B7 ? (int)(wram[0x1A] | (wram[0x5B6] << 8)) : -1;
    t->running = t->have_frame && frame != t->last_frame;
    t->last_frame = frame; t->have_frame = 1;

    smz3_summary s;
    smz3_summarize(wram, wram_len, sram, sram_len, &s);
    static uint8_t cleared[SMZ_TRK_MAX_LOCS];
    memset(cleared, 0, sizeof cleared);
    if (s.started)
        for (unsigned i = 0; i < smz3_location_count && i < SMZ_TRK_MAX_LOCS; i++)
            cleared[i] = smz3_location_cleared(&smz3_locations[i], s.game, wram, wram_len, sram, sram_len) > 0;
    /* a transition frame can briefly read as not started: hold the last live state */
    if (s.started) {
        t->live = s; memcpy(t->live_cleared, cleared, sizeof cleared); t->live_at_ms = t->now_ms; t->have_live = 1;
    } else if (t->have_live && t->now_ms - t->live_at_ms < HOLD_MS) {
        s = t->live; memcpy(cleared, t->live_cleared, sizeof cleared);
    }
    int started = s.started != 0;

    t->save_changed = 0;
    uint32_t h = hash_bytes(sram, sram_len < 0x100000 ? sram_len : 0x100000);
    if (t->have_hash && h != t->sram_hash && started) {
        smz3_summary saved;
        smz3_summarize(NULL, 0, sram, sram_len, &saved);
        t->save_ms = t->base_ms + t->pending_ms;
        t->save_zelda = (int)saved.zelda_cleared; t->save_metroid = (int)saved.metroid_cleared;
        t->save_changed = 1;
    }
    t->sram_hash = h; t->have_hash = 1;

    if (t->seen_valid || t->n_recent_checks || started)
        for (unsigned i = 0; i < smz3_location_count && i < SMZ_TRK_MAX_LOCS; i++)
            if (cleared[i] && !t->cleared[i]) push_front(t->recent_checks, &t->n_recent_checks, (int)i, 0);
    memcpy(t->cleared, cleared, sizeof cleared);
    t->seen_valid = 1;

    int inv[SMZ_TRK_MAX_INV];
    unsigned ni = smz3_inventory_count < SMZ_TRK_MAX_INV ? smz3_inventory_count : SMZ_TRK_MAX_INV;
    smz3_inventory(wram, wram_len, sram, sram_len, inv);
    for (unsigned i = 0; i < ni; i++) if (inv[i] >= 0) {
        /* an item picked up since the last read goes to the recent list; the first read of a run is the baseline */
        if (started && t->inv_baseline && inv[i] > t->inventory[i]) push_front(t->recent_items, &t->n_recent_items, (int)i, 1);
        t->inventory[i] = inv[i];
    }
    t->inv_baseline = started;
    if (t->state_loaded > 0 && started) {   /* a few reads, in case the first one lands on a transition frame */
        t->state_loaded--;
        int n = 0;
        for (int i = 0; i < t->n_recent_checks; i++) if (cleared[t->recent_checks[i]]) t->recent_checks[n++] = t->recent_checks[i];
        t->n_recent_checks = n;
        n = 0;
        for (int i = 0; i < t->n_recent_items; i++) if (t->inventory[t->recent_items[i]] > 0) t->recent_items[n++] = t->recent_items[i];
        t->n_recent_items = n;
    }

    int was_over = t->game == SMZ3_GAME_CREDITS;   /* the read that sees the end still counts the time up to it */
    /* an end only counts on a run already seen started, never on the read where memory first looks like a game */
    int may_end = t->started && started && t->base_ms + t->pending_ms >= t->min_end_ms;
    if (may_end && t->goal.kind != GOAL_NONE && smz_goal_done(&t->goal, wram, wram_len, sram, sram_len)) t->goal_met = 1;
    t->game = (s.finished && may_end) || t->goal_met ? SMZ3_GAME_CREDITS : s.game; t->started = started;   /* game 3 tells the hosts the run is over */
    t->zelda_cleared = s.zelda_cleared; t->zelda_total = s.zelda_total;
    t->metroid_cleared = s.metroid_cleared; t->metroid_total = s.metroid_total;
    if (!started) t->boss_mask = t->crystal_mask = 0;
    else if (s.game == SMZ3_GAME_ZELDA || s.game == SMZ3_GAME_METROID) {
        t->boss_mask = smz_boss_mask(wram, wram_len, sram, sram_len, s.game);
        t->crystal_mask = smz_crystal_mask(wram, wram_len, sram, sram_len, s.game);
    } else {   /* the credits: both games are parked in the save RAM, and nothing is lost at the end */
        t->boss_mask |= smz_boss_mask(wram, wram_len, sram, sram_len, s.game);
        t->crystal_mask |= smz_crystal_mask(wram, wram_len, sram, sram_len, s.game);
    }
    if (started && t->running && !was_over) t->pending_ms += (int64_t)dt_ms * (speed > 0 ? speed : 1);
    t->elapsed_ms = started ? t->base_ms + t->pending_ms : 0;
}

typedef struct { const char *name, *type; int per; } ItemMap;

/* inventory name -> ItemType name; per is 0 for a flag, 1 to repeat per level, N>1 for a count that is level / N */
static const ItemMap item_map[] = {
    {"Sword", "ProgressiveSword", 1}, {"Shield", "ProgressiveShield", 1}, {"Mail", "ProgressiveTunic", 1},
    {"Gloves", "ProgressiveGlove", 1}, {"Bow", "Bow", 0}, {"Silver Arrows", "SilverArrows", 0},
    {"Blue Boomerang", "BlueBoomerang", 0}, {"Red Boomerang", "RedBoomerang", 0}, {"Hookshot", "Hookshot", 0},
    {"Fire Rod", "Firerod", 0}, {"Ice Rod", "Icerod", 0}, {"Bombos", "Bombos", 0}, {"Ether", "Ether", 0},
    {"Quake", "Quake", 0}, {"Lamp", "Lamp", 0}, {"Hammer", "Hammer", 0}, {"Bug Net", "Bugnet", 0},
    {"Book", "Book", 0}, {"Somaria", "Somaria", 0}, {"Byrna", "Byrna", 0}, {"Cape", "Cape", 0},
    {"Mirror", "Mirror", 0}, {"Boots", "Boots", 0}, {"Flippers", "Flippers", 0}, {"Moon Pearl", "MoonPearl", 0},
    {"Powder", "Powder", 0}, {"Mushroom", "Mushroom", 0}, {"Shovel", "Shovel", 0}, {"Flute", "Flute", 0},
    {"Bottles", "Bottle", 1}, {"Half Magic", "HalfMagic", 0},
    {"Varia", "Varia", 0}, {"Gravity", "Gravity", 0}, {"Morph Ball", "Morph", 0}, {"Bombs", "Bombs", 0},
    {"Spring Ball", "SpringBall", 0}, {"Screw Attack", "ScrewAttack", 0}, {"Hi-Jump", "HiJump", 0},
    {"Space Jump", "SpaceJump", 0}, {"Speed Booster", "SpeedBooster", 0}, {"Grapple", "Grapple", 0},
    {"X-Ray", "XRay", 0}, {"Charge", "Charge", 0}, {"Ice", "Ice", 0}, {"Wave", "Wave", 0},
    {"Spazer", "Spazer", 0}, {"Plasma", "Plasma", 0},
    {"Energy Tanks", "ETank", 1}, {"Reserve", "ReserveTank", 100}, {"Missiles", "Missile", 5},
    {"Super Missiles", "Super", 5}, {"Power Bombs", "PowerBomb", 5},
};

static int add(int *out, int n, int cap, const char *type, int count)
{
    int id = smz_item_by_name(type);
    if (id <= 0) return n;
    for (int i = 0; i < count && n < cap; i++) out[n++] = id;
    return n;
}

static const char *const small_keys[11] = {"KeyDP", "KeyCT", "KeySP", "KeyPD", "KeyMM", "KeySW", "KeyIP", "KeyTH", "KeyTT", "KeyTR", "KeyGT"};
static const char *const big_keys[11] = {"BigKeyGT", "BigKeyTR", "BigKeyTT", "BigKeyTH", "BigKeyIP", "BigKeySW", "BigKeyMM", "BigKeyPD", "BigKeySP", "BigKeyDP", "BigKeyEP"};
static const int big_off[11] = {0x366, 0x366, 0x366, 0x366, 0x366, 0x366, 0x367, 0x367, 0x367, 0x367, 0x367};
static const int big_mask[11] = {0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x01, 0x02, 0x04, 0x10, 0x20};
static const char *const cards[16] = {
    "CardCrateriaL1", "CardCrateriaL2", "CardCrateriaBoss", "CardBrinstarL1", "CardBrinstarL2", "CardBrinstarBoss",
    "CardNorfairL1", "CardNorfairL2", "CardNorfairBoss", "CardMaridiaL1", "CardMaridiaL2", "CardMaridiaBoss",
    "CardWreckedShipL1", "CardWreckedShipBoss", "CardLowerNorfairL1", "CardLowerNorfairBoss"};

static int key_count(unsigned v) { return v == 0xFF ? 0 : v > 8 ? 8 : (int)v; }

int smz_tracker_logic_items(const SmzTracker *t, const uint8_t *wram, size_t wl, const uint8_t *sram, size_t sl,
                            int keysanity, int *out, int cap)
{
    int n = 0;
    unsigned count = smz3_inventory_count < SMZ_TRK_MAX_INV ? smz3_inventory_count : SMZ_TRK_MAX_INV;
    for (unsigned i = 0; i < count; i++) {
        int level = t->inventory[i];
        if (level <= 0) continue;
        for (unsigned k = 0; k < sizeof item_map / sizeof item_map[0]; k++) {
            if (strcmp(item_map[k].name, smz3_inventory_items[i].name)) continue;
            int per = item_map[k].per;
            n = add(out, n, cap, item_map[k].type, per == 0 ? 1 : level / per);
            break;
        }
    }
    int zelda_keys = keysanity == 1 || keysanity == 3, metroid_keys = keysanity == 2 || keysanity == 3;
    int game = (int)t->game;
    if (zelda_keys && (game == 1 || game == 2)) {
        const uint8_t *st = game == 1 ? wram + 0xF000 : sram;
        size_t sn = game == 1 ? (wl > 0xF000 ? wl - 0xF000 : 0) : sl;
        const uint8_t *kp = game == 1 ? wram + 0xF000 + 0x4E0 : sram + 0x3F50;
        size_t kn = game == 1 ? (wl > 0xF4E0 ? wl - 0xF4E0 : 0) : (sl > 0x3F50 ? sl - 0x3F50 : 0);
        for (int i = 0; i < 11; i++) if (u8(st, sn, big_off[i]) & big_mask[i]) n = add(out, n, cap, big_keys[i], 1);
        int hc = key_count(u8(kp, kn, 0)) + key_count(u8(kp, kn, 1));
        n = add(out, n, cap, "KeyHC", hc > 8 ? 8 : hc);
        for (int i = 0; i < 11; i++) n = add(out, n, cap, small_keys[i], key_count(u8(kp, kn, 3 + i)));
    }
    if (metroid_keys && (game == 1 || game == 2)) {
        const uint8_t *c = game == 2 ? wram + 0xD830 : sram + 0x3970;
        size_t cn = game == 2 ? (wl > 0xD830 ? wl - 0xD830 : 0) : (sl > 0x3970 ? sl - 0x3970 : 0);
        unsigned bits = u8(c, cn, 0) | (u8(c, cn, 1) << 8);
        for (int i = 0; i < 16; i++) if (bits & (1u << i)) n = add(out, n, cap, cards[i], 1);
    }
    return n;
}

/* Read from the game's pot items (bank 01, item 8) and room sprites (an 0xE4 entry at y 0xFE after the enemy that drops it,
 * 0xFD for a big key). */
const SmzFixedKey smz_fixed_keys[] = {
    {0x16, 56, 39, 0}, {0x35, 30, 12, 0}, {0x36, 57, 32, 0}, {0x37, 30, 12, 0}, {0x38, 18, 39, 0}, {0x3F, 14, 46, 0},
    {0x43, 56, 40, 0}, {0x53, 50, 22, 0}, {0x56, 6, 40, 0}, {0x63, 6, 24, 0}, {0x7B, 32, 8, 0}, {0x8B, 56, 24, 0},
    {0x9B, 24, 24, 0}, {0x9F, 5, 43, 0}, {0xA1, 11, 13, 0}, {0xAB, 10, 48, 0}, {0xB3, 6, 40, 0}, {0xBA, 56, 12, 0},
    {0xBC, 51, 8, 0},
    {0x0E, 10, 52, 0}, {0x13, 44, 48, 0}, {0x21, 10, 12, 0}, {0x39, 10, 42, 0}, {0x3D, 46, 14, 0}, {0x3E, 34, 48, 0},
    {0x71, 52, 48, 0}, {0x72, 34, 12, 0}, {0x80, 52, 18, 1}, {0x99, 34, 46, 0}, {0xB0, 16, 48, 0}, {0xB6, 14, 42, 0},
    {0xC0, 40, 22, 0}, {0xC1, 38, 54, 0},
};
const int smz_fixed_key_count = sizeof smz_fixed_keys / sizeof smz_fixed_keys[0];

int smz_fixed_key_taken(int i, const uint8_t *flags, size_t flags_len, const uint8_t *wram, size_t wram_len)
{
    if (i < 0 || i >= smz_fixed_key_count) return 1;
    unsigned room = smz_fixed_keys[i].room;
    if (flags && flags_len > room * 2 + 1 && (flags[room * 2 + 1] & 0x04)) return 1;   /* bit 10 of the room's word */
    /* the room Link is in keeps its state at 0x0402 until he leaves it (bit 14 there) */
    if (wram && wram_len > 0x404 && wram[0x1B] && (unsigned)(wram[0xA0] | wram[0xA1] << 8) == room && (wram[0x403] & 0x40)) return 1;
    return 0;
}
