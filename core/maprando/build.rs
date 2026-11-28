//! SMZ_MR_PACK_KEY: a hash of the sources that shape a packed GameData (its types and how it is loaded), so a pack
//! written by another build of the code is not read as this one's.
use std::path::{Path, PathBuf};

fn files(dir: &Path, out: &mut Vec<PathBuf>) {
    let Ok(rd) = std::fs::read_dir(dir) else { return };
    for e in rd.flatten() {
        let p = e.path();
        if p.is_dir() {
            files(&p, out);
        } else if p.extension().is_some_and(|x| x == "rs") {
            out.push(p);
        }
    }
}

fn main() {
    let rust = Path::new("../../vendor/maprando/rust");
    let mut list = vec![PathBuf::from("src/lib.rs"), PathBuf::from("Cargo.lock")];
    for c in ["maprando-game", "maprando-logic", "maprando"] {
        files(&rust.join(c).join("src"), &mut list);
        list.push(rust.join(c).join("Cargo.toml"));
    }
    list.sort();
    let mut h: u64 = 0xcbf2_9ce4_8422_2325;
    for p in &list {
        println!("cargo:rerun-if-changed={}", p.display());
        let name = p.strip_prefix(rust).unwrap_or(p).to_string_lossy().into_owned();
        for b in name.bytes().chain(std::fs::read(p).unwrap_or_default()) {
            h = (h ^ b as u64).wrapping_mul(0x100_0000_01b3);
        }
    }
    println!("cargo:rustc-env=SMZ_MR_PACK_KEY={h:016x}");
}
