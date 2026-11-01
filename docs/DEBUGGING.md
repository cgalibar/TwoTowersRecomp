# Depuración

[Inicio](../README.md) · [Compilación](BUILDING.md) · [Estado](STATUS.md)

Usa la build `win-clang-relwithdebinfo` para depurar y perfilar: tiene símbolos y va casi tan rápida como la
`Release` (un 15-25 % más lenta); la `Debug` es unas 10 veces más lenta. Los logs por registro del GS del
runtime están desactivados por defecto (`-DTTR_VERBOSE_RUNTIME_LOGS=ON` los activa).

## Variables de entorno

| Variable | Efecto |
| --- | --- |
| `TTR_FRAME_DUMP=<carpeta>` | Guarda el frame presentado como PNG cada `TTR_FRAME_DUMP_EVERY` VBlanks (50 por defecto) y registra el tiempo real transcurrido (`t=`, sirve para medir VBlanks por segundo), el modo de `ClGame` y los hilos del EE y del IOP |
| `TTR_PROFILE=<segundos>` | Perfilador por muestreo (Windows): toma el puntero de instrucción de todos los hilos cada milisegundo y, cada `<segundos>`, imprime las funciones más calientes y, por hilo, su ocupación y sus funciones principales. Necesita los símbolos de la build `RelWithDebInfo` o `Debug` |
| `PS2X_GS_THREADS=<n>` | Hilos extra del rasterizador del GS (parche `0005`); `0` lo dibuja todo en el hilo del EE |
| `TTR_TRACE=dma` | Cola de cadenas DMA de VBlank y `ClGfxDmaBuffer` |
| `TTR_TRACE=sound` | Streams de EA MultiStream (ver [notas/ea-sound.md](notes/ea-sound.md)) |
| `TTR_TRACE=rpc` | `sceSifBindRpc`/`CallRpc`/`CheckStatRpc` cuando se llaman por la tabla de funciones |
| `TTR_TRACE=fmv` | `ClFMV`: banderas de reproducción, callback "sin datos", peticiones de audio del vídeo y llenado de sus buffers (también en cada volcado de frame) |
| `TTR_TRACE=camera` | Cámara y mundo: `ClCamera::projection`, `tanf`, `getZBufferValue`, `ClearScreen`, `ClPbi::clear`, matrices de `ClWorldDrawData::beginDraw` (avisa si la de dibujo no tiene X/Y), contadores de nodos del mundo y comprobación de `SfMatrixMultiplyVu0` contra C++ |
| `TTR_GS_DUMP=<vblank>` | Activa el historial de depuración del GS durante 10 VBlanks y lista sus registros y primitivas |
| `TTR_HACK_SKIP_BARD=1` | Hack: `ClGameSound::isBardStreamLoaded()` siempre devuelve true |
| `PS2X_IOP_TRACE=1` | Trazas del IOP (parche `0001`): `cdvdman`, `ioman`, enlaces entre módulos, RPC |
| `PS2X_IOP_CALLTRACE=<Mciclos>:<líneas>:<lo>-<hi>` | `jal`/`jalr` ejecutados en `[lo, hi)` a partir de un ciclo IOP |
| `PS2X_WATCH=<lo>-<hi>` | Avisa (con el origen) cuando el runtime escribe en ese rango de la RAM del EE: stubs de libc, DMA SIF/IOP, lecturas de fichero, copias RPC, `sceMpegGetPicture`. No cubre los `sw`/`sq` del código recompilado |
| `PS2X_PROFILE_CALLS=<n>` | Cada `n` llamadas entre funciones del juego, imprime las 30 más llamadas (`tools/disasm.py --sym` las traduce) |

Se pueden combinar: `TTR_TRACE=sound,dma`.

## Herramientas

| Herramienta | Uso |
| --- | --- |
| `tools/disasm.py <símbolo|dirección>` | Desensamblado R5900 del ELF; `--sym <dir>` resuelve a símbolo, `--find <texto>` busca símbolos, `--gpref=-0x7120` lista los accesos a una global relativa a `$gp` |
| `tools/vu_disasm.py <volcado> [--start <dir>] [--count <n>]` | Desensamblado de microcódigo VU0/VU1 (pares upper/lower) de un volcado de memoria de código |
| `tools/irx_disasm.py <IRX> --base <dir> <dir>` | Desensamblado R3000 de un módulo IOP tal como se carga; `--imports`, `--strings` |
| `TwoTowersRecomp/game/sles_51256_trace.cpp` | Wrappers de funciones del juego: registra y llama a la función generada |

Para envolver otra función: declara un wrapper con la firma `void(uint8_t*, R5900Context*, PS2Runtime*)`
que llame a la función generada (`<símbolo>_0x<dirección>` en `ps2_recompiled_functions.h`) y regístralo con
`runtime.replaceFunction(dirección, &wrapper)`. Las llamadas que el recompilador enlaza directamente a un
stub del runtime (`sceSif*`, `sceCd*`...) no pasan por la tabla y no se pueden envolver así.
