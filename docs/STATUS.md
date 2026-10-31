# Estado

[Inicio](../README.md) · [Compilación](BUILDING.md) · [Depuración](DEBUGGING.md)

Fase: **en progreso**. Arranca, carga el prólogo, reproduce el vídeo de introducción y entra en el
nivel: la simulación corre y se dibujan el cielo y el HUD, pero todavía no el mundo 3D. No es jugable.

## Progreso del arranque

| Hito | Estado |
| --- | --- |
| Banner del juego (`v072 PAL`, `Sep 20 2002`) e inicialización de GS/DMA | ✅ |
| IRX del disco ejecutados en `ps2xIOP` (`LIBSD`, `FUFILECD`, `SDRDRV`, `PADMAN`, `MCMAN`, `FUSTREAM`...) | ✅ |
| Pantalla legal en español (`ClGameModeStartup`) | ✅ |
| Pantalla "CARGANDO..." (`ClGameModePlay`) | ✅ |
| Banco de efectos del nivel subido a la RAM del SPU2 (`00ISUL1.MSB`, el prólogo de Isildur) | ✅ |
| Streams de narrador/música (EA MultiStream) y carga del nivel (`ClEnemyDB::load`) | ✅ |
| Vídeo de introducción `MONT.FMV` (`ClFMV::playModal`) | ✅ imagen; ⚠️ se corta a los ~14 s porque su audio no se consume |
| Nivel: actores, colisiones, partículas, guiones; microprogramas VU0 por VIF0 | ✅ |
| Nivel: cielo y HUD (espada, "Defender", luna) | ✅ |
| Nivel: terreno y personajes | ❌ no se dibujan |
| Mando, menú principal, sonido | pendiente |

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

### Vídeos FMV (`sceMpegCbNodata`)

Los `.FMV` de EA son contenedores propios (`FHDR` + bloques `MPCh` de vídeo MPEG-2 y `VBLK` de audio),
no PSS. `ClFMV` no llama a `sceMpegAddBs` por su cuenta: registra un callback "sin datos" que libmpeg
invoca cuando necesita más bitstream, y que devuelve 0 al acabarse el vídeo. El HLE del runtime solo
conocía el modelo de stream de CD. Parche `0003`: `sceMpegGetPicture` invoca ese callback y, cuando
devuelve 0, vacía el decodificador y `sceMpegIsEnd` termina el vídeo.

### Globales del juego machacadas a mitad de vídeo (memoria del runtime)

Hacia el segundo 14 del vídeo, `ClGame` y las banderas de `ClFMV` (`0x382xxx`) aparecían a cero.
`PS2X_WATCH` señaló una copia RPC de 8 KiB a `0x80380A12`. El runtime guarda sus descriptores RPC/TLS
en `0x1F00000+` y sus pilas de callbacks en el último MiB, pero `ClMemory` le da al juego toda la RAM
hasta 32 MiB y reserva desde arriba (búferes DMA, memoria de FMV). El juego sobrescribió el descriptor
del servidor de sonido, y `sceSifCallRpc` copió el buffer de comandos de EA a la "dirección del buffer"
que había quedado allí. Ahora el ejecutable mueve todo eso a `0x80000–0x100000`, que nadie usa
(`setRuntimeGuestRegion` + `configureGuestHeap` en `sles_51256_overrides.cpp`).

### Microprogramas VU0 por VIF0

El juego envía a VIF0, sin parar, `UNPACK` + `MSCNT`/`MSCAL` + `FLUSHE` (física y colisiones del nivel).
El intérprete de VIF0 del runtime abandonaba el paquete en el primer `MSCAL` y solo desempaquetaba
formato V4-32 sin máscaras. Parche `0003`: VIF0 ejecuta el microprograma con el intérprete VU0 del
runtime (el mismo de `VCALLMS`) y comparte el `UNPACK` completo de VIF1. Con ello aparece el HUD.

## Siguiente

1. Mundo 3D: averiguar por qué el terreno y los personajes no llegan al GS (ruta VIF1/VU1 y `XGKICK`).
2. Audio del vídeo: el IOP deja de pedir bloques de sonido (`handleSoundBufferRequest`) tras ~36
   peticiones, el buffer de audio se llena y bloquea el flujo de vídeo.
3. Rendimiento: el intérprete VU0 ejecuta ~1 600 microprogramas por segundo (~2 000 ciclos cada uno).
4. Salida de audio: llevar la mezcla del SPU2 al backend de audio del runtime (raylib).
5. VBlank a 50 Hz: el runtime programa VBlanks NTSC (60 Hz) aunque el juego es PAL (`VMODE = PAL`).

## Observaciones

- ~1000 avisos de `JR`/`JALR` sin resolver (vtables de C++ y jump tables); el runtime los resuelve por
  búsqueda en tiempo de ejecución.
- 29 parches de instrucción propuestos por el analizador como "posible código automodificable"; están
  aplicados (sustituidos por `nop`) pero sin revisar.
- El juego lleva un intérprete **Lua** embebido (`luaV_execute`, `lua_State`) y clases `CYo*` / `Cl*`.
