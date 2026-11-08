# Depuración

[Inicio](../README.md) · [Compilación](BUILDING.md) · [Estado](STATUS.md)

Usa la build `win-clang-relwithdebinfo` para depurar y perfilar: tiene símbolos y el mismo inlining que la
`Release` (`/Ob2`, en `CMakePresets.json`), así que el perfil refleja lo que ve el jugador; la `Debug` es unas
10 veces más lenta. Los logs por registro del GS del
runtime están desactivados por defecto (`-DTTR_VERBOSE_RUNTIME_LOGS=ON` los activa).

## Variables de entorno

| Variable | Efecto |
| --- | --- |
| `TTR_FRAME_DUMP=<carpeta>` | Guarda el frame presentado como PNG cada `TTR_FRAME_DUMP_EVERY` VBlanks (50 por defecto) y registra el tiempo real transcurrido (`t=`, sirve para medir VBlanks por segundo), el modo de `ClGame` y los hilos del EE y del IOP |
| `TTR_PROFILE=<segundos>[,<n>]` | Perfilador por muestreo (Windows): toma el puntero de instrucción de todos los hilos cada milisegundo y, cada `<segundos>`, imprime las funciones más calientes y, por hilo, su ocupación y sus `<n>` funciones principales (8 por defecto). Necesita los símbolos de la build `RelWithDebInfo` o `Debug` |
| `PS2X_GS_THREADS=<n>` | Hilos extra del rasterizador del GS (parches `0005`/`0006`; por defecto, los de la CPU menos 3); `0` lo dibuja todo en el hilo del GS |
| `PS2X_GS_ASYNC=0` | Desactiva el hilo del GS (parche `0006`): el rasterizador vuelve a ejecutarse en el hilo del EE |
| `TTR_TRACE=dma` | Cola de cadenas DMA de VBlank y `ClGfxDmaBuffer` |
| `TTR_TRACE=sound` | Streams de EA MultiStream (ver [notas/ea-sound.md](notes/ea-sound.md)) |
| `TTR_TRACE=rpc` | `sceSifBindRpc`/`CallRpc`/`CheckStatRpc` cuando se llaman por la tabla de funciones |
| `TTR_TRACE=fmv` | `ClFMV`: banderas de reproducción, callback "sin datos", peticiones de audio del vídeo y llenado de sus buffers (también en cada volcado de frame) |
| `TTR_TRACE=camera` | Cámara y mundo: `ClCamera::projection`, `tanf`, `getZBufferValue`, `ClearScreen`, `ClPbi::clear`, matrices de `ClWorldDrawData::beginDraw` (avisa si la de dibujo no tiene X/Y), contadores de nodos del mundo y comprobación de `SfMatrixMultiplyVu0` contra C++ |
| `TTR_TRACE=pad` | `ClPad` (puerto 0): estado de libpad, modo del mando (0x73 = analógico), botones y sticks tal como los ve el juego, en cada cambio |
| `TTR_TRACE=anims` | Personajes y visibilidad, con cada volcado de frame: llamadas a los pases de `ClDisplayList`, a `AddToDisplayList` de cada clase (actores, props, partículas...), frustums registrados y consultados por sector, y las primeras pruebas de `ClFrustum::isBoundingBoxVisible` con la prueba de planos recalculada en C++ |
| `TTR_GS_DUMP=<vblank>` | Activa el historial de depuración del GS durante 10 VBlanks y lista sus registros y primitivas |
| `TTR_PAD_SCRIPT=<paso>;<paso>...` | Mueve el mando según el número de VBlank, para pruebas reproducibles. Un paso es `<desde>[-<hasta>]:<entrada>[+<entrada>...]` (sin `hasta`, 6 VBlanks); entradas: `start`, `select`, `cross`, `circle`, `square`, `triangle`, `l1`, `r1`, `l2`, `r2`, `l3`, `r3`, `up`, `down`, `left`, `right` y ejes `lx=`, `ly=`, `rx=`, `ry=` (0-255, 128 = centro). Ej.: `700:cross;1500-1800:ly=0` |
| `TTR_HACK_SKIPPABLE_FMV=1` | Hack: cualquier botón salta los vídeos, como cuando ya se han visto (sin partida guardada, la introducción no se puede saltar) |
| `TTR_HACK_SKIP_BARD=1` | Hack: `ClGameSound::isBardStreamLoaded()` siempre devuelve true |
| `PS2X_IOP_TRACE=1` | Trazas del IOP (parche `0001`): `cdvdman`, `ioman`, enlaces entre módulos, RPC |
| `PS2X_IOP_CALLTRACE=<Mciclos>:<líneas>:<lo>-<hi>` | `jal`/`jalr` ejecutados en `[lo, hi)` a partir de un ciclo IOP |
| `PS2X_WATCH=<lo>-<hi>` | Avisa (con el origen) cuando el runtime escribe en ese rango de la RAM del EE: stubs de libc, DMA SIF/IOP, lecturas de fichero, copias RPC, `sceMpegGetPicture`. No cubre los `sw`/`sq` del código recompilado |
| `PS2X_VU_STATS=1` | Cada segundo: ejecuciones de microprogramas, ciclos y pares de instrucciones de VU0 y VU1 (parche `0009`). Mide la carga de VU y la velocidad del intérprete |
| `PS2X_VU_DUMP=<carpeta>` | Guarda cada imagen distinta de la memoria de microcódigo que ejecuta una VU como `vu<n>_<fnv64>.bin` (para `tools/vu_disasm.py`) |
| `PS2X_VU_FAST=0` | Desactiva el motor rápido de VU (parche `0010`) y usa el intérprete ciclo a ciclo |
| `PS2X_VU_VERIFY=1` | Ejecuta cada microprograma en el motor rápido y, desde el mismo estado, en el intérprete; informa de diferencias en registros, flags, memoria de datos y paquetes XGKICK (`[vu-verify]`) y se queda con el resultado del intérprete. Lento |
| `PS2X_VU_ACCURATE=1` | Vuelve al modelo de VU con colas de escritura y flags FMAC exactos (más lento; para comparar si se sospecha del modo rápido) |
| `PS2X_VU_FLAGS_ALWAYS=1` | El motor rápido calcula todos los flags MAC/estado/clip, aunque el microcódigo no pueda leerlos |
| `PS2X_VU_CAPTURE=<fichero>` | Guarda el estado inicial, el código y los datos de `PS2X_VU_CAPTURE_COUNT` ejecuciones de VU1 (3000) tras saltarse `PS2X_VU_CAPTURE_SKIP` (20 000), para `ttr_vu_bench` |
| `PS2X_VIF1_ASYNC=0` | VIF1, VU1, PATH1 y PATH2 vuelven al hilo del EE (parche `0011`); por defecto corren en su propio hilo |
| `PS2X_VIF1_STATS=1` | Cada segundo: ocupación del hilo de VIF1 (y qué parte es VU1 y envío al GS), bloques procesados y cuántas veces y cuánto tiempo esperó el EE, por motivo (`[vif1-stats]`) |
| `PS2X_GS_STATS=1` | Cada segundo: píxeles que llegan al pipeline del GS y primitivas por tipo (`[gs-stats]`), y los sprites de más de 60 000 píxeles (`[gs-big]`) |
| `PS2X_AUDIO=0` | Sin salida de audio del SPU2 |
| `PS2X_AUDIO_DUMP=<fichero>` | Vuelca toda la mezcla del SPU2 en PCM crudo (estéreo, 16 bits, 48 kHz) |
| `PS2X_PROFILE_CALLS=<n>` | Cada `n` llamadas entre funciones del juego, imprime las 30 más llamadas (`tools/disasm.py --sym` las traduce) |

