# Estado

[Inicio](../README.md) · [Compilación](BUILDING.md) · [Depuración](DEBUGGING.md)

Fase: **en progreso**. Arranca, reproduce los vídeos y entra en la misión del prólogo: la cinemática en el
motor y la batalla se dibujan con personajes, el mando mueve a Isildur y el tutorial avanza ("Atacar
Rápido" → "Atacar Fiero"). Los menús (pausa, opciones, "Salir a Menú") responden y el SPU2 suena por el
host. **Bloqueo actual: velocidad.** Con personajes en pantalla el juego va a 3-4 VBlanks/s (~7 % de la
velocidad real) porque el intérprete de VU1 no da abasto.

## Progreso del arranque

| Hito | Estado |
| --- | --- |
| Banner del juego (`v072 PAL`, `Sep 20 2002`) e inicialización de GS/DMA | ✅ |
| IRX del disco ejecutados en `ps2xIOP` (`LIBSD`, `FUFILECD`, `SDRDRV`, `PADMAN`, `MCMAN`, `FUSTREAM`...) | ✅ |
| Pantalla legal en español (`ClGameModeStartup`) | ✅ |
| Pantalla "CARGANDO..." (`ClGameModePlay`) | ✅ |
| Banco de efectos del nivel subido a la RAM del SPU2 (`00ISUL1.MSB`, el prólogo de Isildur) | ✅ |
| Streams de narrador/música (EA MultiStream) y carga del nivel (`ClEnemyDB::load`) | ✅ |
| Vídeos de logos e introducción (`ClFMV::playModal`), a 50 VBlanks/s | ✅ |
| Nivel: actores, colisiones, partículas, guiones; microprogramas VU0 por VIF0 | ✅ |
| Nivel: cielo y HUD (espada, "Defender", luna) | ✅ |
| Nivel: terreno, campo de batalla y cielo (plano de apertura del prólogo) | ✅ |
| Nivel: personajes (cinemática en el motor y batalla) | ✅ |
| Control con el mando: Isildur avanza con el stick, ataques y tutorial | ✅ (con `TTR_PAD_SCRIPT`) |
| Menú de pausa de la misión, opciones, confirmación de "Salir a Menú" | ✅ |
| Sonido: mezcla del SPU2 al host (raylib, 48 kHz) | ✅ (falta escucharlo con calma: AutoDMA, efectos, reverb) |
| Velocidad jugable con personajes | ❌ 3-4 VBlanks/s |
| Menú principal y nueva partida desde cero | pendiente |

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

### Mundo y personajes invisibles (`VSQI` de VU0, recompilador)

`ClFrustum::isBoundingBoxVisible`, la prueba de visibilidad de todo (nodos del mundo, árbol de colisión y
objetos del juego), guarda las 8 esquinas de la caja en la memoria de datos de VU0 con `VSQI.xyzw vfs,
(vit++)` y vuelve a leerlas con `VLQI` según el índice que lleva cada plano. El recompilador intercambiaba
los campos de `VSQI`/`VSQD`: usaba fs como registro de dirección y it como dato, así que guardaba siempre
`vf1` en direcciones de otros registros. Con esquinas basura, el primer plano descartaba cualquier caja:
no se dibujaba ningún nodo del mundo (solo el cielo, las nubes y el posproceso) y ningún objeto llegaba a
`AddToDisplayList`, de ahí que no hubiera personajes. Además, esas instrucciones accedían a la RAM del EE
(`vi·16`) en vez de a la memoria de VU0 (0x11004000), y `VDIV`/`VRSQRT` entre cero daban 0 en vez de ±MAX.
Diagnóstico con `TTR_TRACE=anims`: contadores de los pases de personajes, de `AddToDisplayList` por clase y
de los frustums por sector, y la prueba de planos recalculada en C++ junto al resultado del código generado.

### Vídeo cortado a los ~14 s (VBlank NTSC en un juego PAL)

El runtime generaba siempre VBlanks a 60 Hz. El reproductor de `ClFMV` avanza el vídeo con el VBlank y
entrega el audio al IOP al mismo ritmo, pero el IOP lo consume a 48 kHz reales: a 60 Hz el buffer de audio
se llenaba, el IOP dejaba de pedir bloques tras ~36 y el vídeo terminaba antes de tiempo. Parche `0007`:
el VBlank sigue el modo que el juego pasa a `sceGsResetGraph` (PAL = 50 Hz). Ahora el vídeo llega al final
(605 peticiones de audio).

### Microprogramas VU0 por VIF0

El juego envía a VIF0, sin parar, `UNPACK` + `MSCNT`/`MSCAL` + `FLUSHE` (física y colisiones del nivel).
El intérprete de VIF0 del runtime abandonaba el paquete en el primer `MSCAL` y solo desempaquetaba
formato V4-32 sin máscaras. Parche `0003`: VIF0 ejecuta el microprograma con el intérprete VU0 del
runtime (el mismo de `VCALLMS`) y comparte el `UNPACK` completo de VIF1. Con ello aparece el HUD.

### Flags MAC de VU0 en modo macro (recompilador)

El mundo no se dibujaba porque `ClWorldNode::drawRecursive` descartaba todos los nodos: la prueba
`ClFrustum::isBoundingBoxVisible` hace `vsub.xyz $vf0, ...` y lee los bits de signo con
`cfc2 $t0, $vi17` (flags MAC). El recompilador de PS2Recomp no actualizaba los flags MAC/STATUS en las
instrucciones FMAC de modo macro y, además, escribía en `vf0` (en el hardware es constante y descarta
las escrituras). Ahora `vu_translator.cpp` añade `PS2_VU0_SET_MAC` a ADD/SUB/MUL/MADD/MSUB, a sus
variantes ACC y a OPMULA/OPMSUB, y descarta las escrituras a `vf0`. Lo mismo afectaba a
`SfIsLineIntersection` (colisiones con líneas) y a `SfObjFullyContainedByNode`.

### `sqrt.s` usaba el registro equivocado (recompilador)

En el EE, `SQRT.S fd, ft` toma el operando del campo **ft** (fs vale 0); el recompilador usaba fs. Las
452 raíces cuadradas del juego calculaban `sqrt($f0)`, así que `ClVector::normalize3d`, las distancias, la
cámara y la física trabajaban con vectores sin normalizar. Corregido en `fpu_translator.cpp`.

### Escenas con personajes a 2-3 VBlanks/s (intérprete de VU1)

`PS2X_VU_STATS=1` mide 3,4 M pares de instrucciones de VU1 por segundo en la cinemática del prólogo: unos
250 ns por par, con el hilo del EE dedicado casi entero al intérprete. El intérprete modela el pipeline
ciclo a ciclo (colas de escritura de VF/VI/ACC, flags FMAC calculados dos veces en precisión extendida). El
parche `0009` pasa por defecto al modelo de microVU de PCSX2: los resultados se escriben al momento (los
lectores siguen esperando la latencia, así que ven los mismos valores) y los flags salen del resultado float;
`PS2X_VU_ACCURATE=1` vuelve al modelo anterior. La imagen es idéntica y VU1 sube a ~5,7 M pares/s (1,7x),
insuficiente: la escena pide ~1,2 M ciclos de VU1 por frame, unos 60 M pares/s a 50 Hz.

## Rendimiento

Medido en el nivel del prólogo con un Ryzen 5 3600X (`TTR_FRAME_DUMP` + `t=`, build `RelWithDebInfo`):

| Cambio | VBlanks/s |
| --- | --- |
| Punto de partida (todo en el hilo del EE) | 5,8 |
| IOP y temporizadores del EE por lotes de 4096 ciclos (parche `0005`) | 6,7 |
| Rasterizador del GS en 6 hilos (parche `0005`) | 17,4 |
| GS en su propio hilo (parche `0006`) | 23 |
| 9 hilos de rasterizado (CPU de 12 hilos, parche `0006`) | 30 (de 60) |
| VBlank PAL a 50 Hz (parche `0007`) | 28,8 de 50: un 58 % de la velocidad real |

El VBlank exige a la vez 16,7 ms de reloj real y ~4,9 M ciclos del EE cobrados por el código generado, así
que la velocidad del juego depende del tiempo de CPU por frame. El perfilador (`TTR_PROFILE`) mostraba un
único hilo al 99 %: el código del juego, el intérprete de VU1, el IOP y el rasterizador del GS (sprites de
pantalla completa del posproceso: ~45 % del total). El código generado cobra 8 ciclos por llamada y 32 por
arista de bucle, y cada cobro ejecutaba el planificador completo del IOP (hilos, DMA, SPU2, temporizadores)
para 1-4 ciclos del IOP. La build `Release` añade un 15-25 %.

Ahora el límite es el GS: el nivel dibuja unos 3,2 M píxeles por VBlank (unas 22 pasadas de pantalla
completa por frame: posproceso en franjas de sprites, triángulos aditivos gigantes y la cúpula del cielo con
filtrado bilineal) a ~100 ns por píxel y hilo.

Con personajes (cinemática y batalla del prólogo) el límite es VU1, no el GS: ~5,7 M pares/s con el parche
`0009` frente a los ~60 M que harían falta para 50 Hz.

## Siguiente

1. **Recompilador de microcódigo VU1** (el bloqueo para jugar). El juego sube ~30 imágenes de microcódigo
   distintas por VIF (`PS2X_VU_DUMP`), así que hace falta traducir por bloque básico con caché por contenido
   (como microVU) o generar C++ por adelantado a partir de los volcados, con flags MAC/estado retrasados 4
   ciclos y Q/P con su latencia. Objetivo: >60 M pares/s.
2. Llegar al menú principal sin pasar por la misión (Start → "Salir a Menú" → Sí) y empezar una partida nueva.
3. Escuchar el audio: AutoDMA (voz/música por bloques), volumen y efectos; el volcado (`PS2X_AUDIO_DUMP`)
   suena tonal y casi sin saturar.
4. GS: con VU1 resuelto vuelve a ser el límite (coste por píxel, ver *Rendimiento*).

## Observaciones

- ~1000 avisos de `JR`/`JALR` sin resolver (vtables de C++ y jump tables); el runtime los resuelve por
  búsqueda en tiempo de ejecución.
- 29 parches de instrucción propuestos por el analizador como "posible código automodificable"; están
  aplicados (sustituidos por `nop`) pero sin revisar.
- El juego lleva un intérprete **Lua** embebido (`luaV_execute`, `lua_State`) y clases `CYo*` / `Cl*`.
- La cúpula del cielo es la que borra el Z-buffer cada frame (`ZTST ALWAYS`, `Z = sSkyDomeData+0x28`);
  si no se dibuja, el mundo falla la prueba de profundidad (`GEQUAL`) y desaparece.
