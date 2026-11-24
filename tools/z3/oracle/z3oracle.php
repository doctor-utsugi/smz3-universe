<?php
// Oracle CLI for the C port in core/z3gen/, run inside the container next to alttp_php_randomizer (see Dockerfile).
// Verbs mirror tools/gen-cli; everything a C test compares is printed by name (items, locations, regions, bosses).
//   php z3oracle.php schema <out.json>
//   php z3oracle.php vectors <count> <seed> <out.txt>          ItemCollection helpers and Boss::canBeat on random inventories
//                                                             (Z3_PROFILE=next: draw the step 8 options too)
//   php z3oracle.php worldvectors <count> <seed> <out.txt>     Location::canAccess, Region::canEnter/canComplete per case
//   php z3oracle.php placementbatch <cases.jsonl> <out.txt>    one {"seed":n, ...settings} per line, full Randomizer run
//   php z3oracle.php checkplacement <cases.jsonl> <out.txt>    {"settings":{..},"placement":{loc:item}}: winnable + reachable
//   php z3oracle.php placementrom <base.sfc> <case.json> <out.sfc>  a seeded prepSeed run, or writeToRom for a given placement
//                                                             (boss shuffle runs the Enemizer with the case's "enemizer_seed")
//   php z3oracle.php rombatch <base.sfc> <cases.jsonl> <outdir>      placementrom per line, <outdir>/<n>.sfc
//   php z3oracle.php textdata <out.json>                         data for tools/z3/gen_z3_text.py
// alttpr draws from random_int (a CSPRNG). The Dockerfile patches get_random_int() so that, once oracle_seed() has run,
// it draws from mt_rand seeded with mt_srand(seed, MT_RAND_MT19937) instead. Only the oracle is patched; with no seed
// the generator behaves as upstream.

use ALttP\Boss;
use ALttP\Item;
use ALttP\Location;
use ALttP\Randomizer;
use ALttP\Rom;
use ALttP\Support\ItemCollection;
use ALttP\Support\WorldCollection;
use ALttP\World;

// Paths on the command line are relative to the caller's directory (/work); Laravel needs to run from __DIR__.
$GLOBALS['Z3_CWD'] = getcwd();
function wp(string $p): string
{
    return $p !== '' && $p[0] === '/' ? $p : $GLOBALS['Z3_CWD'] . '/' . $p;
}
chdir(__DIR__);
require __DIR__ . '/vendor/autoload.php';
$app = require __DIR__ . '/bootstrap/app.php';
$app->make(Illuminate\Contracts\Console\Kernel::class)->bootstrap();
ini_set('memory_limit', '1G');

function oracle_seed(int $seed): void
{
    mt_srand($seed, MT_RAND_MT19937);
    $GLOBALS['Z3_ORACLE_SEEDED'] = true;
}

// Settings as the alttpr API names them (flat), mapped like RandomizerController::prepSeed.
function make_world(array $s): World
{
    Item::clearCache();
    Boss::clearCache();
    // alttpr serves one world per request, so its world id is always 1 (the id shows up in some hint texts).
    (fn () => static::$max_world = 1)->bindTo(null, World::class)();
    $cg = $s['crystals_ganon'] ?? '7';
    $ct = $s['crystals_tower'] ?? '7';
    $logic = ['none' => 'NoGlitches', 'overworld_glitches' => 'OverworldGlitches', 'hybrid_major_glitches' => 'HybridMajorGlitches',
        'major_glitches' => 'MajorGlitches', 'no_logic' => 'NoLogic'][$s['glitches'] ?? 'none'];
    $placement = ($s['item_pool'] ?? 'normal') === 'crowd_control' ? 'advanced' : ($s['item_placement'] ?? 'basic');
    return World::factory($s['mode'] ?? 'standard', array_merge([
        'itemPlacement' => $placement,
        'dungeonItems' => $s['dungeon_items'] ?? 'standard',
        'accessibility' => $s['accessibility'] ?? 'items',
        'goal' => $s['goal'] ?? 'ganon',
        // prepSeed keeps a chosen count as the request's string; only a random one is an int (World::writeToRom's
        // `crystals.tower === 0` pre-opens the tower for a random 0 only).
        'crystals.ganon' => $cg === 'random' ? get_random_int(0, 7) : (string) $cg,
        'crystals.tower' => $ct === 'random' ? get_random_int(0, 7) : (string) $ct,
        'entrances' => 'none',
        'mode.weapons' => $s['weapons'] ?? 'randomized',
        'tournament' => $s['tournament'] ?? false,
        'spoilers' => 'on',
        'allow_quickswap' => true,
        'override_start_screen' => $s['override_start_screen'] ?? false,
        'pseudoboots' => false,
        'spoil.Hints' => $s['hints'] ?? 'on',
        'logic' => $logic,
        'item.pool' => $s['item_pool'] ?? 'normal',
        'item.functionality' => $s['item_functionality'] ?? 'normal',
        'enemizer.bossShuffle' => $s['boss_shuffle'] ?? 'none',
        'enemizer.enemyShuffle' => 'none',
        'enemizer.enemyDamage' => 'default',
        'enemizer.enemyHealth' => 'default',
        'enemizer.potShuffle' => 'off',
    ], $s['extra'] ?? []));
}

