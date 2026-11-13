#!/usr/bin/env bash
# version.sh [code|name|both]: kiln's version, from git. Releasing is tagging (vMAJOR.MINOR.PATCH).
#   code  10000 + the number of commits (monotonic: which build is newer)
#   name  git describe: v0.1.0, v0.1.0-12-gabc1234 past a tag, a bare hash before the first
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
code=$((10000 + $(git rev-list --count HEAD)))
name=$(git describe --tags --always --dirty)
case "${1:-both}" in
    code) echo "$code" ;;
    name) echo "$name" ;;
    both) echo "$name ($code)" ;;
    *) echo "usage: scripts/version.sh [code|name|both]" >&2; exit 2 ;;
esac