Se pueden combinar: `TTR_TRACE=sound,dma`. `anims` imprime mucho en cada frame y frena el juego (los vídeos
pasan de 50 a ~2 VBlanks/s); úsala en tramos cortos.

### Llegar a la batalla del prólogo

Sin partida guardada, los vídeos de logos solo se saltan con el hack. Con esta secuencia se salta el de logos
y la misión del prólogo arranca con su cinemática en el motor; la batalla empieza hacia el VBlank 2700 (a la
velocidad actual, ~10 min de reloj):

```powershell
$env:TTR_HACK_SKIPPABLE_FMV = '1'
$env:TTR_PAD_SCRIPT = '1100:cross;3000-3300:ly=0;3400:square'
$env:TTR_FRAME_DUMP = 'out\frames'; $env:TTR_FRAME_DUMP_EVERY = '50'
```

Start durante la misión abre la pausa (Opciones / Salir a Menú).

## Herramientas

| Herramienta | Uso |
| --- | --- |
| `tools/disasm.py <símbolo|dirección>` | Desensamblado R5900 del ELF; `--sym <dir>` resuelve a símbolo, `--find <texto>` busca símbolos, `--gpref=-0x7120` lista los accesos a una global relativa a `$gp` |
| `tools/vu_disasm.py <volcado> [--start <dir>] [--count <n>]` | Desensamblado de microcódigo VU0/VU1 (pares upper/lower) de un volcado de memoria de código |
| `tools/irx_disasm.py <IRX> --base <dir> <dir>` | Desensamblado R3000 de un módulo IOP tal como se carga; `--imports`, `--strings` |
| `ttr_vu_bench <captura> [pasadas]` | Junto a `TwoTowersRecomp.exe` (`cmake --build --preset ... --target ttr_vu_bench`). Reproduce una captura de `PS2X_VU_CAPTURE`: compara el motor rápido con el intérprete ejecución a ejecución y mide ns por par de instrucciones. Para iterar sobre el motor de VU sin arrancar el juego |
| `TwoTowersRecomp/game/sles_51256_trace.cpp` | Wrappers de funciones del juego: registra y llama a la función generada |

Para envolver otra función: declara un wrapper con la firma `void(uint8_t*, R5900Context*, PS2Runtime*)`
que llame a la función generada (`<símbolo>_0x<dirección>` en `ps2_recompiled_functions.h`) y regístralo con
`runtime.replaceFunction(dirección, &wrapper)`. Las llamadas que el recompilador enlaza directamente a un
stub del runtime (`sceSif*`, `sceCd*`...) no pasan por la tabla y no se pueden envolver así.
