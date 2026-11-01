# TwoTowersRecomp

A static recompilation of **The Lord of the Rings: The Two Towers** (PlayStation 2, 2002) into a native
Windows executable, following the approach of [Lost Odyssey Recomp](https://github.com/freefrank/LostOdysseyRecomp)
and built on top of [PS2Recomp](https://github.com/ran-j/PS2Recomp).

> [!WARNING]
> Work in progress. The game boots, plays the intro movie and enters the prologue level (3D world,
> characters, sky and HUD render), but it is **not playable yet**: it runs far below real time, there is
> no audio output, and gameplay has not been verified. See [docs/STATUS.md](docs/STATUS.md).

## What it is

The MIPS R5900 code of the original executable (`SLES_512.56`, PAL Spain) is translated to C++ by
PS2Recomp and compiled for x64. A runtime emulates the rest of the PS2 hardware (GS, VU0/VU1, IOP, DMA,
SPU2...) and reads the game's data from your own copy of the disc.

**This repository contains no game data and no BIOS.** You need your own original copy of the PAL Spain
release (`SLES-51256`) and, recommended, a dump of your console's BIOS. Please do not ask for or share
ISOs or BIOS files.

## Repository layout

| Path | Contents |
| --- | --- |
| `TwoTowersRecomp/` | The executable: entry point, BIOS loading, game-specific overrides and debug tooling |
| `TwoTowersRecompLib/config/` | PS2Recomp configuration for `SLES_512.56` |
| `TwoTowersRecompLib/generated/` | Generated C++ (local, git-ignored) |
| `TwoTowersRecompLib/private/` | Extracted disc and BIOS (local, git-ignored) |
| `tools/` | Toolchain setup, ISO extraction, code generation, build scripts, disassemblers |
| `tools/PS2Recomp/` | Submodule: the recompiler and runtime (upstream, unmodified) |
| `tools/patches/PS2Recomp/` | Patch stack applied on top of the submodule (SPU2, FMV, VU fixes, parallel GS...) |
| `docs/` | Build guide, status, debugging guide and technical notes (currently written in Spanish) |

## Quick start (Windows 11 x64)

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup_windows.ps1
git submodule update --init --recursive
py -3.12 -m venv .venv; .venv\Scripts\python -m pip install -r tools\requirements.txt
.venv\Scripts\python -I tools\extract_iso.py "path\to\your\disc.iso"
.venv\Scripts\python -I tools\bios_info.py --install "path\to\your\bios.bin"
powershell -ExecutionPolicy Bypass -File tools\build_tools.ps1
powershell -ExecutionPolicy Bypass -File tools\ghidra\export_functions.ps1
.venv\Scripts\python -I tools\recomp_codegen.py generate
powershell -ExecutionPolicy Bypass -File tools\build_runtime.ps1 -Preset win-clang-release
```

Run it with:

```powershell
out\build\win-clang-release\TwoTowersRecomp\TwoTowersRecomp.exe
```

Keyboard controls: WASD = left stick, IJKL = right stick, arrows = D-pad, X = Cross, C = Circle,
Z = Square, V = Triangle, Enter = Start. A connected gamepad is used when present.

See [docs/BUILDING.md](docs/BUILDING.md) for details and [docs/DEBUGGING.md](docs/DEBUGGING.md) for the
tracing, profiling and scripted-input tools.

## Current status

- Boots, runs the legal screens, loading screen, intro FMV (video only) and the prologue level.
- Fixed recompiler bugs that blocked rendering: VU0 macro-mode MAC flags, `SQRT.S` operand, VU0
  quadword load/store addressing (`VSQI`/`VLQI`), `VDIV`/`VRSQRT` by zero.
- Performance: roughly 30 of 60 VBlanks per second in the prologue on a 12-thread CPU (asynchronous,
  multi-threaded software GS rasterizer; batched IOP catch-up).
- Missing: audio output, character shadow volumes (disabled with `TTR_HACK_NO_SHADOW_VOLUMES=1`),
  gameplay verification, more speed.

## License

GPL-3.0, like PS2Recomp. *The Lord of the Rings: The Two Towers* and its assets belong to their
respective owners; this is an unofficial fan project with no affiliation to them.
