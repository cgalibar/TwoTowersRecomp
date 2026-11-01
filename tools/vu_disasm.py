"""Disassemble VU0/VU1 microcode (64-bit upper/lower pairs).

    python -I tools/vu_disasm.py out/vu1code.bin                 # whole dump
    python -I tools/vu_disasm.py out/vu1code.bin --start 0xa0 --count 40
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

BC = "xyzw"


def dest(word: int) -> str:
    bits = (word >> 21) & 0xF
    return "".join(c for c, b in zip("xyzw", (8, 4, 2, 1)) if bits & b) or "-"


def upper(word: int) -> str:
    ft, fs, fd = (word >> 16) & 31, (word >> 11) & 31, (word >> 6) & 31
    op = word & 0x3F
    d = dest(word)
    three = ["ADD", "SUB", "MADD", "MSUB", "MAX", "MINI", "MUL"]
    if op < 0x1C:
        return f"{three[op >> 2]}{BC[op & 3]}.{d} vf{fd}, vf{fs}, vf{ft}{BC[op & 3]}"
    simple = {
        0x1C: "MULq", 0x1D: "MAXi", 0x1E: "MULi", 0x1F: "MINIi", 0x20: "ADDq", 0x21: "MADDq",
        0x22: "ADDi", 0x23: "MADDi", 0x24: "SUBq", 0x25: "MSUBq", 0x26: "SUBi", 0x27: "MSUBi",
    }
    if op in simple:
        return f"{simple[op]}.{d} vf{fd}, vf{fs}"
    full = {0x28: "ADD", 0x29: "MADD", 0x2A: "MUL", 0x2B: "MAX", 0x2C: "SUB", 0x2D: "MSUB",
            0x2E: "OPMSUB", 0x2F: "MINI"}
    if op in full:
        return f"{full[op]}.{d} vf{fd}, vf{fs}, vf{ft}"
    if op >= 0x3C:
        sub = (((word >> 6) & 0x1F) << 2) | (word & 3)
        if sub < 0x10:
            name = ["ADDA", "SUBA", "MADDA", "MSUBA"][sub >> 2]
            return f"{name}{BC[sub & 3]}.{d} ACC, vf{fs}, vf{ft}{BC[sub & 3]}"
        if 0x10 <= sub < 0x18:
            name = ["ITOF0", "ITOF4", "ITOF12", "ITOF15", "FTOI0", "FTOI4", "FTOI12", "FTOI15"][sub - 0x10]
            return f"{name}.{d} vf{ft}, vf{fs}"
        if 0x18 <= sub < 0x1C:
            return f"MULA{BC[sub & 3]}.{d} ACC, vf{fs}, vf{ft}{BC[sub & 3]}"
        names = {0x1C: "MULAq", 0x1D: "ABS", 0x1E: "MULAi", 0x1F: "CLIP", 0x20: "ADDAq", 0x21: "MADDAq",
                 0x22: "ADDAi", 0x23: "MADDAi", 0x24: "SUBAq", 0x25: "MSUBAq", 0x26: "SUBAi",
                 0x27: "MSUBAi", 0x28: "ADDA", 0x29: "MADDA", 0x2A: "MULA", 0x2C: "SUBA", 0x2D: "MSUBA",
                 0x2E: "OPMULA", 0x2F: "NOP"}
        name = names.get(sub, f"upper?{sub:02x}")
        if name == "NOP":
            return "NOP"
        if name == "ABS":
            return f"ABS.{d} vf{ft}, vf{fs}"
        if name == "CLIP":
            return f"CLIPw.xyz vf{fs}, vf{ft}w"
        return f"{name}.{d} ACC, vf{fs}, vf{ft}"
    return f"upper?{word:08x}"


def simm(value: int, bits: int) -> int:
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def lower(word: int, pc: int) -> str:
    op = word >> 25
    ft, fs, fd = (word >> 16) & 31, (word >> 11) & 31, (word >> 6) & 31
    d = dest(word)
    imm11 = simm(word & 0x7FF, 11)
    if op == 0x40:
        sub = word & 0x3F
        integer = {0x30: "IADD", 0x31: "ISUB", 0x34: "IAND", 0x35: "IOR"}
        if sub in integer:
            return f"{integer[sub]} vi{fd}, vi{fs}, vi{ft}"
        if sub == 0x32:
            return f"IADDI vi{ft}, vi{fs}, {simm(fd, 5)}"
        if sub >= 0x3C:
            sub2 = (((word >> 6) & 0x1F) << 2) | (word & 3)
            names = {0x30: "MOVE", 0x31: "MR32", 0x34: "LQI", 0x35: "SQI", 0x36: "LQD", 0x37: "SQD",
                     0x38: "DIV", 0x39: "SQRT", 0x3A: "RSQRT", 0x3B: "WAITQ", 0x3C: "MTIR", 0x3D: "MFIR",
                     0x3E: "ILWR", 0x3F: "ISWR", 0x40: "RNEXT", 0x41: "RGET", 0x42: "RINIT", 0x43: "RXOR",
                     0x64: "MFP", 0x68: "XTOP", 0x69: "XITOP", 0x6C: "XGKICK", 0x70: "ESADD", 0x71: "ERSADD",
                     0x72: "ELENG", 0x73: "ERLENG", 0x74: "EATANxy", 0x75: "EATANxz", 0x76: "ESUM",
                     0x78: "ESQRT", 0x79: "ERSQRT", 0x7A: "ERCPR", 0x7B: "WAITP", 0x7C: "ESIN", 0x7D: "EATAN",
                     0x7E: "EEXP"}
            name = names.get(sub2, f"lower?{sub2:02x}")
            fsf, ftf = BC[(word >> 21) & 3], BC[(word >> 23) & 3]
            if name in ("MOVE", "MR32"):
                return f"{name}.{d} vf{ft}, vf{fs}"
            if name in ("LQI", "LQD"):
                return f"{name}.{d} vf{ft}, (vi{fs}{'++' if name == 'LQI' else '--'})"
            if name in ("SQI", "SQD"):
                return f"{name}.{d} vf{fs}, (vi{ft}{'++' if name == 'SQI' else '--'})"
            if name in ("DIV", "RSQRT"):
                return f"{name} Q, vf{fs}{fsf}, vf{ft}{ftf}"
            if name == "SQRT":
                return f"SQRT Q, vf{ft}{ftf}"
            if name == "MTIR":
                return f"MTIR vi{ft}, vf{fs}{fsf}"
            if name == "MFIR":
                return f"MFIR.{d} vf{ft}, vi{fs}"
            if name in ("ILWR", "ISWR"):
                return f"{name}.{d} vi{ft}, (vi{fs})"
            if name in ("XGKICK", "XTOP", "XITOP"):
                return f"{name} vi{fs if name == 'XGKICK' else ft}"
            if name == "MFP":
                return f"MFP.{d} vf{ft}, P"
            if name.startswith("E"):
                return f"{name} P, vf{fs}"
            return name
        return f"lower?{word:08x}"
    if op in (0x00, 0x01):
        if op == 0x00:
            return f"LQ.{d} vf{ft}, {imm11}(vi{fs})"
        return f"SQ.{d} vf{fs}, {imm11}(vi{ft})"
    if op in (0x04, 0x05):
        return f"{'ILW' if op == 4 else 'ISW'}.{d} vi{ft}, {imm11}(vi{fs})"
    if op in (0x08, 0x09):
        imm15 = (((word >> 21) & 0xF) << 11) | (word & 0x7FF)
        return f"{'IADDIU' if op == 8 else 'ISUBIU'} vi{ft}, vi{fs}, 0x{imm15:x}"
    if 0x10 <= op <= 0x1C:
        names = {0x10: "FCEQ", 0x11: "FCSET", 0x12: "FCAND", 0x13: "FCOR", 0x14: "FSEQ", 0x15: "FSSET",
                 0x16: "FSAND", 0x17: "FSOR", 0x18: "FMEQ", 0x1A: "FMAND", 0x1B: "FMOR", 0x1C: "FCGET"}
        name = names.get(op, f"lower?{op:02x}")
        if op <= 0x13:
            return f"{name} vi1, 0x{word & 0xFFFFFF:06x}"
        if op == 0x1C:
            return f"FCGET vi{ft}"
        if op in (0x18, 0x1A, 0x1B):
            return f"{name} vi{ft}, vi{fs}"
        return f"{name} vi{ft}, 0x{((word >> 21) & 1) << 11 | (word & 0x7FF):x}"
    target = (pc + 8 + imm11 * 8) & 0x3FFF
    branches = {0x20: "B", 0x21: "BAL", 0x28: "IBEQ", 0x29: "IBNE", 0x2C: "IBLTZ", 0x2D: "IBGTZ",
                0x2E: "IBLEZ", 0x2F: "IBGEZ"}
    if op in branches:
        name = branches[op]
        if op == 0x20:
            return f"B 0x{target:x}"
        if op == 0x21:
            return f"BAL vi{ft}, 0x{target:x}"
        if op in (0x28, 0x29):
            return f"{name} vi{ft}, vi{fs}, 0x{target:x}"
        return f"{name} vi{fs}, 0x{target:x}"
    if op == 0x24:
        return f"JR vi{fs}"
    if op == 0x25:
        return f"JALR vi{ft}, vi{fs}"
    return f"lower?{word:08x}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump", type=Path)
    parser.add_argument("--start", type=lambda s: int(s, 0), default=0)
    parser.add_argument("--count", type=int, default=0)
    args = parser.parse_args()

    data = args.dump.read_bytes()
    end = len(data) if args.count == 0 else min(len(data), args.start + args.count * 8)
    for pc in range(args.start & ~7, end, 8):
        lo, up = struct.unpack_from("<II", data, pc)
        if lo == 0 and up == 0:
            continue
        flags = "".join(f for f, b in (("I", 31), ("E", 30), ("M", 29), ("D", 28), ("T", 27)) if up & (1 << b))
        low = f"LOI {struct.unpack('<f', struct.pack('<I', lo))[0]:g}" if up & (1 << 31) else lower(lo, pc)
        print(f"{pc:04x}: {upper(up & 0x07FFFFFF):40s} {low:40s} {flags}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
