#!/usr/bin/env bash
# build-android.sh [abi...] : builds kiln + samples for each ABI (default arm64-v8a x86_64)
# into build/android/<abi>/. 64-bit only (architecture 3.6).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/android-env.sh"
ABIS=("$@"); [ ${#ABIS[@]} -gt 0 ] || ABIS=(arm64-v8a x86_64)
for abi in "${ABIS[@]}"; do
    kiln_configure_android "$abi"
    cmake --build "$KILN_ROOT/build/android/$abi"
done
