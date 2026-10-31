# Estado

[Inicio](../README.md) · [Compilación](BUILDING.md) · [Depuración](DEBUGGING.md)

Fase: **en progreso**. Arranca, muestra la pantalla legal y la de carga, carga el primer nivel y llega
al primer vídeo FMV. Todavía no es jugable.

## Progreso del arranque

| Hito | Estado |
| --- | --- |
| Banner del juego (`v072 PAL`, `Sep 20 2002`) e inicialización de GS/DMA | ✅ |
| IRX del disco ejecutados en `ps2xIOP` (`LIBSD`, `FUFILECD`, `SDRDRV`, `PADMAN`, `MCMAN`, `FUSTREAM`...) | ✅ |
| Pantalla legal en español (`ClGameModeStartup`) | ✅ |
| Pantalla "CARGANDO..." (`ClGameModePlay`) | ✅ |
| Banco de efectos del nivel subido a la RAM del SPU2 (`00ISUL1.MSB`) | ✅ |
| Streams de narrador/música (EA MultiStream) y carga del nivel (`ClEnemyDB::load`) | ✅ |
| Vídeo FMV (`ClFMV::blitMpegToGSBackBuffer`) | ❌ el hilo espera en `wait/mpeg` |
| Menú principal, mando, partida | pendiente |

## Problemas resueltos

### Puntero nulo en `ClGame::tick()` (mapa de funciones)

El mapa de Ghidra incluye 33 806 etiquetas ejecutables dentro de funciones. PS2Recomp convertía cada una
en un fragmento que acaba en la siguiente etiqueta, así que `entry_001002e8` contenía el `jal ClGame::tick`
**sin su delay slot** (`addiu a0, a0, 0x2510`): `tick` recibía `this = 0x380000` en vez de `0x382510`.
`recomp_codegen.py` construye ahora el mapa con los símbolos del ELF (+ crt0 de Ghidra), descarta las
etiquetas internas y nunca corta una función entre un salto y su delay slot. 4719 funciones, 0 errores.

### Carga infinita (EA MultiStream / SIF RPC)

El sonido de EA hace un "ping-pong" EE↔IOP: cada respuesta del IOP (`FUSTREAM.IRX`, RPC `0x12345`) dispara
el siguiente `SOUND_GetStatus` en `ClSound::tick`, que lee la secuencia de la respuesta **después** de
`FlushIOPCommand`. El runtime completaba los `sceSifCallRpc` sin espera de forma síncrona, así que la
secuencia ya era la nueva y el ping-pong se detenía; el IOP quedaba esperando la confirmación de carga del
stream actual (`0x151B0`) para siempre. Parche `0002`: la función de fin de un RPC sin espera se ejecuta
~100 µs después. Diagnóstico completo en [notas/ea-sound.md](notes/ea-sound.md).

### SPU2

`ps2xIOP` no emulaba el SPU2 (era un almacén de registros). Parche `0001`: RAM de sonido de 2 MiB, DMA
real, voces ADPCM con bucles, `NAX`/`ENDX`/`ENVX`, ADSR e IRQ. La mezcla se genera pero todavía **no se
envía al host** (sin sonido).

## Siguiente

1. FMV: el reproductor del juego usa `sceMpeg*` y espera imágenes que el HLE MPEG del runtime no entrega
   (`wait/mpeg`). Revisar el formato de los `.FMV` (PSS) y la ruta IPU/FFmpeg del runtime.
2. Salida de audio: llevar la mezcla del SPU2 al backend de audio del runtime (raylib).
3. VBlank a 50 Hz: el runtime programa VBlanks NTSC (60 Hz) aunque el juego es PAL (`VMODE = PAL`).
4. Rendimiento: unos 15 VBlanks/s durante la carga en `RelWithDebInfo` (el resto del tiempo, tiempo real).
5. Confirmar qué hay en `0x00303030–0x0037Bxxx` (¿microcódigo VU1?) y si el runtime lo necesita.

## Observaciones

- ~1000 avisos de `JR`/`JALR` sin resolver (vtables de C++ y jump tables); el runtime los resuelve por
  búsqueda en tiempo de ejecución.
- 29 parches de instrucción propuestos por el analizador como "posible código automodificable"; están
  aplicados (sustituidos por `nop`) pero sin revisar.
- El juego lleva un intérprete **Lua** embebido (`luaV_execute`, `lua_State`) y clases `CYo*` / `Cl*`.
