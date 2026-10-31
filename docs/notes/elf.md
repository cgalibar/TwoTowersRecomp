# Notas del ELF: SLES_512.56

Build soportada: **El Señor de los Anillos: Las Dos Torres**, PAL España, serial `SLES-51256`.

| Campo | Valor |
| --- | --- |
| `SYSTEM.CNF` | `BOOT2 = cdrom0:\SLES_512.56;1`, `VER = 1.00`, `VMODE = PAL` |
| `LANG.CFG` | Spanish / Spanish / Spanish |
| Tamaño | 3 627 164 bytes |
| SHA-1 | `414d5a62dd36d82dcab8488b93e2e772e6e43fba` |
| CRC32 | `6BDD1874` |
| Entry point | `0x00100008` |
| Compilador | `MW MIPS C Compiler (2.4.1.01)` (Metrowerks CodeWarrior para PS2) |
| e_flags | `0x20924000` (R5900) |

## Segmentos

| Tipo | vaddr | filesz | memsz | Notas |
| --- | --- | --- | --- | --- |
| `PT_LOAD` | `0x00100000` | `0x281700` | `0x479280` | Sección `main`: código + datos; el resto es BSS |
| `PT_LOAD` | `0x00579280` | `0x010000` | `0x010000` | Sección `heap` |

## Secciones

`main`, `.relmain` (relocalizaciones, `0x7DEA8` bytes), `heap`, `.symtab` / `.strtab`,
`.mwcats` (metadatos de CodeWarrior), `.comment`, `.reginfo`.

## Símbolos

El ELF de retail **no está stripped**:

- 4716 funciones (`STT_FUNC`) y 4018 objetos (`STT_OBJECT`), con nombres C++ en el mangling de CodeWarrior
  (por ejemplo `SetState__5ClGfxFv` = `ClGfx::SetState()`).
- ~258 funciones del SDK de Sony con nombre (`sceVu0*`, `sceDev*`, `sceMpeg*`, ...).
- Sin DWARF.

Consecuencias para la recompilación:

- Los límites de función salen directamente de la tabla de símbolos; Ghidra es opcional para el arranque.
- Las relocalizaciones permiten que PS2Recomp enlace llamadas `J`/`JAL` a handlers del runtime por nombre.

## Contenido del disco

| Carpeta | Ficheros | Contenido |
| --- | --- | --- |
| `FMV/ES` | 38 | Vídeos (`.FMV`, MPEG-2 PSS de Sony) |
| `IOP` | 11 | Módulos IRX: `IOPRP252.IMG`, `FUFILECD`, `FUSTREAM`, `LIBSD`, `SDRDRV`, `PADMAN`, `MCMAN`, `MCSERV`, `SIO2MAN`, `USBD`, `USBKB` |
| `SOUND` | 1008 | Bancos de audio `.MIB` |
| `UI` | 23 | Interfaz |
| `WORLDS` | 60 | Niveles (`.BFA` / `.BFG`) |

Generado con `tools/elf_info.py` y `tools/extract_iso.py`.
