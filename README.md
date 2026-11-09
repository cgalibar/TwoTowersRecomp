# TwoTowersRecomp

A static recompilation of **The Lord of the Rings: The Two Towers** (PlayStation 2, 2002) into a native
Windows executable, following the approach of [Lost Odyssey Recomp](https://github.com/freefrank/LostOdysseyRecomp)
and built on top of [PS2Recomp](https://github.com/ran-j/PS2Recomp).

> [!WARNING]
> Work in progress. The game boots, plays the intro movies and the prologue mission can be played: the
> game runs at full speed with sound, but draws only ~10 frames per second in the battle on a 6-core CPU,
> and gameplay beyond the prologue has not been tested. See [docs/STATUS.md](docs/STATUS.md).

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

Keyboard controls: WASD = left stick, IJKL = right stick, arrows = D-pad, X or Space = Cross,
C or Escape = Circle, Z = Square, V = Triangle, Q/E = L1/R1, Left/Right Shift = L2/R2, Enter = Start,
Tab = Select. A connected gamepad is used when present. F11 toggles fullscreen.

Without a memory card save the intro movie cannot be skipped; `TTR_HACK_SKIPPABLE_FMV=1` lets any button
skip the movies.

See [docs/BUILDING.md](docs/BUILDING.md) for details and [docs/DEBUGGING.md](docs/DEBUGGING.md) for the
tracing, profiling and scripted-input tools.

## Current status

- Boots, runs the legal screens, loading screen, intro FMV and the prologue mission, with SPU2 audio.
- Real-time clock: when the host cannot keep up, the emulated clock jumps forward instead of falling
  behind, so the game (which measures its frame time) plays at full speed with fewer frames.
- Fixed recompiler bugs that blocked rendering: VU0 macro-mode MAC flags, `SQRT.S` operand, VU0
  quadword load/store addressing (`VSQI`/`VLQI`), `VDIV`/`VRSQRT` by zero.
- Performance: ~10 frames per second in the prologue battle on a Ryzen 5 3600X (VU1 block compiler,
  VIF1/VU1 and GS on their own threads, multi-threaded software GS rasterizer). The limits are the GS
  pipeline (texture uploads serialize the raster threads) and VU1.
- Missing: more speed, gameplay verification beyond the prologue, main menu flow.

## License

GPL-3.0, like PS2Recomp. *The Lord of the Rings: The Two Towers* and its assets belong to their
respective owners; this is an unofficial fan project with no affiliation to them.
