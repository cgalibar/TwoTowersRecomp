# Plan para llegar a 60 fps

Objetivo fijado el 2026-11-09: **el juego tal cual es en PS2, sin mejoras, pero a 60 fps en PC**. Nada de más
resolución, filtros ni cambios de imagen: la salida tiene que seguir siendo la del GS por software que ya está
verificado. Este documento recoge lo que se midió, el rediseño acordado y las tareas, en orden, con el detalle
suficiente para hacer cada una en una sesión nueva sin releer la conversación original.

Estado de partida: la batalla del prólogo va a **~10 fps** (Release). Solo se ha hecho la fase de medición; del
runtime no se ha cambiado nada. Lo único tocado es el perfilador (`TwoTowersRecomp/debug/sampling_profiler.cpp`,
sin commitear) y los scripts de `tools/perf/`.

## 0. Cómo usar este documento

- Una tarea por sesión. Las tareas van en orden (sección 5); cada una dice de cuáles depende.
- Al empezar una sesión, pega esto cambiando el identificador:

  > Lee `docs/PLAN_60FPS.md` entero. Haz la tarea **B2**. Sigue sus pasos, mide antes y después como indica la
  > sección 3, y al terminar actualiza la tabla de estado de la sección 6 con el resultado medido. No hagas
  > commit ni push si no te lo pido.

- Al acabar cada tarea: compilar, medir, comprobar la imagen, exportar el parche (sección 3.6) y apuntar el
  resultado en la sección 6. Si una tarea no da ganancia medible, se revierte y se apunta el porqué: ya hubo
  varios experimentos sin ganancia (sección 2.4) y no hay que repetirlos.
- Los números de línea son orientativos (del 2026-11-09, parche 0013). Buscar siempre por nombre de función.

## 1. Reglas del proyecto que afectan a este trabajo

- **Parches.** PS2Recomp es un submódulo en `tools/PS2Recomp`. Sus cambios no se commitean en el submódulo:
  viven como pila de parches en `tools/patches/PS2Recomp/*.patch` (hay 13). `git status` mostrando
  ` m tools/PS2Recomp` es lo normal. Cada tarea que toque el runtime termina exportando un parche nuevo
  (`0014-...`, `0015-...`).
- **Código generado.** `TwoTowersRecompLib/generated/` (4722 ficheros) no se commitea ni se edita a mano. Los
  cambios de comportamiento del juego van en `TwoTowersRecomp/game/sles_51256_overrides.cpp`.
- **Memoria del juego.** El juego usa la RAM hasta 32 MiB; los datos del runtime en memoria de invitado van en
  `0x80000-0x100000`.
- **Finales de línea.** Editar con la herramienta de edición, no con scripts de Python que reescriban el fichero
  entero: varios ficheros son CRLF y otros LF (`sampling_profiler.cpp` y `main.cpp` son LF).
- **Exactitud.** Cualquier ruta rápida nueva tiene que dar la misma imagen que la actual. No se aceptan
  "aproximaciones que se ven bien": se compara con el banco de la fase A.
- **Sin descargas.** No añadir bibliotecas de terceros descargándolas sin preguntar antes.

## 2. Qué se midió

Máquina: Ryzen 5 3600X (6 núcleos, 12 hilos, Zen 2, AVX2). Escena: batalla del prólogo, a partir del VBlank
~2700. Compilación de perfilado: `win-clang-relwithdebinfo` (va ~25 % más lenta que Release: 7,5 fps frente a 10).

### 2.1 Hilos

| Hilo | Qué hace | Ocupado | Por frame (a 7,5 fps, 125 ms) | Bloqueado en |
| --- | --- | --- | --- | --- |
| EE | código recompilado, VU0, emulación IOP | 21 % | ~26 ms | 72 % esperando a VIF1 (`Vif1Worker::enqueue`) |
| VIF1 | desempaquetado VIF1, VU1, parseo GIF, envío al GS | 53 % | ~66 ms | 47 % en `GSAsyncBackend::Submit` (cola del GS llena) |
| GS | `GSAsyncBackend::run`: dependencias, subidas, CLUT, presentación | 31 % | ~38 ms | 55 % en `FlushPendingUnlocked` (37 % desde `LoadClut`), 20 % esperando hueco en `publish` |
| 9 de raster | `GSBandPipeline::workerLoop` | 55-72 % | ~500 ms sumados | ~35 % en barreras (`waitUntil`), 10 % sin trabajo |

Ningún hilo está al 100 %: cada etapa espera a la siguiente. Por eso afinar una sola etapa no da nada.

### 2.2 Carga por frame en la batalla

- ~8,2 M de píxeles escritos (36 veces la pantalla de 512x448), ~57 000 triángulos, ~2 000 sprites. La mitad
  del tiempo de raster son sprites (pocos y enormes) y la otra mitad triángulos.
- ~250 subidas de textura por frame (~2 100 por segundo) sobre **la misma zona de VRAM reutilizada**. Cada una
  obliga hoy a una barrera en la cola de bandas: de ~4 000 huecos por segundo, ~3 900 llevan barrera.
- ~45 cargas de CLUT por segundo que esperan a que se aplique una subida pendiente (~7 ms cada una).
- ~1 600 sprites por frame cuya textura comparte páginas con su propio destino (`read-after-write: self`).
- VU1: ~2,9 M de pares de instrucciones por frame. VU0: ~234 000 pares en ~1 850 ejecuciones.
- Coste de raster: ~80 ns por píxel escrito. Un rasterizador escalar razonable anda por 10-15 ns; con AVX2, 3-5.

### 2.3 Qué hace falta para 60 fps

Cada etapa tiene 16,6 ms de su hilo por frame:

| Etapa | Ahora (Release, estimado) | Objetivo | Factor |
| --- | --- | --- | --- |
| Raster (suma de 9 hilos) | ~400 ms + barreras | ≤ 100 ms y sin barreras | ~5x |
| Hilo VIF1 | ~50 ms | ≤ 12 ms | ~4-5x |
| Hilo GS | ~30 ms | ≤ 8 ms | ~3-4x |
| Hilo EE | ~20 ms | ≤ 10 ms | ~2x |

En los planos con humo los píxeles se doblan (~16 M por frame): el raster necesita margen.

### 2.4 Experimentos que ya se probaron y no sirvieron (no repetir)

- Dependencias más precisas para TEXFLUSH y para las subidas en serie: la espera solo cambia de sitio.
- `Flush`/`Present` sin bloqueo, espera de CLUT más precisa: sin ganancia.
- 6 u 8 hilos de raster en lugar de 9: peor.
- Lo que sí ayudó: cola del GS de 8192 a 65536 (+20 %) y que `VIF0_STAT` no sincronice VIF1.

### 2.5 Conclusiones

1. Las barreras vienen de las subidas de textura sobre una zona reutilizada. Mientras los dibujos en cola lean
   las texturas de la VRAM viva, cada subida tiene que esperar a los dibujos anteriores. La solución es la del
   renderizador por software de PCSX2: **cada dibujo lee su textura de una copia tomada al encolarlo**, y la
   subida se aplica a la VRAM en el acto.
2. El coste por píxel es 5-8 veces mayor de lo necesario: direcciones de VRAM calculadas con funciones
   genéricas por píxel, copias de páginas de textura por cada hilo, y preparación de cada primitiva repetida en
   los 9 hilos (57 000 x 9 por frame).
3. En el hilo VIF1 el código generado por el JIT es solo una cuarta parte; el resto es `commit`, `vifUnpack`,
   `jitStall`, llamadas a manejadores y el camino GIF → `Submit`.
4. El hilo EE cabe en el presupuesto si se quitan copias (datos DMA hacia VIF1, estado de VU0 en cada ejecución).
5. El límite de frames del juego es el VBlank (sección 5, fase E): con VBlank a 60 Hz y el reloj HBLANK en
   tiempo real, el juego va a 60 sin cambiar su velocidad.

## 3. Entorno: compilar, ejecutar, medir

Raíz del repo: `J:\El Señor de los Anillos Las Dos Torres Recompilado`. Runtime: `tools/PS2Recomp/ps2xRuntime`.

### 3.1 Compilar

Desde PowerShell, con la raíz del repo como directorio de trabajo, tiempo límite de 10 minutos:

```powershell
Set-Location "J:\El Señor de los Anillos Las Dos Torres Recompilado"; & .\tools\build_runtime.ps1 -Preset win-clang-release -SkipPatches | Select-Object -Last 8
```

- Presets: `win-clang-release` (medir fps), `win-clang-relwithdebinfo` (perfilar: tiene símbolos, sin LTO).
- **No añadir `2>&1`**: PowerShell 5.1 convierte los avisos de cmake en error y la orden sale con código 1.
- Compilador: clang-cl, `/O2 /Ob2 /Oi /GL` más LTCG y **`/arch:AVX`** (no AVX2: el ejecutable solo exige AVX,
  `TTR_X64_ARCH`/`PS2X_RELEASE_ARCH` en `docs/BUILDING.md`; corregido el 2026-11-11, antes este documento decía
  AVX2). `/fp:fast` solo se activa con MSVC real, así que clang-cl mantiene coma flotante IEEE estricta y, sin
  FMA en el objetivo, no hay contracción: el código vectorial escrito con las mismas fórmulas y en el mismo orden
  da resultados idénticos bit a bit. Con AVX hay operaciones de coma flotante de 256 bits, pero las enteras de
  256 bits, los *gather* y FMA son de AVX2. Para usar AVX2 sin subir el requisito: un fichero aparte compilado con
  `/arch:AVX2` y `/clang:-ffp-contract=off`, elegido en tiempo de ejecución con `cpuid`. Nunca `_mm256_fmadd_ps`.

### 3.2 Ejecutar la batalla de forma automática

```bash
tools/perf/run_battle.sh <carpeta_salida> <segundos> [preset]
```

