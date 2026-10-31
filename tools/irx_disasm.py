"""Disassemble an IOP module (IRX) as loaded at a given base address (R3000A, via rabbitizer).

    python -I tools/irx_disasm.py FUSTREAM.IRX --base 0x86d00 0x91894 --count 40
    python -I tools/irx_disasm.py FUSTREAM.IRX --base 0x86d00 --imports
    python -I tools/irx_disasm.py FUSTREAM.IRX --base 0x86d00 --strings

The file is looked up in TwoTowersRecompLib/private/disc/IOP/ unless a path is given.
Relocations are not applied, so absolute jal targets are shown both raw and rebased.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import rabbitizer

REPO = Path(__file__).resolve().parent.parent
IOP_DIR = REPO / "TwoTowersRecompLib" / "private" / "disc" / "IOP"


def load_text(path: Path) -> tuple[bytes, int]:
    """Return (memory image starting at vaddr 0, entry offset) from the IRX program header."""
    data = path.read_bytes()
    e_phoff = struct.unpack_from("<I", data, 0x1C)[0]
    e_phnum = struct.unpack_from("<H", data, 0x2C)[0]
    e_entry = struct.unpack_from("<I", data, 0x18)[0]
    image = bytearray()
    for i in range(e_phnum):
        p_type, p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz = struct.unpack_from("<6I", data, e_phoff + 32 * i)
        if p_type == 1:  # PT_LOAD
            end = p_vaddr + p_memsz
            if len(image) < end:
                image.extend(bytes(end - len(image)))
            image[p_vaddr:p_vaddr + p_filesz] = data[p_offset:p_offset + p_filesz]
    return bytes(image), e_entry


def imports(image: bytes, base: int) -> list[tuple[int, str, int]]:
    out = []
    i = 0
    while True:
        i = image.find(struct.pack("<I", 0x41E00000), i)
        if i < 0:
            return out
        lib = image[i + 12:i + 20].split(b"\0")[0].decode(errors="replace")
        j = i + 20
        while j + 8 <= len(image):
            a, b = struct.unpack_from("<II", image, j)
            if a != 0x03E00008:
                break
            out.append((base + j, lib, b & 0xFFFF))
            j += 8
        i = j


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("irx")
    parser.add_argument("address", nargs="?", type=lambda s: int(s, 0))
    parser.add_argument("--base", type=lambda s: int(s, 0), required=True)
    parser.add_argument("--count", type=int, default=32)
    parser.add_argument("--imports", action="store_true")
    parser.add_argument("--strings", action="store_true")
    args = parser.parse_intermixed_args()

    path = Path(args.irx)
    if not path.is_file():
        path = IOP_DIR / args.irx
    image, entry = load_text(path)
    stubs = {addr: (lib, num) for addr, lib, num in imports(image, args.base)}

    if args.imports:
        for addr, (lib, num) in sorted(stubs.items()):
            print(f"0x{addr:08x} {lib}:{num}")
        return 0
    if args.strings:
        run = b""
        for off, byte in enumerate(image + b"\0"):
            if 32 <= byte < 127:
                run += bytes([byte])
                continue
            if len(run) >= 6:
                print(f"0x{args.base + off - len(run):08x} {run.decode()}")
            run = b""
        return 0

    start = args.address if args.address is not None else args.base + entry
    for k in range(args.count):
        addr = start + 4 * k
        off = addr - args.base
        if not 0 <= off < len(image) - 3:
            break
        word = struct.unpack_from("<I", image, off)[0]
        instr = rabbitizer.Instruction(word, vram=addr, category=rabbitizer.InstrCategory.CPU)
        note = ""
        if instr.isJumpWithAddress():
            raw = instr.getInstrIndexAsVram() & 0x0FFFFFFF
            target = args.base + raw if raw < len(image) else raw
            note = f"  ; -> 0x{target:08x}"
            if target in stubs:
                lib, num = stubs[target]
                note += f" [{lib}:{num}]"
        elif instr.isBranch():
            note = f"  ; -> 0x{instr.getBranchVramGeneric():08x}"
        print(f"  {addr:08x}: {word:08x}  {instr.disassemble()}{note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
