# Estado

[Inicio](../README.md) · [Compilación](BUILDING.md) · [Depuración](DEBUGGING.md)

Fase: **en progreso, el prólogo se puede jugar**. Arranca, reproduce los vídeos y entra en la misión del
prólogo: la cinemática en el motor y la batalla se dibujan con personajes, el mando o el teclado mueven a
Isildur y el tutorial avanza ("Atacar Rápido" → "Atacar Fiero"). Los menús (pausa, opciones, "Salir a Menú")
responden y el SPU2 suena por el host. El juego va a **velocidad real** (50 VBlanks/s, el tiempo del juego
sigue al reloj del PC) con menos fotogramas: ~10 fps en la batalla y ~7-9 en la cinemática del motor, en un
Ryzen 5 3600X con la build `Release`. Ver *Rendimiento*.

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
| Control con el mando: Isildur avanza con el stick, ataques y tutorial | ✅ (con `TTR_PAD_SCRIPT`; teclado y mando del PC por raylib) |
| Menú de pausa de la misión, opciones, confirmación de "Salir a Menú" | ✅ |
| Sonido: mezcla del SPU2 al host (raylib, 48 kHz) | ✅ (falta escucharlo con calma: AutoDMA, efectos, reverb) |
| Velocidad del juego | ✅ tiempo real (reloj del EE anclado al del PC, parche `0013`) |
| Fotogramas por segundo con personajes | ⚠️ ~10 fps en batalla, ~7-9 en la cinemática del motor (el juego apunta a 25) |
| Menú principal y nueva partida desde cero | pendiente |

## Problemas resueltos

### El juego iba a cámara lenta (reloj del EE, parche `0013`)

El juego mide el tiempo de cada frame con el temporizador 0 del EE en modo HBLANK (`ClClock::tick` lee y pone
a cero `T0_COUNT` y divide entre 15 600), así que su lógica es de paso variable. Pero el runtime solo avanzaba
el reloj del EE con los ciclos que cobra el código generado: mientras el hilo del EE esperaba al de VIF1 o al
GS, o ejecutaba más despacio que la consola, no pasaba el tiempo para los VBlanks, los temporizadores ni el
IOP, y el juego iba al ~35 % de su velocidad. Ahora, cuando el reloj del EE se queda más de 1 ms por detrás
del del PC, salta hacia delante (como si el EE hubiera estado esperando al DMAC, que es lo que haría en la
consola): VBlanks a 50 Hz reales, el juego a su velocidad con menos fotogramas y el audio del SPU2 al ritmo
correcto. Un parón de más de 200 ms (depurador, arrastrar la ventana) se olvida en vez de reproducirse como
una ráfaga de VBlanks. `PS2X_REALTIME=0` vuelve al reloj anterior. Los temporizadores en modo HBLANK cuentan
15 625 líneas/s en PAL (antes, siempre las 15 734 de NTSC).

Los fines de RPC sin espera que se aplazan ~100 µs (parche `0002`) no avanzan con esos saltos: representan
código del EE que debe ejecutarse antes. Si se cumplían por un salto a mitad de `ClSound::tick`, el apretón de
manos de EA MultiStream se paraba y la precarga del narrador (`ClBard::preloadCinematicAndWait`) se quedaba
para siempre en "CARGANDO...".

### El EE esperaba a VIF1 al leer `VIF0_STAT`

El runtime trataba los registros de VIF0 (`0x10003800`) como los de VIF1/GIF y esperaba a que el hilo de VIF1
vaciara su cola. `ClVif0DmaBuffer::Sync` lee `VIF0_STAT` tras cada lote de VU0 (física y colisiones), así que
el frame siguiente no podía empezar hasta que VIF1/VU1/GS terminaban de dibujar el anterior. VIF0 y VU0 corren
en el hilo del EE: ya no esperan. Para que el EE no se adelante sin límite (el DMA de VIF1 se da por terminado
al encolarlo), lanzar una transferencia de VIF1 espera mientras haya dos sin procesar (`PS2X_VIF1_QUEUE`), como
`ClGfxDmaBuffer::EndChain` espera en la consola a la cadena anterior.

### Fuga de memoria de ~18 MB/s (cola del GS)

