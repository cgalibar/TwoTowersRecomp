# Herramientas

| Script | Qué hace |
| --- | --- |
| `setup_windows.ps1` | Instala CMake, Ninja, LLVM, VS 2022 Build Tools, JDK 21, 7-Zip, PCSX2 y GitHub CLI con `winget` |
| `vsenv.ps1` | Carga el entorno x64 de Visual Studio (se usa con *dot-source*) |
| `extract_iso.py` | Extrae la ISO a `TwoTowersRecompLib/private/disc/` (`--list` solo lista) |
| `elf_info.py` | Resumen del ELF: hashes, segmentos, secciones y símbolos |
| `build_tools.ps1` | Compila `ps2_recomp` y `ps2_analyzer` en `out/build/tools` |
| `recomp_codegen.py` | `verify`, `analyze`, `import-analyzer` y `generate` del código recompilado |
| `build_runtime.ps1` | Configura y compila `TwoTowersRecomp` con un preset de CMake |
| `ghidra/export_functions.ps1` | Ghidra headless + `ExportPS2Functions.java` → mapa de funciones en `out/ghidra/` |

Ejecuta los scripts de Python con `-I` desde la raíz del repositorio usando el `.venv`.
