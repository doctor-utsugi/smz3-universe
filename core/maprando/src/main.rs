//! smz3-maprando <data dir> <vanilla SM ROM> <settings json> <seed> <out> [--bps]: a seed as smz_mr_generate builds it, or
//! with --bps its patch from the headerless vanilla ROM (what the server sends the 3DS). Prints the info JSON last.
use anyhow::{Context, Result, bail};
use std::path::Path;

fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 6 {
        bail!("usage: smz3-maprando <data dir> <rom> <settings json> <seed> <out> [--bps]");
    }
    let rom = std::fs::read(&args[2]).context("Reading the ROM")?;
    let seed: u64 = args[4].parse().context("Bad seed")?;
    let (out, info) = smz3_maprando::generate_with(Path::new(&args[1]), &args[3], seed, 0, &rom)?;
    if args.get(6).map(String::as_str) == Some("--bps") {
        let plain = if rom.len() % 0x8000 == 0x200 { &rom[0x200..] } else { &rom[..] };
        std::fs::write(&args[5], smz3_maprando::bps_encode(plain, &out))?;
    } else {
        std::fs::write(&args[5], &out)?;
    }
    println!("{info}");
    Ok(())
}
