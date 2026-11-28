//! C interface to Map Rando's generator (see smz3_maprando.h). The flow follows upstream's maprando-test and
//! maprando-cli: a map from a local pregenerated pool, its areas, objectives and doors, then item placement attempts.

use anyhow::{Context, Result, bail};
use maprando::customize::{
    ControllerConfig, CustomizeSettings, DoorTheme, FlashingSetting, ItemDotChange, MapTheme, MusicSettings,
    PaletteTheme, ShakingSetting, StatuesHallwayAudio, StatuesHallwayTiling, TileTheme,
};
use maprando::difficulty::{get_full_global, get_link_difficulty_length};
use maprando::map_repository::{MapPoolsConfig, MapRepository, PregeneratedMapRepository};
use maprando::patch::{Rom, make_rom};
use maprando::preset::PresetData;
use maprando::randomize::{
    DoorLinkCache, DoorState, FlagLocationState, ItemLocationState, Randomization, RandomizationState, Randomizer,
    SaveLocationState, TraverserPair, assign_map_areas, get_difficulty_tiers, get_objectives, make_locked_door_data,
    randomize_doors,
};
use maprando::settings::{RandomizerSettings, StartLocationMode};
use maprando::traverse::Traverser;
use maprando_game::{GameData, HubLocation, Item, VertexKey};
use maprando_logic::LocalState;
use rand::{RngCore, SeedableRng};
use std::ffi::{CStr, c_char};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::sync::atomic::{AtomicU32, Ordering};

struct State {
    dir: PathBuf,
    game_data: GameData,
    preset_data: PresetData,
    maps: PregeneratedMapRepository,
    pools: Vec<String>,
}

static STATE: Mutex<Option<State>> = Mutex::new(None);
/// Attempts of the seed being built, for the hosts' progress line.
static ATTEMPTS: AtomicU32 = AtomicU32::new(0);

/// Upstream reads its data relative to the working directory (`rust/` of its checkout), so every call runs there and
/// puts the host's directory back afterwards.
fn in_dir<T>(dir: &Path, f: impl FnOnce() -> Result<T>) -> Result<T> {
    let old = std::env::current_dir().ok();
    std::env::set_current_dir(dir.join("rust")).with_context(|| format!("No Map Rando data in {}", dir.display()))?;
    let r = f();
    if let Some(old) = old {
        let _ = std::env::set_current_dir(old);
    }
    r
}

/// The pools shipped in `maps/`: each folder with a manifest, listing only the batches present.
fn local_pools() -> Result<(MapPoolsConfig, Vec<String>)> {
    let mut names = vec![];
    for entry in std::fs::read_dir("../maps").context("No map pools")? {
        let entry = entry?;
        if entry.path().join("manifest.json").is_file() {
            names.push(entry.file_name().to_string_lossy().into_owned());
        }
    }
    names.sort();
    let pools: Vec<_> = names.iter().map(|n| serde_json::json!({"name": n, "display_name": n})).collect();
    let cfg = serde_json::from_value(serde_json::json!({"preset_fallbacks": {}, "pools": pools}))?;
    Ok((cfg, names))
}

/// The game data as the generator uses it, made from the JSON (with the links' difficulties, web-only data dropped).
fn game_data_from_json() -> Result<GameData> {
    let mut game_data = GameData::load(Path::new("."))?;
    let preset_data = presets(&game_data)?;
    let global = get_full_global(&game_data);
    game_data.make_links_data(&|link, gd| get_link_difficulty_length(link, gd, &preset_data, &global));
    game_data.drop_web_data();
    Ok(game_data)
}

fn presets(game_data: &GameData) -> Result<PresetData> {
    PresetData::load(
        Path::new("data/tech_data.json"),
        Path::new("data/notable_data.json"),
        Path::new("data/presets"),
        game_data,
    )
}

/// `rust/gamedata.bin`: what game_data_from_json makes, packed by `smz3-maprando --pack` so a device does not parse the
/// JSON. It holds the key of the code that wrote it and the names and sizes of the data files it was made from; with
/// either different it is not used.
const PACK: &str = "gamedata.bin";
const PACK_MAGIC: &[u8; 8] = b"SMZMRPK1";