Cada hueco del anillo de comandos del hilo del GS conservaba el búfer de la mayor subida de imagen que había
llevado; con miles de huecos, el proceso crecía ~18 MB/s desde el arranque hasta varios GB. Los búferes de más
de 1 KiB se liberan al ejecutar la subida; la memoria se queda en ~300 MB. La cola pasa de 8192 a 65 536
comandos: en la batalla hay fases en que VU1 trabaja y el GS está libre y otras al revés, y una cola de un
quinto de frame no las solapaba (+20 % de fps).

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

### Motor rápido de VU (parche `0010`)

`VuFastEngine` (`ps2xRuntime/src/lib/vu/ps2_vu_fast.cpp`) decodifica cada par de instrucciones una vez por
subida de microcódigo en un manejador para la mitad superior y otro para la inferior (SSE para las FMAC),
más los registros que lee y escribe. Usa las mismas reglas de espera que el intérprete, los mismos flags
MAC/estado/clip con su latencia de 4 ciclos, Q y P con la suya, la copia de seguridad de VI para las ramas
condicionales y los bits E/D/T; el XGKICK manda el paquete entero al emitirse. `PS2X_VU_VERIFY=1` ejecuta
cada microprograma en los dos motores desde el mismo estado y compara registros, flags, memoria de datos y
paquetes GIF: **0 diferencias en 430 000 ejecuciones** del juego (logos, cinemática del prólogo). Hizo falta
quitar DAZ del MXCSR: el producto denormal dentro de un MADD debe sumarse (con redondeo hacia cero cambia 1
ulp) y un resultado denormal debe activar el flag U. VU deja de ser el límite en las cinemáticas.

### GS por lotes en todos los hilos (parche `0010`)

Con VU resuelto, el GS pasó a ser el límite y tres problemas lo hacían lento:

- La caché de páginas de textura de cada hilo tenía **una sola página**: una textura que ocupa varias (o el
  bilineal en el borde de una página) copiaba 8 KiB casi en cada muestra (`memmove`, 18 % de cada hilo).
  Ahora son 64 páginas de correspondencia directa.
- Los triángulos de menos de 4096 píxeles (todos los de los personajes) se dibujaban en el hilo del GS, y los
  grandes con una barrera después de cada uno. Ahora triángulos y sprites se acumulan en un lote que dibujan
  todos los hilos a la vez, cada uno en sus franjas de 8 filas entrelazadas; cada píxel lo sigue escribiendo
  un solo hilo y en orden. El lote se vacía antes de cualquier operación que lea o escriba VRAM, la CLUT o la
  caché de texturas (subidas, `TEXFLUSH`, lecturas, presentación).
- Coste por píxel: bilineal entero que no lee los texels de peso 0 (las copias alineadas del posproceso leen
  uno en vez de cuatro) y estado de píxel y muestreador decodificados una vez por primitiva.

`PS2X_GS_STATS=1` mide la carga: ~3,4 M píxeles y ~33 000 triángulos por frame en la batalla; ~16 M píxeles
en los planos con humo, donde partículas STQ de ~1000×1000 píxeles se apilan unas 50 veces.

### VIF1/VU1 en su propio hilo y medición por partes (parche `0011`)

En el hardware VIF1 y VU1 trabajan a la vez que el EE. `Vif1Worker` procesa en orden los datos de VIF1 (que el
DMA ya copia al lanzarse), con VU1, XGKICK y DIRECT, en otro hilo; el EE solo espera cuando toca algo que ese
hilo cambia. Hubo que vaciar el árbitro del GIF en el hilo de VIF1 (el EE lo vaciaba a la vez y corrompía los
registros privilegiados del GS) y sincronizar también en el canal DMA del GIF (las rutas nativas de PATH3
leen el estado del árbitro). `PS2X_VIF1_STATS=1` muestra el resultado en la batalla: el juego lanza la lista
de VIF1 de cada frame y enseguida espera a que termine (lee registros GIF/VIF1), así que el EE pasa ~85 % del
tiempo esperando a ese hilo, que va al ~85 %: ~35 % enviando primitivas al GS (paquetes GIF, montaje de cada
primitiva con una copia completa del estado del GS) y el resto en VU1 y UNPACK. La ganancia fue de 12 a 14-15
VBlanks/s.