function cfg_line(World $w): string
{
    $keys = ['mode.state', 'logic', 'itemPlacement', 'dungeonItems', 'accessibility', 'goal', 'crystals.ganon', 'crystals.tower',
        'mode.weapons', 'item.pool', 'item.functionality'];
    return 'cfg' . implode('', array_map(fn ($k) => " $k=" . var_export($w->config($k), true), $keys));
}

function item_names(World $w): array
{
    return array_values(array_map(fn ($i) => $i->getRawName(), Item::all($w)->values()));
}

function random_items(World $w, array $names): ItemCollection
{
    $density = mt_rand() / mt_getrandmax();
    $items = new ItemCollection;
    foreach ($names as $n) {
        if (mt_rand() / mt_getrandmax() > $density) {
            continue;
        }
        $c = mt_rand(0, 3) == 0 ? mt_rand(1, 4) : 1;
        for ($i = 0; $i < $c; $i++) {
            $items->addItem(Item::get($n, $w));
        }
    }
    $items->setChecksForWorld($w->id);
    return $items;
}

function items_line(ItemCollection $items): string
{
    $counts = [];
    foreach ($items->values() as $i) {
        $counts[$i->getRawName()] = ($counts[$i->getRawName()] ?? 0) + 1;
    }
    return 'items' . implode('', array_map(fn ($n, $c) => " $n:$c", array_keys($counts), $counts));
}

// The first release's settings; with $GLOBALS['Z3_PROFILE'] = 'next' also Inverted and Retro, the hunt goals,
// glitched logic and boss shuffle (step 8). Draws stay the same for the first release's profile.
function random_settings(): array
{
    $pick = fn (array $a) => $a[mt_rand(0, count($a) - 1)];
    if (($GLOBALS['Z3_PROFILE'] ?? '') === 'next') {
        return [
            'mode' => $pick(['open', 'standard', 'inverted', 'retro']),
            'item_placement' => $pick(['basic', 'advanced']),
            'dungeon_items' => $pick(['standard', 'mc', 'mcs', 'full']),
            'accessibility' => $pick(['items', 'locations', 'none']),
            'goal' => $pick(['ganon', 'fast_ganon', 'dungeons', 'pedestal', 'triforce-hunt', 'ganonhunt', 'completionist']),
            'crystals_ganon' => (string) mt_rand(0, 7),
            'crystals_tower' => (string) mt_rand(0, 7),
            'weapons' => $pick(['randomized', 'assured', 'vanilla', 'swordless']),
            'item_pool' => $pick(['normal', 'hard', 'expert']),
            'item_functionality' => $pick(['normal', 'hard', 'expert']),
            'glitches' => $pick(['none', 'none', 'overworld_glitches', 'major_glitches', 'no_logic']),
            'boss_shuffle' => $pick(['none', 'none', 'simple', 'full', 'random']),
        ];
    }
    return [
        'mode' => $pick(['open', 'standard']),
        'item_placement' => $pick(['basic', 'advanced']),
        'dungeon_items' => $pick(['standard', 'mc', 'mcs', 'full']),
        'accessibility' => $pick(['items', 'locations', 'none']),
        'goal' => $pick(['ganon', 'fast_ganon', 'dungeons', 'pedestal']),
        'crystals_ganon' => (string) mt_rand(0, 7),
        'crystals_tower' => (string) mt_rand(0, 7),
        'weapons' => $pick(['randomized', 'assured', 'vanilla', 'swordless']),
        'item_pool' => $pick(['normal', 'hard', 'expert']),
        'item_functionality' => $pick(['normal', 'hard', 'expert']),
    ];
}

