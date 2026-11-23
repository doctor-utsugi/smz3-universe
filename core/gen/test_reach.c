/* Logic reachability as the tracker uses it (smz_reachable): real filled seeds, inventories built from what the seed placed.
 * Covers keysanity keys and keycards, dungeon rewards and Metroid bosses (both derived from the items by the logic). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_fill.h"
#include "smz3_generate.h"
#include "smz3_world_gen.h"
#include "smz3_itemnames.h"

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int is_key_item(int t)
{
    const char *n = NULL;
    for (int i = 0; i < SMZ_ITEM_NAME_COUNT; i++) if (SMZ_ITEM_NAMES[i].id == t) n = SMZ_ITEM_NAMES[i].name;
    if (!n) return 0;
    return !strncmp(n, "Key", 3) || !strncmp(n, "BigKey", 6) || !strncmp(n, "Card", 4);
}

/* Reachable count with every item of the seed except those rejected by skip(), plus the progression used. */
static int reach(const SmzSeed *seed, int (*skip)(int), SmzProg *prog)
{
    int items[1100], n = smz_assumed_items(seed, items, 600), out[600];
    for (int i = 0; i < SMZ_LOC_COUNT; i++) {
        int t = seed->world.item_at[SMZ_LOCS[i].id];
        if (t > 0 && !(skip && skip(t))) items[n++] = t;
    }
    return smz_reachable_prog(seed, items, n, out, 600, prog);
}

static int skip_keys(int t) { return is_key_item(t); }   /* the assumed keys and cards come back through smz_assumed_items */

int main(void)
{
    int total_all = 0;
    for (int seed_no = 1; seed_no <= 6; seed_no++) {
        for (int ks = 0; ks <= 3; ks++) {   /* None, Zelda, SuperMetroid, Both */
            SmzSettings s, eff;
            smz_settings_defaults(&s);
            s.seed = seed_no * 7919; s.keysanity = ks;
            static SmzSeed seed;
            if (smz_fill_valid(&seed, &s, &eff)) { printf("fill failed seed %d keysanity %d\n", seed_no, ks); return 1; }
            SmzProg all, nokeys, none;
            int n_all = reach(&seed, NULL, &all), n_nokeys = reach(&seed, skip_keys, &nokeys);
            int out[600];
            int n_none = smz_reachable_prog(&seed, NULL, 0, out, 600, &none);
            total_all = n_all;
            /* the whole seed is completable: with every placed item every location is in logic */
            CHECK(n_all >= SMZ_LOC_COUNT - 2, "seed %d keysanity %d: only %d of %d in logic with every item", seed_no, ks, n_all, SMZ_LOC_COUNT);
            if (ks == 0) CHECK(n_nokeys == n_all, "seed %d no keysanity: removing keys changed the count (%d vs %d)", seed_no, n_nokeys, n_all);
            else CHECK(n_nokeys < n_all, "seed %d keysanity %d: keys and cards must matter (%d vs %d)", seed_no, ks, n_nokeys, n_all);
            CHECK(n_none < n_all / 4, "seed %d keysanity %d: %d in logic with an empty inventory", seed_no, ks, n_none);
            /* dungeon rewards and bosses come from the logic: all of them with every item, none with nothing */
            int rewards_all = 0, rewards_none = 0, bosses_all = 0, bosses_none = 0;
            for (int r = 0; r < 16; r++) { rewards_all += all.rewards[r]; rewards_none += none.rewards[r]; }
            for (int b = 0; b < 24; b++) { bosses_all += all.bosses[b]; bosses_none += none.bosses[b]; }
            CHECK(rewards_all >= 10, "seed %d keysanity %d: only %d rewards with every item", seed_no, ks, rewards_all);
            CHECK(bosses_all == 4, "seed %d keysanity %d: %d metroid bosses with every item", seed_no, ks, bosses_all);
            CHECK(rewards_none <= 1 && bosses_none == 0, "seed %d keysanity %d: rewards %d bosses %d with nothing", seed_no, ks, rewards_none, bosses_none);
            /* monotonic: a one-item inventory never reaches fewer checks than none */
            int one[1] = { smz_item_by_name("Morph") }, k1 = smz_reachable(&seed, one, 1, out, 600);
            CHECK(k1 >= n_none, "seed %d keysanity %d: morph ball lost checks", seed_no, ks);
        }
    }
    printf(fails ? "%d failures\n" : "reach ok (%d locations)\n", fails ? fails : total_all);
    return fails != 0;
}