Arranca el juego con `TTR_HACK_SKIPPABLE_FMV=1`, un guion de mando que entra en la partida
(`TTR_PAD_SCRIPT="1100:cross;3000-3300:ly=0;3400:square"`), volcado de frames cada 100 VBlanks en la carpeta,
`run.log` con la salida y `mem.log` con la memoria cada 10 s; al acabar mata el proceso. La batalla empieza hacia
el VBlank 2700 (~1 minuto). Las variables de entorno que se pongan delante pasan al juego. La carpeta de salida
va fuera del repo (directorio temporal de la sesión).

### 3.3 Medir fps

El `run.log` tiene líneas con `fps=` por tramo de VBlanks. **La cifra a comparar es la media de `fps=` entre los
VBlanks 3400 y 6900** (120 s de ejecución bastan). Una ejecución suelta varía ±0,5 fps: para diferencias
pequeñas, tres ejecuciones de cada variante. Referencia actual: menús/logos ~49,5; FMV 25; cinemática con motor
4,5-8; batalla ~10 (Release).

### 3.4 Perfilar

```bash
TTR_PROFILE=25,70 TTR_FRAME_DUMP_EVERY=1000 tools/perf/run_battle.sh <carpeta> 120 win-clang-relwithdebinfo
python tools/perf/prof_report.py <carpeta>/run.log -1 "GSBandPipeline::workerLoop" 60
```

`TTR_PROFILE=<segundos>,<n>` saca cada `<segundos>` un informe por hilo (los que pasen del 5 % ocupado):
`self x% incl y% función`, luego las funciones por tiempo inclusivo y líneas `from x% hoja <- llamante`. El
tercer argumento de `prof_report.py` es una expresión regular que elige los hilos: EE `dispatchGuestBranch`,
VIF1 `Vif1Worker::run`, GS `GSAsyncBackend::run`, raster `GSBandPipeline::workerLoop`. Los porcentajes son sobre
todas las muestras del hilo, incluidas las esperas (`ZwWaitForAlertByThreadId`). La pila se corta en el código
del JIT de VU, así que el tiempo inclusivo de los marcos exteriores no cuenta las muestras dentro del JIT.

Contadores (uno por segundo en el log): `PS2X_GS_STATS=1` (`[gs-time]` reparto dibujo/barrera/ocioso de los
hilos de raster y tipos de hueco, `[gs-flush]`, `[gs-stats]` píxeles y primitivas), `PS2X_VIF1_STATS=1`,
`PS2X_VU_STATS=1`, `PS2X_VU_JIT_STATS=1`. `PS2X_GS_STATS` añade ~4 % al hilo GS por las lecturas del reloj: no
dejarlo puesto al medir fps. El resto de interruptores está en `docs/DEBUGGING.md`.

### 3.5 Comprobar la imagen

- Banco del GS (fase A): comparación exacta de VRAM contra la ruta de referencia. Es la prueba principal.
- En el juego: `TTR_FRAME_DUMP` y comparar a ojo los PNG del mismo VBlank antes y después (el juego no es
  determinista entre ejecuciones porque el reloj es real, así que no sirve comparar por hash).
- Bloques morados de 8x8 (carrera conocida de la cola de bandas, ~3-4 % de los frames de batalla):
  `powershell -File tools\purple_scan.ps1 <carpeta> 2700`. Tras la fase B tiene que dar 0.
- Interruptores para aislar fallos: `PS2X_GS_THREADS=1`, `PS2X_GS_ASYNC=0`, `PS2X_VIF1_ASYNC=0`,
  `PS2X_VU_FAST=0`, `PS2X_VU_JIT=0`, `PS2X_REALTIME=0`.

### 3.6 Exportar el parche al acabar una tarea

```powershell
Set-Location "J:\El Señor de los Anillos Las Dos Torres Recompilado"; & .\tools\export_patch.ps1 -Name "gs-capture-bench"
```

Después, `tools\apply_patches.ps1` tiene que responder que todos los parches ya están aplicados. Añadir la línea
del parche nuevo a `tools/patches/README.md`.

## 4. Mapa del código que se va a tocar

Rutas relativas a `tools/PS2Recomp/ps2xRuntime`.

**Recorrido de un frame.** El juego (hilo EE) construye la lista de dibujo y la deja en una cola de 4 cadenas
DMA; su manejador de VBlank lanza una por VBlank por el canal VIF1. `Vif1Worker` (hilo VIF1) desempaqueta, ejecuta
los microprogramas de VU1 y cada XGKICK entrega un paquete GIF: `VuFastEngine::kick` → `VU1Interpreter::submitKick`
→ `PS2Memory::submitGifPacket` → `GifArbiter::drain` → `GS::processGIFPacket` → `GS::vertexKick` →
`GSAsyncBackend::Submit`. El hilo GS saca las órdenes de un anillo de 65 536 y llama a `GSCpuBackend`, que reparte
las primitivas en bandas de 8 filas entre 9 hilos de raster.

| Fichero | Líneas | Qué contiene |
| --- | --- | --- |
| `src/lib/gs/gs_cpu_backend.cpp` | 3720 | Todo el GS por software (ver tabla siguiente) |
| `include/runtime/gs/gs_cpu_backend.h` | 182 | Lote pendiente `m_pending`, conjuntos de páginas `std::bitset<512>` (páginas de 8 KiB), `m_pendingSerial`, VRAM sombra, `m_clut` |
| `src/lib/gs/gs_async_backend.cpp` y `.h` | 357 + 107 | Hilo GS. `Command { type, batch, tex0, texclut, transfer, present, data }`, `kCapacity = 65536`, `kPublishBatch = 32`, `execute(Command&)` |
| `include/runtime/gs/gs_types.h` | 315 | `GSVertex`, `GSDrawState`, `GSPrimitiveBatch` (3 vértices + estado completo por primitiva), `GSTransferCommand`, `GSPresentationRequest` |
| `include/runtime/gs/ps2_gs_memory.h`, `ps2_gs_psm*.h`, `src/lib/gs/ps2_gs_memory.cpp` | 591 + … + 448 | `GSMem`: direcciones entrelazadas (swizzle) y lectura/escritura por formato |
| `include/runtime/gs/gs_texture_page_cache.h` | 52 | `GSMem::TexturePageCache`: 64 copias de páginas de 8 KiB por hilo (a eliminar en B2) |
| `src/lib/gs/gs_frontend.cpp` | 1769 | `GS::processGIFPacket`, `vertexKick`, `buildDrawBatch`, `writeRegisterPacked`, `recordDrawDebugEventUnlocked` |
| `src/lib/vu/ps2_vu_fast.cpp` | 2304 | Motor rápido de VU y JIT x64 por bloques |
| `src/lib/vu/ps2_vu1_core.cpp` | 2241 | `VU1Interpreter` (`execute`, `reset`, `submitKick`, XGKICK) |
| `src/lib/ps2_vif1_interpreter.cpp` | 724 | `vifUnpack` |
| `src/lib/ps2_vif1_worker.cpp` | 240 | `Vif1Worker::enqueue/run/sync`; cola `std::deque<std::vector<uint8_t>>` |
| `src/lib/ps2_gif_arbiter.cpp` | 69 | `GifArbiter::submit/drain` |
| `src/lib/ps2_memory.cpp` | — | `processVIF1Data`, `submitGifPacket`, `processPendingTransfers`, `writeIORegister`, `setEeTimerHBlankRate` (línea ~424) |
| `src/lib/ps2_runtime.cpp` | — | `executeVU0Microprogram` (~1720), lambda de VU0 (~966), `dispatchGuestBranch` |
| `src/lib/Kernel/EeScheduler.cpp` | — | `setVBlankRate` (~496), `catchUpToHostClock` |
| `src/lib/Kernel/Stubs/GS.cpp` (~807), `src/lib/Kernel/Syscalls/System.cpp` (~16) | — | Llaman a `setVBlankRate(modo == 3 ? 50 : 60)` |

Dentro de `gs_cpu_backend.cpp` (línea de inicio):

| Línea | Elemento |
| --- | --- |
| 218 | `class GSBandPipeline` (`touchesPagesInFlight` 315, `waitAll` 362, `workerLoop` 399) |
| 489 | `class GSRasterWorkers` |
| 1141 | `Submit` · 1284 `MarkPendingWritesUnlocked` · 1380 `PrimitiveReadPages` · 1510 `PrimitiveWritePages` |
| 1559 | `FlushPendingUnlocked` · 1597 `DrawPendingUnlocked` · 1629 `DrawBatchBands` |
| 1659 | `LoadClut` · 1766 `ShadowPrepareUnlocked` · 1790 `LoadClutUnlocked` · 1848 `TextureFlush` |
| 1905 | `SnapshotVram` · 1929 `DrawPrimitive` · 2067 `WritePixel` (ruta lenta de referencia) |
| 2279 | `struct PixelPipe` · 2304 `BuildPixelPipe` · 2350 `struct Sampler` · 2458 `BuildSampler` |
| ~2514 | `writePixelFast` · 2607 `SampleTexture` · 2707 `ForEachRowBand` |
| 2758 | `DrawSprite` · 2888 `DrawTriangle` · 3057 `DrawLine` |
| 3124 | `BeginTransfer` · 3191 `UploadImage` · 3217 `UploadImageUnlocked` · 3324 `PerformLocalToLocalTransfer` · 3364 `PerformLocalToHostTransfer` |
| 3447 | `ClearFramebuffer` · 3508 `CopyFrameToHostRgba` · 3586 `Present` · 3600 `PresentFromLocalMemory` |

En el juego (`TwoTowersRecomp/`): `main.cpp`, `debug/{frame_dump,pad_script,sampling_profiler,vu_bench}.cpp`,
`game/sles_51256_overrides.cpp`, `game/sles_51256_trace.cpp`.

## 5. Tareas

Orden: A0 → A1 → A2 → B1 → B2 → B3 → B4 → B5 → B6 → B7 → C1 → C2 → C3 → C4 → D1 → D2 → E1 → E2 → F1.
Las fases C y D no dependen de la B y se pueden adelantar, pero su efecto en los fps no se ve hasta que el GS
deje de ser el tapón. La E no tiene sentido hasta que la batalla pase de ~50 fps.

### Fase A — Banco de pruebas del GS