function cmd_vectors(int $count, int $seed, string $path): void
{
    oracle_seed($seed);
    $out = '';
    for ($n = 0; $n < $count; $n++) {
        $w = make_world(random_settings());
        $items = random_items($w, item_names($w));
        $r = [];
        foreach (['canLiftRocks', 'canLiftDarkRocks', 'canLightTorches', 'canSpinSpeed', 'canBlockLasers', 'canBombThings',
            'canGetGoodBee', 'hasABottle', 'glitchedLinkInDarkWorld'] as $m) {
            $r[$m] = $items->$m();
        }
        foreach (['canMeltThings', 'canFly', 'canBunnyRevive', 'canKillEscapeThings', 'canAcquireFairy'] as $m) {
            $r[$m] = $items->$m($w);
        }
        foreach ([1, 2] as $l) {
            $r["canShootArrows/$l"] = $items->canShootArrows($w, $l);
            $r["hasArmor/$l"] = $items->hasArmor($l);
        }
        foreach ([1, 2, 3, 4] as $l) {
            $r["hasSword/$l"] = $items->hasSword($l);
            $r["hasBottle/$l"] = $items->hasBottle($l);
            $r["canExtendMagic/$l"] = $items->canExtendMagic($w, $l);
        }
        foreach ([5, 6, 8] as $e) {
            $r["canKillMostThings/$e"] = $items->canKillMostThings($w, $e);
        }
        foreach ([3, 6, 9, 12, 15, 20] as $h) {
            $r["hasHealth/$h"] = $items->hasHealth($h);
        }
        foreach (Boss::all($w) as $boss) {
            $r['boss/' . str_replace(' ', '_', $boss->getName())] = $boss->canBeat($items, $w->getLocations());
        }
        $out .= "case\n" . cfg_line($w) . "\n" . items_line($items) . "\nres";
        foreach ($r as $k => $v) {
            $out .= " $k=" . ($v ? 1 : 0);
        }
        $out .= "\n";
    }
    file_put_contents($path, $out);
    echo "$count cases -> $path\n";
}

function cmd_worldvectors(int $count, int $seed, string $path): void
{
    oracle_seed($seed);
    $out = '';
    for ($n = 0; $n < $count; $n++) {
        $w = make_world(random_settings());
        $locs = $w->getLocations();
        // prizes, medallions and a few dungeon keys placed at random: closures read them through hasItem/itemInLocations
        $prizes = array_merge(array_map(fn ($i) => "Crystal$i", range(1, 7)), ['PendantOfCourage', 'PendantOfWisdom', 'PendantOfPower']);
        $prize_locs = array_values(array_filter($locs->values(), fn ($l) => $l instanceof Location\Prize\Crystal || $l instanceof Location\Prize\Pendant));
        $prizes = fy_shuffle($prizes);
        foreach ($prize_locs as $i => $l) {
            $l->setItem(Item::get($prizes[$i], $w));
        }
        foreach ($w->getRegion('Medallions')->getLocations() as $l) {
            $l->setItem(Item::get(['Ether', 'Bombos', 'Quake'][mt_rand(0, 2)], $w));
        }
        $free = array_values(array_filter($locs->values(), fn ($l) => !$l->hasItem() && !$l instanceof Location\Prize
            && !$l instanceof Location\Medallion && !$l instanceof Location\Fountain));
        $placed = [];
        if (mt_rand(0, 1)) {
            foreach (['KeyA2', 'BigKeyA2', 'KeyD1', 'KeyD4', 'BigKeyD4', 'BigKeyD7', 'KeyD7', 'KeyD2', 'KeyD3', 'KeyD5', 'KeyD6'] as $t) {
                if (mt_rand(0, 2) == 0) {
                    $l = $free[mt_rand(0, count($free) - 1)];
                    if (!$l->hasItem()) {
                        $l->setItem(Item::get($t, $w));
                        $placed[] = "$l\t$t";
                    }
                }
            }
        }
        $items = random_items($w, item_names($w));
        $out .= "case\n" . cfg_line($w) . "\n" . items_line($items) . "\n";
        foreach ($prize_locs as $l) {
            $out .= "prize\t$l\t{$l->getItem()->getRawName()}\n";
        }
        foreach ($w->getRegion('Medallions')->getLocations() as $l) {
            $out .= "medallion\t$l\t{$l->getItem()->getRawName()}\n";
        }
        foreach ($placed as $p) {
            $out .= "placed\t$p\n";
        }
        foreach ($w->getRegions() as $key => $region) {
            $out .= "region\t$key\t" . ($region->canEnter($locs, $items) ? 1 : 0) . ($region->canComplete($locs, $items) ? 1 : 0) . "\n";
        }
        foreach ($locs as $l) {
            $out .= "loc\t$l\t" . ($l->canAccess($items) ? 1 : 0) . "\n";
        }
    }
    file_put_contents($path, $out);
    echo "$count world cases -> $path\n";
}

