# Parches sobre PS2Recomp

`tools/PS2Recomp` se mantiene en el commit de upstream; los cambios que necesita este juego viven aquí
como parches y se aplican con `tools/apply_patches.ps1` (los scripts de compilación lo llaman solos).

| Parche | Qué hace |
| --- | --- |
| `0001-ps2xIOP-spu2-core-and-iop-tracing.patch` | Núcleo SPU2 para el IOP: RAM de sonido de 2 MiB, DMA real por TSA (canales 4/7), 48 voces ADPCM con banderas de bucle, `NAX`/`ENDX`/`ENVX`, ADSR, IRQ por dirección y mezcla estéreo. Accesos de 16 bits a sus registros. Trazas opcionales del IOP (`PS2X_IOP_TRACE`, `PS2X_IOP_CALLTRACE`) y lista de hilos en `iopDebugSnapshot()`. |
| `0002-ps2xRuntime-defer-nowait-rpc-completion.patch` | Un `sceSifCallRpc` sin espera devuelve primero y su función de fin se ejecuta ~100 µs después, como en el hardware. Antes se completaba de forma síncrona y el ping-pong de estado del sonido de EA (`ClSound::tick`) se detenía, dejando la carga colgada. |

Para regenerarlos tras editar el submódulo:

```powershell
cd tools\PS2Recomp
git add -N <ficheros nuevos>
git diff -- ps2xIOP > ..\patches\PS2Recomp\0001-ps2xIOP-spu2-core-and-iop-tracing.patch
git diff -- ps2xRuntime > ..\patches\PS2Recomp\0002-ps2xRuntime-defer-nowait-rpc-completion.patch
git reset
```
