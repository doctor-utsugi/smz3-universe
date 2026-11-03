## Quick install notes

You need your own Super Metroid (JU) and A Link to the Past (Japan v1.0) ROMs. Full steps are in the
**[installation guide](https://github.com/doctor-utsugi/smz3-universe#2-install-it)**.

- **AYN Thor / Android phones:** install `SMZ3-android-arm64.apk`, create a run and pick your two ROMs (or a zip) when asked.
- **3DS:** copy `SMZ3-3ds.3dsx` to `sdmc:/3ds/` and the ROMs to `sdmc:/3ds/SMZ3/`, then open SMZ3 from the Homebrew Launcher. No sound? Run DSP1 once.
- **Linux:** `chmod +x SMZ3-x86_64.AppImage`, run it and drop the ROMs on the window.
- **Linux on ARM (aarch64):** the same with `SMZ3-aarch64.AppImage`. Without FUSE, start it with `APPIMAGE_EXTRACT_AND_RUN=1 ./SMZ3-aarch64.AppImage`.
- **Windows:** unzip `SMZ3-windows-x64.zip`, run `smz3.exe` (SmartScreen: More info, Run anyway) and drop the ROMs on the window.
- **macOS (Intel, also Apple silicon through Rosetta):** `tar xzf SMZ3-macos-intel.tar.gz`, `xattr -d com.apple.quarantine smz3`, then `./smz3`. Not signed.