function placement_text(World $w): string
{
    $out = '';
    foreach ($w->getLocations() as $l) {
        $out .= "P\t$l\t" . ($l->hasItem() ? $l->getItem()->getRawName() : '-') . "\n";
    }
    foreach ($w->getPreCollectedItems()->values() as $i) {
        $out .= "pre\t{$i->getRawName()}\n";
    }
    foreach ($w->getRegions() as $key => $region) {
        $out .= "boss\t$key\t" . ($region->getBoss('top') ? $region->getBoss('top')->getName() : '-') . "\n";
    }
    foreach (['top', 'middle', 'bottom'] as $level) {
        $out .= "gtboss\t$level\t" . $w->getRegion('Ganons Tower')->getBoss($level)->getName() . "\n";
    }
    return $out;
}

function cmd_placementbatch(string $in, string $path): void
{
    $out = '';
    foreach (file($in, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES) as $line) {
        $s = json_decode($line, true);
        oracle_seed((int) ($s['seed'] ?? 1));
        $out .= "case\n";
        try {
            $w = make_world($s);
            $rand = new Randomizer([$w]);
            $rand->randomize();
            $out .= cfg_line($w) . "\nwinnable " . ((new WorldCollection([$w]))->isWinnable() ? 1 : 0) . "\n" . placement_text($w);
        } catch (\Throwable $e) {
            $out .= "error\t" . str_replace("\n", ' ', $e->getMessage()) . "\n";
        }
    }
    file_put_contents($path, $out);
}

// A placement made elsewhere (the C filler), read back through the PHP logic.
function load_placement(array $case): World
{
    $w = make_world($case['settings'] ?? []);
    $rand = new Randomizer([$w]);  // sets the default pre-collected items
    if ($w->config('mode.state') != 'standard') {
        $w->addPreCollectedItem(Item::get('RescueZelda', $w));
    }
    foreach ($case['pre'] ?? [] as $name) {
        $w->addPreCollectedItem(Item::get($name, $w));
    }
    foreach ($case['placement'] as $loc => $item) {
        $w->getLocation($loc)->setItem(Item::get($item, $w));
    }
    return $w;
}

function cmd_checkplacement(string $in, string $path): void
{
    $out = '';
    foreach (file($in, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES) as $line) {
        $out .= "case\n";
        try {
            $w = load_placement(json_decode($line, true));
            $out .= 'winnable ' . ((new WorldCollection([$w]))->isWinnable() ? 1 : 0) . "\n";
            // getCollectableLocations() is cached on first use, so read reachability off a fresh sweep instead.
            $w->resetCollectedLocations();
            $items = $w->collectItems();
            foreach ($w->getLocations() as $l) {
                if ($l->hasItem() && !$l instanceof Location\Medallion && !$l instanceof Location\Fountain && !$l->canAccess($items)) {
                    $out .= "unreached\t$l\t{$l->getItem()->getRawName()}\n";
                }
            }
        } catch (\Throwable $e) {
            $out .= "error\t" . str_replace("\n", ' ', $e->getMessage()) . "\n";
        }
    }
    file_put_contents($path, $out);
}

