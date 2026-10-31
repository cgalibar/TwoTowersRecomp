# TwoTowersRecomp

Recompilación estática de **El Señor de los Anillos: Las Dos Torres** (PlayStation 2, 2002) a un ejecutable
nativo de PC, siguiendo el enfoque de [Lost Odyssey Recomp](https://github.com/freefrank/LostOdysseyRecomp)
y construido sobre [PS2Recomp](https://github.com/ran-j/PS2Recomp).

> [!WARNING]
> Proyecto en fase de exploración: todavía no arranca el juego. Consulta el [estado](docs/STATUS.md).

## Qué es

El código MIPS R5900 del ejecutable original (`SLES_512.56`) se traduce a C++ con PS2Recomp y se compila
para x64. Un runtime emula el resto del hardware de PS2 (GS, VU, IOP, DMA...) y lee los datos del juego
desde tu copia del disco.

**El repositorio no incluye ningún dato del juego.** Necesitas tu propia copia original de la edición
PAL España (`SLES-51256`). No pidas ni compartas ISOs.

## Estructura

| Ruta | Contenido |
| --- | --- |
| `TwoTowersRecomp/` | Ejecutable: punto de entrada y parches específicos del juego |
| `TwoTowersRecompLib/config/` | Configuración de PS2Recomp para `SLES_512.56` |
| `TwoTowersRecompLib/generated/` | C++ generado (local, ignorado por Git) |
| `TwoTowersRecompLib/private/` | Disco extraído (local, ignorado por Git) |
| `tools/` | Instalación del toolchain, extracción, generación y compilación |
| `tools/PS2Recomp/` | Submódulo: recompilador y runtime |
| `docs/` | [Compilación](docs/BUILDING.md), [estado](docs/STATUS.md), [notas técnicas](docs/notes/elf.md) |

## Inicio rápido

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup_windows.ps1
git submodule update --init --recursive
py -3.12 -m venv .venv; .venv\Scripts\python -m pip install -r tools\requirements.txt
.venv\Scripts\python -I tools\extract_iso.py "ruta\a\tu\disco.iso"
powershell -ExecutionPolicy Bypass -File tools\build_tools.ps1
.venv\Scripts\python -I tools\recomp_codegen.py generate
powershell -ExecutionPolicy Bypass -File tools\build_runtime.ps1
```

Detalles en [docs/BUILDING.md](docs/BUILDING.md).

## Licencia

GPL-3.0, igual que PS2Recomp. *El Señor de los Anillos: Las Dos Torres* y sus recursos pertenecen a sus
respectivos propietarios; este es un proyecto de aficionados no oficial y sin relación con ellos.
