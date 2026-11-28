//! Parity check between hosts (here: the 64-bit PC build and the wasm32 web build): finds seeds (no ROM needed) and
//! prints a digest of each one's randomization (map, doors, items), so two hosts' outputs can be diffed.
//! parity <data dir> <first seed> <count>
use std::path::Path;

fn fnv(b: &[u8]) -> u64 { b.iter().fold(0xcbf29ce484222325u64, |h, &x| (h ^ x as u64).wrapping_mul(0x100000001b3)) }

fn main() {
    let a: Vec<String> = std::env::args().collect();
    let first: u64 = a[2].parse().unwrap();
    let count: u64 = a[3].parse().unwrap();
    let presets = [
        r#"{"preset":"Default"}"#,
        r#"{"preset":"Community Race Season 5"}"#,
        r#"{"preset":"Mentor Tournament"}"#,
        r#"{"preset":"Summer Series Expert Challenge"}"#,
    ];
    if a.len() > 4 {   // parity <dir> <seed> 1 <preset index>: the whole randomization JSON of one seed
        let r = smz3_maprando::randomization_with(Path::new(&a[1]), presets[a[4].parse::<usize>().unwrap()], first).unwrap();
        println!("{r}");
        return;
    }
    for p in presets {
        for s in first..first + count {
            if let Ok(rp) = std::env::var("PARITY_ROM") {   // the whole seed ROM, from a real vanilla ROM
                let rom = std::fs::read(&rp).unwrap();
                match smz3_maprando::generate_with(Path::new(&a[1]), p, s, 0, &rom) {
                    Ok((out, info)) => println!("{p} {s} ROM {:016x} {} {info}", fnv(&out), out.len()),
                    Err(e) => println!("{p} {s} ERR {e:#}"),
                }
                continue;
            }
            match smz3_maprando::randomization_with(Path::new(&a[1]), p, s) {
                Ok(r) => println!("{p} {s} {:016x} {} {}", fnv(r.as_bytes()), r.len(), &r[..r.find(' ').unwrap_or(0)]),
                Err(e) => println!("{p} {s} ERR {e:#}"),
            }
        }
    }
}