El motor de VU se mide fuera del juego con `ttr_vu_bench` sobre 3000 ejecuciones reales de la batalla
(`PS2X_VU_CAPTURE`): 30,4 ns por par al empezar, 22,5 ns tras quitar los flags que nadie lee (el microcódigo no
usa nunca el registro de estado) y llevar la disponibilidad de registros en SSE; siempre idéntico al intérprete.

### Compilador de bloques de VU y pipeline del GS sin esperas (parche `0012`)

`PS2X_VIF1_STATS` mostró que en la batalla el hilo de VIF1 pasaba hasta un 55 % del tiempo bloqueado con la cola
del GS llena, y `PS2X_GS_STATS` por qué: el hilo del GS esperaba a todos los hilos de rasterizado en cada carga
de CLUT (~1000/s), cada `TEXFLUSH` y cada subida de textura. Ahora cada lote del pipeline de franjas lleva su copia
de la CLUT, su época de caché de texturas y las páginas de VRAM que lee y escribe, y el hilo del GS solo espera
cuando hay una dependencia real:

- Carga de CLUT: si ningún lote en curso escribe sus páginas, se lee al momento. Si solo las escriben subidas en
  cola, se lee de una VRAM sombra a la que el hilo del GS también aplica esas subidas.
- `TEXFLUSH`: barrera dentro del pipeline (los hilos de rasterizado se esperan entre ellos) en vez de una espera
  del hilo del GS.
- Subidas host→local: al momento si no tocan nada en curso; si no, en orden en el hilo de rasterizado 0. En los
  dos casos las cachés de página de los hilos se vacían para los lotes siguientes.
- Lectura tras escritura: una primitiva que muestrea páginas que dibuja el lote pendiente o uno en curso va tras
  una barrera. Las que leen las páginas que escriben ellas mismas (conversiones de formato en el sitio, unas
  10 000/s, y posproceso sobre el propio framebuffer) se dibujan en orden: las pequeñas al momento en el hilo del
  GS, las grandes en todos los hilos desde una copia congelada de las páginas que leen.

Esto último corrige una carrera que ya existía con las franjas: el hilo que dibujaba una franja podía leer una
página antes o después de que otro la escribiera, y la lluvia salía a veces con rayas moradas.

El compilador de bloques de VU traduce cada tramo recto de pares a código x64 que llama a los manejadores (y emite
en línea las FMAC sin flags) y lleva ciclos, disponibilidad de registros y saltos igual que el bucle. Las
comprobaciones de espera se hacen en línea con SSE y solo donde un registro puede no estar listo (`kNoStall`, que
también usa el bucle). Una caché por contenido evita volver a decodificar y compilar el microcódigo que el juego
vuelve a subir (15 imágenes y 23 cambios en 3000 ejecuciones). `ttr_vu_bench`: 22,4 → 14,2 ns por par, idéntico al
intérprete en las 3000 ejecuciones.

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

Medido con la build `Release` (Ryzen 5 3600X, 6 núcleos/12 hilos) tras el parche `0012`, antes del reloj real:

| Escena | VBlanks/s | Límite |
| --- | --- | --- |
| Vídeos FMV | 50 | ninguno (tiempo real) |
| Cinemática del prólogo en el motor | ~14-15 (eran ~11) | hilo de VIF1 (VU1) y GS |
| Plano con humo a pantalla completa | ~8 | GS (~16 M píxeles por frame) |
| Batalla del prólogo | ~17-18 (eran ~14,5) | hilo de VIF1: VU1 y espera a la cola del GS (~25 %) |

Antes del parche `0013` los VBlanks/s eran también la velocidad del juego (~35 % de la real en la batalla).
Desde el `0013` el juego va a 50 VBlanks/s reales y lo que cae son los fotogramas que dibuja (`fps=` en el
registro de `TTR_FRAME_DUMP`, contados en `ClDisplay::end`; `rt-skip=` es la parte del tiempo del EE que el
reloj real ha tenido que saltar):

| Escena | fps (`0013`) | Límite |
| --- | --- | --- |
| Vídeos FMV | 25 (los del vídeo) | ninguno |
| Cinemática del prólogo en el motor | ~7-9 | hilo de VIF1 y GS |
| Batalla del prólogo | ~10 (8,5 con la cola del GS de 8192) | GS (rasterizado con barreras) y VU1 |

Reparto en la batalla (`PS2X_VIF1_STATS=1`, `PS2X_GS_STATS=1`):

