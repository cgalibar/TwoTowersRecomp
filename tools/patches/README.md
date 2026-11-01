# Parches sobre PS2Recomp

`tools/PS2Recomp` se mantiene en el commit de upstream; los cambios que necesita este juego viven aquí
como parches y se aplican con `tools/apply_patches.ps1` (los scripts de compilación lo llaman solos).
Forman una pila: cada parche se aplica sobre los anteriores y puede tocar los mismos ficheros.

| Parche | Qué hace |
| --- | --- |
| `0001-ps2xIOP-spu2-core-and-iop-tracing.patch` | Núcleo SPU2 para el IOP: RAM de sonido de 2 MiB, DMA real por TSA (canales 4/7), 48 voces ADPCM con banderas de bucle, `NAX`/`ENDX`/`ENVX`, ADSR, IRQ por dirección y mezcla estéreo. Accesos de 16 bits a sus registros. Trazas opcionales del IOP (`PS2X_IOP_TRACE`, `PS2X_IOP_CALLTRACE`) y lista de hilos en `iopDebugSnapshot()`. |
| `0002-ps2xRuntime-defer-nowait-rpc-completion.patch` | Un `sceSifCallRpc` sin espera devuelve primero y su función de fin se ejecuta ~100 µs después, como en el hardware. Antes se completaba de forma síncrona y el ping-pong de estado del sonido de EA (`ClSound::tick`) se detenía, dejando la carga colgada. |
| `0003-ps2xRuntime-fmv-guest-memory-vu0-micro.patch` | Vídeo: modelo `sceMpegCbNodata` de libmpeg (el decodificador pide datos al callback del juego; si devuelve 0, el vídeo termina y `sceMpegIsEnd` lo indica). Memoria: `PS2Runtime::setRuntimeGuestRegion()` mueve los descriptores RPC/TLS y las pilas de callbacks que el runtime guarda en la RAM del invitado. VIF0: `MSCAL`/`MSCALF`/`MSCNT` ejecutan el microprograma de VU0 y `UNPACK` comparte la implementación completa de VIF1 (formatos, máscaras, modos). Ayudas de depuración: `PS2X_WATCH` y `PS2X_PROFILE_CALLS`. |
| `0004-ps2xRecomp-vu0-mac-flags-ee-sqrt-pad-sticks.patch` | Recompilador: las instrucciones FMAC de VU0 en modo macro (ADD/SUB/MUL/MADD/MSUB, variantes ACC, OPMULA/OPMSUB) actualizan los flags MAC/STATUS y descartan las escrituras a `vf0`; `SQRT.S` toma el operando del campo ft, como en el EE. Runtime: en el teclado, WASD e IJKL mueven los sticks analógicos y las flechas la cruceta. Hace falta regenerar el código (`recomp_codegen.py generate`) tras aplicarlo. |
| `0005-ps2xRuntime-iop-batching-parallel-gs-raster.patch` | Rendimiento. Los temporizadores del EE y el IOP se ponen al día con el reloj del EE en lotes de 4096 ciclos, no en cada cobro de 8-32 ciclos del código generado (que ejecutaba el planificador del IOP para 1-4 ciclos). El rasterizador del GS reparte los sprites y triángulos de más de 4096 píxeles en franjas de filas entre varios hilos (`PS2X_GS_THREADS`, por defecto la mitad de los hilos de la CPU menos uno); cada píxel lo sigue escribiendo un solo hilo, en el orden de las primitivas. `ReleaseMode.cmake`: con clang-cl no pasa `/Qspectre-` (no existe) ni `/fp:fast` (sería `-ffast-math`, que elimina las comprobaciones de NaN/Inf), y la arquitectura de la build Release sale de `PS2X_RELEASE_ARCH`. |
| `0006-ps2xRuntime-async-gs-thread.patch` | `GSAsyncBackend`: el rasterizador del GS corre en su propio hilo. Las primitivas, cargas de CLUT, transferencias, subidas de imagen y `TEXFLUSH` se encolan en orden y vuelven al momento; lo que devuelve datos o necesita la VRAM dibujada (`Present`, lecturas y escrituras de VRAM, transferencias GS→EE, `Sync` salvo `FINISH`) espera antes a que el hilo vacíe la cola. `PS2X_GS_ASYNC=0` lo desactiva. Los hilos de rasterizado pasan a ser los de la CPU menos 3. |
| `0007-ps2xRuntime-pal-vblank-rate.patch` | El VBlank sigue el modo de salida que el juego pide a `sceGsResetGraph` o `SetGsCrt`: 50 Hz en PAL (modo 3), 60 Hz en NTSC (modo 2). Antes era siempre 60 Hz y un juego PAL iba un 20 % rápido; en este, el vídeo producía audio más deprisa de lo que el IOP lo consumía y se cortaba a los ~14 s. |
| `0008-vu0-memory-ops-vu-budget-dma-chain-gs-spans.patch` | Recompilador: `VSQI/VSQD/VLQI/VLQD/VILWR/VISWR` de VU0 en modo macro usaban los campos equivocados y la RAM del EE en vez de la memoria de VU0 (hacían invisibles el mundo y los personajes); `VDIV/VRSQRT/VSQRT` con semántica correcta. Runtime: los microprogramas de VU0/VU1 corren hasta su bit E (límite 4M ciclos), `JR/JALR` de VU leen el registro actual, cadenas DMA de hasta 1M etiquetas, salida rápida del commit de pipelines de VU1, tramos por fila en triángulos del GS y cachés de texturas de los hilos de rasterizado por época. |

## Añadir un parche

Edita el submódulo, compila con `tools\build_runtime.ps1 -SkipPatches` mientras iteras y, cuando esté
listo, exporta lo que todavía no cubren los parches existentes:

```powershell
powershell -ExecutionPolicy Bypass -File tools\export_patch.ps1 -Name 0004-ps2xRuntime-descripcion
```

El script aplica la pila a una copia limpia del commit fijado y guarda la diferencia con tu árbol.
