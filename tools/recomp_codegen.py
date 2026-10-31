"""Drive PS2Recomp for SLES_512.56.

    python -I tools/recomp_codegen.py verify     # check the extracted boot ELF is the supported build
    python -I tools/recomp_codegen.py analyze    # run ps2_analyzer -> out/analysis/
    python -I tools/recomp_codegen.py import-analyzer  # adopt the analyzer output as the tracked config
    python -I tools/recomp_codegen.py generate   # run ps2_recomp -> TwoTowersRecompLib/generated/

`generate` writes into a staging folder first; a failed run keeps the previous output.
Build the tools beforehand with tools/build_tools.ps1.
"""
from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

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