Sin esto no se puede trabajar en el raster: cada prueba en el juego cuesta dos minutos y no es reproducible.

#### A0. Dejar limpio el punto de partida

1. Compilar `win-clang-relwithdebinfo` y comprobar que `TTR_PROFILE=25,70` saca informes con el formato nuevo.
2. Revisar que la fila de `TTR_PROFILE` en `docs/DEBUGGING.md` describe el formato con pila (sección 3.4).
3. Pedir al usuario permiso para commitear `sampling_profiler.cpp`, `tools/perf/` y este documento.

Hecho cuando: `git status` solo muestra ` m tools/PS2Recomp`.

#### A1. Captura del flujo de órdenes del GS

**Objetivo.** Grabar en un fichero todo lo que recibe `GSCpuBackend` durante N frames de la batalla, más el
estado inicial, para reproducirlo fuera del juego.

**Dónde.** `src/lib/gs/gs_async_backend.cpp`, en `GSAsyncBackend::execute(Command&)` (hilo GS: ahí las órdenes
ya están en orden). Código nuevo en `src/lib/gs/gs_capture.cpp` + `include/runtime/gs/gs_capture.h`.

**Pasos.**
1. Variable `PS2X_GS_CAPTURE=<fichero>,<vblank_inicio>,<frames>`. Hace falta saber el VBlank actual en el hilo
   GS: usar el contador de órdenes `Present` (una por VBlank) o `GSPresentationRequest::vsyncTick`.
2. La captura empieza **justo después de ejecutar una orden `Present`**, cuando el contador llega a
   `vblank_inicio`, y solo si no hay una transferencia a medias (`GetTransferSnapshot()`: `totalPixels == 0` o
   `copiedPixels == totalPixels`); si la hay, esperar al siguiente `Present`.
3. Cabecera: magia `TTRGS001`, tamaño de VRAM, volcado de VRAM (`SnapshotVram`, 4 MiB; antes vaciar lo
   pendiente con `Flush`), el búfer CLUT (`m_clut`, `uint16_t[512]`) y el último `GSTex0Reg`/`GSTexClutReg`
   cargados. Añadir a `GSCpuBackend` un método `CaptureState(...)` que devuelva todo esto.
4. Un registro por orden: `uint8_t tipo`, `uint32_t tamaño`, datos. Para `Submit`, `GSPrimitiveBatch` es copiable
   byte a byte; para no llenar el disco, escribir los vértices siempre y el `GSDrawState` solo si cambia respecto
   al anterior (un byte indica si viene). Para `UploadImage`, los bytes de `data`. Para `Present`, la petición.
5. Terminar tras `<frames>` órdenes `Present` **que cierren un frame del juego**. Como la batalla va a 10 fps y
   hay 50 `Present` por segundo, contar frames por `Present` daría capturas casi vacías: capturar por tiempo
   (por ejemplo 250 `Present` = 5 s = ~50 frames de juego) y anotar el número de primitivas en la cabecera final.
6. Escribir con `fwrite` a un búfer grande; la captura puede frenar el juego, no importa.

**Verificación.** Tamaño esperado: ~57 000 primitivas x ~150 bytes ≈ 9 MB por frame de juego más las texturas.
Capturar ~20 frames de batalla (VBlank 4000) y otra de un plano con humo. Guardarlas fuera del repo.

#### A2. Herramienta `ttr_gs_bench`

**Objetivo.** Reproducir una captura contra `GSCpuBackend`, medir tiempo y comparar la VRAM resultante entre
dos configuraciones.

**Dónde.** `TwoTowersRecomp/debug/gs_bench.cpp`, objetivo CMake `ttr_gs_bench` copiando cómo está montado
`ttr_vu_bench` (`debug/vu_bench.cpp` y su entrada en `TwoTowersRecomp/CMakeLists.txt`).

**Pasos.**
1. Cargar la captura, crear un búfer de VRAM de 4 MiB, `GSCpuBackend::Initialize(vram, size)`, restaurar el
   estado (método `RestoreState` simétrico al de A1).
2. Reproducir las órdenes llamando a los mismos métodos que `GSAsyncBackend::execute`. En cada `Present`: `Flush`
   y acumular un hash FNV-1a de 64 bits de la VRAM completa.
3. Opciones: `--repeat N` (repetir y dar el mejor tiempo), `--dump-vram <fichero>` (VRAM final),
   `--hashes <fichero>` (un hash por `Present`), `--compare <a> <b>` (número de bytes distintos y primeras 20
   direcciones con página, para localizar el fallo), `--png <prefijo>` (imagen presentada, con el código de
   `debug/frame_dump.cpp`), `--stats` (histograma descrito abajo), `--selftest` (lo usa B1).
4. Salida: milisegundos totales y por frame de juego, primitivas por segundo, píxeles por segundo.
5. `--stats`: por cada primitiva, clasificar el estado y sumar píxeles (área del rectángulo recortado) por
   clase: tipo (sprite/triángulo/línea), formato de framebuffer y de Z, prueba Z, `zmask`, prueba alfa, mezcla
   (`abe` y los cuatro selectores de `ALPHA`), `tme` + formato de textura + filtro lineal, `tfx`/`tcc`, niebla,
   `fst`, DATE, `fbmsk != 0`. Imprimir las 25 clases con más píxeles. **Esta lista decide qué variantes se
   especializan en B5 y B6.** Guardarla en `docs/notes/gs-state-classes.md`.

**Referencia.** La ruta de referencia es la actual con un solo hilo (`PS2X_GS_THREADS=1`), que es determinista.
Comprobar que con un hilo el dibujo va por `DrawPrimitive` sin cola de bandas. Guardar sus hashes como
`referencia.hashes`; toda tarea de la fase B tiene que reproducirlos con 1 y con 9 hilos.

**Verificación.** Dos ejecuciones con un hilo dan los mismos hashes. Con 9 hilos puede haber diferencias (la
carrera de los bloques morados): apuntar cuántos frames difieren; tras B3 tienen que ser 0.

**Línea base a apuntar en la sección 6:** ms por frame de juego con 9 hilos en la captura de batalla y en la
de humo. Objetivo final de la fase B: **≤ 10 ms** en batalla y ≤ 16 ms en humo.

### Fase B — GS: sin barreras y con raster rápido

Decisión tomada: **se mantiene el GS por software** (es exacto y está verificado; un renderizador por GPU sería
otro proyecto) y se reorganiza como el renderizador por software de PCSX2. Todo se valida con `ttr_gs_bench`.

#### B1. Direcciones de VRAM separables (tablas fila + columna)

**Por qué.** Hoy cada píxel y cada texel llama a `addrPSMCT32` y similares (divisiones, dos tablas, sumas) a
través de punteros a función. Esas direcciones son una suma de una parte que depende de `y` y otra de `x`.

**Hecho de partida** (`include/runtime/gs/ps2_gs_psmct32.h`, `…psmct16.h`, `…psmt8.h`, `…psmt4.h`): todas las
funciones tienen la forma `dir = bp*256 + (fila_pagina*paginasPorFila + col_pagina)*8192 +
tablaBloque[by][bx]*256 + tablaColumna[py][px]*unidad`, en bytes (en T4, en medios bytes: `página << 14`,
bloque x 512). `paginasPorFila` es `bw` (o 1 si es 0) y en T8/T4 es `bw >> 1` (o 1). La máscara de 4 MiB se
aplica después, al acceder.

**Qué hacer.** Fichero nuevo `include/runtime/gs/gs_swizzle_tables.h` (+ `.cpp`):

1. Por cada disposición (CT32 —vale para CT24, T8H, T4HL, T4HH—, CT16, CT16S, Z32 —vale para Z24—, Z16, Z16S,
   T8, T4) construir **al arrancar, a partir de las funciones existentes**, no a mano:
   - `col[x]`, `x` en `[0, 2048)`: `addr(bp=0, bw=1, x, 0) - addr(0, 1, 0, 0)` más el término de página de `x`.
   - `filaBase[y]`, `y` en `[0, 2048)`, sin el término de página vertical, y aparte `paginaFila[y] = y / altoPagina`.
   - La fila final de una superficie concreta es `fila[y] = bp*256 + paginaFila[y]*paginasPorFila*8192 +
     filaBase[y]`. Se calcula por superficie `(bp, bw, psm)` para las filas del recorte y se guarda en una caché
     pequeña (8 entradas) dentro del backend.
2. **T8 y T4 no son una suma pura**: en `columnTable8` y `columnTable4` las filas 2-3 y 6-7 de cada grupo de 4
   usan otra permutación de columnas. Para ellas, dos tablas de columnas y un selector por fila:
   `dir = fila[y] + col[sel[y]][x]`. Deducir `sel` comparando `tablaColumna[py][px] - tablaColumna[py][0]`
   entre filas, no escribirlo a mano.
3. `ttr_gs_bench --selftest`: para cada formato, `bw` de 1 a 32, `bp` en {0, 1, 31, 32, 0x1234, 0x3FFF} y todo
   `(x, y)` en `[0, 1024) x [0, 1024)`, comprobar que `fila[y] + col[x]` es igual a la función original. Si para
   algún formato no hay descomposición, el test lo dice y ese formato se queda con la función original.
4. Funciones `inline` de acceso que usen las tablas: `leerPixel32(vram, fila, col, x)`, etc.

Esta tarea no cambia el comportamiento: solo añade tablas y el test. Hecho cuando el test pasa para todos.

#### B2. Texturas leídas de instantáneas (la tarea que quita las barreras)

**Por qué.** Sección 2.5, punto 1. Es el cambio estructural del plan. Además elimina `TexturePageCache` (cada
hilo copia páginas de 8 KiB: ~5 % del tiempo de raster en `memmove`) y deja las texturas en formato lineal,
que es lo que necesita el muestreo con AVX2.

**Diseño.** Todo lo siguiente ocurre **en el hilo GS, dentro de `Submit`**, antes de encolar la primitiva.

