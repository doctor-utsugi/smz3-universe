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
use maprando::randomize::{Randomizer, assign_map_areas, get_difficulty_tiers, get_objectives, randomize_doors};
use maprando::settings::{RandomizerSettings, StartLocationMode};
use maprando_game::GameData;
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

fn load(dir: &Path) -> Result<State> {
    in_dir(dir, || {
        let mut game_data = GameData::load(Path::new("."))?;
        let preset_data = PresetData::load(
            Path::new("data/tech_data.json"),
            Path::new("data/notable_data.json"),
            Path::new("data/presets"),
            &game_data,
        )?;
        let global = get_full_global(&game_data);
        game_data.make_links_data(&|link, gd| get_link_difficulty_length(link, gd, &preset_data, &global));
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

fn generate(st: &State, json: &str, seed: u64, max_attempts: u32, rom: &[u8]) -> Result<(Vec<u8>, String)> {
    in_dir(&st.dir, || {
        let gd = &st.game_data;
        let settings = settings_of(st, json)?;
        let tiers = get_difficulty_tiers(
            &settings,
            &st.preset_data.difficulty_tiers,
            gd,
            &st.preset_data.tech_by_difficulty["Implicit"],
            &st.preset_data.notables_by_difficulty["Implicit"],
        );
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
        let mut attempt = 0;
        let mut found = None;
        'maps: for _ in 0..max_attempts.div_ceil(per_map) {
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
            let r = Randomizer::new(&map, &doors, objectives, &settings, &tiers, gd, &gd.base_links_data, &mut rng);
            for _ in 0..per_map {
                attempt += 1;
                ATTEMPTS.store(attempt as u32, Ordering::Relaxed);
                let item_seed = (rng.next_u64() & 0xFFFFFFFF) as usize;
                if let Ok((randomization, _spoiler)) = r.randomize(attempt, item_seed, display_seed, true) {
                    found = Some(randomization);
                    break 'maps;
                }
            }
        }
        let Some(mut randomization) = found else { bail!("No seed after {attempt} attempts") };
        // Upstream names a seed from the clock (its storage key); here the name follows the seed so every host agrees.
        randomization.seed_name = format!("smz{:x}", seed & 0xFFFF_FFFF_FFFF);
        // A copier header (512 bytes) is dropped; the ROM grows to the 4 MB Map Rando writes into.
        let body = if rom.len() % 0x8000 == 0x200 { &rom[0x200..] } else { rom };
        let mut input = Rom::new(body.to_vec());
        input.data.resize(0x400000, 0);
        let out = make_rom(&input, &settings, &customize(), &randomization, gd, &[], &[])?;
        let info = serde_json::json!({
            "seedName": randomization.seed_name,
            "displaySeed": randomization.display_seed,
            "hash": seed_hash(randomization.display_seed),
            "attempts": attempt,
        });
        Ok((out.data, info.to_string()))
    })
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