// A ROM for one seeded case. Without "placement": the whole of prepSeed on the seeded generator (crystals, Randomizer
// ::randomize with texts, credits and hints, writeToRom, isWinnable, tournament). With "placement": the Customizer
// path, writeToRom for a given placement after setTexts/randomizeCredits. Either way the seed string is
// str_pad("VT <seed>", 21) and the hash icons the case's "hash" (alttpr takes both from its database record).
function cmd_placementrom(string $base, string $in, string $path): void
{
    echo case_rom($base, json_decode(file_get_contents($in), true), $path) . "\n";
}

// placementrom for every line of a .jsonl, ROMs written as <outdir>/<line number from 0>.sfc
function cmd_rombatch(string $base, string $in, string $dir): void
{
    foreach (file($in, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES) as $n => $line) {
        try {
            echo "$n " . case_rom($base, json_decode($line, true), "$dir/$n.sfc") . "\n";
        } catch (\Throwable $e) {
            echo "$n error " . str_replace("\n", ' ', $e->getMessage()) . "\n";
        }
    }
}

function case_rom(string $base, array $case, string $path): string
{
    $settings = $case['settings'] ?? $case;
    $seed = (int) ($settings['seed'] ?? 1);
    $hash = $case['hash'] ?? [0, 1, 2, 3, 4];
    oracle_seed($seed);
    $settings['override_start_screen'] = $hash;
    $rom = new Rom($base);
    if (!$rom->checkMD5()) {
        fwrite(STDERR, "base ROM md5 {$rom->getMD5()} is not Rom::HASH " . Rom::HASH . "\n");
        exit(3);
    }
    if (isset($case['placement'])) {
        $case['settings'] = $settings;
        $w = load_placement($case);
        $rand = new Randomizer([$w]);
        $rand->setTexts($w);
        $rand->randomizeCredits($w);
    } else {
        $w = make_world($settings);
        $rand = new Randomizer([$w]);
        $rand->randomize();
    }
    $w->writeToRom($rom);
    $winnable = (new WorldCollection([$w]))->isWinnable();
    if ($w->isEnemized()) {
        enemize($rom, $w, $base, $seed, $hash, (int) ($case['enemizer_seed'] ?? 1));
    }
    if ($settings['tournament'] ?? false) {
        $rom->setTournamentType('standard');
        $rom->rummageTable();
    }
    // Cosmetics: alttpr.com applies them in the browser on save; these are Rom.php's own setters for them.
    if (isset($settings['heart_beep'])) {
        $rom->setHeartBeepSpeed($settings['heart_beep']);
    }
    if (isset($settings['menu_speed'])) {
        $rom->setMenuSpeed($settings['menu_speed']);
    }
    if (isset($settings['quickswap'])) {
        $rom->setQuickSwap($settings['quickswap'] === 'true' || $settings['quickswap'] === true);
    }
    $rom->setSeedString(str_pad("VT $seed", 21, ' '));
    $rom->setStartScreenHash($hash);
    $rom->updateChecksum();
    $rom->save($path);
    return 'winnable ' . ($winnable ? 1 : 0);
}

// prepSeed's Enemizer run, after the seed string and hash writeToRom(save) writes (it wants a "VT" ROM). --rom is the
// patched base, so --base is empty; the seed is the case's "enemizer_seed" (random_int upstream).
function enemize(Rom $rom, World $w, string $base, int $seed, array $hash, int $enemizer_seed): void
{
    $rom->setSeedString(str_pad("VT $seed", 21, ' '));
    $rom->setStartScreenHash($hash);
    if (!is_file(Rom::getJsonPatchLocation())) {
        @mkdir(dirname(Rom::getJsonPatchLocation()), 0777, true);
        file_put_contents(Rom::getJsonPatchLocation(), '[]');
    }
    config(['enemizer.base' => $base]);
    $en = new \ALttP\Enemizer($w, $rom->getWriteLog());
    $en->randomize($enemizer_seed);
    $en->writeToRom($rom);
}