1. *Versiones de página.* `uint32_t m_pageVersion[512]` (páginas de 8 KiB). Se incrementa en toda escritura a
   VRAM: subidas (`UploadImageUnlocked`), copias local→local, `ClearFramebuffer`, y por cada primitiva encolada,
   las páginas de su framebuffer y de su Z (ya las calcula `PrimitiveWritePages`).
2. *Secuencias.* `m_submitSeq` (se incrementa por primitiva o por grupo encolado), `m_doneSeq` (mínimo de lo
   que han terminado todos los hilos de raster, lo publica la cola), y `uint64_t m_pageLastWriteSeq[512]`
   (última primitiva encolada que escribe en la página).
3. *Instantánea de textura.* Clave: `(tbp0, tbw, psm, tw, th)` y, solo en formatos de 24 y 16 bits, también
   `TEXA`. Contenido: imagen lineal de `2^tw x 2^th` texels:
   - formatos directos (CT32, CT24, CT16, CT16S): `uint32_t` RGBA **final**, con `applyTexa` y la conversión de
     5551 ya aplicadas, exactamente como hace hoy `Sampler::point` para `kind` 1 y 2;
   - formatos con paleta (T8, T4, T8H, T4HL, T4HH): `uint8_t` con el índice.
   Se convierte por teselas (una tesela = una página de VRAM del formato). Cada tesela guarda la versión de la
   página con la que se convirtió; es válida si coincide con `m_pageVersion`.
4. *Paleta resuelta.* Objeto inmutable `uint32_t rgba[256]` con el color final de cada índice: lo que hoy
   calculan `clutLookup` + `applyTexa` por texel (reglas `csa`/`csm`/`cpsm`, CLUT `uint16_t[512]` con la palabra
   baja en `[i]` y la alta en `[i + 256]`). Clave: generación del CLUT (contador que sube en cada `LoadClut`
   que cambia el búfer) + `csa`, `cpsm`, `psm`, `TEXA`. Se reutiliza mientras no cambie.
5. *Al encolar una primitiva con textura:*
   a. Calcular el rectángulo de texels que puede leer (la lógica de `PrimitiveReadPages`: caja de UV más modos
      de repetición; si repite o se sale, toda la textura; con filtro lineal, un texel más por lado).
   b. Para cada tesela del rectángulo que no sea válida: si `m_pageLastWriteSeq[página] > m_doneSeq`, **esperar**
      a que el raster pase esa secuencia (es la dependencia real de dibujar-a-textura); después convertir la
      tesela desde la VRAM con las tablas de B1.
   c. Si hay que reconvertir una tesela que ya era válida y la instantánea la usan dibujos en vuelo
      (`ultimoUso > m_doneSeq`), **no se pisa**: se toma una instantánea nueva de una reserva, se convierte en
      ella lo necesario y sustituye a la vieja en la caché. La vieja va a una lista de retiradas
      `(ultimoUso, instantánea)` y vuelve a la reserva cuando `m_doneSeq >= ultimoUso`. Así el raster nunca
      toca contadores de referencias.
   d. La primitiva encolada lleva punteros a la instantánea y a la paleta; `ultimoUso = m_submitSeq`.
6. *Subidas (`UploadImage`).* Se ejecutan **en el acto en el hilo GS** sobre la VRAM. Solo esperan si alguna
   página destino tiene escritores en vuelo (`m_pageLastWriteSeq[página] > m_doneSeq`). Dejan de ser operaciones
   en serie del hilo 0 y dejan de crear barreras. Suben `m_pageVersion`.
7. *`LoadClut`.* Lee la VRAM en el hilo GS; espera solo con la misma condición que las subidas. Las ~45 esperas
   por segundo de hoy desaparecen porque la paleta recién subida ya está en VRAM.
8. *Muestreo.* `Sampler` pasa a tener `texels`, `ancho`, tipo y `paleta`. `point(u, v)` = envolver coordenada
   (igual que ahora) → `texels[v*ancho + u]` → si es índice, `paleta[índice]`. Se eliminan `TexturePageCache`,
   `m_workerClut`, `ShadowPrepareUnlocked` y la VRAM sombra si ya no las usa nadie.

**Pasos.**
1. Implementar versiones, secuencias, caché de instantáneas y paletas (fichero nuevo
   `src/lib/gs/gs_texture_snapshots.cpp`); todavía sin usarlas.
2. Cambiar `BuildSampler`/`Sampler` para leer de la instantánea. Comprobar hashes con 1 hilo: **idénticos**.
3. Pasar las subidas y `LoadClut` a ejecución inmediata con la regla del punto 6. Quitar de `Submit`,
   `MarkPendingWritesUnlocked`, `TextureFlush` y `FlushPendingUnlocked` las dependencias que solo existían
   por las lecturas de textura. Comprobar hashes con 1 y 9 hilos.
4. Medir en el banco y en el juego con `PS2X_GS_STATS=1`.

**Trampas.**
- Texturas enormes declaradas (`tw`/`th` de 10 con una región pequeña en uso): reservar la imagen completa pero
  convertir solo las teselas del rectángulo; no convertir nunca la textura entera por adelantado.
- `tbw` menor que el ancho de la textura: la conversión usa la misma función de dirección, así que sale igual.
- Los sprites con realimentación (textura en las mismas páginas que su destino, ~1 600 por frame): con este
  diseño cada uno invalidaría las teselas que acaba de escribir el anterior. Mirar primero qué hace hoy el
  código en `Submit` con `read-after-write: self` ("dibujado en el acto" frente a "desde una copia") y qué
  primitivas son (`ttr_gs_bench --stats` filtrando las que tienen `tbp0` dentro de su propio framebuffer). Regla
  propuesta: una tesela escrita por primitivas encoladas solo obliga a esperar y reconvertir si el rectángulo de
  texels de la nueva primitiva la toca; si las zonas leída y escrita son disjuntas a nivel de tesela, no pasa
  nada. Si aun así hay cientos de esperas por frame, tratarlo en una tarea aparte antes de seguir (B8, abajo).

**Hecho cuando:** hashes iguales a la referencia con 1 y 9 hilos; en `[gs-time]` casi todos los huecos son
`plain`; `clut` en `[gs-flush] waits` cae a ~0; `purple_scan` da 0. Ganancia esperada: de ~10 a 15-20 fps (el
tope pasa a ser el coste por píxel y el hilo VIF1).

#### B3. Reglas de sincronización que quedan

Con B2, el hilo GS solo espera al raster en estos casos. Revisar `GSBandPipeline` y `Submit` para que sean
exactamente estos y ninguno más:

| Caso | A qué se espera |
| --- | --- |
| Convertir una tesela de textura cuya página tiene escritores en vuelo | hasta `m_pageLastWriteSeq[página]` |
| Subida, copia local→local o borrado sobre páginas con escritores en vuelo | ídem |
| `LoadClut` desde páginas con escritores en vuelo | ídem |
| Lectura local→host, `Present`, lecturas de depuración | las páginas leídas |
| Primitiva cuyo framebuffer o Z solapa en páginas con una superficie en vuelo de **otra disposición** (distinto `bp`, `bw` o clase de formato) | todo lo anterior (barrera completa) |

Sobre el último caso: las bandas de 8 filas garantizan que dos hilos no tocan los mismos píxeles solo si todas
las primitivas en vuelo usan la misma correspondencia fila → memoria. Llevar una lista corta de superficies en
vuelo `(bp, bw, clase, páginas)`; una primitiva nueva encaja con una existente o no solapa con ninguna; si no,
barrera y se vacía la lista. Framebuffer y Z cuentan como superficies distintas. Los formatos de 24 bits
(CT24, Z24) escriben la palabra entera leyendo antes el byte alto: por eso una subida a T8H/T4HL/T4HH sobre
páginas que se están dibujando también tiene que esperar (ya lo cubre la segunda fila).

Si `GSBandPipeline` (64 huecos, barreras entre huecos) no permite expresar "esperar hasta la secuencia N" sin
barrera general, sustituirlo por una cola única ordenada: el hilo GS añade grupos de primitivas, cada hilo de
raster tiene su cursor y procesa todo en orden limitándose a sus bandas, `m_doneSeq` es el mínimo de los
cursores. Mantener el reparto actual por bandas entrelazadas de 8 filas (coincide con los bloques de 8x8 de
CT32 y de 16x8 de CT16, así que dos hilos no comparten línea de caché).

**Hecho cuando:** en `[gs-time]`, `barrier` + `serial-barrier` < 5 % del tiempo de los hilos; el hilo GS pasa
< 10 % en `FlushPendingUnlocked`; hashes iguales con 9 hilos en 10 repeticiones seguidas.

#### B4. Preparar cada primitiva una sola vez

**Por qué.** Hoy cada uno de los 9 hilos ejecuta para cada primitiva `BuildPixelPipe`, `BuildSampler`, la caja
y los coeficientes: 57 000 x 9 preparaciones por frame, aunque la primitiva solo toque una banda.

**Qué hacer.**
1. `struct RasterPrim`, que rellena el hilo GS en `Submit`: tipo; caja ya recortada por el *scissor*
   (`x0, x1, y0, y1`); para triángulos `fx0..fx2`, `fy0..fy2`, `winding`, `invAbsDenom` y los vértices; para
   sprites las coordenadas enteras ordenadas, `u0f`, `v0f`, incrementos y color; índice a un `PipeState`.
   Las primitivas degeneradas (`|denom| < 0.001f`, caja vacía) se descartan aquí y no se encolan.
2. `struct PipeState` = lo que hoy son `PixelPipe` + `Sampler` + filas de B1 + punteros a instantánea y paleta.
   Se crea **uno por cambio de estado**, no por primitiva: comparar el `GSDrawState` con el anterior (`memcmp`) y
   reutilizar. Viven en un vector por grupo encolado, inmutables para el raster.
3. El hilo de raster, por primitiva: cortar `[y0, y1]` con sus bandas; si no le toca ninguna fila, siguiente.
4. `GSPrimitiveBatch` deja de copiarse entero (3 vértices + ~170 bytes de estado) por cada primitiva en la cola
   de bandas.

