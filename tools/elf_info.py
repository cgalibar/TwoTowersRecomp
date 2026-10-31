"""Print a summary of a PS2 EE ELF: identity hashes, segments, sections and symbol coverage.

Usage:
    python -I tools/elf_info.py TwoTowersRecompLib/private/disc/SLES_512.56 [--symbols N]
"""
from __future__ import annotations

import argparse
import hashlib
import sys
import zlib
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--symbols", type=int, default=0, help="print the first N function symbols")
    args = parser.parse_args()

    data = args.elf.read_bytes()
    print(f"File:   {args.elf.name}  ({len(data)} bytes)")
    print(f"SHA-1:  {hashlib.sha1(data).hexdigest()}")
    print(f"CRC32:  {zlib.crc32(data) & 0xFFFFFFFF:08X}")

    with args.elf.open("rb") as fh:
        elf = ELFFile(fh)
        hdr = elf.header
        print(f"Class:  {elf.elfclass}-bit {'LE' if elf.little_endian else 'BE'}  machine={hdr['e_machine']}  "
              f"flags=0x{hdr['e_flags']:08X}")
        print(f"Entry:  0x{hdr['e_entry']:08X}")

        print("\nSegments:")
        for seg in elf.iter_segments():
            h = seg.header
            print(f"  {h['p_type']:<12} vaddr=0x{h['p_vaddr']:08X} filesz=0x{h['p_filesz']:07X} "
                  f"memsz=0x{h['p_memsz']:07X} flags={h['p_flags']}")

        print("\nSections:")
        for sec in elf.iter_sections():
            h = sec.header
            if not sec.name:
                continue
            print(f"  {sec.name:<20} addr=0x{h['sh_addr']:08X} size=0x{h['sh_size']:07X} type={h['sh_type']}")

        funcs = []
        objects = 0
        for sec in elf.iter_sections():
            if not isinstance(sec, SymbolTableSection):
                continue
            for sym in sec.iter_symbols():
                kind = sym["st_info"]["type"]
                if kind == "STT_FUNC":
                    funcs.append((sym["st_value"], sym["st_size"], sym.name))
                elif kind == "STT_OBJECT":
                    objects += 1
        has_dwarf = elf.has_dwarf_info()
        print(f"\nSymbols: {len(funcs)} functions, {objects} objects, DWARF={'yes' if has_dwarf else 'no'}")
        for addr, size, name in sorted(funcs)[: args.symbols]:
            print(f"  0x{addr:08X} {size:>6} {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