fn data_fingerprint() -> u64 {
    fn walk(p: &Path, out: &mut Vec<(String, u64)>) {
        if let Ok(rd) = std::fs::read_dir(p) {
            for e in rd.flatten() {
                walk(&e.path(), out);
            }
        } else if let Ok(m) = std::fs::metadata(p) {
            out.push((p.to_string_lossy().replace('\\', "/"), m.len()));
        }
    }
    let mut files = vec![];
    for p in ["../sm-json-data", "data", "../room_geometry.json", "../TitleScreen"] {
        walk(Path::new(p), &mut files);
    }
    files.sort();
    let mut h = crc32fast::Hasher::new();
    for (name, len) in &files {
        h.update(name.as_bytes());
        h.update(&len.to_le_bytes());
    }
    h.finalize() as u64 | (files.len() as u64) << 32
}

fn pack_header() -> Vec<u8> {
    let mut v = PACK_MAGIC.to_vec();
    v.extend_from_slice(env!("SMZ_MR_PACK_KEY").as_bytes());
    v.extend_from_slice(&data_fingerprint().to_le_bytes());
    v
}

fn pack_bytes(gd: &GameData) -> Result<(Vec<u8>, Vec<u8>)> {
    let (body, reqs) = maprando_game::shared_requirement::writing(|| bincode::serialize(gd));
    Ok((bincode::serialize(&reqs)?, body?))
}

fn game_data_from_pack() -> Option<GameData> {
    use std::io::Read;
    let mut f = std::io::BufReader::new(std::fs::File::open(PACK).ok()?);
    let header = pack_header();
    let mut got = vec![0; header.len()];
    f.read_exact(&mut got).ok()?;
    if got != header {
        return None;
    }
    let reqs: Vec<maprando_game::Requirement> = bincode::deserialize_from(&mut f).ok()?;
    let mut gd: GameData = maprando_game::shared_requirement::reading(reqs, || bincode::deserialize_from(&mut f).ok())?;
    gd.replay_insertion_order();
    Some(gd)
}

/// Writes `rust/gamedata.bin` into a data folder, after checking that it reads back and that the maps the randomizer
/// iterates come back in the order loading from JSON gives.
pub fn pack(dir: &Path) -> Result<usize> {
    in_dir(dir, || {
        let gd = game_data_from_json()?;
        let (reqs, body) = pack_bytes(&gd)?;
        let mut out = pack_header();
        out.extend_from_slice(&reqs);
        out.extend_from_slice(&body);
        drop((reqs, body));
        let tmp = format!("{PACK}.tmp");
        std::fs::write(&tmp, &out)?;
        std::fs::rename(&tmp, PACK)?;
        let back = game_data_from_pack();
        let same = back.as_ref().is_some_and(|b| {
            gd.raw_room_id_by_ptr.keys().eq(b.raw_room_id_by_ptr.keys())
                && gd.door_ptr_pair_map.keys().eq(b.door_ptr_pair_map.keys())
                && gd.node_door_unlock.keys().eq(b.node_door_unlock.keys())
        });
        if !same {
            let _ = std::fs::remove_file(PACK);
            bail!("The packed game data does not read back as the loaded one");
        }
        Ok(out.len())
    })
}

fn load(dir: &Path) -> Result<State> {
    in_dir(dir, || {
        let game_data = match game_data_from_pack() {
            Some(gd) => gd,
            None => game_data_from_json()?,
        };
        let preset_data = presets(&game_data)?;
        let (cfg, pools) = local_pools()?;
        let maps = PregeneratedMapRepository::new(&cfg)?;
        Ok(State { dir: dir.to_path_buf(), game_data, preset_data, maps, pools })
    })
}

fn read_preset<T: serde::de::DeserializeOwned>(folder: &str, name: &str) -> Result<T> {
    if name.contains('/') || name.contains("..") {
        bail!("Bad preset name {name}");
    }
    let path = format!("data/presets/{folder}/{name}.json");
    let s = std::fs::read_to_string(&path).with_context(|| format!("Unknown preset {folder}/{name}"))?;
    Ok(serde_json::from_str(&s)?)
}