**Verificación.** Hashes idénticos. En el perfil, `BuildPixelPipe`, `BuildSampler` y `memmove` desaparecen de
los hilos de raster.

#### B5. Bucles de píxel especializados (primero escalar)

**Por qué.** `writePixelFast` es una ruta genérica con ramas por píxel (recorte, niebla, prueba alfa, lectura
de framebuffer, DATE, prueba Z, mezcla, máscara, conversión a 16 bits) y accesos por puntero a función.

**Qué tiene que reproducir exactamente** (resumen; la fuente de verdad es el código actual):

- *`writePixelFast`*, en este orden: recorte; niebla `((fog*c) >> 8) + (((255 - fog)*fogC) >> 8)`; prueba alfa
  (`classifyAlphaTest` → qué se escribe: color, alfa, Z); lectura del framebuffer si hay DATE, `fbmsk`, mezcla o
  hay que conservar el alfa (16 bits → `Rgba5551ToRgba8888`; 24 bits → `| 0x80000000`); prueba DATE; prueba Z
  (0 nunca, 1 siempre, 2 `>=`, 3 `>`); mezcla `clampU8(((A - B) * C >> 7) + D)` teniendo en cuenta `pabe`; `fba`
  pone el bit 7 del alfa; `fbmsk`; conversión a 16 bits; escribir color; escribir Z si procede y `!zmask`.
- *`Sampler::sample`*: con `fst`, `texUf = u / 16.0f`; si no, `invQ = 1 / |q|`, `texUf = s * invQ * anchoTex`.
  Punto: `point((int)texUf, (int)texVf)`. Lineal: `sampleU = texUf - 0.5f`, `u0 = floor`,
  `wx = (uint32_t)(fx * 256.0f + 0.5f)`, los texels con peso 0 no se leen, y
  `lerp2(a, b, w) = (((a*(256 - w)) + (b*w) + 0x00800080) >> 8) & 0x00FF00FF` sobre los pares RB y GA.
- *`combineTexture`* (`tfx`, `tcc`) sin cambios.

**Qué hacer.**
1. Plantilla `template <Variante> void filaPixeles(const PipeState&, int y, int xa, int xb, ...)` donde
   `Variante` fija en compilación: formato de framebuffer (32/24/16), Z (sin Z, solo prueba, prueba y
   escritura; formato), prueba alfa sí/no, mezcla sí/no, textura (no, directa, paleta) y filtro, niebla, DATE.
   Dentro, el mismo código que `writePixelFast` pero con `if constexpr` y direcciones `fila[y] + col[x]` (B1).
2. Instanciar solo las ~10-15 variantes que suman > 95 % de los píxeles según
   `docs/notes/gs-state-classes.md` (A2). Las demás siguen por `writePixelFast`.
3. `PipeState` guarda el puntero a la función de fila elegida.
4. Empezar por **sprites** (mitad del coste, bucle más simple: Z, color y niebla constantes, solo varía la
   coordenada de textura). Semántica de `DrawSprite`: coordenadas enteras `(int)v.x - (ofx >> 4)`, ordenadas;
   con `fst`, `u0f = float(v0.u >> 4)`; si no, `u0f = (v0.s / q0) * anchoTex`; por píxel
   `tx = ((x - x0SinRecortar) + 0.5f) / anchoSprite`, `texUf = u0f + (u1f - u0f) * tx`; con `fst`,
   `sampleU = clamp((int)(texUf*16 + 0.5f), 0, 0xFFFF)`; color, Z y niebla de `v1`.
5. Casos triviales que merecen ruta propia si aparecen en el histograma: sprite sin textura ni mezcla (relleno
   de filas) y sprite 1:1 sin filtro.

**Verificación.** Hashes idénticos con cada variante activada por separado (variable `PS2X_GS_FAST_MASK` con un
bit por variante, para aislar la que rompa). Objetivo: de ~80 a ≤ 20 ns por píxel en el banco.

#### B6. Triángulos y sprites con AVX2 (8 píxeles por iteración)

**Depende de:** B1, B2, B4, B5. Hacerla solo si tras B5 el banco no baja de 10 ms por frame.

**Semántica de `DrawTriangle` que hay que conservar:** `fx = v.x - float(ofx >> 4)`; caja con floor/ceil
recortada; `denom = (fy1 - fy2)*(fx0 - fx2) + (fx2 - fx1)*(fy0 - fy2)`, descartar si `|denom| < 0.001f`; por
píxel, en `px = x + 0.5f`, `py = y + 0.5f`:

```
w0 = (((fy1 - fy2)*(px - fx2) + (fx2 - fx1)*(py - fy2)) * winding) * invAbsDenom
w1 = (((fy2 - fy0)*(px - fx2) + (fx0 - fx2)*(py - fy2)) * winding) * invAbsDenom
w2 = 1 - w0 - w1                      // fuera si alguno < -1.0e-4f
z  = (uint32_t)(double(v0.z)*w0 + double(v1.z)*w1 + double(v2.z)*w2 + 0.5)   // en double
r  = clampU8((int)(v0.r*w0 + v1.r*w1 + v2.r*w2))    // con iip; si no, color de v2
u  = (uint16_t)(v0.u*w0 + v1.u*w1 + v2.u*w2)        // con fst; si no, interpolar s, t, q y dividir
fog = clampU8((int)(v0.fog*w0 + v1.fog*w1 + v2.fog*w2))
```

**Reglas para que salga bit a bit igual.**
- Evaluar la fórmula por píxel, en el mismo orden de operaciones, con `_mm256_mul_ps`/`_mm256_add_ps`. Nada de
  interpolación incremental (sumar un paso por píxel da otros redondeos) ni FMA.
- `(int)` y `(uint16_t)` son truncamientos: `_mm256_cvttps_epi32`. La división de perspectiva, `_mm256_div_ps`
  (no `rcp`). `floor` → `_mm256_floor_ps`.
- Z: convertir los pesos a `double` (`_mm256_cvtps_pd`, dos mitades), operar en `double`, sumar 0,5 y truncar.
  `_mm256_cvttpd_epi32` solo vale por debajo de 2^31: sirve para Z24 y Z16; para Z32 restar 2^31 antes y
  sumarlo después, o dejar Z32 en la ruta escalar.
- Recorrer grupos de 8 píxeles **alineados a múltiplos de 8 en x** (empezar en `xa & ~7`) con una máscara para
  los de fuera. Así cada grupo cae dentro de un bloque de VRAM.
- Lectura de framebuffer y Z en formatos de 32/24 bits: un bloque de 8x8 son 64 palabras; la fila par ocupa las
  palabras 0,1,4,5,8,9,12,13 del par de filas y la impar las 2,3,6,7,10,11,14,15. Dos cargas de 256 bits +
  `_mm256_unpacklo_epi64`/`unpackhi` + `_mm256_permute4x64_epi64(…, 0xD8)` dan los 8 píxeles de la fila. Para
  escribir: operación inversa y mezclar con lo cargado. Leer-modificar-escribir las 16 palabras es seguro
  porque las filas `y` e `y ^ 1` están en la misma banda de 8 filas y por tanto en el mismo hilo.
- Formatos de 16 bits: lecturas y escrituras escalares dentro del bucle vectorial.
- Texturas: `_mm256_i32gather_epi32` sobre la instantánea lineal (B2); con paleta, segundo gather sobre la
  paleta resuelta (para índices de 8 bits, gather de 32 bits sobre los bytes y máscara `0xFF`; la instantánea
  necesita 3 bytes de relleno al final). Filtro lineal: 4 gathers y `lerp2` con enteros de 16 bits.
- Píxeles descartados por prueba (alfa, Z, DATE): máscara; el almacenamiento final mezcla con lo que había.

**Orden.** Primero la variante con más píxeles del histograma, validar, y seguir una a una. Mantener la
estrechez por fila actual (`rowSpan`, margen de ±2 píxeles) para no evaluar toda la caja.

**Verificación.** Hashes idénticos a la referencia. Objetivo: ≤ 6-8 ns por píxel de media; banco ≤ 10 ms por
frame de batalla con 9 hilos.

#### B7. Abaratar el camino hasta la cola (hilo GS y entrada del GS)

**Por qué.** El hilo GS gasta ~38 ms por frame aun sin contar esperas, y en el hilo VIF1 el camino
GIF → `Submit` son ~8 puntos de 53.

**Qué hacer** (medir cada punto con el perfil antes y después):
1. `GSAsyncBackend::Command` lleva un `std::vector<uint8_t> data` y un `GSPrimitiveBatch` completo en cada orden.
   Separar: las primitivas van en un anillo propio de registros compactos (3 vértices + índice de estado); el
   estado va en una tabla que solo crece cuando cambia (comparar con el anterior en `GS::buildDrawBatch`). Los
   datos de las subidas, en un búfer circular de bytes, sin `vector` por orden (hoy: `_Resize_reallocate`,
   `operator new`, `RtlSizeHeap`, `NtFreeVirtualMemory` en los perfiles).
2. `PrimitiveReadPages`/`PrimitiveWritePages` por primitiva con `std::bitset<512>`: calcular por primitiva solo
   el intervalo de páginas de su caja y acumular por grupo de mismo estado; las páginas de textura se calculan
   una vez por estado y rectángulo (B2).
3. Quitar del camino por primitiva `GS::recordDrawDebugEventUnlocked` (0,9 % del hilo VIF1: dejarlo tras una
   bandera que se lea una vez), los bloqueos (`Mtx_lock`, SRW: ~2 %) y las lecturas de reloj de `PS2X_GS_STATS`
   cuando está apagado.
4. `GSAsyncBackend::Submit` despierta al hilo GS con `WaitOnAddress`; publicar en grupos mayores (hoy 32) si el
   perfil muestra muchas llamadas a `NtAlertThreadByThreadIdEx`.
5. Subidas: `UploadImageUnlocked` + `WriteCT32/24/16/P8` píxel a píxel suman ~9 % del hilo GS. Con las tablas de
   B1, escribir por filas; para subidas alineadas a bloque, copiar bloques enteros.
6. `Present`: `CopyFrameToHostRgba` con las tablas de B1 (por filas). Cuesta ~1 ms y se hace por VBlank.

