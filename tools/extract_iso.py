"""Extract a PS2 DVD image (ISO9660) into the private game folder.

Usage:
    python -I tools/extract_iso.py <game.iso> [--out TwoTowersRecompLib/private/disc] [--list]

The original image is only read, never modified. Extracted files are ignored by Git.
"""
from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

import pycdlib

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = REPO_ROOT / "TwoTowersRecompLib" / "private" / "disc"


def walk(iso: pycdlib.PyCdlib):
    for dirname, _dirs, files in iso.walk(iso_path="/"):
        for name in files:
            iso_path = f"{dirname.rstrip('/')}/{name}"
            # ISO9660 level-1 names carry a ";1" version suffix.
            rel = iso_path.lstrip("/").split(";")[0]
            yield iso_path, rel


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--list", action="store_true", help="only list the files")
    args = parser.parse_args()

    iso = pycdlib.PyCdlib()
    iso.open(str(args.iso))
    try:
        pvd = iso.pvd
        print(f"Volume: {pvd.volume_identifier.decode(errors='replace').strip()}")
        print(f"System: {pvd.system_identifier.decode(errors='replace').strip()}")

        total = 0
        for iso_path, rel in walk(iso):
            record = iso.get_record(iso_path=iso_path)
            size = record.get_data_length()
            total += size
            if args.list:
                print(f"{size:>12}  {rel}")
                continue
            dest = args.out / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            if dest.exists() and dest.stat().st_size == size:
                continue
            with dest.open("wb") as fh:
                iso.get_file_from_iso_fp(fh, iso_path=iso_path)
            print(f"extracted {rel} ({size} bytes)")
        print(f"Total: {total / (1024 * 1024):.1f} MiB")
    finally:
        iso.close()

    if args.list:
        return 0

    cnf = args.out / "SYSTEM.CNF"
    if cnf.exists():
        print("\nSYSTEM.CNF:")
        print(cnf.read_text(errors="replace"))
        for line in cnf.read_text(errors="replace").splitlines():
            if line.strip().upper().startswith("BOOT2"):
                boot = line.split("cdrom0:\\", 1)[-1].split(";")[0].strip()
                elf = args.out / boot
                if elf.exists():
                    digest = hashlib.sha1(elf.read_bytes()).hexdigest()
                    print(f"Boot ELF: {boot}  sha1={digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
