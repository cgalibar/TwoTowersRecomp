# Depuración

[Inicio](../README.md) · [Compilación](BUILDING.md) · [Estado](STATUS.md)

Usa la build `win-clang-relwithdebinfo` para ejecutar a velocidad razonable; la `Debug` es unas 10 veces más
lenta. Los logs por registro del GS del runtime están desactivados por defecto
(`-DTTR_VERBOSE_RUNTIME_LOGS=ON` los activa).

## Variables de entorno

| Variable | Efecto |
| --- | --- |
| `TTR_FRAME_DUMP=<carpeta>` | Guarda el frame presentado como PNG cada `TTR_FRAME_DUMP_EVERY` VBlanks (50 por defecto) y registra el modo de `ClGame` y los hilos del EE y del IOP |
| `TTR_TRACE=dma` | Cola de cadenas DMA de VBlank y `ClGfxDmaBuffer` |
| `TTR_TRACE=sound` | Streams de EA MultiStream (ver [notas/ea-sound.md](notes/ea-sound.md)) |
| `TTR_TRACE=rpc` | `sceSifBindRpc`/`CallRpc`/`CheckStatRpc` cuando se llaman por la tabla de funciones |
| `TTR_HACK_SKIP_BARD=1` | Hack: `ClGameSound::isBardStreamLoaded()` siempre devuelve true |
| `PS2X_IOP_TRACE=1` | Trazas del IOP (parche `0001`): `cdvdman`, `ioman`, enlaces entre módulos, RPC |
| `PS2X_IOP_CALLTRACE=<Mciclos>:<líneas>:<lo>-<hi>` | `jal`/`jalr` ejecutados en `[lo, hi)` a partir de un ciclo IOP |

Se pueden combinar: `TTR_TRACE=sound,dma`.

## Herramientas

| Herramienta | Uso |
| --- | --- |
| `tools/disasm.py <símbolo|dirección>` | Desensamblado R5900 del ELF; `--sym <dir>` resuelve a símbolo, `--find <texto>` busca símbolos |
| `tools/irx_disasm.py <IRX> --base <dir> <dir>` | Desensamblado R3000 de un módulo IOP tal como se carga; `--imports`, `--strings` |
| `TwoTowersRecomp/game/sles_51256_trace.cpp` | Wrappers de funciones del juego: registra y llama a la función generada |

Para envolver otra función: declara un wrapper con la firma `void(uint8_t*, R5900Context*, PS2Runtime*)`
que llame a la función generada (`<símbolo>_0x<dirección>` en `ps2_recompiled_functions.h`) y regístralo con
`runtime.replaceFunction(dirección, &wrapper)`. Las llamadas que el recompilador enlaza directamente a un
stub del runtime (`sceSif*`, `sceCd*`...) no pasan por la tabla y no se pueden envolver así.
