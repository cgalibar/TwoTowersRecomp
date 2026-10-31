# Notas: sonido de EA (MultiStream V5) en Las Dos Torres

## Piezas

- **EE**: `ClSound`, `ClGameSound`, `ClMStream` (C++ del juego) sobre la librería `SOUND_*` de EA, que
  acumula comandos en un buffer (`0x48C400`) y los envía con `FlushIOPCommand` → `sceSifCallRpc`
  (cliente `0x490A90`, servidor `0x12345`, modo sin espera, función de fin `SOUND_JP` →
  `SOUND_CopyIOPBuffer`).
- **IOP**: `FUSTREAM.IRX` ("MultiStream V5"), cargado en `0x86D00` en este orden de arranque. Usa
  `LIBSD.IRX` para el SPU2, `cdvdman` para leer el disco y `FfFile:5` (export de `FUFILECD.IRX`) para
  resolver nombre → LSN/tamaño.

## Estructuras del IOP (`base = 0x86D00`)

| Dirección | Contenido |
| --- | --- |
| `base+0x17D00 + i*0x180` | Registro del stream `i` (hasta `*(base+0x17180)` streams) |
| `+0x28` | activo (1) |
| `+0x29` | estado: 3 abierto, 2 en cola, 5 asignado al lector, 7 listo, 1/6 lectura en curso |
| `+0x38` | LSN del fichero (o fd con origen `ioman`) |
| `+0xFF` | tipo de grupo (0 banco, 1 stream) |
| `+0x13E` | origen de carga (`SOUND_SetStreamLoadSource`, comando `0x44`) |
| `base+0x151B0` | stream asignado al lector (-1 ninguno) |
| `base+0x151B4` | stream confirmado por el EE (comando `0x09`, `0xFFFF` ninguno) |
| `base+0x171B0` | secuencia de la respuesta (se incrementa en cada RPC) |
| `base+0x171B8` | "lectura completada" (lo pone el callback de `sceCdCallback`) |
| `base+0x1C50C` | buffer de respuesta: `[0]` nº de entradas, 8 palabras por stream, después la cola |

Cola de la respuesta: `[1]` secuencia, `[2]` stream que el IOP quiere cargar (`0x151B0`, o -1 si
`0x171A8 == 2`), ... El EE la copia en `gp-0x7580` (secuencia) y `gp-0x7DB8` (petición de carga).

## Apretón de manos de carga

1. El hilo de VBlank del IOP (`0x8C038`) asigna un stream al lector (`0x8D874`: estado 5, `0x151B0 = i`).
2. La siguiente respuesta RPC informa `i` al EE.
3. `ClSound::tick` lo devuelve con `SOUND_GetStatus(i, …)` (comando 9) en el siguiente envío.
4. El IOP ve `0x151B4 == 0x151B0` y el "kicker" (`0x8DB04`) lanza `0x94B08` → `0x90C18` → lectura.

`ClSound::tick` hace `seqDone = seq` **después** de `FlushIOPCommand`; si el RPC se completa dentro de la
llamada, nunca ve una secuencia nueva y el paso 3 no ocurre. Arreglado en el runtime (parche `0002`).

## Herramientas

- `TTR_TRACE=sound`: estado de `ClMStream`, tablas por handle del EE, registros del IOP, globals del
  lector y el apretón de manos.
- `PS2X_IOP_TRACE=1` (+ `PS2X_IOP_CALLTRACE=<Mciclos>:<líneas>:<lo>-<hi>`): llamadas `cdvdman`, `ioman`,
  enlaces entre módulos (`FfFile`, `libsd`), RPC del IOP y traza de `jal`/`jalr` en un rango.
- `tools/irx_disasm.py FUSTREAM.IRX --base 0x86d00 <addr>`.