fn settings_of(st: &State, json: &str) -> Result<RandomizerSettings> {
    let v: serde_json::Value = serde_json::from_str(json).context("Bad settings")?;
    let field = |k: &str| v.get(k).and_then(|x| x.as_str());
    let mut s: RandomizerSettings = match field("preset") {
        Some(p) => read_preset("full-settings", p)?,
        None => st.preset_data.default_preset.clone(),
    };
    if let Some(p) = field("skill") {
        s.skill_assumption_settings = read_preset("skill-assumptions", p)?;
    }
    if let Some(p) = field("items") {
        s.item_progression_settings = read_preset("item-progression", p)?;
    }
    if let Some(p) = field("qol") {
        s.quality_of_life_settings = read_preset("quality-of-life", p)?;
    }
    if let Some(p) = field("objectives") {
        s.objective_settings = read_preset("objectives", p)?;
    }
    // Fresh maps come from upstream's map generation server; the devices draw from the shipped pool instead.
    if !st.pools.contains(&s.map_settings.source) {
        let want = if s.map_settings.generation.small_map { "small" } else { "standard" };
        s.map_settings.source = st
            .pools
            .iter()
            .find(|p| p.ends_with(want))
            .or(st.pools.first())
            .context("No map pool")?
            .clone();
    }
    Ok(s)
}

fn customize() -> CustomizeSettings {
    CustomizeSettings {
        samus_sprite: None,
        etank_color: None,
        item_dot_change: ItemDotChange::Fade,
        transition_letters: true,
        reserve_hud_style: true,
        vanilla_screw_attack_animation: true,
        room_names: true,
        boss_icons: true,
        save_icons: true,
        miniboss_icons: true,
        start_location_icon: true,
        map_theme: MapTheme::Light,
        palette_theme: PaletteTheme::AreaThemed,
        tile_theme: TileTheme::Vanilla,
        door_theme: DoorTheme::Vanilla,
        music: MusicSettings::AreaThemed,
        statues_hallway_tiling: StatuesHallwayTiling::Default,
        statues_hallway_audio: StatuesHallwayAudio::Enabled,
        disable_beeping: false,
        shaking: ShakingSetting::Vanilla,
        flashing: FlashingSetting::Vanilla,
        controller_config: ControllerConfig::default(),
    }
}

/// A seed found from settings and a seed number, with what rebuilds its randomizer: the attempt and item seed that made it.
struct Found {
    settings: RandomizerSettings,
    randomization: Randomization,
    attempt: usize,
    item_seed: usize,
}

fn find(st: &State, json: &str, seed: u64, max_attempts: u32) -> Result<Found> {
    let gd = &st.game_data;
    let settings = settings_of(st, json)?;
    let tiers = tiers_of(st, &settings);
    let mut rs = [0u8; 32];
    rs[..8].copy_from_slice(&seed.to_le_bytes());
    let mut rng = rand::rngs::StdRng::from_seed(rs);
    // The menu's hash comes from the seed number, mixed (splitmix64) so nearby numbers look different.
    let mut z = seed.wrapping_add(0x9E37_79B9_7F4A_7C15);
    z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
    let display_seed = ((z ^ (z >> 31)) & 0xFFFF_FFFF) as usize;
    let max_attempts = if max_attempts == 0 { 10000 } else { max_attempts as usize };
    let per_map = if settings.start_location_settings.mode == StartLocationMode::Random { 10 } else { 1 };
    let mut batch = vec![];
    let door_cache = DoorLinkCache::default();
    let mut attempt = 0;
    for _ in 0..max_attempts.div_ceil(per_map) {
        let map_seed = (rng.next_u64() & 0xFFFFFFFF) as usize;
        let door_seed = (rng.next_u64() & 0xFFFFFFFF) as usize;
        if batch.is_empty() {
            batch = st.maps.get_map_batch(map_seed, &settings, gd)?;
        }
        let mut map = batch.pop().context("Empty map batch")?;
        if !assign_map_areas(&mut map, &settings, map_seed, gd) {
            continue;
        }
        let objectives = get_objectives(&settings, Some(&map), gd, &mut rng);
        let doors = randomize_doors(gd, &map, &settings, &objectives, door_seed);
        let mut r = Randomizer::new_with_door_cache(
            &map,
            &doors,
            objectives,
            &settings,
            &tiers,
            gd,
            &gd.base_links_data,
            Some(&door_cache),
            &mut rng,
        );
        r.spoiler_details = false;
        for _ in 0..per_map {
            attempt += 1;
            ATTEMPTS.store(attempt as u32, Ordering::Relaxed);
            let item_seed = (rng.next_u64() & 0xFFFFFFFF) as usize;
            // No spoiler routes: rebuilding them changes nothing the ROM holds and took most of a seed's time.
            if let Ok((mut randomization, _spoiler)) = r.randomize(attempt, item_seed, display_seed, false) {
                // Upstream names a seed from the clock (its storage key); here the name follows the seed so every host agrees.
                randomization.seed_name = format!("smz{:x}", seed & 0xFFFF_FFFF_FFFF);
                return Ok(Found { settings, randomization, attempt, item_seed });
            }
        }
    }
    bail!("No seed after {attempt} attempts")
}

