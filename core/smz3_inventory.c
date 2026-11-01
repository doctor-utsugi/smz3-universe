#include "smz3_state.h"

/* Addresses follow the Cas' tracker autotracker (ZeldaMiscData.cs, MetroidStateChecks.cs). */
#define Z(name, icon, kind, addr, flag) {name, icon, SMZ3_GAME_ZELDA, kind, addr, flag}
#define M(name, icon, kind, addr, flag) {name, icon, SMZ3_GAME_METROID, kind, addr, flag}

const smz3_inv_item smz3_inventory_items[] = {
    Z("Sword", "sword.png", SMZ3_INV_BYTE, 0x359, 0),
    Z("Shield", "shield.png", SMZ3_INV_BYTE, 0x35A, 0),
    Z("Mail", "mail.png", SMZ3_INV_BYTE, 0x35B, 0),
    Z("Gloves", "gloves.png", SMZ3_INV_BYTE, 0x354, 0),
    Z("Bow", "bow.png", SMZ3_INV_ONE, 0x340, 0),
    Z("Silver Arrows", "silver arrows.png", SMZ3_INV_FLAG, 0x38E, 0x40),
    Z("Blue Boomerang", "blue boomerang.png", SMZ3_INV_FLAG, 0x38C, 0x80),
    Z("Red Boomerang", "red boomerang.png", SMZ3_INV_FLAG, 0x38C, 0x40),
    Z("Hookshot", "hookshot.png", SMZ3_INV_ONE, 0x342, 0),
    Z("Fire Rod", "fire rod.png", SMZ3_INV_ONE, 0x345, 0),
    Z("Ice Rod", "ice rod.png", SMZ3_INV_ONE, 0x346, 0),
    Z("Bombos", "bombos.png", SMZ3_INV_ONE, 0x347, 0),
    Z("Ether", "ether.png", SMZ3_INV_ONE, 0x348, 0),
    Z("Quake", "quake.png", SMZ3_INV_ONE, 0x349, 0),
    Z("Lamp", "lamp.png", SMZ3_INV_ONE, 0x34A, 0),
    Z("Hammer", "hammer.png", SMZ3_INV_ONE, 0x34B, 0),
    Z("Bug Net", "bug catching net.png", SMZ3_INV_ONE, 0x34D, 0),
    Z("Book", "book of mudora.png", SMZ3_INV_ONE, 0x34E, 0),
    Z("Somaria", "cane of somaria.png", SMZ3_INV_ONE, 0x350, 0),
    Z("Byrna", "cane of byrna.png", SMZ3_INV_ONE, 0x351, 0),
    Z("Cape", "magic cape.png", SMZ3_INV_ONE, 0x352, 0),
    Z("Mirror", "magic mirror.png", SMZ3_INV_ONE, 0x353, 0),
    Z("Boots", "pegasus boots.png", SMZ3_INV_ONE, 0x355, 0),
    Z("Flippers", "zora's flippers.png", SMZ3_INV_ONE, 0x356, 0),
    Z("Moon Pearl", "moon pearl.png", SMZ3_INV_ONE, 0x357, 0),
    Z("Powder", "magic powder.png", SMZ3_INV_FLAG, 0x38C, 0x10),
    Z("Mushroom", "mushroom.png", SMZ3_INV_FLAG, 0x38C, 0x20),
    Z("Shovel", "shovel.png", SMZ3_INV_FLAG, 0x38C, 0x04),
    Z("Flute", "flute.png", SMZ3_INV_FLAG, 0x38C, 0x03),
    Z("Bottles", "bottle.png", SMZ3_INV_BOTTLES, 0x35C, 0),
    Z("Half Magic", "half magic.png", SMZ3_INV_ONE, 0x37B, 0),
    Z("Hearts", "heart container.png", SMZ3_INV_HEARTS, 0x36C, 0),
    Z("Heart Pieces", "heart piece.png", SMZ3_INV_BYTE, 0x36B, 0),

    M("Varia", "varia suit.png", SMZ3_INV_FLAG, 0x9A4, 0x01),
    M("Gravity", "gravity suit.png", SMZ3_INV_FLAG, 0x9A4, 0x20),
    M("Morph Ball", "morphing ball.png", SMZ3_INV_FLAG, 0x9A4, 0x04),
    M("Bombs", "morph bombs.png", SMZ3_INV_FLAG, 0x9A5, 0x10),
    M("Spring Ball", "spring ball.png", SMZ3_INV_FLAG, 0x9A4, 0x02),
    M("Screw Attack", "screw attack.png", SMZ3_INV_FLAG, 0x9A4, 0x08),
    M("Hi-Jump", "hi-jump boots.png", SMZ3_INV_FLAG, 0x9A5, 0x01),
    M("Space Jump", "space jump.png", SMZ3_INV_FLAG, 0x9A5, 0x02),
    M("Speed Booster", "speed booster.png", SMZ3_INV_FLAG, 0x9A5, 0x20),
    M("Grapple", "grappling beam.png", SMZ3_INV_FLAG, 0x9A5, 0x40),
    M("X-Ray", "x-ray scope.png", SMZ3_INV_FLAG, 0x9A5, 0x80),
    M("Charge", "charge beam.png", SMZ3_INV_FLAG, 0x9A9, 0x10),
    M("Ice", "ice beam.png", SMZ3_INV_FLAG, 0x9A8, 0x02),
    M("Wave", "wave beam.png", SMZ3_INV_FLAG, 0x9A8, 0x01),
    M("Spazer", "spazer.png", SMZ3_INV_FLAG, 0x9A8, 0x04),
    M("Plasma", "plasma beam.png", SMZ3_INV_FLAG, 0x9A8, 0x08),
    M("Energy Tanks", "energy tank.png", SMZ3_INV_ENERGY_TANKS, 0x9C4, 0),
    M("Reserve", "reserve tank.png", SMZ3_INV_WORD, 0x9D4, 0),
    M("Missiles", "missile.png", SMZ3_INV_WORD, 0x9C8, 0),
    M("Super Missiles", "super missile.png", SMZ3_INV_WORD, 0x9CC, 0),
    M("Power Bombs", "power bomb.png", SMZ3_INV_WORD, 0x9D0, 0),
};

