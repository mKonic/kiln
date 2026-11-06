#!/usr/bin/env bash
# build-android.sh [abi...] : builds kiln + samples for each ABI (default arm64-v8a x86_64)
# into build/android/<abi>/. 64-bit only (architecture 3.6).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/android-env.sh"
ABIS=("$@"); [ ${#ABIS[@]} -gt 0 ] || ABIS=(arm64-v8a x86_64)
for abi in "${ABIS[@]}"; do
    dir="$KILN_ROOT/build/android/$abi"
    cmake -S "$KILN_ROOT" -B "$dir" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" -DANDROID_PLATFORM="android-$ANDROID_MIN_SDK" \
        -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE="${KILN_CONFIG:-RelWithDebInfo}" >/dev/null
    cmake --build "$dir"
done