fn tiers_of(st: &State, settings: &RandomizerSettings) -> Vec<maprando::randomize::DifficultyConfig> {
    get_difficulty_tiers(
        settings,
        &st.preset_data.difficulty_tiers,
        &st.game_data,
        &st.preset_data.tech_by_difficulty["Implicit"],
        &st.preset_data.notables_by_difficulty["Implicit"],
    )
}

fn generate(st: &State, json: &str, seed: u64, max_attempts: u32, rom: &[u8]) -> Result<(Vec<u8>, String)> {
    in_dir(&st.dir, || {
        let f = find(st, json, seed, max_attempts)?;
        // A copier header (512 bytes) is dropped; the ROM grows to the 4 MB Map Rando writes into.
        let body = if rom.len() % 0x8000 == 0x200 { &rom[0x200..] } else { rom };
        let mut input = Rom::new(body.to_vec());
        input.data.resize(0x400000, 0);
        let out = make_rom(&input, &f.settings, &customize(), &f.randomization, &st.game_data, &[], &[])?;
        let info = serde_json::json!({
            "seedName": f.randomization.seed_name,
            "displaySeed": f.randomization.display_seed,
            "hash": seed_hash(f.randomization.display_seed),
            "attempts": f.attempt,
        });
        Ok((out.data, info.to_string()))
    })
}

/// What a proof check keeps of a seed between finishes: the placement and the hub its logic starts from.
#[derive(serde::Serialize, serde::Deserialize)]
struct CheckCache {
    randomization: Randomization,
    hub: (usize, usize),
}

/// The hub randomize() chose: its start location step replayed with the item seed's generator, which it draws from first.
fn hub_of(r: &Randomizer, attempt: usize, item_seed: usize) -> Result<(usize, usize)> {
    let mut rs = [0u8; 32];
    rs[..8].copy_from_slice(&item_seed.to_le_bytes());
    let mut rng = rand::rngs::StdRng::from_seed(rs);
    let (global, local) = r.get_initial_states();
    let n = r.game_data.vertex_isv.keys.len();
    let mut tp = TraverserPair {
        forward: Traverser::new(n, false, local, &global),
        reverse: Traverser::new(n, true, local, &global),
    };
    let mode = r.settings.start_location_settings.mode;
    let tries = if r.game_data.start_locations.len() > 1 && mode != StartLocationMode::Custom { 10 } else { 1 };
    let d = r.determine_start_location(attempt, tries, &mut rng, &mut tp)?;
    Ok((d.hub_location.room_id, d.hub_location.node_id))
}