**Hecho cuando:** hilo GS ≤ 8 ms por frame de juego en el banco (tiempo total del banco menos el de los hilos
de raster) y hashes idénticos.

#### B8. Sprites con realimentación (solo si B2 lo deja pendiente)

~1 600 primitivas por frame leen una textura que comparte páginas con su propio destino. Pasos: (1) con
`ttr_gs_bench --stats`, listar esas primitivas: tamaño, `tbp0`, `fbp`, rectángulo leído y escrito, y a qué efecto
corresponden (volcar el frame con `--png` justo antes y después); (2) si leen y escriben zonas disjuntas de la
misma página, basta la regla por tesela de B2; (3) si de verdad leen lo que acaba de escribir la primitiva
anterior, agruparlas: una instantánea por grupo de primitivas consecutivas del mismo estado cuyas zonas leídas
no se solapen con lo escrito por el propio grupo, y espera solo entre grupos. Comparar siempre con la
referencia de un hilo; si la imagen cambia, no vale.

**Resultado esperado de la fase B completa:** banco ≤ 10 ms por frame; en el juego la batalla queda limitada por
el hilo VIF1 (~18-20 fps en Release mientras no se haga la fase C).

### Fase C — Hilo VIF1: VU1, desempaquetado y entrada GIF

Reparto medido del hilo VIF1 (53 % ocupado ≈ 66 ms por frame en RelWithDebInfo):

| Parte | % del hilo | Funciones |
| --- | --- | --- |
| Código generado por el JIT | 11,9 | marcos sin símbolo (`?`) |
| Manejadores llamados desde el JIT | ~10 | `hFmac<…,1>` (con banderas) ~6, `hLq` 1,7, `hDiv` 1,4, `hSq` 0,6, `hIaddiu` 0,5, `hItof`, `hIlw`, `hOuter`, `hCondBranch`, `hFtoi` |
| Contabilidad por par | ~12 | `VuFastEngine::commit` 4,2, `jitStall` 6,5 (inclusivo), `execute` 1,7 |
| Desempaquetado | 4,2 | `vifUnpack` |
| Camino GIF → GS | ~8 | `kick`, `GS::writeRegisterPacked`, `buildDrawBatch`, `vertexKick`, `processGIFPacket`, `recordDrawDebugEventUnlocked`, bloqueos, `GifArbiter::submit`, `vector::_Resize_reallocate`, `operator new` |
| Copias | 2,5 | `memmove` desde `VU1Interpreter::execute`, `GSAsyncBackend::Submit`, `UploadImage`, `GifArbiter::submit` |

Objetivo: ≤ 12 ms por frame. Herramienta: `ttr_vu_bench` (`TwoTowersRecomp/debug/vu_bench.cpp`) con capturas de
`PS2X_VU_CAPTURE`; compara el resultado del motor rápido con el intérprete de referencia y mide. **Cada cambio
del JIT se valida ahí antes de probar en el juego.** Interruptores para aislar: `PS2X_VU_JIT=0`,
`PS2X_VU_JIT_INLINE=0`, `PS2X_VU_FLAGS_ALWAYS=1`, `PS2X_VU_STALL_SKIP=0`, `PS2X_VU_FAST=0`.

Cómo es hoy el JIT (`src/lib/vu/ps2_vu_fast.cpp`): `decode` rellena un `Pair` por par de instrucciones;
`rebuild` guarda las imágenes de microcódigo por hash FNV; `compileBlock` emite x64 con `ps2x::x64::Emitter`
(`rbx` = motor, `rsi` = estado de VU, `rdi` = `&vu.m_viBranchBackupValid`), bloques de hasta 256 pares que
acaban en un salto o en un par no compilable. Solo van en línea los FMAC sin banderas (`emitFmac`); **toda
instrucción inferior es una llamada a manejador**. Por cada par se emite: comprobación y registro de
disponibilidad (`readyFor`), copia de seguridad de VI para saltos, restauración de VF0, `vi[0] = 0` y
`add qword [cycle], 1`. `execute` entra en un bloque si `budgetEnd - cycle > block.pairs + 64`.

#### C1. Quitar el coste fijo por par

1. Leer `commit`, `jitStall`, `readyFor` y la parte de `compileBlock` que emite el prólogo de cada par.
2. `cycle`: sumar una vez por bloque (o por tramo entre puntos que lo lean) en vez de un `add` por par.
3. Disponibilidad (parones por dependencia): registrar solo las escrituras de los últimos `m_stallWindow` pares
   antes de cada salida del bloque; dentro del bloque los parones se conocen al compilar (el desplazamiento de
   ciclo es estático). `jitStall` solo debería llamarse en el primer tramo de un bloque.
4. Restaurar VF0 y `vi[0] = 0` solo después de pares que puedan escribirlos (se sabe al compilar).
5. `commit` (anillo de banderas, Q de FDIV, P de EFU): llamarlo solo cuando el bloque tenga algo pendiente
   (división en curso, banderas leídas más adelante), no en cada par.

Verificación: `ttr_vu_bench` sin diferencias; `[vu-stats]` igual número de pares; hilo VIF1 más bajo en perfil.

#### C2. Instrucciones inferiores en línea

Emitir código x64 directo, por orden de coste medido: `LQ`, `SQ`, `LQI`, `SQI`, `LQD`, `SQD` (dirección
`(vi + imm) * 16` con la máscara de memoria de VU1; cargar/guardar 128 bits respetando la máscara `dest` con
`blendps`), `IADDIU`, `IADDI`, `IADD`, `ISUB`, `ISUBIU`, `IAND`, `IOR`, `ILW`, `ISW`, `MOVE`, `MR32`, `MFIR`,
`MTIR`, `ITOF0/4/12/15`, `FTOI0/4/12/15`, saltos condicionales (`IBEQ`, `IBNE`, `IBGEZ`, …) y `DIV`/`SQRT`/
`RSQRT` (dejan Q pendiente con su latencia: respetar la cola). Para cada una, copiar la semántica exacta del
manejador `h…` correspondiente, incluida la copia de seguridad de VI que usan los saltos. Hacerlas de tres en
tres y pasar `ttr_vu_bench` cada vez.

#### C3. FMAC con banderas

`hFmac<K,S,ToAcc,1>` suma ~6 %. Mirar primero qué hace ya el motor para omitir banderas
(`PS2X_VU_FLAGS_ALWAYS`). Añadir análisis de vida dentro del bloque: las banderas MAC/estado que escribe un par
solo hay que calcularlas si las lee una instrucción (`FMAND`, `FMEQ`, `FMOR`, `FSAND`, `FSEQ`, `FSOR`, `FSSET`,
saltos que dependan de ellas) antes de que otro FMAC las pise, **o** si el bloque termina antes de que se pisen
(al salir del bloque, conservador: calcularlas). Las que sí hacen falta, emitirlas en línea con SSE
(`cmpps`/`movmskps` para cero y signo; desbordamiento según lo que haga el manejador). `CLIP` igual.

#### C4. `vifUnpack`

`src/lib/ps2_vif1_interpreter.cpp`. Hoy es genérico por elemento. Especializar por formato y modo:
`V4-32` sin máscara ni modo de suma = copia directa; `V3-32`, `V2-32`, `V4-16`, `V3-16`, `V2-16`, `V4-8`,
`V3-8`, `V4-5` con extensión de signo o cero mediante `pmovsx`/`pmovzx`; fila y máscara (`STMASK`, `STROW`,
`STCOL`, `STMOD`) solo en la ruta genérica. Elegir la rutina una vez por orden UNPACK, no por elemento.
Validar comparando la memoria de VU1 tras cada UNPACK con la ruta genérica (modo de comprobación activable por
variable de entorno durante el desarrollo).

#### C5. De XGKICK a la cola del GS sin copias

Cadena actual: JIT → `VuFastEngine::kick` (copia el paquete) → `VU1Interpreter::submitKick` →
`PS2Memory::submitGifPacket` → `GifArbiter::submit`/`drain` (reserva un `vector`) → `GS::processGIFPacket` →
`GS::writeRegisterPacked` → `GS::vertexKick` → `GS::buildDrawBatch` → `GSAsyncBackend::Submit`.

1. Si el árbitro GIF está libre (caso normal de PATH1), analizar el paquete directamente desde la memoria de
   VU1 (cuidando la vuelta al final de los 16 KiB), sin copiarlo ni pasar por `vector`.
2. En `processGIFPacket`, ruta rápida para etiquetas PACKED con las firmas de registros habituales (sacarlas
   con un contador temporal: típicamente `ST, RGBAQ, XYZF2` y `RGBAQ, XYZF2`): bucle específico que rellena
   vértices sin pasar por `writeRegisterPacked` registro a registro.
3. `buildDrawBatch`: no copiar `GSDrawState` por primitiva (enlaza con B7.1).
4. Fuera del camino por vértice: `recordDrawDebugEventUnlocked`, bloqueos y consultas al montón.

Verificación: capturas del GS (A1) antes y después tienen que ser idénticas byte a byte en una escena
determinista (menú o primer frame de batalla con `PS2X_REALTIME=0`), y la imagen igual en el juego.

### Fase D — Hilo EE

Medido: 21 % ocupado ≈ 26 ms por frame en RelWithDebInfo (≈ 20 ms en Release). Reparto: código del juego
11,7 % inclusivo (`PS2Runtime::dispatchGuestBranch`, incluye `executeVU0Microprogram` 2,4 %); `memmove` 5,8 %;
emulación IOP 3,5 % (constante en tiempo real, no crece con los fps); `QueueDmaChain` 3,0 %. Objetivo ≤ 10 ms.

#### D1. Búferes DMA reutilizados

