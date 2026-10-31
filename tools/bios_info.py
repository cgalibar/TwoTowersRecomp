"""List PS2 BIOS dumps by their ROMVER and optionally install one for the runtime.

    python -I tools/bios_info.py <bios.bin | folder> [...]
    python -I tools/bios_info.py --install <bios.bin>   # copies to TwoTowersRecompLib/private/bios/

The runtime only reads ROMVER from the installed dump (see TwoTowersRecomp/bios/). Use a dump
of your own console; the European v1.6 (SCPH-30004, ROMVER 0160EC20011004) matches the
PAL release of the game best.
"""
from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BIOS_DIR = REPO / "TwoTowersRecompLib" / "private" / "bios"
BIOS_SIZE = 4 * 1024 * 1024
REGIONS = {"J": "Japan", "A": "USA", "E": "Europe", "H": "Asia", "C": "China", "T": "TOOL/devkit"}


def read_romdir(data: bytes) -> dict[str, bytes]:
    """Walk the ROMDIR table that starts with the RESET entry."""
    start = data.find(b"RESET\0")
    if start < 0:
        return {}
    files: dict[str, bytes] = {}
    offset = 0
    entry = start
    while entry + 16 <= len(data):
        name = data[entry:entry + 10].split(b"\0")[0]
        if not name:
            break
        size = struct.unpack_from("<I", data, entry + 12)[0]
        files[name.decode(errors="replace")] = data[offset:offset + size]
        offset += (size + 15) & ~15
        entry += 16
    return files


def describe(path: Path) -> str | None:
    data = path.read_bytes()
    if len(data) != BIOS_SIZE:
        return None
    romver = read_romdir(data).get("ROMVER", b"").split(b"\0")[0].decode(errors="replace").strip()
    if len(romver) < 14:
        return f"{'(no ROMVER)':<16} {path.name}"
    version = f"v{int(romver[0:2])}.{romver[2:4]}"
    region = REGIONS.get(romver[4], romver[4])
    date = f"{romver[6:10]}-{romver[10:12]}-{romver[12:14]}"
    sha1 = hashlib.sha1(data).hexdigest()[:12]
    return f"{romver:<16} {version:<6} {region:<8} {date}  {sha1}  {path.name}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="*", type=Path)
    parser.add_argument("--install", type=Path, help="copy this dump to TwoTowersRecompLib/private/bios/")
    args = parser.parse_args()

    if args.install:
        info = describe(args.install)
        if info is None or "(no ROMVER)" in info:
            sys.exit(f"{args.install} is not a 4 MiB PS2 BIOS dump with a ROMVER entry.")
        BIOS_DIR.mkdir(parents=True, exist_ok=True)
        for old in BIOS_DIR.glob("*.bin"):
            old.unlink()
        dest = BIOS_DIR / "bios.bin"
        shutil.copyfile(args.install, dest)
        print(f"Installed {info}\n  -> {dest.relative_to(REPO)}")
        return 0

    paths = args.paths or [BIOS_DIR]
    for root in paths:
        candidates = sorted(p for p in root.iterdir() if p.is_file()) if root.is_dir() else [root]
        for path in candidates:
            if (line := describe(path)) is not None:
                print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
