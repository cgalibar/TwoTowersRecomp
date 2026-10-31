# Estado

[Inicio](../README.md) · [Compilación](BUILDING.md)

Fase: **exploración**. Nada es jugable todavía.

## Hecho

- [x] Toolchain de Windows y scripts de instalación/compilación.
- [x] Extracción del disco e identificación de la build (`SLES-51256`, ELF con símbolos).
- [x] Proyecto CMake que enlaza el runtime de PS2Recomp con el código generado.
- [x] Prueba de humo sin código generado: el runtime carga los segmentos del ELF, aplica el override de
      `SLES-51256` y llega al entry point `0x00100008`.
- [x] Primer análisis (`ps2_analyzer`) importado en `TwoTowersRecompLib/config/sles_51256.toml`.
- [x] Primera generación (`ps2_recomp`): 5088 funciones (4757 recompiladas, 331 stubs del runtime),
      64 469 relocalizaciones procesadas, ~5090 ficheros C++ (~167 MB).

- [x] Ghidra headless + extensión EE: 4719 funciones y 33 807 etiquetas ejecutables exportadas.
- [x] Generación con el mapa de Ghidra: 38 526 entradas (38 194 recompiladas, 332 stubs), **0 errores**.
- [x] Build completa `win-clang-debug` con todo el código generado (~6 min en Ryzen 5 3600X, exe de 184 MB).

## Primer arranque (2026-10-31)

El código recompilado arranca y llega bastante lejos:

1. Imprime el banner del juego: `Version number: v072 PAL`, `Build date: Sep 20 2002`,
   `Build mode: INTERNAL`, `File media: CD`.
2. Inicializa el heap, el GS (`sceGsResetGraph`) y envía listas DMA/GIF.
3. Carga desde `cdrom0:` y ejecuta en `ps2xIOP` los IRX del disco: `SDRDRV`, `SIO2MAN`, `PADMAN`, `MCMAN`,
   `MCSERV`, `FUSTREAM` (+ `FUFILECD`). El sistema de ficheros del juego construye su árbol de
   directorios y el streaming de sonido reserva sus buffers.
4. Presenta frames de 640×512.
5. **Primer bloqueo:** `ClGame::tick()` (`0x0015EB3C`, `tick__6ClGameFv+0x3c`) hace un `JALR` a un puntero
   nulo. Después el hilo principal queda en `SfConsoleDrawThread__FPv` (`0x001713FC`), el hilo de la
   consola de depuración del motor, que probablemente está mostrando el error.

## Observaciones de la primera generación

- Sin el mapa de Ghidra hay **~12 000 instrucciones no reconocidas**, todas en funciones sin nombre
  (`sub_00303030`, `sub_00344E20`, `sub_0036C598`...) entre `0x00303030` y `0x0037Bxxx`: la sección `main`
  mezcla código y datos (probablemente también microcódigo de VU1). Con el mapa de Ghidra desaparecen.
- ~1000 avisos de `JR`/`JALR` sin resolver (vtables de C++ y jump tables); el runtime los resuelve por
  búsqueda en tiempo de ejecución.
- 29 parches de instrucción propuestos por el analizador como "posible código automodificable"; están
  aplicados (sustituidos por `nop`) pero sin revisar.
- El juego lleva un intérprete **Lua** embebido (`luaV_execute`, `lua_State`) y clases `CYo*` / `Cl*`.

## Siguiente

1. Depurar el puntero nulo de `ClGame::tick()`: averiguar qué estado/callback no se inicializó
   (¿carga de fichero o RPC del IOP sin completar?) y leer el texto de `SfConsoleDrawThread`.
2. Confirmar qué hay en `0x00303030–0x0037Bxxx` (¿microcódigo VU1?) y si el runtime lo necesita.
3. Módulos IOP propios del juego: `FUSTREAM.IRX` (streaming), `SDRDRV.IRX` / `LIBSD.IRX` (audio).
4. Vídeos `FMV/` (MPEG-2 vía IPU + FFmpeg).
5. Renderizado: ruta GS/VIF1/VU1 del runtime.
6. Mover los parches específicos a `TwoTowersRecomp/game/sles_51256_overrides.cpp`.

## Bucle de trabajo recomendado (de PS2Recomp)

1. Ejecutar con la configuración mínima, sin `skip` agresivos.
2. Arreglar primero los bloqueos duros.
3. Usar stubs temporales (`ret0`, `ret1`, `reta0`) solo para clasificar llamadas.
4. Convertir los arreglos temporales en implementaciones reales.
5. Los hacks de este juego van en *game overrides*, no en el runtime global.
6. Volver a probar desde arranque en frío tras cada tanda.