`Vif1Worker::enqueue(std::vector<uint8_t> &&data)` (`src/lib/ps2_vif1_worker.cpp:101`) recibe un vector nuevo
por transferencia, que el hilo EE llena con `insert` (`_Insert_counted_range`, 2,5-3,3 %) y que luego se libera
(`NtFreeVirtualMemory`, 0,7 %: son bloques grandes). Buscar quién construye ese vector (`PS2Memory::
processVIF1Data` / `processPendingTransfers`) y:
1. Reserva de búferes: el hilo VIF1 devuelve los vectores consumidos a una lista libre (conservan su
   capacidad) y el hilo EE los toma de ahí. Tamaño de la reserva: `m_maxPending + 2`.
2. Reservar de una vez el tamaño de la cadena DMA en lugar de crecer con `insert`.
3. Si la cadena es contigua en RAM de invitado, valorar pasar puntero + tamaño; solo es seguro si el juego no
   reescribe ese búfer antes de que VIF1 lo consuma (el juego alterna búferes: comprobarlo antes).

#### D2. Ejecuciones de VU0 sin copiar estado

`PS2Runtime::executeVU0Microprogram` (`src/lib/ps2_runtime.cpp:~1720`) y la lambda de la línea ~966 hacen, en
cada una de las ~1 850 ejecuciones por frame: `m_vu0.reset()` + `copyVu0ContextToState` + `execute` +
`copyVu0StateToContext`. En el perfil: `memmove` desde `VU1Interpreter::reset` (1,0 %) y desde `execute`
(1,2 %).
1. Mirar qué copia `reset()` (`src/lib/vu/ps2_vu1_core.cpp:103`): si reinicia colas y arrays grandes, separar
   un `resetForRun()` que solo ponga a cero lo que una ejecución anterior haya podido dejar sucio.
2. Mirar el `memmove` de `execute`: si copia código o datos de VU0 a un búfer interno en cada llamada, usar
   punteros a la memoria de VU0 y revalidar solo si cambió (hash o bandera de escritura).
3. Las copias de registros (32 VF + 16 VI + ACC, Q, banderas) son pequeñas; dejarlas salvo que sigan saliendo.

#### D3. Volver a medir en Release

Con las fases B y C hechas, perfilar el hilo EE en la batalla. Si pasa de 10 ms por frame, mirar por tiempo
inclusivo las funciones del juego que encabezaban la lista (`Update__11CYoGameData`, `RenderShadowVolume`,
`ComputeSkeletalHierarchy`, `draw__13ClDisplayList`) y el coste de `dispatchGuestBranch` (llamadas indirectas
con búsqueda): caché de la última función por punto de llamada. No reescribir funciones del juego a mano
salvo que una sola supere el 15 % del frame.

### Fase E — Pasar el límite del juego de 50 a 60

**Cómo limita el juego los frames** (leído del desensamblado, `TwoTowersRecompLib/generated/`):

- El bucle principal deja cada frame terminado como una cadena DMA en una cola de 4
  (`QueueDmaChain__18ClVBlankIntHandlerFP1i_0x1064e0`; tablas en `0x38DB10` y `0x38DB14`, 8 bytes por entrada).
- `VBlankInterruptHandler__Fi_0x106290` se ejecuta en cada VBlank: incrementa el contador total (`gp-0x7BB8`)
  y el de VBlanks desde el último lanzamiento (`gp-0x7BB4`); si hay cadenas en cola (`gp-0x7BC0`) y los dos
  canales están libres (objetos en `gp-0x6F00` y `gp-0x6EFC`, campo `+8` igual a `+0x10`), lanza **una** cadena
  por VIF1 (`D1_QWC = 0`, `D1_TADR`, `D1_CHCR = 0x105`) y pone `gp-0x7BB4` a 0.
- Si el byte `gp-0x7BAC` está a 1 (`ClVBlankIntHandler::CapTo30FPS`, `0x106670`, al que salta
  `ClDisplay::CapTo30FPS`, `0x17cb30`), solo lanza si han pasado al menos 2 VBlanks. No se han encontrado
  llamadas directas a `CapTo30FPS` en el código generado.
- `WaitForVSync__9ClDisplayFv_0x17cb40` espera a que cambie el bit 13 (FIELD) de `GS_CSR` (`0x12001000`).
- El tiempo de juego no depende del VBlank: `tick__7ClClockFv_0x1cf7c0` lee y pone a cero `T0_COUNT`
  (temporizador 0 con reloj HBLANK, modo `0x283`) y divide por 15 600.

Es decir: **como mucho un frame por VBlank**. En PAL son 50. Los menús ya van a 49,5.

En el runtime: `EeScheduler::setVBlankRate(hz)` (`src/lib/Kernel/EeScheduler.cpp:496`) fija el periodo y llama a
`PS2Memory::setEeTimerHBlankRate(hz)` (`src/lib/ps2_memory.cpp:424`: 15 625 Hz si son 50, 15 734 si no). Lo
llaman `Stubs/GS.cpp:807` y `Syscalls/System.cpp:16` con 50 porque el juego pide modo PAL. La ventana ya se
presenta a 60 (`SetTargetFPS(60)` en `ps2_runtime.cpp:1004`).

#### E1. VBlank a 60 Hz con el reloj HBLANK en tiempo real

**Depende de:** batalla por encima de ~50 fps (fases B, C y D).

1. Separar las dos frecuencias en `setVBlankRate`: la del VBlank y la del temporizador HBLANK. Añadir un ajuste
   del juego (en `TwoTowersRecomp`, por ejemplo `TTR_VBLANK_HZ`, valor por defecto 60 cuando esté verificado)
   que fuerce el VBlank a 60 Hz **manteniendo el HBLANK en 15 625 Hz**. Así `ClClock::tick` sigue midiendo
   tiempo real y la velocidad del juego no cambia.
2. Comprobar antes de nada el estado de la bandera de 30 fps durante la batalla: añadir una traza temporal en
   `TwoTowersRecomp/game/sles_51256_trace.cpp` que lea el byte `gp - 0x7BAC` (el valor de `gp` está en el
   registro 28 del contexto). Si está a 1 en la partida, el juego se limita solo a 25: en ese caso hacer que el
   override de `CapTo30FPS__18ClVBlankIntHandlerFb_0x106670` guarde siempre 0, con un interruptor
   `TTR_ALLOW_30FPS_CAP=1` para volver al comportamiento original.
3. Probar por partes, comparando con 50 Hz:
   - Velocidad: cronometrar algo de duración fija (una cinemática con motor, una animación en bucle) a 50 y a
     60 Hz. Tiene que durar lo mismo en segundos.
   - Menús y logos: mismas duraciones.
   - FMV: los vídeos son de 25 fps. Si el reproductor avanza por número de VBlanks, a 60 Hz irían un 20 % más
     rápido y se desincronizarían del audio. En ese caso, volver a 50 Hz mientras hay un FMV en marcha (el
     código de `TTR_HACK_SKIPPABLE_FMV` en `game/sles_51256_overrides.cpp` ya sabe cuándo hay uno) y a 60 al
     acabar.
   - Audio: sin cortes ni cambios de tono durante 10 minutos de juego (la música va por flujo desde el IOP).
   - Vigilante del manejador: si pasan 60 VBlanks sin lanzar una cadena llama a `func_105A80`; a 60 Hz salta
     tras 1 s en vez de 1,2 s. Comprobar que no se dispara en las cargas.
4. Buscar otros usos del contador de VBlanks como tiempo:
   `GetNumVBlanksSinceStart__18ClVBlankIntHandlerFv_0x106680` — listar quién la llama en `generated/` y revisar
   si alguno mide duraciones (temporizadores de menú, parpadeos, repetición de teclas).

**Hecho cuando:** `fps=` ≈ 60 en menús y en la batalla, con la misma velocidad de juego que a 50 Hz.

#### E2. Latencia y regularidad

Con todo a 60: (1) latencia de entrada: hay hasta 4 frames en la cola del juego, 2 transferencias pendientes
en VIF1 (`PS2X_VIF1_QUEUE`) y la cola del GS; medir con el volcado de frames cuántos VBlanks pasan entre una
pulsación del guion de mando y el primer frame que la refleja, y bajar `PS2X_VIF1_QUEUE` a 1 si no cuesta fps;
(2) regularidad: mirar la desviación de `fps=` por tramo; si hay tirones periódicos, buscar esperas largas en
el perfil (carga de datos, reservas de memoria); (3) comprobar que con el realtime de `catchUpToHostClock` el
juego recupera tras un tirón sin acelerarse.

### Fase F — Cierre

#### F1. Verificación final y documentación

1. Jugar el prólogo entero a mano y con el guion: sin cuelgues, imagen correcta, audio correcto.
2. `tools/perf/run_battle.sh <carpeta> 600`: `mem.log` estable (sin crecimiento sostenido) y
   `tools\purple_scan.ps1` con 0 frames afectados.
3. Banco del GS: hashes iguales a la referencia en las dos capturas.
4. Exportar el último parche, comprobar `apply_patches.ps1` desde un submódulo limpio, actualizar
   `tools/patches/README.md`.
5. Actualizar `docs/STATUS.md` (historial de rendimiento, reparto por hilo, lista "Siguiente") y
   `docs/DEBUGGING.md` (variables nuevas: `PS2X_GS_CAPTURE`, `PS2X_GS_FAST_MASK`, `TTR_VBLANK_HZ`,
   `TTR_ALLOW_30FPS_CAP`, y `ttr_gs_bench`).
6. Quitar las trazas temporales.

## 6. Estado

Actualizar al terminar cada tarea. "Medida" = fps medios en la batalla (VBlank 3400-6900, Release) y, en la
fase B, ms por frame del banco con 9 hilos.

