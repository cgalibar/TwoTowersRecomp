# Compilación

[Inicio](../README.md) · [Estado](STATUS.md) · [Notas del ELF](notes/elf.md)

## Requisitos

Entorno probado: Windows 11 x64. El ejecutable requiere una CPU con **AVX** (opción `TTR_X64_ARCH`,
`AVX` por defecto o `AVX2`): el código generado usa intrínsecos SSE4.1+ para las instrucciones MMI/VU0.

| Herramienta | Versión | Uso |
| --- | --- | --- |
| Visual Studio 2022 Build Tools | 17.14 + Windows SDK 10.0.26100 | Toolset MSVC, cabeceras y librerías |
| LLVM (`clang-cl`) | 23.x | Compilador recomendado para el runtime |
| CMake | 3.28+ (probado 4.4) | Sistema de build |
| Ninja | 1.13 | Generador |
| Python | 3.11+ | Scripts de `tools/` (usa `tomllib`) |
| JDK | Temurin 21 | Solo para Ghidra |
| Ghidra | 12.1.4 + [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) v2.1.39 | Opcional: mapa de funciones |
| PCSX2 | 2.8 | Opcional: comparar comportamiento con el original |

`tools/setup_windows.ps1` instala todo lo anterior con `winget` (pide elevación una vez).
Ghidra no está en `winget`: descomprímelo en una ruta sin espacios ni acentos (por ejemplo
`J:\DevTools\ghidra_12.1.4_PUBLIC`) y copia la extensión EE en `Ghidra\Extensions\`.

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup_windows.ps1
```

## Dependencias

PS2Recomp va como submódulo en `tools/PS2Recomp`. El resto de dependencias (raylib, ImGui, ELFIO,
toml11, fmt, rabbitizer, FFmpeg precompilado...) las descarga CMake con `FetchContent` en la primera
configuración, así que hace falta red.

```powershell
git submodule update --init --recursive
py -3.12 -m venv .venv
.venv\Scripts\python -m pip install -r tools\requirements.txt
```

## Datos del juego

Necesitas tu propia copia de **SLES-51256** (PAL España). El repositorio no contiene ni enlaza datos del juego.

```powershell
.venv\Scripts\python -I tools\extract_iso.py "ruta\a\tu\disco.iso"
.venv\Scripts\python -I tools\recomp_codegen.py verify
```

El disco se extrae en `TwoTowersRecompLib/private/disc/`, que Git ignora. La imagen original solo se lee.

### BIOS (recomendada)

El runtime de PS2Recomp es un entorno HLE sin BIOS: no ejecuta la BIOS ni la necesita para arrancar.
El juego, sin embargo, lee `rom0:ROMVER`, y el runtime responde por defecto con una BIOS **americana**
v2.00 (`0200AC20040614`). Con un volcado de tu propia consola el ejecutable sirve el `ROMVER` real.
Lo más fiel para esta edición PAL de 2002 es la europea v1.60 (SCPH-30004, `0160EC20011004`).

```powershell
.venv\Scripts\python -I tools\bios_info.py "$env:USERPROFILE\Documents\PCSX2\bios"     # lista ROMVER/región
.venv\Scripts\python -I tools\bios_info.py --install "ruta\a\tu\bios.bin"            # -> private/bios/bios.bin
```

Al arrancar, el ejecutable busca la BIOS en `--bios <fichero-o-carpeta>`, en `bios\` junto al `.exe` o en
`TwoTowersRecompLib/private/bios/`. Si no hay ninguna, sigue con el `ROMVER` por defecto del runtime.
La misma BIOS europea es la recomendada en PCSX2 para comparar con el juego original.

## Generar el código recompilado

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_tools.ps1        # ps2_recomp + ps2_analyzer
powershell -ExecutionPolicy Bypass -File tools\ghidra\export_functions.ps1   # mapa de funciones (~2 min)
.venv\Scripts\python -I tools\recomp_codegen.py generate             # -> TwoTowersRecompLib/generated/
```

El mapa de Ghidra (`out/ghidra/sles_51256.functions.csv`, local) es el flujo recomendado por PS2Recomp:
sin él, la heurística toma por código las regiones de datos de la sección `main` y la generación da
~12 000 errores; con él, ninguno. Si falta, `generate` avisa y usa la heurística.

La configuración versionada ya contiene el resultado del analizador. Para rehacerla desde cero
(sobrescribe las ediciones manuales):

```powershell
.venv\Scripts\python -I tools\recomp_codegen.py analyze              # -> out/analysis/sles_51256.analyzer.toml
.venv\Scripts\python -I tools\recomp_codegen.py import-analyzer      # -> TwoTowersRecompLib/config/sles_51256.toml
```

`generate` escribe primero en `out/codegen/staging`; si falla, conserva la salida anterior.
La configuración del recompilador está en
[`TwoTowersRecompLib/config/sles_51256.toml`](../TwoTowersRecompLib/config/sles_51256.toml).

### Ghidra

`export_functions.ps1` importa el ELF en un proyecto headless (`out/ghidra/project`) con el lenguaje
`r5900:LE:32:default` de la extensión Emotion Engine y ejecuta `ExportPS2Functions.java` de PS2Recomp.
Las siguientes ejecuciones reutilizan el análisis guardado; `-Reimport` lo rehace. Ese mismo proyecto
se puede abrir en la GUI de Ghidra (`ghidraRun.bat`) para investigar.

## Compilar el ejecutable

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_runtime.ps1 -Preset win-clang-debug
```

Presets disponibles (`CMakePresets.json`): `win-clang-debug`, `win-clang-relwithdebinfo`,
`win-clang-release`, `win-msvc-debug`, `win-msvc-release`. Los scripts cargan el entorno de
Visual Studio con `tools/vsenv.ps1`; para usar CMake a mano, abre antes un *Developer PowerShell for VS 2022*.

Sin código generado el proyecto compila igualmente con una tabla de funciones vacía, lo que sirve
para comprobar el toolchain, pero el juego no arranca.

Antes de configurar, el script aplica los parches de [`tools/patches/`](../tools/patches/README.md) al
submódulo. Si estás editando ficheros del submódulo que esos parches también tocan, añade `-SkipPatches`
y exporta tus cambios con `tools\export_patch.ps1` cuando termines.

## Ejecutar

```powershell
out\build\win-clang-debug\TwoTowersRecomp\TwoTowersRecomp.exe [--game <carpeta-o-ELF>] [--bios <fichero-o-carpeta>]
```

Sin `--game`, el ejecutable busca `SLES_512.56` en `game\` junto al `.exe` y, en builds de desarrollo,
en `TwoTowersRecompLib/private/disc/` subiendo desde la carpeta del ejecutable.
