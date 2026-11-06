#!/usr/bin/env python3
"""Add files to an APK produced by aapt2.

aapt2 link builds the manifest, resources and assets, but it has no idea what a JNI library is —
lib/<abi>/*.so has to be appended afterwards. The usual tool for that is `zip`, which is not
installed on the dev box, and zipfile gives finer control anyway: entry names must use forward slashes and
keep the exact lib/<abi>/name.so shape, or the loader will not find the library at all.

    apkzip.py <apk> <staging-dir> <relative-path> [<relative-path> ...]

Paths are relative to the staging directory and become the entry names verbatim.
"""

import sys
import zipfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2

    apk = Path(sys.argv[1])
    staging = Path(sys.argv[2])
    entries = sys.argv[3:]

    if not apk.is_file():
        print(f"apkzip: {apk} does not exist", file=sys.stderr)
        return 1

    with zipfile.ZipFile(apk, "a", compression=zipfile.ZIP_DEFLATED) as zf:
        existing = set(zf.namelist())
        for rel in entries:
            source = staging / rel
            if not source.is_file():
                print(f"apkzip: missing {source}", file=sys.stderr)
                return 1
            name = Path(rel).as_posix()
            if name in existing:
                print(f"apkzip: {name} is already in the APK", file=sys.stderr)
                return 1
            # Native libraries are stored, not deflated, so the loader can map them straight
            # out of the APK (extractNativeLibs=false); zipalign -P 16 then page-aligns them.
            kind = zipfile.ZIP_STORED if name.endswith('.so') else zipfile.ZIP_DEFLATED
            zf.write(source, name, compress_type=kind)
            print(f"  + {name} ({source.stat().st_size} bytes)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