| Tarea | Estado | Medida | Parche |
| --- | --- | --- | --- |
| Medición y diseño | hecho (2026-11-09) | batalla ~10 fps | — |
| A0 Punto de partida limpio | hecho (2026-11-11) | commit `52587f0` | — |
| A1 Captura del GS | hecho (2026-11-11) | `battle4000.gscap`: 100 presentaciones, 99 frames, 8,3 M primitivas | `0014` |
| A2 `ttr_gs_bench` | hecho (2026-11-11) | línea base: 173 ms/frame con 9 hilos, 670 con 1 | `0014` |
| B1 Tablas fila + columna | hecho (2026-11-11) | 556 M direcciones iguales; T8/T4 con periodo de 8 filas | `0014` |
| B2 Instantáneas de textura | hecho con otra forma (ver 6.1) | 141 ms/frame; 13,5 fps en la batalla | `0014` |
| B3 Reglas de sincronización | hecho en parte (ver 6.1) | 124 ms/frame; 0 frames con bloques morados | `0014` |
| B4 Preparación única | pendiente | | |
| B5 Bucles especializados | en parte (ver 6.1) | 1 hilo: 670 → 510 ms/frame | `0014`, `0015` |
| B6 AVX2 | empezado (ver 6.1) | 1 hilo: 527 → 413 ms/frame; 9 hilos ~101 ms/frame; batalla 15,4 fps | `0016` |
| B7 Camino hasta la cola | pendiente | | |
| B8 Sprites con realimentación | solo si hace falta | | |
| C1 Coste fijo por par | pendiente | | |
| C2 Inferiores en línea | pendiente | | |
| C3 FMAC con banderas | pendiente | | |
| C4 `vifUnpack` | pendiente | | |
| C5 XGKICK sin copias | pendiente | | |
| D1 Búferes DMA | pendiente | | |
| D2 VU0 sin copias | pendiente | | |
| D3 Medir EE en Release | pendiente | | |
| E1 VBlank a 60 Hz | pendiente | | |
| E2 Latencia y regularidad | pendiente | | |
| F1 Cierre | pendiente | | |

### 6.1 Notas de lo hecho (2026-11-11)

Estado del banco (`battle4000.gscap`, Release, 9 hilos): **173 → 122-124 ms por frame de juego**; con un hilo de
raster, 670 → 510 ms. En el juego, la batalla pasa de ~10 a ~13,5 fps y desaparecen los bloques morados. Lo
que se hizo distinto de lo previsto y por qué:

- **B2 sin texturas lineales.** Convertir cada textura a formato lineal costaba demasiado en el hilo del GS
  (~7 MB de subidas por frame). En su lugar, cada página de 8 KiB que se muestrea se copia tal cual (sin
  desentrelazar) una vez por versión, y los hilos de raster leen las texturas a través de un mapa de 512 punteros
  por lote (`TexturePageCache::SetPageMap`). Las subidas se escriben en la VRAM al momento en el hilo del GS y
  solo esperan si un lote en vuelo usa esas páginas como framebuffer o Z.
- **Semántica de caché de texturas.** Una página mantiene la copia que se tomó en su primera lectura tras el
  último `TEXFLUSH`, aunque luego se dibuje en ella (es lo que hace la ruta de un hilo y la caché real del GS). Sin
  esto, ~1 700 sprites por frame que escriben el canal alfa de una página leyéndola como T8 se serializaban.
- **Copias diferidas.** Si una página que hay que copiar aún se está dibujando, el hilo GS no espera: el hilo de
  raster 0 la copia antes del siguiente lote, cuando acaban los lotes que la dibujan.
- **Presentación.** Solo espera las páginas que puede mostrar (ambos circuitos, la fuente preferida y los
  framebuffers de contexto) y copia esas, en vez de vaciar todo el pipeline 50 veces por segundo.
- **Carrera de los bloques morados (resuelta).** La tubería de píxel prueba Z con ZTST 2/3 aunque `ZTE` sea 0, y
  esas lecturas de Z no se contaban: un hilo podía leer Z mientras otro lo escribía con otra disposición.
- **Barreras por disposición.** Dos primitivas que usan la misma página con otra disposición (base, ancho o
  familia de formato: CT32, CT16, CT16S, Z32, Z16, Z16S) no pueden dibujarse a la vez por franjas; la segunda
  espera a la primera. Quedan ~1 000 por segundo en el banco (pasadas de posproceso con una vista CT16 del
  framebuffer de 32 bits) y se comen ~1,1 s por segundo de los hilos de raster.
- **B5 parcial.** Direcciones de framebuffer, Z y texturas por tablas (`GSSwizzle::AddressFast`), paletas
  resueltas una vez por versión de CLUT y por hilo, coordenada U de los sprites por columna, muestreo
  especializado por formato de texel (`DrawSpriteK`/`DrawTriangleK`). Cada cambio se comprobó con el banco.
- **B6 empezado.** La aritmética por píxel de los triángulos (pesos, Z en double, color, U/V o S/T/Q, niebla) se
  hace de 8 en 8 con AVX (ruta escalar si el compilador no tiene AVX); el resto de la tubería sigue por píxel.
  Un hilo: 527 → 484 ms por frame, idéntico a la referencia.
- **Z primero.** Un píxel que falla la prueba Z no escribe nada (todas las pruebas solo descartan), así que se
  prueba antes de muestrear la textura y de leer el framebuffer: 484 → 439 ms. Sprites con UV sin filtro: U ya
  envuelta por columna y parte de fila de la dirección del texel por fila: 439 → 413 ms. Parche `0016`.

**Resultado al cerrar el 2026-11-11:** banco 173 → ~101 ms por frame de juego con 9 hilos (413 con uno), idéntico
bit a bit a la referencia; **batalla ~10 → 15,4 fps** (`0016`). En el juego el límite ya no es el GS: el hilo de
VIF1 está al 99 % con VU1 al 88 % (`PS2X_VIF1_STATS=1`).

**Lo que dicen las medidas sobre el objetivo (importante para decidir):**

- El raster no escala con los hilos en esta CPU (6 núcleos, 12 hilos): 3 hilos ×2,5, 5 hilos ×3,3, 7 ×3,3,
  9 ×3,7 respecto a uno. Además el EE, VIF1/VU1 y el hilo del GS necesitan sus propios núcleos.
- Presupuesto a 60 fps: 6 núcleos × 16,7 ms ≈ 100 ms de CPU por frame. Si el EE se lleva ~10, VIF1 ~12 y el hilo
  del GS ~8, para el raster quedan ~70 ms por frame contando el escalado real, es decir, el coste de un hilo tiene
  que bajar de 413 a ~70-100 ms: **4-6 veces más rápido por píxel**. La batalla escribe ~16 M de píxeles por
  frame (el GS de PS2 tiene fuerza bruta para eso); 70 ms son ~4,4 ns por píxel, el terreno de un rasterizador
  con AVX2 en todo (lecturas y escrituras con *gather*, mezcla en vectores), como el de PCSX2.
- Quitar las barreras por disposición solo gana ~1 % (medido desactivándolas): se descarta el reparto por páginas.
- Las optimizaciones escalares ya dan poco. Lo que queda en el GS es la tubería por píxel en AVX2 (fichero
  aparte con `/arch:AVX2` y elección por `cpuid`, ver 3.1), y es mucho trabajo. **Alternativa a valorar con el
  usuario:** un renderizador por GPU que dibuje lo mismo que el GS (a la resolución nativa, sin mejoras); es otro
  proyecto, pero es la forma habitual de llegar a 60 fps con este volumen de píxeles.

**Siguiente recomendado:** fase C (VU1/VIF1), que es ahora el límite del juego; después volver a B6 (tubería por
píxel en AVX2) o a la decisión sobre la GPU.

Cómo comprobar un cambio del GS ahora:

```bash
cd out/gs_captures
../build/win-clang-release/TwoTowersRecomp/ttr_gs_bench.exe battle4000.gscap --check-hashes ref2.hashes
```

Sin líneas `differs` = idéntico a la referencia (VRAM e imagen presentada en las 100 presentaciones). `ref2.hashes`
se generó con `PS2X_GS_THREADS=0 ... --hashes ref2.hashes`; si la captura se rehace, hay que regenerarlo igual
(la ruta de un hilo es la referencia determinista). Con 9 hilos, repetir la comprobación 2-3 veces: las
carreras no salen siempre.

**Siguiente en el GS, por orden de ganancia esperada:**

1. **AVX2 (B6)** para triángulos y sprites: los hilos de raster son ahora el cuello de botella del juego
   (70-80 % ocupados). Perfil de un hilo: `writePixelFast` ~23 %, aritmética del triángulo ~22 %, lectura de
   texels ~18-20 %, sprite ~12 %. Las optimizaciones escalares ya no dan mucho (especializar por formato no
   ganó nada): el coste está en la aritmética y en los accesos a memoria.
2. **Quitar las barreras por disposición**: reparto de trabajo por página de memoria (hilo = página % hilos)
   en lugar de por franja de filas, de modo que dos disposiciones de la misma memoria caigan en el mismo hilo;
   solo haría falta barrera cuando dos primitivas en vuelo comparten Z con distinta relación framebuffer→Z o una
   página se usa a la vez como framebuffer y como Z.
3. **Preparación duplicada (B4)**: con 9 hilos la CPU total es ~1,8 veces la de un hilo (cada hilo repite
   `BuildPixelPipe`, `BuildSampler`, la paleta y la preparación del triángulo o de las columnas del sprite).

## 7. Riesgos y avisos

- **Es mucho trabajo y no está garantizado llegar a 60 en todos los planos con este procesador.** El raster
  necesita ~5x y los planos con humo doblan los píxeles. Si tras B6 el banco no baja de 16 ms en la captura de
  humo, las opciones son: más variantes vectorizadas, o saltar trabajo invisible (por ejemplo descartar antes
  los píxeles que fallan la prueba Z, que no cambia la imagen).
- No intentar las fases C-E para "ver ya los 60" antes de la B: el hilo VIF1 espera al GS casi la mitad del
  tiempo y no se notaría.
- Cada vez que una tarea cambie la imagen respecto a la referencia, parar y encontrar la causa. Las diferencias
  de 1 bit en un canal suelen ser un redondeo distinto (orden de operaciones, FMA, truncar frente a redondear).
- La carrera de los bloques morados es de la cola de bandas actual; debe desaparecer con B2/B3. Si sigue, hay
  dos hilos escribiendo la misma banda o una superficie con otra disposición sin barrera (tabla de B3).
- No tocar el reloj de tiempo real (`catchUpToHostClock`, parche 0013) al hacer E1: las llamadas RPC diferidas
  no deben saltar en el tiempo.
