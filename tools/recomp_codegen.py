"""Drive PS2Recomp for SLES_512.56.

    python -I tools/recomp_codegen.py verify     # check the extracted boot ELF is the supported build
    python -I tools/recomp_codegen.py analyze    # run ps2_analyzer -> out/analysis/
    python -I tools/recomp_codegen.py import-analyzer  # adopt the analyzer output as the tracked config
    python -I tools/recomp_codegen.py generate   # run ps2_recomp -> TwoTowersRecompLib/generated/

`generate` writes into a staging folder first; a failed run keeps the previous output.
Build the tools beforehand with tools/build_tools.ps1.

When a Ghidra map is configured, `generate` does not hand it to ps2_recomp as-is: it builds
out/codegen/functions.csv from the ELF symbol table (exact compiler boundaries) plus the Ghidra
functions that fall outside every symbol (crt0). Ghidra's executable labels inside functions are
dropped: ps2_recomp turns each one into a fragment that ends at the next label, which can split a
branch from its delay slot (e.g. main's `jal ClGame::tick` lost the `addiu a0` that builds `this`).
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import re
import shutil
import struct
import subprocess
import sys
import tomllib
from pathlib import Path

from elftools.elf.elffile import ELFFile

REPO = Path(__file__).resolve().parent.parent
CONFIG = REPO / "TwoTowersRecompLib" / "config" / "sles_51256.toml"
TOOLS_BUILD = REPO / "out" / "build" / "tools"
CODEGEN_DIR = REPO / "out" / "codegen"
ANALYSIS_DIR = REPO / "out" / "analysis"

SUPPORTED_SHA1 = {
    "414d5a62dd36d82dcab8488b93e2e772e6e43fba": "SLES-51256 (PAL Spain, VER 1.00)",
}


def find_tool(name: str) -> Path:
    hits = sorted(TOOLS_BUILD.rglob(f"{name}.exe")) or sorted(TOOLS_BUILD.rglob(name))
    if not hits:
        sys.exit(f"{name} not found under {TOOLS_BUILD}. Run tools/build_tools.ps1 first.")
    return hits[0]


def load_config() -> dict:
    with CONFIG.open("rb") as fh:
        return tomllib.load(fh)


def cmd_verify(_args) -> int:
    elf = REPO / load_config()["general"]["input"]
    if not elf.is_file():
        print(f"Missing {elf}. Extract your disc with tools/extract_iso.py.")
        return 1
    digest = hashlib.sha1(elf.read_bytes()).hexdigest()
    label = SUPPORTED_SHA1.get(digest)
    if label is None:
        print(f"{elf.name}: sha1={digest} is not a supported build.")
        return 1
    print(f"{elf.name}: {label} OK")
    return 0


def cmd_analyze(_args) -> int:
    if cmd_verify(_args):
        return 1
    elf = REPO / load_config()["general"]["input"]
    ANALYSIS_DIR.mkdir(parents=True, exist_ok=True)
    out = ANALYSIS_DIR / "sles_51256.analyzer.toml"
    result = subprocess.run([str(find_tool("ps2_analyzer")), str(elf), str(out)], cwd=REPO)
    if result.returncode == 0:
        print(f"Analyzer config written to {out.relative_to(REPO)}")
    return result.returncode


def has_delay_slot(word: int) -> bool:
    """True for R5900 branches/jumps, whose following instruction is a delay slot."""
    op, rs, funct = word >> 26, (word >> 21) & 0x1F, word & 0x3F
    if op in (0x02, 0x03):                       # J, JAL
        return True
    if op == 0x00 and funct in (0x08, 0x09):     # JR, JALR
        return True
    if op in (0x01, 0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17):  # REGIMM, BEQ.., BEQL..
        return True
    return op in (0x10, 0x11, 0x12) and rs == 0x08  # BC0x, BC1x, BC2x


def build_function_map(elf_path: Path, ghidra_csv: Path, out_csv: Path) -> tuple[int, int, int]:
    """Write a ps2_recomp function map: ELF symbols + Ghidra functions in symbol gaps."""
    with elf_path.open("rb") as fh:
        elf = ELFFile(fh)
        code = [(seg["p_vaddr"], seg.data()) for seg in elf.iter_segments() if seg["p_type"] == "PT_LOAD"]
        funcs = sorted({(s["st_value"], s["st_value"] + s["st_size"], s.name)
                        for s in elf.get_section_by_name(".symtab").iter_symbols()
                        if s["st_info"]["type"] == "STT_FUNC" and s["st_size"] > 0})

    def word(addr: int) -> int:
        for base, data in code:
            if base <= addr < base + len(data) - 3:
                return struct.unpack_from("<I", data, addr - base)[0]
        return 0

    starts = [f[0] for f in funcs]
    start_set = set(starts)

    def inside_symbol(addr: int) -> bool:
        i = bisect.bisect_right(starts, addr) - 1
        return i >= 0 and funcs[i][0] <= addr < funcs[i][1]

    gap_rows = []
    dropped = 0
    for line in ghidra_csv.read_text(encoding="utf-8", errors="replace").splitlines()[1:]:
        # Names can contain commas (C++ templates), so split from the right.
        name, start, end, _size = line.rsplit(",", 3)
        start, end = int(start, 16), int(end, 16)
        if start in start_set:
            continue  # same function as the ELF symbol, which keeps its exact size
        if inside_symbol(start):
            dropped += 1
        else:
            gap_rows.append((start, end, name))
    gap_rows.sort()
    # A Ghidra label nested inside another gap function is a fragment too.
    kept_gaps = []
    for start, end, name in gap_rows:
        if kept_gaps and kept_gaps[-1][0] < start < kept_gaps[-1][1]:
            dropped += 1
            continue
        kept_gaps.append((start, end, name))

    rows = sorted(funcs + kept_gaps)
    extended = 0
    with out_csv.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("Name,Start,End,Size\n")
        for start, end, name in rows:
            if has_delay_slot(word(end - 4)):
                end += 4  # never end a function between a branch and its delay slot
                extended += 1
                print(f"  extended {name} to 0x{end:08X} to include its trailing delay slot")
            fh.write(f"{name.replace(',', '_')},0x{start:08X},0x{end:08X},{end - start}\n")
    return len(rows), dropped, extended


def cmd_import_analyzer(_args) -> int:
    source = ANALYSIS_DIR / "sles_51256.analyzer.toml"
    if not source.is_file():
        sys.exit(f"Missing {source.relative_to(REPO)}; run `analyze` first.")
    raw = source.read_bytes()
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        # ps2_analyzer writes absolute paths in the ANSI code page on Windows.
        text = raw.decode("cp1252")

    general = load_config()["general"]
    for key in ("input", "output", "ghidra_output"):
        text, count = re.subn(rf'(?m)^{key}\s*=.*$', f'{key} = "{general[key]}"', text, count=1)
        if count != 1:
            sys.exit(f"Could not find `{key} =` in {source.name}.")
    header = (
        "# PS2Recomp configuration for The Two Towers, PAL Spain (SLES-51256, VER 1.00).\n"
        "# Imported from ps2_analyzer output by `tools/recomp_codegen.py import-analyzer`; edit by hand from here.\n"
        "# Paths are relative to the repository root.\n"
    )
    CONFIG.write_text(header + text, encoding="utf-8", newline="\n")
    print(f"Updated {CONFIG.relative_to(REPO)} from {source.relative_to(REPO)}")
    return 0


def cmd_generate(args) -> int:
    if cmd_verify(args):
        return 1
    config = load_config()
    final_dir = REPO / config["general"]["output"]
    staging = CODEGEN_DIR / "staging"
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir(parents=True)

    # Same config, with the output redirected to the staging folder.
    text = CONFIG.read_text(encoding="utf-8")
    staged_output = staging.relative_to(REPO).as_posix()
    text, count = re.subn(r'(?m)^output\s*=.*$', f'output = "{staged_output}"', text, count=1)
    if count != 1:
        sys.exit("Could not find `output =` in the config.")
    ghidra_map = config["general"].get("ghidra_output", "")
    if ghidra_map and not (REPO / ghidra_map).is_file():
        # The map is produced locally by tools/ghidra/export_functions.ps1 and is not committed.
        print(f"Warning: {ghidra_map} not found; falling back to heuristic function boundaries. "
              "Run tools/ghidra/export_functions.ps1 for the recommended map.")
        text = re.sub(r'(?m)^ghidra_output\s*=.*$', 'ghidra_output = ""', text, count=1)
    elif ghidra_map:
        function_map = CODEGEN_DIR / "functions.csv"
        count, dropped, extended = build_function_map(REPO / config["general"]["input"], REPO / ghidra_map,
                                                      function_map)
        print(f"Function map: {count} functions ({dropped} intra-function Ghidra labels dropped, "
              f"{extended} ends extended over a delay slot) -> {function_map.relative_to(REPO)}")
        text = re.sub(r'(?m)^ghidra_output\s*=.*$',
                      f'ghidra_output = "{function_map.relative_to(REPO).as_posix()}"', text, count=1)
    staged_config = CODEGEN_DIR / "sles_51256.staged.toml"
    staged_config.write_text(text, encoding="utf-8")

    result = subprocess.run([str(find_tool("ps2_recomp")), str(staged_config)], cwd=REPO)
    produced = list(staging.glob("*.cpp"))
    if result.returncode != 0 or not produced:
        print(f"ps2_recomp failed (exit {result.returncode}, {len(produced)} sources); keeping previous output.")
        return result.returncode or 1

    for entry in final_dir.iterdir() if final_dir.exists() else []:
        if entry.name == ".gitkeep":
            continue
        shutil.rmtree(entry) if entry.is_dir() else entry.unlink()
    final_dir.mkdir(parents=True, exist_ok=True)
    for entry in staging.iterdir():
        shutil.move(str(entry), final_dir / entry.name)
    print(f"Generated {len(produced)} sources into {final_dir.relative_to(REPO)}. Re-run CMake configure to pick them up.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("verify").set_defaults(func=cmd_verify)
    sub.add_parser("analyze").set_defaults(func=cmd_analyze)
    sub.add_parser("import-analyzer").set_defaults(func=cmd_import_analyzer)
    sub.add_parser("generate").set_defaults(func=cmd_generate)
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
