#!/usr/bin/env bash
# Resolved Android toolchain paths, sourced by the other tools. Override with KILN_NDK / KILN_SDK.
KILN_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK="${KILN_NDK:-${ANDROID_NDK_ROOT:-/opt/android-ndk}}"
SDK="${KILN_SDK:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
# minSdk 31 is derived (architecture 3.6): TextRunShaper, Canvas.drawGlyphs, NDK ICU4C.
ANDROID_MIN_SDK="${KILN_MIN_SDK:-31}"
ANDROID_TARGET_SDK="${KILN_TARGET_SDK:-36}"
BUILD_TOOLS="$(find "$SDK/build-tools" -maxdepth 1 -mindepth 1 -type d 2>/dev/null | sort -V | tail -1)"
ANDROID_JAR="$(find "$SDK/platforms" -maxdepth 2 -name android.jar 2>/dev/null | sort -V | tail -1)"
NDK_TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
AAPT2="$BUILD_TOOLS/aapt2"; D8="$BUILD_TOOLS/d8"; ZIPALIGN="$BUILD_TOOLS/zipalign"
APKSIGNER="$BUILD_TOOLS/apksigner"; ADB="$SDK/platform-tools/adb"
for f in "$NDK/build/cmake/android.toolchain.cmake" "$AAPT2" "$D8" "$ANDROID_JAR"; do
    [ -e "$f" ] || { echo "android-env: missing $f" >&2; exit 1; }
done

# Configures build/android/<abi> (again each time, so KILN_CONFIG changes apply).
kiln_configure_android() {
    local abi="$1" dir="$KILN_ROOT/build/android/$1"
    cmake -S "$KILN_ROOT" -B "$dir" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" -DANDROID_PLATFORM="android-$ANDROID_MIN_SDK" \
        -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE="${KILN_CONFIG:-RelWithDebInfo}" >/dev/null
}