/// Sets the flags and opens the locked doors the traversal reached, as the randomizer's steps do, until nothing changes.
fn settle(r: &Randomizer, state: &mut RandomizationState, tp: &mut TraverserPair) {
    loop {
        let mut any = false;
        for (i, &flag_id) in r.game_data.flag_ids.iter().enumerate() {
            let fs = &state.flag_location_state[i];
            let reached = if flag_id == r.game_data.mother_brain_defeated_flag_id {
                fs.reachable_traversal.is_some()
            } else {
                fs.bireachable_traversal.is_some()
            };
            if !state.global_state.flags[flag_id] && reached {
                state.global_state.flags[flag_id] = true;
                any = true;
            }
        }
        for i in 0..r.locked_door_data.locked_doors.len() {
            if !state.global_state.doors_unlocked[i] && state.door_state[i].bireachable_traversal.is_some() {
                state.global_state.doors_unlocked[i] = true;
                any = true;
            }
        }
        if !any {
            return;
        }
        state.step_num += 1;
        r.update_reachability(state, tp);
    }
}

/// Which of the checks, in the order the run cleared them (their item bits, as the tracker reads them at $7E:D870), the
/// seed's logic reached with the items found before them. A check is in logic once one way there is.
fn check(st: &State, json: &str, seed: u64, bits: &[u32], rom: &[u8], cache: &Path) -> Result<Vec<bool>> {
    in_dir(&st.dir, || {
        let gd = &st.game_data;
        let settings = settings_of(st, json)?;
        let tiers = tiers_of(st, &settings);
        let cached: Option<CheckCache> = std::fs::read(cache).ok().and_then(|b| serde_json::from_slice(&b).ok());
        let (c, fresh) = match cached {
            Some(c) => (c, None),
            None => {
                let f = find(st, json, seed, 0)?;
                (CheckCache { randomization: f.randomization, hub: (0, 0) }, Some((f.attempt, f.item_seed)))
            }
        };
        let ra = &c.randomization;
        let doors = make_locked_door_data(ra.locked_doors.clone(), gd);
        let mut rng = rand::rngs::StdRng::from_seed([0; 32]);
        let r = Randomizer::new(&ra.map, &doors, ra.objectives.clone(), &settings, &tiers, gd, &gd.base_links_data, &mut rng);
        let hub = match fresh {
            Some((attempt, item_seed)) => {
                let hub = hub_of(&r, attempt, item_seed)?;
                let keep = CheckCache { randomization: serde_json::from_value(serde_json::to_value(ra)?)?, hub };
                std::fs::write(cache, serde_json::to_vec(&keep)?)?;
                hub
            }
            None => c.hub,
        };

        let body = if rom.len() % 0x8000 == 0x200 { &rom[0x200..] } else { rom };
        let bit_of = |k: usize| -> Option<u32> {
            let p = *gd.node_ptr_map.get(&gd.item_locations[k])?;
            Some(u16::from_le_bytes([*body.get(p + 4)?, *body.get(p + 5)?]) as u32)
        };
        let loc_of: std::collections::HashMap<u32, usize> =
            (0..gd.item_locations.len()).filter_map(|k| bit_of(k).map(|b| (b, k))).collect();

        let (global, local) = r.get_initial_states();
        let n = gd.vertex_isv.keys.len();
        let mut tp = TraverserPair {
            forward: Traverser::new(n, false, local, &global),
            reverse: Traverser::new(n, true, local, &global),
        };
        let hub_v = gd.vertex_isv.index_by_key[&VertexKey { room_id: hub.0, node_id: hub.1, obstacle_mask: 0, actions: vec![] }];
        tp.forward.add_origin(local, &global.inventory, hub_v);
        tp.forward.finish_step(1);
        tp.reverse.add_origin(LocalState::full(true), &global.inventory, hub_v);
        tp.reverse.finish_step(1);
        let item_state = ItemLocationState {
            placed_item: None,
            placed_tier: None,
            collected: false,
            reachable_traversal: None,
            bireachable_traversal: None,
            bireachable_vertex_id: None,
            difficulty_tier: None,
        };
        let flag_state = FlagLocationState {
            reachable_traversal: None,
            reachable_vertex_id: None,
            bireachable_traversal: None,
            bireachable_vertex_id: None,
        };
        let mut state = RandomizationState {
            step_num: 1,
            start_location: ra.start_location.clone(),
            hub_location: HubLocation { room_id: hub.0, node_id: hub.1, vertex_id: hub_v },
            item_precedence: vec![],
            save_location_state: vec![SaveLocationState { bireachable_traversal: None }; gd.save_locations.len()],
            item_location_state: vec![item_state; gd.item_locations.len()],
            flag_location_state: vec![flag_state; gd.flag_ids.len()],
            door_state: vec![DoorState { bireachable_traversal: None, bireachable_vertex_id: None }; doors.locked_doors.len()],
            items_remaining: r.initial_items_remaining.clone(),
            global_state: global,
            starting_local_state: local,
            last_key_areas: vec![],
        };
        r.update_reachability(&mut state, &mut tp);
        settle(&r, &mut state, &mut tp);

        let ips = &settings.item_progression_settings;
        let mut stale = false;
        let mut out = Vec::with_capacity(bits.len());
        for &b in bits {
            let Some(&k) = loc_of.get(&b) else {
                out.push(false);
                continue;
            };
            // Reachability only grows with items, so the traversal reruns only for a check not reached yet.
            if state.item_location_state[k].reachable_traversal.is_none() && stale {
                state.step_num += 1;
                r.update_reachability(&mut state, &mut tp);
                settle(&r, &mut state, &mut tp);
                stale = false;
            }
            out.push(state.item_location_state[k].reachable_traversal.is_some());
            let item = ra.item_placement[k];
            if item != Item::Nothing {
                state.global_state.collect(
                    item,
                    gd,
                    ips.ammo_collect_fraction,
                    ips.missile_size,
                    ips.super_size,
                    ips.powerbomb_size,
                    ips.etank_size,
                    ips.reserve_size,
                    &tiers[0].tech,
                );
                stale = true;
            }
        }
        Ok(out)
    })
}

