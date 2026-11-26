#!/usr/bin/env bash
# Builds the SMZ3 app (the vendored Lemuroid fork) with the JDK 21 Gradle already cached (the system JDK 25 is too new for Gradle 8.10).
#   ./build-android.sh            debug APK   -> build/smz3-debug.apk   (com.utsugi.smz3.debug, "SMZ3 debug")
#   ./build-android.sh release    release APK -> build/smz3-release.apk (com.utsugi.smz3, "SMZ3", signed with vendor/Lemuroid/release.jks)
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
variant="${1:-debug}"
python3 "$root/tools/gen_app_icon.py"
cd "$root/vendor/Lemuroid"
# the CI fetches lemuroid-cores without its prebuilt cores (the whole submodule is 6 GB)
[ -e lemuroid-cores/.git ] || git submodule update --init --depth 1
git -C ../LibretroDroid submodule update --init --depth 1
sdk="${ANDROID_HOME:-$HOME/Android/Sdk}"
echo "sdk.dir=$sdk" > local.properties
echo "sdk.dir=$sdk" > ../LibretroDroid/local.properties
# snes9x is built from the PC's source and commit, so both write the same save states (#!s9xsnp:0014)
s9x="$root/vendor/snes9x_libretro"
s9x_commit=fae2fea08f74180759ef540ee94259213f503480   # keep in step with build-pc.sh
[ -d "$s9x/.git" ] || git clone -q https://github.com/libretro/snes9x.git "$s9x"
git -C "$s9x" checkout -q "$s9x_commit"
ndk="${ANDROID_NDK_HOME:-$(ls -d "$sdk"/ndk/* | sort -V | tail -1)}"
s9x_out="$root/build/android-snes9x"
"$ndk/ndk-build" -s -C "$s9x/libretro/jni" APP_ABI=arm64-v8a NDK_OUT="$s9x_out/obj" NDK_LIBS_OUT="$s9x_out/libs" -j"$(getconf _NPROCESSORS_ONLN)"
s9x_jni=lemuroid-cores/lemuroid_core_snes9x/src/main/jniLibs/arm64-v8a
mkdir -p "$s9x_jni" && cp "$s9x_out/libs/arm64-v8a/libretro.so" "$s9x_jni/libsnes9x_libretro_android.so"
# keeps lemuroid-cores clean, so a git diff of vendor/Lemuroid (the patch) never picks up the swapped core
git -C lemuroid-cores update-index --skip-worktree "${s9x_jni#lemuroid-cores/}/libsnes9x_libretro_android.so" 2>/dev/null || true
jdk="$HOME/.gradle/jdks/eclipse_adoptium-21-amd64-linux.2"
[ -d "$jdk" ] && export JAVA_HOME="$jdk"   # elsewhere (the CI) JAVA_HOME already points at a JDK 21
mkdir -p ../../build
# the menu music (assets/music) goes in beside the app's own smz3 assets
mkdir -p "$root/build/android-assets/smz3" && cp "$root/assets/music/smz3_intro.mp3" "$root/build/android-assets/smz3/"
# Map Rando's data (tools/maprando/fetch.sh packs build/maprando-data) goes in as one zip, with a stamp that says when to unpack again
assets="$root/build/android-assets/maprando"
rm -rf "$assets"
if [ -d "$root/build/maprando-data/rust" ]; then
    mkdir -p "$assets"
    (cd "$root/build/maprando-data" && find . -type f | LC_ALL=C sort | zip -q -X -9 "$assets/data.zip" -@)
    md5sum "$assets/data.zip" | cut -c1-32 > "$assets/stamp.txt"
fi
if [ "$variant" = "release" ]; then
    # Local signing key (kept out of git); packaging the 190 MB APK needs more heap than the default 2.5 GB.
    [ -f release.jks ] || "$JAVA_HOME/bin/keytool" -genkeypair -keystore release.jks -alias lemuroid -storepass lemuroid -keypass lemuroid \
        -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=SMZ3 local release, O=utsugi"
    ./gradlew :lemuroid-app:assembleFreeBundleRelease --console=plain -Dorg.gradle.jvmargs="-Xmx8g -Dfile.encoding=UTF-8"
    cp lemuroid-app/build/outputs/apk/freeBundle/release/lemuroid-app-free-bundle-release.apk ../../build/smz3-release.apk
else
    ./gradlew :lemuroid-app:assembleFreeBundleDebug --console=plain
    cp lemuroid-app/build/outputs/apk/freeBundle/debug/lemuroid-app-free-bundle-debug.apk ../../build/smz3-debug.apk
fi
