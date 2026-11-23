/* Build with the generated map data: cc ... core/ui/test_discovery.c core/ui/smz3_discovery.c build/pc/map_data.c -Ifrontends/pc */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "smz3_discovery.h"
#include "pc_assets.h"

static uint8_t w[0x20000];

int main(void)
{
    static SmzDiscovery d;
    smz_disc_init(&d, &PC_MAP_DATA);
    assert(PC_MAP_DATA.n_markers == 316 && d.has_pedestal && d.has_zora);

    /* ALttP overworld, light world: screen from $8A and Link's own screen */
    w[0x8A] = 0x1B; w[0x22] = 1000 & 255; w[0x23] = 1000 >> 8; w[0x20] = 600 & 255; w[0x21] = 600 >> 8;
    smz_disc_update(&d, w, sizeof w, 1);
    assert(d.has_position && fabsf(d.x - 1000 * 958.0f / 4096 / 2) < 0.01f && fabsf(d.y - 600 * 958.0f / 4096 / 2) < 0.01f);
    assert(smz_disc_count(&d) == 2 && d.dirty);
    /* dark world shifts the map down by half */
    w[0xF3CA] = 0x40;
    smz_disc_update(&d, w, sizeof w, 1);
    assert(d.y > 479);

    /* a dungeon: Eastern Palace */
    w[0x1B] = 1; w[0x040C] = 0x04;
    smz_disc_update(&d, w, sizeof w, 1);
    assert(d.dungeons & 2);
    SmzRect rects[300];
    int n = smz_disc_rects(&d, rects, 300);
    assert(n == 5);

    /* Super Metroid: the room must be seen in two samples in normal gameplay */
    const SmzSmRoom *r = &PC_MAP_DATA.rooms[3];
    w[0x998] = 8; w[0x79B] = r->ptr & 255; w[0x79C] = r->ptr >> 8;
    int before = d.n_rooms;
    smz_disc_update(&d, w, sizeof w, 2);
    assert(d.n_rooms == before);
    smz_disc_update(&d, w, sizeof w, 2);
    assert(d.n_rooms == before + 1 && d.area == r->area);
    SmzRect box;
    assert(smz_disc_area_box(&d, 2, &box) && box.w > 100);

    /* Tourian (area 5) is placed too: its rooms count and Samus has a position there */
    const SmzSmRoom *tr = NULL;
    for (int i = 0; i < PC_MAP_DATA.n_rooms; i++) if (PC_MAP_DATA.rooms[i].area == 5) { tr = &PC_MAP_DATA.rooms[i]; break; }
    assert(tr);
    w[0x79B] = tr->ptr & 255; w[0x79C] = tr->ptr >> 8; w[0x7A1] = (uint8_t)tr->mx; w[0x7A3] = (uint8_t)tr->my;   /* Samus in the room's top-left tile */
    before = d.n_rooms;
    smz_disc_update(&d, w, sizeof w, 2);
    smz_disc_update(&d, w, sizeof w, 2);
    assert(d.n_rooms == before + 1 && d.area == 5 && d.has_position);
    assert(d.x > 600 && d.x < 800 && d.y > 150 && d.y < 420);   /* under Crateria on the half-size map */
    assert(smz_disc_area_box(&d, 2, &box) && box.w > 100);

    /* save and load keep the sets */
    char text[4096];
    assert(smz_disc_save(&d, text, sizeof text) > 0);
    static SmzDiscovery e;
    smz_disc_init(&e, &PC_MAP_DATA);
    smz_disc_load(&e, text);
    assert(smz_disc_count(&e) == smz_disc_count(&d) && e.dungeons == d.dungeons && !e.dirty);
    printf("discovery ok\n");
    return 0;
}