/// Loads the data folder and checks a run's cleared checks against its seed (see `check`), the randomization kept in
/// `cache` for the next finish of the same seed.
pub fn check_with(data_dir: &Path, settings_json: &str, seed: u64, bits: &[u32], rom: &[u8], cache: &Path) -> Result<Vec<bool>> {
    let st = load(data_dir)?;
    check(&st, settings_json, seed, bits, rom, cache)
}

/// The four enemy names the main menu shows (maprando-web's get_seed_hash, matching seed_hash_display.asm).
fn seed_hash(display_seed: usize) -> String {
    const ENEMIES: [&str; 32] = [
        "GEEMER", "RIPPER", "ATOMIC", "POWAMP", "SCISER", "NAMIHE", "PUROMI", "ALCOON", "BEETOM", "OWTCH", "ZEBBO",
        "ZEELA", "HOLTZ", "VIOLA", "WAVER", "RINKA", "BOYON", "CHOOT", "KAGO", "SKREE", "COVERN", "EVIR", "TATORI",
        "OUM", "PUYO", "YARD", "ZOA", "FUNE", "GAMET", "GERUTA", "SOVA", "BULL",
    ];
    (display_seed as u32).to_le_bytes().iter().map(|&i| ENEMIES[i as usize % 32]).collect::<Vec<_>>().join(" ")
}

/// A BPS patch from source to target (byd's format, as Flips writes it): unchanged runs at the same offset are SourceRead,
/// everything else TargetRead, which keeps a Map Rando ROM's patch to the bytes it rewrote.
pub fn bps_encode(source: &[u8], target: &[u8]) -> Vec<u8> {
    fn num(out: &mut Vec<u8>, mut v: u64) {
        loop {
            let x = (v & 0x7f) as u8;
            v >>= 7;
            if v == 0 {
                out.push(0x80 | x);
                break;
            }
            out.push(x);
            v -= 1;
        }
    }
    let same = |i: usize| i < source.len() && source[i] == target[i];
    let mut out = b"BPS1".to_vec();
    num(&mut out, source.len() as u64);
    num(&mut out, target.len() as u64);
    num(&mut out, 0);
    let mut i = 0;
    while i < target.len() {
        let start = i;
        if same(i) {
            while i < target.len() && same(i) {
                i += 1;
            }
            num(&mut out, ((i - start - 1) as u64) << 2);
        } else {
            // A changed run ends at the next 4 unchanged bytes, shorter matches cost more than they save.
            while i < target.len() && !(same(i) && (1..4).all(|k| i + k >= target.len() || same(i + k))) {
                i += 1;
            }
            num(&mut out, (((i - start - 1) as u64) << 2) | 1);
            out.extend_from_slice(&target[start..i]);
        }
    }
    out.extend_from_slice(&crc32fast::hash(source).to_le_bytes());
    out.extend_from_slice(&crc32fast::hash(target).to_le_bytes());
    let crc = crc32fast::hash(&out);
    out.extend_from_slice(&crc.to_le_bytes());
    out
}

