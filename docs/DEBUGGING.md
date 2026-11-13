# Depuración

[Inicio](../README.md) · [Compilación](BUILDING.md) · [Estado](STATUS.md)

Usa la build `win-clang-relwithdebinfo` para depurar y perfilar: tiene símbolos y el mismo inlining que la
`Release` (`/Ob2`, en `CMakePresets.json`), así que el perfil refleja lo que ve el jugador; la `Debug` es unas
10 veces más lenta. Los logs por registro del GS del
runtime están desactivados por defecto (`-DTTR_VERBOSE_RUNTIME_LOGS=ON` los activa).

## Variables de entorno

| Variable | Efecto |
| --- | --- |
| `TTR_FRAME_DUMP=<carpeta>` | Guarda el frame presentado como PNG cada `TTR_FRAME_DUMP_EVERY` VBlanks (50 por defecto) y registra el tiempo real transcurrido (`t=`), desde el volcado anterior los fotogramas que el juego ha terminado por segundo (`fps=`, llamadas a `ClDisplay::end`), los VBlanks por segundo (`vbps=`) y la parte del tiempo del EE que el reloj real ha saltado (`rt-skip=`), el modo de `ClGame` y los hilos del EE y del IOP |
| `TTR_PROFILE=<segundos>[,<n>]` | Perfilador por muestreo (Windows): suspende cada hilo, recorre su pila y, cada `<segundos>`, imprime por cada hilo con más de un 5 % de ocupación: `[profile] thread <id> busy x% of <n> samples`, las `<n>` funciones principales como `self x% incl y% <función>`, las principales por tiempo inclusivo (`incl`) y hasta 24 líneas `from x% <hoja> <- <llamante>` para las hojas compartidas (`memmove`, montón, código del JIT). Los porcentajes son sobre todas las muestras del hilo, esperas incluidas; la pila se corta en el código del JIT de VU. Necesita los símbolos de la build `RelWithDebInfo` o `Debug`. Para leerlo: `tools/perf/prof_report.py` |
| `PS2X_REALTIME=0` | Sin reloj real (parche `0013`): el tiempo del EE (VBlanks, temporizadores, IOP) solo avanza con los ciclos que cobra el código generado, así que el juego va a cámara lenta cuando el PC no llega |
| `PS2X_VIF1_QUEUE=<n>` | Transferencias de VIF1 sin procesar a partir de las cuales lanzar otra espera (2 por defecto; `0` sin límite) |
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
| `PS2X_VU_JIT=0` | Sin el compilador de bloques de VU (parche `0012`): todo el microcódigo pasa por el bucle del motor rápido |
| `PS2X_VU_JIT_INLINE=0` | El compilador de bloques llama a todos los manejadores en vez de emitir en línea las FMAC sin flags |
| `PS2X_VU_JIT_STATS=1` | Al salir: pares ejecutados por bloques compilados y por el bucle, bloques compilados y tamaño medio (`[vu-jit]`) |
| `PS2X_VU_STALL_SKIP=0` | El motor rápido comprueba la disponibilidad de registros en todos los pares (sin `kNoStall`) |
| `PS2X_VIF1_ASYNC=0` | VIF1, VU1, PATH1 y PATH2 vuelven al hilo del EE (parche `0011`); por defecto corren en su propio hilo |
| `PS2X_VIF1_STATS=1` | Cada segundo: ocupación del hilo de VIF1 y tiempo sin trabajo (`idle=`), qué parte es VU1 y envío al GS, y cuánto de ese envío esperó con la cola del GS llena, bloques procesados y cuántas veces y cuánto tiempo esperó el EE, por motivo (`[vif1-stats]`; `queue=` es la espera para lanzar una transferencia con la cola de VIF1 llena) |
| `PS2X_GS_STATS=1` | Cada segundo: píxeles y primitivas por tipo (`[gs-stats]`), sprites de más de 60 000 píxeles (`[gs-big]`); en `[gs-flush]`, lotes del pipeline de franjas, espera por huecos y esperas del hilo del GS por motivo (`flush` vaciados completos, `transfer` esperas a páginas de framebuffer/Z); en `[gs-publish]`, por qué se entregó cada lote (lleno, carga de CLUT, copia de textura, espera de páginas, cambio de disposición, vaciado), las páginas de textura que copió el hilo 0 por estar aún dibujándose y las esperas del hilo del GS; en `[gs-time]`, el tiempo de los hilos de rasterizado (dibujando, en barreras o esperando copias, sin trabajo), el del hilo del GS en `Submit` y los lotes por tipo (con copias, barrera, normales) con su espera; `[gs-thread]`, el tiempo que el hilo del GS esperó comandos |
| `PS2X_GS_GPU=0` | GS por software (`GSCpuBackend`) en lugar del GS en la GPU, que es el de por defecto cuando hay OpenGL 4.3 (también vuelven al de software `PS2X_GS_CAPTURE` y `PS2X_GS_ASYNC=0`). El log dice `[gs] GS on the GPU: <tarjeta>` o por qué no |
| `PS2X_GS_GPU_TIMING=1` | GS en la GPU: tiempo de CPU en `Submit`, lotes y subidas, y de GPU por lote y por subida (consultas `GL_TIME_ELAPSED`; esperan a la GPU, solo para medir). Lo imprime `ttr_gs_bench --gpu` |
| `PS2X_GS_GPU_DEFINES=A,B` | GS en la GPU: `#define A` y `#define B` en el *kernel* de dibujo (experimentos; `NO_ROWSPAN` quita la réplica del recorte por filas de los triángulos) |
| `PS2X_GS_GPU_RAW_LOG=<n>` | GS en la GPU: escribe las `<n>` primeras primitivas que cortan un lote por leer columnas que el lote ya dibujó (`[gpu-raw]`: framebuffer, Z, textura, UV y XY) |
| `PS2X_GS_GPU_NO_PRESENT=1` | GS en la GPU: `Present` no lee la imagen (para medir el resto en el banco) |
| `TTR_VBLANK_HZ=<hz>` | VBlank del port (60 por defecto; `0` = el del modo de vídeo, 50 PAL). El reloj HBLANK sigue en PAL, así que la velocidad del juego no cambia. Mientras hay un vídeo, 50 Hz |
| `TTR_GAME_FRAME_CAP=1` | Mantiene el tope del juego en partida (`ClDisplay::CapTo30FPS(1)`: un frame cada 2 VBlanks); por defecto el port lo quita |
| `PS2X_GS_GPU_EXACT=1` | Kernel de dibujo exacto (el que `ttr_gs_bench --check-hashes` compara con el GS por software); por defecto el rápido del port |
| `PS2X_VU_FAST_MATH=0` | VU1 con los recortes exactos de PS2 en cada FMAC (lo que `ttr_vu_bench` compara con el intérprete); por defecto sin ellos |
| `PS2X_VU_JIT_LINKS=0`, `PS2X_VU_JIT_LOOPS=0`, `PS2X_VU_INLINE_DIV=0`, `PS2X_VU_JIT_CACHE=0`, `PS2X_VU_FLAGS_LOCAL=1` | Desactivan partes del compilador de VU1 (para aislar fallos) |
| `PS2X_GIF_ASYNC=0` | Los paquetes GIF de VIF1 se procesan en el hilo VIF1 (sin hilo GIF) |
| `PS2X_VIF_UNPACK_CHECK=1` | Compara cada UNPACK de la ruta rápida con la general (`[vif-unpack]` si difieren) |
| `PS2X_PIPE_STATS=1` | Una vez por segundo, cuántas veces se llenó la cola GIF y el anillo del GS |
| `PS2X_GS_PRESENT_LOG=1` | Registros de presentación (PMODE, DISPFB, DISPLAY...) cada 50 presentaciones |
| `PS2X_GS_DROP_CLAMPED=0` | Dibuja los triángulos con un vértice en 0 o 4095 (los programas de VU1 del juego llevan ahí los vértices muy fuera de pantalla y salen rayas desde el personaje); el port los descarta por defecto |
| `PS2X_GS_PROGRESSIVE=0` | Presentación entrelazada como en PS2 (en cada VBlank, las líneas de un campo duplicadas); el port muestra todas las líneas |
| `PS2X_GS_DEFLICKER=1` | Mantiene el filtro antiparpadeo de PS2 (el circuito 2 muestra el mismo búfer una línea más abajo y se mezcla con el 1); el port lo quita y muestra el circuito 1 solo |
| `PS2X_VU_IMAGE_LOG=1`, `PS2X_VU_PC_PROFILE=1`, `PS2X_VU_JIT_DUMP=<pc hex>` | Banco de VU1: banderas por imagen, pares más ejecutados (bucle sin compilar), volcado del bloque compilado en `vu_block.s` |
| `PS2X_GS_CAPTURE=<fichero>[,<vblank>[,<n>]]` | Graba todo lo que recibe el GS por software desde la primera presentación en o tras `<vblank>` (4000) durante `<n>` presentaciones (100), con la VRAM y la CLUT de partida, para `ttr_gs_bench`. Una captura de la batalla ocupa ~1,7 GB: guardarla en `out/gs_captures/` (fuera de git) |
| `PS2X_GS_SPIN=<n>` | Pausas que giran los hilos de rasterizado y del GS antes de dormir (4096) |
| `PS2X_GS_UV_READS=1` | Cuenta como leídas solo las páginas bajo las coordenadas de textura de cada primitiva, no la textura entera |
| `PS2X_GS_SHADOW_CLUT=0`, `PS2X_GS_IMMEDIATE_UPLOAD=0` | Diagnóstico del pipeline del GS (parche `0012`): las cargas de CLUT no leen la VRAM sombra; las subidas host→local siempre van en cola |
| `PS2X_GS_SELF_NOW=0`, `PS2X_GS_SELF_COPY=0` | Las primitivas que leen páginas que escriben no se dibujan al momento en el hilo del GS / no se dibujan desde una copia congelada (esperan a todo y se dibujan en el hilo del GS) |
| `PS2X_GS_RAW_BARRIER=0`, `PS2X_GS_RAW_TRIM=1`, `PS2X_GS_UV_READS=1` | Sin barrera cuando una primitiva lee páginas que dibuja un lote en curso; barrera solo si el que escribe va después de la última barrera; páginas leídas según las coordenadas en vez de la textura entera |
| `PS2X_GS_DEFER_CLUT=1` | Experimental: una CLUT cuyas páginas dibuja un lote en curso la carga el hilo de rasterizado 0 en orden, en vez de esperar (puede dar paletas incorrectas) |
| `PS2X_AUDIO=0` | Sin salida de audio del SPU2 |
| `PS2X_AUDIO_DUMP=<fichero>` | Vuelca toda la mezcla del SPU2 en PCM crudo (estéreo, 16 bits, 48 kHz) |
| `PS2X_PROFILE_CALLS=<n>` | Cada `n` llamadas entre funciones del juego, imprime las 30 más llamadas (`tools/disasm.py --sym` las traduce) |

