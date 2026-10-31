# Parches sobre PS2Recomp

`tools/PS2Recomp` se mantiene en el commit de upstream; los cambios que necesita este juego viven aquí
como parches y se aplican con `tools/apply_patches.ps1` (los scripts de compilación lo llaman solos).
Forman una pila: cada parche se aplica sobre los anteriores y puede tocar los mismos ficheros.

| Parche | Qué hace |
| --- | --- |
| `0001-ps2xIOP-spu2-core-and-iop-tracing.patch` | Núcleo SPU2 para el IOP: RAM de sonido de 2 MiB, DMA real por TSA (canales 4/7), 48 voces ADPCM con banderas de bucle, `NAX`/`ENDX`/`ENVX`, ADSR, IRQ por dirección y mezcla estéreo. Accesos de 16 bits a sus registros. Trazas opcionales del IOP (`PS2X_IOP_TRACE`, `PS2X_IOP_CALLTRACE`) y lista de hilos en `iopDebugSnapshot()`. |
| `0002-ps2xRuntime-defer-nowait-rpc-completion.patch` | Un `sceSifCallRpc` sin espera devuelve primero y su función de fin se ejecuta ~100 µs después, como en el hardware. Antes se completaba de forma síncrona y el ping-pong de estado del sonido de EA (`ClSound::tick`) se detenía, dejando la carga colgada. |
| `0003-ps2xRuntime-fmv-guest-memory-vu0-micro.patch` | Vídeo: modelo `sceMpegCbNodata` de libmpeg (el decodificador pide datos al callback del juego; si devuelve 0, el vídeo termina y `sceMpegIsEnd` lo indica). Memoria: `PS2Runtime::setRuntimeGuestRegion()` mueve los descriptores RPC/TLS y las pilas de callbacks que el runtime guarda en la RAM del invitado. VIF0: `MSCAL`/`MSCALF`/`MSCNT` ejecutan el microprograma de VU0 y `UNPACK` comparte la implementación completa de VIF1 (formatos, máscaras, modos). Ayudas de depuración: `PS2X_WATCH` y `PS2X_PROFILE_CALLS`. |

## Añadir un parche

Edita el submódulo, compila con `tools\build_runtime.ps1 -SkipPatches` mientras iteras y, cuando esté
listo, exporta lo que todavía no cubren los parches existentes:

```powershell
powershell -ExecutionPolicy Bypass -File tools\export_patch.ps1 -Name 0004-ps2xRuntime-descripcion
```

El script aplica la pila a una copia limpia del commit fijado y guarda la diferencia con tu árbol.