/// Loads the data folder and builds a seed, for the server's command line tool.
pub fn generate_with(data_dir: &Path, settings_json: &str, seed: u64, max_attempts: u32, rom: &[u8]) -> Result<(Vec<u8>, String)> {
    let st = load(data_dir)?;
    generate(&st, settings_json, seed, max_attempts, rom)
}

fn put_str(dst: *mut c_char, cap: usize, s: &str) {
    if dst.is_null() || cap == 0 {
        return;
    }
    let n = s.len().min(cap - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(s.as_ptr(), dst as *mut u8, n);
        *dst.add(n) = 0;
    }
}

fn panic_text(e: Box<dyn std::any::Any + Send>) -> String {
    e.downcast_ref::<&str>().map(|s| s.to_string()).or_else(|| e.downcast_ref::<String>().cloned()).unwrap_or("panic".into())
}

/// # Safety
/// `data_dir` is a NUL-terminated path; `err` has room for `err_cap` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn smz_mr_init(data_dir: *const c_char, err: *mut c_char, err_cap: usize) -> i32 {
    let mut guard = STATE.lock().unwrap_or_else(|e| e.into_inner());
    if guard.is_some() {
        return 0;
    }
    let dir = PathBuf::from(unsafe { CStr::from_ptr(data_dir) }.to_string_lossy().into_owned());
    match std::panic::catch_unwind(|| load(&dir)) {
        Ok(Ok(st)) => {
            *guard = Some(st);
            0
        }
        Ok(Err(e)) => {
            put_str(err, err_cap, &format!("{e:#}"));
            -1
        }
        Err(p) => {
            put_str(err, err_cap, &panic_text(p));
            -1
        }
    }
}

/// # Safety
/// Pointers as in smz3_maprando.h: `settings_json` NUL-terminated, `rom` `rom_len` bytes, `out` `out_cap` bytes,
/// `info` `info_cap` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn smz_mr_generate(
    settings_json: *const c_char,
    seed: u64,
    max_attempts: u32,
    rom: *const u8,
    rom_len: usize,
    out: *mut u8,
    out_cap: usize,
    info: *mut c_char,
    info_cap: usize,
) -> i32 {
    let guard = STATE.lock().unwrap_or_else(|e| e.into_inner());
    let fail = |msg: String| {
        put_str(info, info_cap, &serde_json::json!({ "error": msg }).to_string());
        -1
    };
    let Some(st) = guard.as_ref() else { return fail("Map Rando is not loaded".into()) };
    ATTEMPTS.store(0, Ordering::Relaxed);
    let json = unsafe { CStr::from_ptr(settings_json) }.to_string_lossy().into_owned();
    let rom = unsafe { std::slice::from_raw_parts(rom, rom_len) };
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| generate(st, &json, seed, max_attempts, rom))) {
        Ok(Ok((data, text))) => {
            if data.len() > out_cap {
                return fail(format!("ROM needs {} bytes", data.len()));
            }
            unsafe { std::ptr::copy_nonoverlapping(data.as_ptr(), out, data.len()) };
            put_str(info, info_cap, &text);
            0
        }
        Ok(Err(e)) => fail(format!("{e:#}")),
        Err(p) => fail(panic_text(p)),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn smz_mr_attempts() -> u32 {
    ATTEMPTS.load(Ordering::Relaxed)
}

#[unsafe(no_mangle)]
pub extern "C" fn smz_mr_free() {
    *STATE.lock().unwrap_or_else(|e| e.into_inner()) = None;
}