// Data for tools/z3/gen_z3_text.py: the dialog table after Rom's removeUnwanted, the string pools setTexts and
// HintService draw from, the hint names (strings or arrays, exactly as __() returns them) and every location text
// that writeItem sets, per item. Plus encoder samples (string -> convertDialogCompressed bytes) to check the C encoder.
function cmd_textdata(string $path): void
{
    $w = World::factory('open', []);
    $text = new \ALttP\Text;
    $text->removeUnwanted();
    $table = (fn () => $this->text_array)->call($text);
    $out = ['text' => []];
    foreach ($table as $k => $bytes) {
        $out['text'][] = [$k, array_values($bytes)];
    }
    $rand = new Randomizer([$w]);
    foreach (['uncle', 'tavern_man', 'blind', 'ganon_1', 'ganon_phase_3_no_silvers', 'triforce', 'hint'] as $f) {
        $out['strings'][$f] = array_values($rand->getTextArray("strings/$f.txt"));
    }
    foreach (Item::all($w) as $item) {
        $out['hint_item'][$item->getRawName()] = __('hint.item.' . $item->getRawName());
    }
    foreach ($w->getLocations() as $l) {
        $out['hint_location']["$l"] = __('hint.location.' . $l);
    }
    $special = [['Master Sword Pedestal', 'getItemCreditsText'], ['Master Sword Pedestal', 'getItemPedestalText'],
        ['Ether Tablet', 'getItemText'], ['Bombos Tablet', 'getItemText'], ["Link's Uncle", 'getItemCreditsText'],
        ['King Zora', 'getItemCreditsText'], ['Potion Shop', 'getItemCreditsText'], ['Sick Kid', 'getItemCreditsText'],
        ['Flute Spot', 'getItemCreditsText']];
    foreach ($special as [$name, $method]) {
        $l = $w->getLocation($name);
        $m = new \ReflectionMethod($l, $method);
        $m->setAccessible(true);
        $texts = [];
        foreach (Item::all($w) as $item) {
            if ($item instanceof \ALttP\ItemAlias) {
                continue;
            }
            $l->setItem($item);
            $texts[$item->getRawName()] = $m->invoke($l);
        }
        $out['loctext'][] = ['location' => $name, 'method' => $method, 'texts' => $texts];
        $l->setItem(null);
    }
    $dialog = new \ALttP\Support\Dialog;
    $samples = [];
    foreach ($out['strings'] as $pool) {
        foreach ($pool as $str) {
            $samples[] = $str;
        }
    }
    foreach ($out['loctext'] as $lt) {
        foreach ($lt['texts'] as $str) {
            $samples[] = $str;
        }
    }
    $samples = array_merge($samples, ["{NOBORDER}\nhello there", "a → b ≥ c… x", "Lonk! Boots\nare in the\nDark World",
        "hint.item.Bottle:1, hint.item.Hammer:1 and hint.item.Lamp:1 hint.location.Misery Mire - Big Key Chest:1",
        "You need 3 crystals to enter.", "To beat Ganon you must collect 7 Crystals and defeat his minion at the top of his tower."]);
    foreach (array_unique($samples) as $str) {
        $out['encode'][] = [$str, $dialog->convertDialogCompressed($str)];
    }
    file_put_contents($path, json_encode($out, JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT));
}

function cmd_schema(string $path): void
{
    $opts = config('alttp.randomizer.item');
    unset($opts['presets']);
    file_put_contents($path, json_encode([
        'build' => Rom::BUILD,
        'hash' => Rom::HASH,
        'options' => array_map(fn ($o) => array_keys($o), $opts),
        'items' => item_names(World::factory('open', [])),
    ], JSON_PRETTY_PRINT));
}

$a = array_slice($argv, 1);
$GLOBALS['Z3_PROFILE'] = getenv('Z3_PROFILE') ?: '';
switch ($a[0] ?? '') {
    case 'schema': cmd_schema(wp($a[1])); break;
    case 'textdata': cmd_textdata(wp($a[1])); break;
    case 'vectors': cmd_vectors((int) $a[1], (int) $a[2], wp($a[3])); break;
    case 'worldvectors': cmd_worldvectors((int) $a[1], (int) $a[2], wp($a[3])); break;
    case 'placementbatch': cmd_placementbatch(wp($a[1]), wp($a[2])); break;
    case 'checkplacement': cmd_checkplacement(wp($a[1]), wp($a[2])); break;
    case 'placementrom': cmd_placementrom(wp($a[1]), wp($a[2]), wp($a[3])); break;
    case 'rombatch': cmd_rombatch(wp($a[1]), wp($a[2]), wp($a[3])); break;
    default:
        fwrite(STDERR, "usage: z3oracle.php schema|vectors|worldvectors|placementbatch|checkplacement|placementrom ...\n");
        exit(1);
}