- **EE**: ~20 % ocupado; el resto espera a poder lanzar el frame siguiente a VIF1 (`queue=`).
- **Hilo de VIF1**: 100 % ocupado: ~33 % VU1, ~11 % enviando primitivas al GS, ~11 % UNPACK y demás, y ~45 %
  bloqueado con la cola del GS llena. Aunque el GS fuera infinitamente rápido, VU1 limitaría a ~18 fps.
- **Hilo del GS**: siempre con trabajo; ~0,3 s/s en `Submit`, ~0,3 s/s esperando hueco en el pipeline de franjas
  y ~0,3 s/s en ~45 cargas de CLUT por segundo de paletas que el juego acaba de dibujar (dependencia real: hay
  que esperar a que se dibujen).
- **Hilos de rasterizado** (9): ~57 % dibujando, ~34 % parados en barreras, ~9 % sin trabajo. Unos 4400 lotes
  por segundo, de los que ~2500 llevan una subida de textura en serie (el juego sube ~2100 texturas/s a un área
  de VRAM que reutiliza): cada una espera a que todos los hilos terminen lo anterior (escritura tras lectura de
  la textura vieja) y los demás esperan a que el hilo 0 la haga (la usan los lotes siguientes). Hacer esas
  dependencias exactas no ganó nada: es la propia secuencia subir → dibujar → subir en las mismas páginas.

## Siguiente

**Objetivo actual: el juego tal cual en PS2 pero a 60 fps.** El plan detallado, con las medidas por hilo, el
rediseño del GS y las tareas en orden, está en [PLAN_60FPS.md](PLAN_60FPS.md); los puntos 1-5 de esta lista
quedan desarrollados allí.

1. **Subidas de textura sin barreras**: versiones de página. Si solo hay lectores en vuelo de la textura vieja,
   el hilo del GS escribiría la subida en una copia de las páginas y los lotes siguientes leerían de esa copia
   (la caché de páginas de cada hilo ya copia páginas de 8 KiB: bastaría con resolver la página por lote); el
   hilo 0 la volcaría a la VRAM cuando los lectores terminen. Libera ~1/3 del tiempo de rasterizado.
2. **VU1 en el hilo de VIF1** (límite de ~18 fps): emitir en línea las FMAC con flags, las cargas/almacenamientos y
   la aritmética de VI, y abaratar `commit` y las esperas de Q/P; `ttr_vu_bench` lo comprueba sin el juego.
3. **Rasterizado**: coste por píxel (direcciones de VRAM por fila/columna, bucles de sprite especializados).
4. **Envío de primitivas al GS**: cada triángulo copia el estado completo del GS al montarse y al encolarse.
5. Latencia del mando: con ~10 fps hay hasta ~2 frames en la cola de VIF1/GS.
6. Llegar al menú principal sin pasar por la misión y empezar una partida nueva; escuchar el audio.

## Observaciones

- Pipeline del GS: en la batalla, ~3-4 % de los frames volcados tienen bloques morados de 8×8 (de unos cientos a
  varios miles de píxeles, a veces media pantalla); pasa igual con la build del parche `0012`, así que es una
  carrera anterior del pipeline de franjas, no del reloj real. `tools/purple_scan.ps1` (umbral `b > g+25`, `r > g+15`,
  `g < 10` sobre los PNG de `TTR_FRAME_DUMP`) los cuenta; el frame 1100 tiene 4 píxeles de contenido real. `PS2X_GS_SELF_NOW=0`,
  `PS2X_GS_SELF_COPY=0`, `PS2X_GS_IMMEDIATE_UPLOAD=0` y `PS2X_GS_SHADOW_CLUT=0` desactivan cada optimización para
  aislarlo.

- ~1000 avisos de `JR`/`JALR` sin resolver (vtables de C++ y jump tables); el runtime los resuelve por
  búsqueda en tiempo de ejecución.
- 29 parches de instrucción propuestos por el analizador como "posible código automodificable"; están
  aplicados (sustituidos por `nop`) pero sin revisar.
- El juego lleva un intérprete **Lua** embebido (`luaV_execute`, `lua_State`) y clases `CYo*` / `Cl*`.
- La cúpula del cielo es la que borra el Z-buffer cada frame (`ZTST ALWAYS`, `Z = sSkyDomeData+0x28`);
  si no se dibuja, el mundo falla la prueba de profundidad (`GEQUAL`) y desaparece.