Se pueden combinar: `TTR_TRACE=sound,dma`. `anims` imprime mucho en cada frame y frena el juego (los vídeos
pasan de 50 a ~2 VBlanks/s); úsala en tramos cortos.

### Llegar a la batalla del prólogo

Sin partida guardada, los vídeos de logos solo se saltan con el hack. Con esta secuencia se salta el de logos
y la misión del prólogo arranca con su cinemática en el motor; la batalla empieza hacia el VBlank 2700 (~1 min de
reloj con el reloj real; con `PS2X_REALTIME=0`, unos 3 min):

```powershell
$env:TTR_HACK_SKIPPABLE_FMV = '1'
$env:TTR_PAD_SCRIPT = '1100:cross;3000-3300:ly=0;3400:square'
$env:TTR_FRAME_DUMP = 'out\frames'; $env:TTR_FRAME_DUMP_EVERY = '50'
```

Start durante la misión abre la pausa (Opciones / Salir a Menú).

## Herramientas

| Herramienta | Uso |
| --- | --- |
| `tools/perf/run_battle.sh <carpeta> <segundos> [preset]` | Arranca el juego, entra en la batalla del prólogo con un guion de mando, vuelca frames y deja `run.log` y `mem.log` en la carpeta; al acabar mata el proceso. Las variables de entorno puestas delante pasan al juego. Detalles en `docs/PLAN_60FPS.md`, sección 3 |
| `tools/perf/prof_report.py <run.log> [informe] [regex] [líneas]` | Extrae de un `run.log` el informe de `TTR_PROFILE` (`-1` = el último) de los hilos cuyo bloque case con la expresión regular: EE `dispatchGuestBranch`, VIF1 `Vif1Worker::run`, GS `GSAsyncBackend::run`, raster `GSBandPipeline::workerLoop` |
| `tools/purple_scan.ps1 <carpeta> [vblank]` | Cuenta en los PNG de `TTR_FRAME_DUMP` (desde un VBlank, 1100 por defecto) los píxeles morados de las carreras del pipeline del GS; tras cualquier cambio del GS, comparar varias ejecuciones con la referencia |
| `tools/disasm.py <símbolo|dirección>` | Desensamblado R5900 del ELF; `--sym <dir>` resuelve a símbolo, `--find <texto>` busca símbolos, `--gpref=-0x7120` lista los accesos a una global relativa a `$gp` |
| `tools/vu_disasm.py <volcado> [--start <dir>] [--count <n>]` | Desensamblado de microcódigo VU0/VU1 (pares upper/lower) de un volcado de memoria de código |
| `tools/irx_disasm.py <IRX> --base <dir> <dir>` | Desensamblado R3000 de un módulo IOP tal como se carga; `--imports`, `--strings` |
| `ttr_gs_bench <captura> [opciones]` | Junto a `TwoTowersRecomp.exe`. Reproduce una captura de `PS2X_GS_CAPTURE` en el GS por software y da ms por frame de juego (y CPU de todo el proceso). Respeta `PS2X_GS_THREADS` (`0` = referencia determinista) y `TTR_PROFILE`. `--hashes <f>` escribe un hash por presentación (VRAM + imagen presentada) y `--check-hashes <f>` compara con uno guardado y vuelca `diff_<n>.vram` donde difiere; `--dump-at <n>`, `--dump-vram`, `--compare-vram <a> <b>` y `--compare-hashes <a> <b>` para localizar diferencias; `--touching <página> [--from <n>]` lista las primitivas que usan una página; `--skinny <n>` lista los triángulos largos y finos (rayas) de la presentación `n`, con `TTR_BENCH_CLAMPED=1` los que tienen un vértice en 0 o 4095 y con `TTR_BENCH_RECT=x0,y0,x1,y1` todas las primitivas que tocan ese rectángulo (estado, textura, filtro y vértices); `--stats` reparte píxeles por clase de estado; `--list-presents`; `--presents <n>` corta la reproducción; `--png <prefijo>`; `--selftest` comprueba las tablas de direcciones (`GSSwizzle`); `--gpu-selftest` prueba el contexto OpenGL de cómputo y `--gpu-uploads` reproduce solo las subidas host→local en la CPU y en la GPU y compara la VRAM (fase G del plan); `--gpu` reproduce la captura entera en el GS en la GPU (con `--check-hashes ref2.hashes` la compara con la referencia de software; imprime lotes, causas de corte y, con `PS2X_GS_GPU_TIMING=1`, tiempos). Procedimiento en `docs/PLAN_60FPS.md` |
| `ttr_vu_bench <captura> [pasadas]` | Junto a `TwoTowersRecomp.exe` (`cmake --build --preset ... --target ttr_vu_bench`). Reproduce una captura de `PS2X_VU_CAPTURE`: compara el motor rápido con el intérprete ejecución a ejecución (registros, banderas, datos y paquetes XGKICK), mide ns por par dentro del motor e imprime el total de ciclos del motor rápido, que tiene que coincidir con el de `PS2X_VU_JIT=0` al tocar el compilador de bloques. Para iterar sobre el motor de VU sin arrancar el juego. `--find-xy <x> <y>` busca en los paquetes XGKICK del intérprete los vértices con esas coordenadas (hex, 12.4) y dice qué imagen y PC los emiten; `--dump-code <registro> <fichero>` guarda su código (para `tools/vu_disasm.py`) y su memoria de datos |
| `TwoTowersRecomp/game/sles_51256_trace.cpp` | Wrappers de funciones del juego: registra y llama a la función generada |

Para envolver otra función: declara un wrapper con la firma `void(uint8_t*, R5900Context*, PS2Runtime*)`
que llame a la función generada (`<símbolo>_0x<dirección>` en `ps2_recompiled_functions.h`) y regístralo con
`runtime.replaceFunction(dirección, &wrapper)`. Las llamadas que el recompilador enlaza directamente a un
stub del runtime (`sceSif*`, `sceCd*`...) no pasan por la tabla y no se pueden envolver así.
