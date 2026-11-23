/* Logic from a run's stored settings: the chests reachable with nothing, then with a morph ball. Build like test_tracker with smz3_logic.c. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "smz3_runlogic.h"
#include "smz3_tracker.h"
#include "smz3_ui.h"
#include "smz3_ui_gen.h"

int main(void)
{
    static SmzUi u;
    ui_init(&u);
    ui_preset(&u, 1);
    u.value[0] = 4242;
    char json[4096];
    assert(ui_settings_json(&u, 1, json, sizeof json) > 0);
    static SmzLogic l;
    assert(smz_logic_build(&l, json) == 0);
    uint8_t none[512], some[512];
    int a = smz_logic_reachable(&l, NULL, 0, none, 512);
    int morph = smz_item_by_name("Morph");
    int items[3] = {morph, smz_item_by_name("Bombs"), smz_item_by_name("Boots")};
    int b = smz_logic_reachable(&l, items, 3, some, 512);
    printf("reachable with nothing %d, with morph+bombs+boots %d of %u\n", a, b, smz3_location_count);
    assert(a > 0 && b >= a && b < (int)smz3_location_count);
    for (int i = 0; i < 512; i++) assert(!none[i] || some[i]);
    printf("logic ok\n");
    return 0;
}
