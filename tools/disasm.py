"""Disassemble SLES_512.56 by symbol or address (R5900, via rabbitizer).

    python -I tools/disasm.py tick__6ClGameFv           # whole function by symbol
    python -I tools/disasm.py 0x15eb00 --count 40        # N instructions from an address
    python -I tools/disasm.py --sym 0x15eb3c             # which symbol contains an address
    python -I tools/disasm.py --find ClGame              # list symbols matching a substring
    python -I tools/disasm.py --gpref=-0x7120            # loads/stores of _gp + offset (any width)
"""
from __future__ import annotations

import argparse
import bisect
import struct
import sys
from pathlib import Path

import rabbitizer
from elftools.elf.elffile import ELFFile

REPO = Path(__file__).resolve().parent.parent
ELF = REPO / "TwoTowersRecompLib" / "private" / "disc" / "SLES_512.56"


class Image:
    def __init__(self, path: Path):
        with path.open("rb") as fh:
            elf = ELFFile(fh)
            self.segments = []
            for seg in elf.iter_segments():
                if seg["p_type"] == "PT_LOAD":
                    data = seg.data() + bytes(seg["p_memsz"] - seg["p_filesz"])
                    self.segments.append((seg["p_vaddr"], data))
            self.funcs = []
            self.objects = []
            for sym in elf.get_section_by_name(".symtab").iter_symbols():
                kind = sym["st_info"]["type"]
                if kind == "STT_FUNC":
                    self.funcs.append((sym["st_value"], sym["st_size"], sym.name))
                elif kind == "STT_OBJECT":
                    self.objects.append((sym["st_value"], sym["st_size"], sym.name))
        self.funcs.sort()
        self.objects.sort()
        self.all_syms = sorted(self.funcs + self.objects)
        self.by_name = {name: (addr, size) for addr, size, name in self.all_syms}
        self._starts = [a for a, _, _ in self.all_syms]

    def read(self, addr: int, size: int) -> bytes:
        for base, data in self.segments:
            if base <= addr and addr + size <= base + len(data):
                return data[addr - base:addr - base + size]
        raise ValueError(f"0x{addr:08x} is outside the loaded segments")

    def word(self, addr: int) -> int:
        return struct.unpack("<I", self.read(addr, 4))[0]

    def symbolize(self, addr: int) -> str:
        i = bisect.bisect_right(self._starts, addr) - 1
        if i < 0:
            return f"0x{addr:08x}"
        base, size, name = self.all_syms[i]
        if addr == base:
            return name
        if size == 0 or addr < base + max(size, 4):
            return f"{name}+0x{addr - base:x}"
        return f"0x{addr:08x}"


def disassemble(img: Image, start: int, count: int) -> None:
    for i in range(count):
        addr = start + 4 * i
        word = img.word(addr)
        instr = rabbitizer.Instruction(word, vram=addr, category=rabbitizer.InstrCategory.R5900)
        text = instr.disassemble()
        note = ""
        if instr.isJumpWithAddress():
            note = f"  ; -> {img.symbolize(instr.getInstrIndexAsVram())}"
        elif instr.isBranch():
            note = f"  ; -> 0x{instr.getBranchVramGeneric():x}"
        label = img.symbolize(addr)
        prefix = f"{label}:" if "+" not in label else ""
        if prefix:
            print(prefix)
        print(f"  {addr:08x}: {word:08x}  {text}{note}")


GP_BASE = 28
LOAD_STORE_OPS = {
    0x20: "lb", 0x21: "lh", 0x23: "lw", 0x24: "lbu", 0x25: "lhu", 0x27: "lwu", 0x37: "ld", 0x1E: "lq",
    0x28: "sb", 0x29: "sh", 0x2B: "sw", 0x3F: "sd", 0x1F: "sq", 0x31: "lwc1", 0x39: "swc1",
}


def find_gp_refs(img: Image, offset: int) -> None:
    """Every load/store whose address is $gp + offset, in executable code."""
    imm = offset & 0xFFFF
    for base, data in img.segments:
        for pos in range(0, len(data) - 3, 4):
            word = struct.unpack_from("<I", data, pos)[0]
            op = word >> 26
            if op in LOAD_STORE_OPS and (word >> 21) & 0x1F == GP_BASE and word & 0xFFFF == imm:
                addr = base + pos
                instr = rabbitizer.Instruction(word, vram=addr, category=rabbitizer.InstrCategory.R5900)
                print(f"  {addr:08x}: {instr.disassemble():40s} {img.symbolize(addr)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target", nargs="?")
    parser.add_argument("--count", type=int, default=0)
    parser.add_argument("--sym", type=lambda s: int(s, 0))
    parser.add_argument("--find")
    parser.add_argument("--gpref", type=lambda s: int(s, 0))
    args = parser.parse_args()

    img = Image(ELF)
    if args.sym is not None:
        print(img.symbolize(args.sym))
        return 0
    if args.gpref is not None:
        find_gp_refs(img, args.gpref)
        return 0
    if args.find:
        for addr, size, name in img.all_syms:
            if args.find.lower() in name.lower():
                print(f"0x{addr:08x} {size:>6} {name}")
        return 0
    if not args.target:
        parser.error("give a symbol or address")

    if args.target in img.by_name:
        start, size = img.by_name[args.target]
        count = args.count or max(size // 4, 1)
    else:
        start = int(args.target, 0)
        count = args.count or 32
    disassemble(img, start, count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