const unsigned smz3_inventory_count = sizeof smz3_inventory_items / sizeof smz3_inventory_items[0];

static int read_byte(const uint8_t *p, size_t len, size_t off, unsigned *out)
{
    if (!p || off >= len) return 0;
    *out = p[off];
    return 1;
}

void smz3_inventory(const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len,
                    int *levels)
{
    smz3_game cur = smz3_current_game(sram, sram_len);
    int live = smz3_started(wram, wram_len, sram, sram_len) && cur != SMZ3_GAME_NONE;

    for (unsigned i = 0; i < smz3_inventory_count; i++) {
        const smz3_inv_item *it = &smz3_inventory_items[i];
        const uint8_t *base;
        size_t len, off;
        levels[i] = -1;
        if (!live) continue;

        if (it->game == SMZ3_GAME_ZELDA) {
            if (cur == SMZ3_GAME_ZELDA) {
                base = wram + 0xF000; len = wram_len > 0xF000 ? wram_len - 0xF000 : 0;
            } else {
                base = sram; len = sram_len;
            }
            off = it->addr;
        } else {
            if (cur == SMZ3_GAME_METROID) {
                base = wram; len = wram_len; off = it->addr;
            } else {
                /* While Zelda runs the Super Metroid save-slot copy is parked in SRAM; $09A2 sits at +0x2010. */
                base = sram; len = sram_len; off = (size_t)it->addr + 0x166E;
            }
        }

        unsigned v = 0, v2 = 0;
        if (!read_byte(base, len, off, &v)) continue;
        switch (it->kind) {
        case SMZ3_INV_BYTE: levels[i] = v == 0xFF ? 0 : (int)v; break;
        case SMZ3_INV_ONE: levels[i] = v != 0; break;
        case SMZ3_INV_FLAG: levels[i] = (v & it->flag) != 0; break;
        case SMZ3_INV_BOTTLES: {
            int n = 0;
            for (unsigned b = 0; b < 4; b++)
                if (read_byte(base, len, off + b, &v2) && v2 != 0) n++;
            levels[i] = n;
            break;
        }
        case SMZ3_INV_HEARTS: levels[i] = (int)(v / 8); break;
        case SMZ3_INV_WORD:
            if (!read_byte(base, len, off + 1, &v2)) continue;
            levels[i] = (int)(v | (v2 << 8));
            break;
        case SMZ3_INV_ENERGY_TANKS: {
            if (!read_byte(base, len, off + 1, &v2)) continue;
            int e = (int)(v | (v2 << 8));
            levels[i] = e >= 99 ? (e - 99) / 100 : 0;
            break;
        }
        }
    }
}
