# Arquitectura de RdPiano

**Alcance:** `librdpiano/`, `rdpiano_juce/`, `roms/`, build y CI.
**Árbol analizado:** `develop` @ `0e23248` (2026-09-11).
**Complemento:** [REVISION-CODIGO.md](REVISION-CODIGO.md) es lo que se podía mejorar y lo que se hizo
con ello; [FIRMWARE.md](FIRMWARE.md), qué firmware corre y por qué sólo ése. Este documento describe
*cómo está construido el sistema*. Las trampas operativas —lo que hay que leer **antes** de
modificar— viven en [CLAUDE.md](../CLAUDE.md), y no se repiten aquí.

---

## Índice

1. [Qué es y qué emula](#1-qué-es-y-qué-emula)
2. [Vista de capas](#2-vista-de-capas)
3. [Grafo de dependencias real](#3-grafo-de-dependencias-real)
4. [El núcleo: `librdpiano`](#4-el-núcleo-librdpiano)
   - 4.1 [`Mcu` y `RdBoard` — la CPU y el bus](#41-mcu-y-rdboard--la-cpu-y-el-bus)
   - 4.2 [El handshake CPU-A ↔ CPU-B](#42-el-handshake-cpu-a--cpu-b)
   - 4.3 [`CommandPort` — el protocolo, en un solo sitio](#43-commandport--el-protocolo-en-un-solo-sitio)
   - 4.4 [`SoundChip` — IC19 / IC9 / IC8](#44-soundchip--ic19--ic9--ic8)
   - 4.5 [ROMs y desencriptado de líneas](#45-roms-y-desencriptado-de-líneas)
   - 4.6 [Qué es un "parche"](#46-qué-es-un-parche)
   - 4.7 [`RdPianoEngine` — la cadena de audio entera](#47-rdpianoengine--la-cadena-de-audio-entera)
5. [La capa plugin: `rdpiano_juce`](#5-la-capa-plugin-rdpiano_juce)
   - 5.1 [`PluginProcessor` — el puente con JUCE](#51-pluginprocessor--el-puente-con-juce)
   - 5.2 [Cadena de señal completa](#52-cadena-de-señal-completa)
   - 5.3 [Efectos `lsp/`](#53-efectos-lsp)
   - 5.4 [Resampling](#54-resampling)
   - 5.5 [`PluginEditor` y `Lcd`](#55-plugineditor-y-lcd)
6. [Modelo de concurrencia](#6-modelo-de-concurrencia)
7. [Ciclo de vida completo de una nota](#7-ciclo-de-vida-completo-de-una-nota)
8. [Pruebas](#8-pruebas)
9. [Lo que es independiente de la plataforma](#9-lo-que-es-independiente-de-la-plataforma)
10. [Plataforma: macOS](#10-plataforma-macos)
11. [Build y CI](#11-build-y-ci)
12. [Material de ingeniería inversa (`re_stuff/`)](#12-material-de-ingeniería-inversa-re_stuff)
13. [Puntos frágiles de la arquitectura](#13-puntos-frágiles-de-la-arquitectura)

---

## 1. Qué es y qué emula

RdPiano **no** es un sintetizador que imita el sonido de un MKS-20: es un emulador a nivel de
hardware de la placa **CPU-B** que Roland reutilizó en el MKS-20, el RD-1000 y el Rhodes MK-80.
Dentro del plugin corre el **firmware original** de la máquina, byte a byte, sobre una CPU HD63701
emulada, y ese firmware programa una reimplementación *gate-level* de los tres chips custom de
síntesis (IC19, IC9, IC8), derivada de análisis de silicio.

Consecuencia arquitectónica que gobierna todo lo demás:

> **No existe una función `noteOn()`.** Un Note On de MIDI se traduce a bytes del protocolo interno
> CPU-A → CPU-B, se encolan, y hay que *ejecutar instrucciones de la CPU emulada* hasta que el
> firmware los consuma y escriba por su cuenta en los registros del chip de sonido. La latencia y el
> comportamiento son los de la máquina real porque el camino es el de la máquina real.

De esto se deriva la segunda regla estructural: **el reloj maestro es el audio**. No hay un bucle de
CPU independiente; `Mcu::generate_next_sample()` produce una muestra y después avanza la CPU un
número fijo de pasos.

---

## 2. Vista de capas

```
┌──────────────────────────────────────────────────────────────────────┐
│  HOST (DAW / Standalone)                                             │
│  VST3 · AU · AUv3 · LV2 · Standalone                                 │
└───────────────┬──────────────────────────────────────────────────────┘
                │ processBlock(buffer, midiMessages)  [hilo de audio]
┌───────────────▼──────────────────────────────────────────────────────┐
│  rdpiano_juce  —  CONOCE JUCE, y nada más que eso                    │
│                                                                      │
│  PluginProcessor ── APVTS, presets, buses, latencia y cola declaradas │
│  PluginEditor + lcd/ ── el panel  [hilo de mensajes]                 │
└───────────────┬──────────────────────────────────────────────────────┘
                │ engine->pushMidi() / render() / requestPatch()
┌───────────────▼──────────────────────────────────────────────────────┐
│  librdpiano  —  SIN JUCE Y SIN stdio                                 │
│                                                                      │
│  RdPianoEngine ── LA FRONTERA: la cadena de audio entera             │
│       ├── lsp/       SpaceD (chorus BBD), Phaser  · int32 punto fijo │
│       ├── resample/  libresample (Mazzoni/Smith)  · C, sinc windowed │
│       ├── RdBiquad   EQ medio, sin juce::dsp                        │
│       │                                                              │
│       └── Mcu ──► RdBoard ──┬──► SoundChip   IC19 → IC9 → IC8       │
│           HD63701            ├──► CommandPort  protocolo del firmware│
│           (core MAME)        └──► RAM, latch, ROMs                   │
└──────────────────────────────────────────────────────────────────────┘
                                  ▲
                                  │  también consumido por
                    librdpiano/test/  e2e.cpp · unit/ · standalone.cpp
```

La frontera entre las dos capas es dura y deliberada, y **está en `RdPianoEngine`**
([rd_engine.h](../librdpiano/include/rd_engine.h)), no en el emulador: la cadena de audio completa
—efectos, remuestreo, EQ, reparto del MIDI, declick— es C++ puro sin una línea de JUCE. El plugin
sólo traduce: parámetros del host a `RdEngineParams`, `juce::MidiBuffer` a `pushMidi()`, y un
`AudioBuffer` a los dos punteros de `render()`.

El contrato de tiempo real está escrito en la propia cabecera:

| Método | Contrato |
|---|---|
| `prepare(hostRate, maxBlock)` | Reserva **todo**. Idempotente. |
| `render(l, r, n)` | No reserva, no bloquea, no imprime. |
| `pushMidi(frame, …)` | Cola fija de 512 eventos, sin reservas. |
| `requestPatch()` / `requestMasterTune()` | Un `exchange` atómico y nada más. Desde cualquier hilo. |
| `setPatch()` / `setMasterTune()` / `allNotesOff()` | Corren el emulador **en el acto**: arranque y pruebas, nunca con `render()` vivo en otro hilo. |

---

## 3. Grafo de dependencias real

```
PluginProcessor.cpp ──┬─► PluginParams.h ──► rd_engine.h
                      ├─► rd_engine.h    ──► patches.h, lsp/*.h, mame_utils.h, <atomic>
                      └─► JuceHeader.h   ──► BinaryData (ROMs + PNGs empotrados)

PluginEditor.cpp ─────┬─► PluginProcessor.h
                      ├─► lcd/Lcd.h ──► lcd/lcd_font.h
                      └─► JuceHeader.h

rd_engine.cpp ────────┬─► rd_engine.h, mcu.h, command_port.h, rom_loader.h
                      └─► resample/libresample.h   (C, extern "C")

mcu.h ────────────────► rd_board.h ──┬─► command_port.h
                                     ├─► sound_chip.h ──► sa_blocks.h, sa_tables.h
                                     ├─► rom_loader.h
                                     └─► rd_trace.h

mcu.cpp ──────────────► mcu_ops.h   (#include a mitad del .cpp — no es un header
                                     autónomo: depende de las macros PC/A/B/CC/RM/WM
                                     definidas arriba)
```

Tres detalles no obvios:

- **`mcu_ops.h` no es un header normal.** Se incluye en medio de `mcu.cpp` porque los ~250 handlers
  de opcodes usan las macros (`PC`, `EAD`, `SET_NZ8`, `RM`, `WM`…) definidas justo antes. Es código
  derivado de MAME (BSD-3); no debe reescribirse por estilo.
- **Las cabeceras del núcleo no incluyen la stdlib.** La única excepción es el `<atomic>` de
  `rd_engine.h` —las peticiones que sustituyen al cerrojo— y los `<stddef.h>`/`<cstdint>` sueltos.
  La traza sale por `RD_TRACE` ([rd_trace.h](../librdpiano/include/rd_trace.h)), que sin
  `-DRDPIANO_TRACE` es un no-op.
- **Las ROMs no se leen de disco en el plugin.** Están empotradas como `BinaryData` vía
  `juce_add_binary_data` en [rdpiano_juce/CMakeLists.txt](../rdpiano_juce/CMakeLists.txt). Sólo el
  harness, la suite unitaria y el standalone de SDL las cargan con `fopen`, con la ruta que les pasa
  `--roms`.

---

## 4. El núcleo: `librdpiano`

### 4.1 `Mcu` y `RdBoard` — la CPU y el bus

Están separados a propósito: **`Mcu` es lo que hay dentro del chip** (juego de instrucciones,
registros, temporizador, líneas de interrupción) y **`RdBoard` es todo lo soldado al bus** (RAM,
chip de sonido, latch de banco, ROMs y el puerto de comandos).

El acople va en un solo sentido y por exactamente tres métodos, la interfaz `RdBoardCpu`
([rd_board.h](../librdpiano/include/rd_board.h)): la placa le pide a la CPU el **contador de
programa** (que es lo que mira el handshake), le fija **líneas de interrupción**, y le delega los
registros de `0x0000-0x001F` que son del chip y no suyos.

El mapa de memoria está en `RdBoard::read`/`RdBoard::write`, en la cabecera y `inline` porque se
ejecuta millones de veces por segundo:

```
0x0000-0x001F  registros MCU  ┌ 0x00/0x01  dirección de puertos (no-op)
                              │ 0x02       PUERTO 1 — bus de datos CPU-A ↔ CPU-B
                              │ 0x03       PUERTO 2 — control / handshake
                              └ 0x08       TCSR (timer control & status)
0x0000-0x0FFF  RAM            (`ram[0x1000]`: el mapa no direcciona más)
0x1000-0x1FFF  SoundChip      (16 voces × 256 B; lectura = m_irq_id)
0x2000-0x3FFF  latch de banco (cualquier escritura aquí fija `latch_val`, se usan 2 bits)
0x4000-0xBFFF  params ROM     (ventana de 32 KB dentro de 128 KB, banco = latch_val & 0b11)
0xC000-0xFFFF  program ROM    (8 KB espejados dos veces: (addr-0xC000) & 0x1FFF)
```

El espejado de la program ROM merece atención: el array es de `0x2000` bytes pero la ventana es de
16 KB, y la máscara hace que `0xE000-0xFFFF` recaiga sobre `0x0000-0x1FFF`. Por eso los vectores de
interrupción (`0xFFF6` ICI, `0xFFF8` IRQ1, `0xFFFC` NMI, `0xFFFE` RESET) se resuelven sobre los
últimos bytes del dump de 8 KB.

**Interrupciones.** Hay dos fuentes vivas:

| Fuente | Vector | Cómo se dispara |
|---|---|---|
| **ICI** (input capture) | `0xFFF6` | La cola de comandos no está vacía → `M6801_TIN_LINE` a `ASSERT` |
| **IRQ1** | `0xFFF8` | `SoundChip::m_irq_triggered` — un segmento de envolvente terminó |

`execute_run()` sondea ambas antes de **cada instrucción**. El firmware limpia la IRQ del chip de
sonido escribiendo en su espacio de registros, lo cual `RdBoard::write` intercepta para bajar la
línea; escribir en el puerto 2 baja TIN por el mismo camino.

**Temporización.** `execute_run()` ejecuta **una instrucción** por llamada, y
`generate_next_sample()` la llama 100 veces (62 en los parches de 32 kHz) por muestra. Es decir: el
emulador ejecuta **100 instrucciones por muestra**, no 100 ciclos. Funciona porque el firmware es un
bucle de servicio, no código sensible a ciclo exacto — pero hay que tenerlo presente antes de
"arreglar" nada aquí. El detalle completo, con el bucle por presupuesto de ciclos que se abandonó,
está en [FIRMWARE.md §4](FIRMWARE.md).

**Reset.** `Mcu::reset()` reinicia **todo el estado**: sus registros y su temporizador, y vía
`RdBoard::reset()` la RAM, el latch, la cola de comandos y las 160 `SA_Part`. Lo que **no** es
estado —las dos ROM, la página de params ya mapeada, las tablas de onda descifradas— sobrevive. Por
eso `boot()` no pierde el parche.

### 4.2 El handshake CPU-A ↔ CPU-B

Esta es la trampa más importante del proyecto. En la máquina real hay dos CPUs: la **CPU-A** lee el
teclado y el MIDI, la **CPU-B** (la emulada) sintetiza. Se hablan por un bus paralelo de 8 bits con
handshake por los puertos 1 y 2.

RdPiano no emula la CPU-A. En su lugar, `RdBoard::read` **reconoce el contador de programa** dentro
de la rutina de lectura del firmware y le sirve el siguiente byte de la cola:

```cpp
// HACK: only works with the RD200 ROM (docs/FIRMWARE.md §2)
const u32 pc = cpu->programCounter();
CommandQueue &queue = command_port.queue();
if (!queue.empty() && (pc == 0xE12B || pc == 0xE15E || pc == 0xE168))
{
    data_comm_bus = queue.front();
    queue.pop();
}
...
// puerto 2: "hay dato disponible"
if (cpu->programCounter() == 0xE15A)
    return 0xFF;
```

Cuatro direcciones absolutas, codificadas a mano. **De ahí se sigue que sólo se carga
`RD200_B.bin`** aunque `roms/` contenga los dumps de firmware del MKS-20 y del MK-80: cambiar de
firmware exige volver a desensamblar y recalcular esas cuatro direcciones. Las equivalentes del
MKS-20, verificadas en su momento, están en [FIRMWARE.md §2](FIRMWARE.md).

### 4.3 `CommandPort` — el protocolo, en un solo sitio

El "protocolo" que viaja por ese bus **no es MIDI**: son bytes del bus interno, y su única
definición está en [command_port.h](../librdpiano/include/command_port.h). Fuera de ahí se habla por
intención (`boot()`, `selectPatch()`, `sendMidiCmd()`, `allNotesOff()`), nunca por bytes.

| Evento | Bytes encolados |
|---|---|
| Program change | `0x30 \| (pp & 0xF)` |
| Note On | `0xC0`, nota, velocidad |
| Note Off | `0xB0`, nota, `0x00` |
| Sustain (CC 64) | `0x50 \| (0xF ó 0x0)` |
| Master tune | `0xE0`, msb, lsb |

Todo lo demás (pitch bend, otros CC, aftertouch) se descarta silenciosamente.

La cola es un **anillo de tamaño fijo** (`CommandQueue::CAPACITY = 1024`), así que no reserva nunca
—requisito para vivir dentro de `render()`—. Al desbordar descarta **el byte nuevo**, no el más
viejo: tirar por delante partiría un mensaje que el firmware ya está leyendo a medias.

La temporización no vive aquí: `boot()` y `setMasterTune()` tienen que correr la CPU *entre*
mensajes, así que son de `Mcu`.

### 4.4 `SoundChip` — IC19 / IC9 / IC8

`SoundChip::update()` produce **una muestra mono** sumando 16 voces × 10 partes = **160 slots por
muestra** (3,2 M actualizaciones de parte por segundo a 20 kHz). Cada slot atraviesa tres bloques
que replican los sumadores reales de cada chip; están `inline` en
[sa_blocks.h](../librdpiano/include/sa_blocks.h) como `tick_ic19` / `tick_ic9` / `tick_ic8`.

```
        SA_Part (estado por slot)
        ├ sub_phase      24 bits, acumulador de fase
        ├ env_value      28 bits, acumulador de envolvente
        ├ pitch_lut_i    índice a phase_exp_table
        ├ wave_addr_loop / wave_addr_high
        ├ env_dest / env_speed / env_offset
        └ flags_0 / flags_1   (compartidos: viven en la parte 0 de la voz)

┌─ IC19 · ENVOLVENTE ────────────────────────────────────────────────┐
│  adder1: env_value += env_table[env_speed]     (28 bits, con signo  │
│          por complemento cuando bit7 de env_speed está puesto)      │
│  adder3: offset → parte alta del volumen                            │
│  adder2: compara env_value>>20 contra env_dest                      │
│  ► volume  (14 bits, invertido)                                     │
│  ► end_reached → IRQ al firmware, env_value := env_dest<<20         │
└────────────────────────────────────────────────────────────────────┘
                              │ volume  (Ic19Out: el bus real entre chips)
┌─ IC9 · FASE / DIRECCIÓN ───▼───────────────────────────────────────┐
│  adder1: sub_phase += tables.phase_exp[pitch_lut_i]  (24 bits)      │
│  adder2: bucle — si cruza wave_addr_loop, reengancha                │
│  ► waverom_addr = (wave_addr_high << 11) | ((sub_phase >> 9) & 0x7FF)│
│  ► ag3_sel_sample_type, ag1_phase_hi  (líneas de control a IC8)     │
└────────────────────────────────────────────────────────────────────┘
                              │ waverom_addr  (Ic9Out)
┌─ IC8 · SUMA LOGARÍTMICA ───▼───────────────────────────────────────┐
│  De la wave ROM salen DOS valores por dirección:                    │
│    · exp    (14 bits + signo en el bit 15) — la muestra             │
│    · delta  ( 9 bits + signo en el bit 15) — pendiente para interpolar│
│  adder1: volume + exp                 → tmp_1                       │
│  adder3: addr_table[(sub_phase>>5)&0xF] + delta                     │
│  adder1': volume + (adder3 << 5)      → tmp_2                       │
│  antilog: tables.samples_exp[...] ×2, se restan 0x8000 si negativo  │
│  ► result += exp_val1 + exp_val2                                    │
└────────────────────────────────────────────────────────────────────┘
```

La clave conceptual: **todo el camino de señal es logarítmico**. El volumen no se *multiplica* por la
muestra, se *suma* a su exponente, y una LUT de antilogaritmo (`samples_exp`) vuelve al dominio
lineal. La interpolación entre muestras de la wave ROM se hace con un segundo tap (`delta` escalado
por `addr_table`, indexado por los bits 5-8 de la subfase), no con interpolación lineal en software.

**Las LUT no se leen de ROM, se generan** evaluando *expresiones lógicas bit a bit* que replican las
ROM internas IC10/IC11 y las tablas cableadas dentro de los gate arrays. El comentario del autor es
explícito: *"This is bit accurate, but I want to believe there is a better way"*. Hoy viven en
[sa_tables.h](../librdpiano/include/sa_tables.h) y son **compartidas por todas las instancias**:
`phase_exp` (64 K entradas de 32 bits, 256 KB) + `samples_exp` (32 K de 16 bits, 64 KB) = 320 KB que
se pagan una sola vez. `env_table` y `addr_table`, que son pequeñas y constantes, viven en
[sa_blocks.h](../librdpiano/include/sa_blocks.h).

Las **muestras** sí son de cada `SoundChip`, en el montón y una ranura por juego de ROM:

```cpp
struct WaveEntry { uint16_t exp; uint16_t delta; };   // el signo, en el bit 15 de cada uno
struct WaveTables { WaveEntry entries[0x20000]; };    // 512 KB por ranura, 3 ranuras
```

Meter el signo en el bit 15 en vez de en un `bool` aparte no es cosmético: baja la ranura de 768 KB a
512 KB y deja una sola línea de caché por lectura en vez de cuatro tablas paralelas.

**Decodificación de escrituras:**

```
offset (0x000-0xFFF dentro del chip)
  ├ voz    = offset / 0x100          (16)
  ├ parte  = (offset % 0x100) / 0x10 (16 en memoria, 10 activas)
  └ campo  = offset % 8              (0..7)
```

Campos: `0/1` = pitch (hi/lo), `2` = loop, `3` = dirección alta de onda, `4` = destino de envolvente,
`5` = velocidad, `6` = flags (se escriben siempre en la parte 0 — son de voz, no de parte),
`7` = offset de envolvente. Cada part ocupa 16 bytes del mapa pero sólo tiene 8 registros: `+8..+F`
se descartan, porque el firmware RD200 sólo escribe ahí ceros de arranque. La lectura ignora el
offset y devuelve siempre `m_irq_id`, que codifica `parte | (voz << 4)`: así el firmware sabe *qué*
slot pidió atención.

**Dos hacks conscientes en `update()`**, ambos marcados en el código y **fijados por
`test/vectors/ic_blocks.txt`** (2.256 vectores):

1. `if (part.env_value == 0 && part.env_dest == 0) continue;` — atajo de rendimiento; sin él se
   procesarían los 160 slots incluso en silencio. Tiene efecto funcional: decide qué slots se
   procesan.
2. `if (part.env_value != 0) result += exp_val;` — silenciado condicional *"to prevent voices ringing
   when env value is 0, investigate"*. Es un parche sobre un síntoma; el propio código lo admite.

### 4.5 ROMs y desencriptado de líneas

En la placa real las líneas de dirección y de datos de las ROMs están permutadas (por trazado del
PCB, no por protección). Al cargar hay que deshacer la permutación, y cada ROM tiene la suya. Todo
está hoy en [rom_loader.h](../librdpiano/include/rom_loader.h), fuera del emulador:

| ROM | Contenido | Tamaño | Desencriptado |
|---|---|---|---|
| Program (`RD200_B.bin`) | Firmware CPU-B | 8 KB | `unscramble_program`: bitswap 14 de dirección + bitswap 8 de datos |
| Params (`IC18`) | Parámetros de parche | 128 KB | `unscramble_params`: bitswap 17 + el mismo bitswap de datos |
| Wave (`IC5`/`IC6`/`IC7`) | Muestras | 3 × 128 KB | `unscramble_wave`: permutación explícita bit a bit |

Las tres ROMs de onda no contienen una muestra cada una: **cada muestra se reparte entre las tres**.
La decodificación reensambla, por cada dirección, un exponente de 14 bits + signo y un delta de
9 bits + signo tomando bits sueltos (algunos invertidos) de `ic5`, `ic6` e `ic7`. Es una
transcripción directa del ruteo de pines.

> **Regla operativa:** cualquier cambio en los `unscramble_*` o en la decodificación de muestras
> **mueve los 16 hashes de `golden.txt`**. El harness lo detecta, pero no juzga el timbre: eso se
> verifica de oído. Hay además una suite propia, `test_rom_loader`.

### 4.6 Qué es un "parche"

Un parche = **juego de ROMs de onda** (IC5/6/7) + **offset dentro de la params ROM** + **frecuencia
de muestreo**. Las cinco tablas paralelas viven en [patches.h](../librdpiano/include/patches.h), en
el **núcleo**, y las comparten el plugin y el harness:

```
patchNames[16]        →  "MKS-20: Piano 1" … "MK-80: Vibraphone"
patchToRomSetId[16]   →  ROMSET_MKS20_A (0-2) · ROMSET_MKS20_B (3-7) · ROMSET_MK80 (8-15)
patchToOffset[16]     →  dónde empieza el parche dentro de los 128 KB de params
patchSampleRates[16]  →  20000 ó 32000 Hz
patchOutputGain[16]   →  normalización de nivel (§5.2)
```

Las cinco están cerradas con `static_assert`: longitud correcta, offsets dentro de la ROM, romset
válido, tasa que sea 20 k ó 32 k, ganancia en rango y nombres no vacíos. Un descuadre no compila.

**El trabajo caro se hace una vez, al construir el motor** (~9 ms, ~2 MB). `SoundChip` guarda una
ranura de tablas de onda **por juego de ROM** (3 × 512 KB) y `RdPianoEngine` las **16 páginas de
params ya descifradas** (32 KB cada una). A partir de ahí, cambiar de parche es:

1. `selectRomSet()` — mover un puntero a la ranura ya descifrada (sólo si el juego cambia de verdad).
2. `selectPatchPage()` — un `memcpy` de 32 KB a `params_rom[0x8000]`, es decir al banco 1, visible en
   `0x4000-0xBFFF` cuando `latch_val == 1`.
3. **Parchear los bytes `0x00-0x02`** (banco 0, visibles en `0x4000-0x4002`) con
   `{0x01, target_hi, target_lo}` — un puntero *banco + dirección* que redirige al firmware al parche
   elegido.

Total: **microsegundos**, lo que lo hace viable desde el hilo de audio. Esa es toda la razón de ser
del desglose. El paso 3 es lo bonito: en vez de tocar el firmware, se le miente sobre dónde están sus
datos. El firmware sigue su camino normal, lee el puntero en `0x4000` y aterriza donde queremos.

`loadRomSet()` (descifrar) + `selectPatch()` (barato) siguen existiendo por separado, y `loadSounds()`
es los dos juntos: es lo que usa quien no ha precalculado nada. `prepareRomSetFor()` sobrevive como
no-op, para que un integrador antiguo siga compilando.

### 4.7 `RdPianoEngine` — la cadena de audio entera

Es la clase que el plugin ve, y la única que hay que entender para integrar el emulador en cualquier
otro anfitrión. Concentra emulador, efectos, remuestreo, EQ, reparto del MIDI y los dos mecanismos
que hacen que el instrumento se pueda tocar en directo: el **declick** y el **espejo de lo pulsado**.

`render()` corre siempre las mismas cinco fases, en este orden:

```
serviceRequests()   ── atiende parche/afinación pendientes, entre bloques
framesForBlock()    ── cuántas muestras del emulador toca, con corrección de deriva
synthesise()        ── el bucle del emulador + chorus + phaser + volumen  (a sourceRate)
resampleBlock()     ── libresample → la tasa del host
outputStage()       ── ×patchOutputGain, trémolo, EQ medio, declick   (a hostRate)
```

**Los cambios que apagan el firmware van con declick, y son dos.** Tanto el cambio de parche como el
de afinación mandan program changes que, *dentro del firmware*, apagan las voces y sueltan el pedal.
Sin tratamiento, mover cualquiera de los dos diales cortaba el sonido en seco. El mecanismo es común:

- Rampa a cero en **6 ms** → aplicar el cambio → subida en **15 ms**, o en **80 ms** si el cambio ha
  tenido que volver a disparar notas (la subida larga esconde el golpe de martillo de la reentrada).
- La ganancia va por **smoothstep** (`g²(3−2g)`): arranca plana —que es lo que tapa el transitorio—
  pero también llega plana, y en 0 y en 1 no cambia nada.
- Siempre **entre bloques**, nunca a mitad de uno: la tasa del emulador cambia con el parche y el
  bloque entero depende de ella.
- Si hay parche *y* afinación pendientes, **afina primero y cambia después**; al revés, los program
  change del afinado matarían las notas que acaba de devolver el parche.

**Notas y pedal sobreviven al cambio.** El motor lleva un espejo de lo que le ha enviado
(`heldVelocity[128]` + `sustainDown`, actualizado en `sendTracked()`, el único camino por el que sale
MIDI) y tras cambiar le devuelve el pedal y las teclas todavía pulsadas. Lo que estuviera sostenido
**sólo por el pedal** (tecla ya soltada) no se resucita a propósito: serían decenas de ataques a la
vez.

**Y reentran al nivel al que habían llegado, no al de su ataque**, o se oiría el golpe de tecla que
no se ha dado (+8 a +14 dB sobre lo que sonaba, medido). Dos seguidores de la salida cruda del
emulador —`onsetSq`, el ataque de la última nota, y `levelSq`, el nivel de ahora, ambos RMS al
cuadrado porque con detectores de pico el ataque de un acorde suma en fase y el sustain no— dan
cuánto ha decaído; una constante medida (0,228 dB por unidad de velocidad, la curva velocidad→nivel
es recta entre v16 y v120) lo convierte en cuánta velocidad restar. Queda en ±6 dB.

**Program change MIDI:** lo intercepta `pushMidi()` y lo convierte en `requestPatch()`. Reenviarlo al
firmware dejaba el motor mudo, porque cambiaba el número de parche pero no la página mapeada.

---

## 5. La capa plugin: `rdpiano_juce`

Son **tres archivos y dos tablas**. Todo lo que no es JUCE está en el núcleo.

### 5.1 `PluginProcessor` — el puente con JUCE

Ya no orquesta nada: traduce. Los **diez** parámetros se declaran una sola vez como tabla en
[PluginParams.h](../rdpiano_juce/Source/PluginParams.h), y sus valores de fábrica salen literalmente
de `RdEngineParams`, el POD que lee el motor — plugin y núcleo no pueden discrepar sobre qué es "de
fábrica".

```
kVolume · kChorusEnabled · kChorusRate · kChorusDepth
kTremoloEnabled · kTremoloRate · kTremoloDepth
kEfxEnabled · kEfxPhaserRate · kEfxPhaserDepth
```

El `id` de cada uno es el que va en el preset: renombrarlo rompe las sesiones guardadas. El orden es
el que ve el host, así que sólo se añade por el final.

`masterTune` y `currentPatch` **no están ahí, y es deliberado**: no son automatizables porque el
parche es el *programa* del anfitrión y la afinación no es un mando de mezcla. Viajan en el preset
como propiedades de la raíz del árbol APVTS, con los nombres de atributo de siempre.

El procesador es además `juce::Timer` **a 10 Hz**: el motor puede cambiar de parche sin pasar por
aquí (un program change MIDI lo hace), así que el temporizador lee ese atómico desde el hilo de
mensajes y actualiza espejo, preset y panel. El hilo de audio no toca la interfaz.

`getNumPrograms()` devuelve 16, y `setCurrentProgram()` es hoy una línea: `requestPatch()`.

**Buses:** entrada estéreo + salida estéreo. La entrada **no se lee nunca** —`processBlock` la
sobrescribe entera— y en un instrumento sobra, pero quitarla **rompe Logic**: lo inserta, no enseña
la interfaz y no suena. Probado y revertido; lo fija `plugin_bus_layout`.

**Latencia y cola declaradas.** `latencySamples()` es el retardo de grupo del remuestreador (67
muestras a 48 kHz), y es el **peor caso** (parche de 20 kHz) a propósito: no se renegocia al cambiar
de sonido. `tailLengthSeconds()` son 3 s, el doble de la cola real más larga de los 16 parches
(1,45 s a −60 dBFS, parche 5). Con los 0 s de antes el anfitrión dejaba de pedir bloques al soltar la
tecla y cortaba el final de la nota al exportar o al congelar la pista.

### 5.2 Cadena de señal completa

```
  MIDI del host
       │  pushMidi() → cola fija de 512 eventos
       ▼
┌──────────────────────────────────────────────────────────────────┐
│  synthesise() — corre a sourceRate (20 000 ó 32 000 Hz)          │
│                                                                  │
│  mcu->generate_next_sample(mode32khz)   →  s32 mono              │
│       │  << 5                                                    │
│       ▼                                                          │
│  SpaceD (chorus BBD)   ── mezcla en rampa, nunca bypass duro     │
│       │  >> 6                                                    │
│       ▼                                                          │
│  Phaser (EFX)          ── ídem            << 5 … >> 6            │
│       │                                                          │
│       ▼  × 1/65536 × volume(interpolado dentro del bloque)       │
│  emuL / emuR   (float, aún mono duplicado si el chorus está a 0) │
└───────────────────────┬──────────────────────────────────────────┘
                        │  libresample, factor = hostRate/sourceRate
                        ▼
┌──────────────────────────────────────────────────────────────────┐
│  outputStage() — a la frecuencia del host                        │
│    × 0.5 × patchOutputGain[parche]   (interpolado en el bloque)  │
│      × declickGain (smoothstep)                                  │
│    trémolo — dos senos en contrafase (L/R), a rate/2 Hz          │
│    RdBiquad peak: 350 Hz, Q 0.2, +8 dB  "tuned by ear"           │
└───────────────────────┬──────────────────────────────────────────┘
                        ▼
                  buffer de salida del host
```

Detalles con consecuencias:

- **El estéreo es falso hasta el chorus.** El emulador es mono (`s32`); L y R se duplican y sólo se
  separan al pasar por SpaceD, que sí tiene taps distintos por canal.
- **Los efectos corren SIEMPRE**, encendidos o no. `SpaceD::process()`/`Phaser::process()` son lo
  único que avanza sus líneas de retardo; saltárselos las congela y sueltan el audio viejo entero al
  reactivar —un estallido de −11 dBFS salido de la nada—. El bypass es la **mezcla**, en rampa de
  10 ms; con mezcla 0 ó 1 la salida es bit a bit la de antes. Coste: +0,03 ms/bloque, constante.
- **`patchOutputGain[]` normaliza los 16 parches a +6 dBFS** (acorde de 16 notas, vel 127). Se aplica
  en la salida, **tras** el emulador y `lsp/` (aritmética entera del hardware), así que no mueve el
  golden ni los hashes de `test_lsp.cpp`. Se regenera con `rdpiano_e2e --headroom`, que es
  idempotente y no entra en ctest. **No hay limitador detrás**: peor caso medido +10,8 dBFS con
  chorus de fábrica.
- **Ganancia y volumen se interpolan *dentro* del bloque.** Una lectura por bloque era zíper al mover
  el mando y un escalón de hasta 12 dB al cambiar de parche. Con el valor quieto el paso es
  exactamente 0 y la salida es idéntica.
- **El trémolo va después del resampler**, con fase propia acotada a 2π, y modula a `rate/2` Hz del
  reloj del **host** sea cual sea el parche. Los LFO del chorus y el phaser van *antes*, al ritmo del
  **emulador**: los cinco parches de 32 kHz modulan 1,6× más rápido con el mismo ajuste de panel. Es
  deliberado —escalarlo se probó, se escuchó y se descartó— y lo fija `engine_lfo_rate`.
- Los desplazamientos `<<5` / `>>6` adaptan el rango del emulador al punto fijo de 24 bits que usan
  los efectos `lsp` — no son ganancia arbitraria.
- **El declick va después del remuestreador**, multiplicando junto a la ganancia de salida. Ahí el
  cero es cero de verdad; aplicado antes quedaría cola del parche viejo sonando por debajo de las
  tablas de onda nuevas.
- `RdBiquad` es `juce::dsp::IIR::Filter<float>` reimplementado (misma forma directa II traspuesta,
  mismos coeficientes) salvo su `snapToZero`, que fuera de Intel es un no-op. Existe para que el EQ
  no obligue al núcleo a conocer JUCE.

### 5.3 Efectos `lsp/`

`spaced.cpp` y `phaser.cpp` no son DSP escrito a mano: son **transcripciones del microcódigo del DSP
de efectos original**. Se reconoce al instante por la forma:

```cpp
accA = (readMemOffs(120) * 127) >> 7;      // multiplicar-acumular con coeficiente
writeMemOffs(117, clamp_24(accA_1));       // saturación a 24 bits
```

Un acumulador (`accA`/`accB`), una IRAM circular direccionada por `(memOffs + bufferPos) & 0x7f`, y
—en SpaceD— una ERAM de 64 K palabras (`int32_t eram[0x10000]` = **256 KB**) para las líneas de
retardo largas del chorus BBD. Las variables se llaman `accA_0`, `accA_1`, `accA_370`… porque son
*pasos del microprograma*, no conceptos. Las tablas `spaceDRateTable`, `spaceDDepthTable`,
`phaserRateTable`, `phaserDepthTable` y `phaserResonanceTable` son volcados de las tablas de la
máquina.

Son los dos únicos que quedan: `enhancer.cpp` y `reverb.cpp` existieron —el segundo, vacío— y se
borraron. `lsp/` está excluido del formateo y de la norma de documentación, como el código de MAME.

Su respuesta al impulso está **congelada por hash** en `test_lsp.cpp`. Detecta cualquier cambio; no
juzga si suena mejor o peor.

### 5.4 Resampling

Se usa **libresample** de Dominic Mazzoni (basada en `resample-1.7` de Julius O. Smith): C puro,
interpolación sinc con ventana, calidad alta.

Es necesario porque el emulador sólo puede correr a 20 000 ó 32 000 Hz —son las frecuencias reales de
la máquina y están ligadas al parche—, mientras que el host pide 44 100, 48 000, 96 000…

Dos cosas importan aquí:

- **Los handles se abren en `prepare()` y no se tocan más.** `resample_open()` reserva ~600 KB y
  calcula un filtro Kaiser: 2,5 ms por handle, imposible dentro de `render()`. Se abren para todo el
  rango de factores de esta tasa de host, así que **cambiar de parche no los reabre** aunque
  `sourceRate` salte de 20 k a 32 k. `stats.resamplerOpens` lo vigila, y `test_engine` falla si sube
  durante el render — porque libresample usa `malloc` y el `operator new` sustituido no lo vería.
- **La deriva se corrige por bloque.** `framesForBlock()` calcula cuántas muestras del emulador
  *debería* haber producido (fraccionario), redondea, y corrige en bloques posteriores cuando el
  error acumulado pasa de un cuarto de bloque. Por eso `emuCapacity` se dimensiona para 32 kHz —el
  parche cambia sin re-preparar— más `maxBlock/4` de margen.

### 5.5 `PluginEditor` y `Lcd`

La UI es una **reproducción fotográfica del panel**, con tamaño fijo. Está guiada por dos tablas:
`ButtonSpec`×17 (un botón cada una) y `ModeSpec`×8 (un modo del display). El **dial alfa** es modal,
igual que en la máquina real: su efecto depende de qué modo esté activo, y los botones "params"
ciclan entre modos.

```
kModePatch · kModeTune · kModeChorusRate · kModeChorusDepth
kModeTremoloRate · kModeTremoloDepth · kModePhaserRate · kModePhaserDepth
```

Tres decisiones de rendimiento que ya están tomadas y conviene no deshacer:

- **Las tres hojas de arte se decodifican una sola vez en el constructor** y se reparten a los botones
  y al dial. Nada de `ImageCache::getFromMemory()` dentro de un `paint()`.
- **El display se dibuja a una `juce::Image` cacheada**, que sólo se rehace al cambiar el texto, la
  escala o el tamaño. Son 2 × 17 caracteres de 5 × 7 píxeles = **1.190 rectángulos** por repintado, y
  antes se pagaban todos, siempre.
- `updateValues()` repinta el fondo **sólo si se movió el fader**; cada control se repinta solo.

`Lcd` guarda **bytes, no `juce::String`**: con una cadena de JUCE el marcador `0xff` de la barra de
parámetros salía en UTF-8 de dos bytes y descuadraba la fila.

**Los diales de parche y de afinación aplican al soltarlos** (`sliderDragEnded`); arrastrando sólo
enseñan el nombre o los Hz. Los dos apagan el firmware, y un gesto entero serían decenas de
reentradas encadenadas.

La sincronización UI ↔ procesador va por `juce::ChangeBroadcaster` más el temporizador de 10 Hz de
§5.1.

---

## 6. Modelo de concurrencia

**No hay ningún cerrojo.** Lo hubo —un `juce::SpinLock` que el hilo de UI tomaba para hacer
`loadSounds()` mientras el hilo de audio giraba en vacío— y se eliminó entero. Hoy el emulador es
propiedad exclusiva del hilo de audio, y tocarlo desde fuera es **publicar una petición**:

```
  HILO DE AUDIO (tiempo real, el host manda)      HILO DE MENSAJES (UI)
  ────────────────────────────────────────        ──────────────────────
  processBlock()                                   sliderDragEnded()
    └─ engine->pushMidi(...)                         └─ engine->requestPatch(n)
    └─ engine->render(l, r, n)                            └─ patchRequest.exchange(n)
         ├─ serviceRequests()  ◄───────────────────────────┘  (un atómico, nada más)
         │    └─ rampa a cero → applyPatch() → rampa arriba
         ├─ framesForBlock()                         timerCallback()  @10 Hz
         ├─ synthesise()                               └─ engine->patch()  (lee un atómico)
         ├─ resampleBlock()
         └─ outputStage()
```

Cuatro propiedades del diseño:

1. **`processBlock` no espera a nadie.** No hay sección crítica que pueda estar tomada, así que el
   plugin no pierde bloques por mucho que se manosee el panel.
2. **Las peticiones se colapsan.** `requestPatch()` es un `exchange`: barrer el dial de parche deja
   una sola petición pendiente, no dieciséis. Es justo lo que hace falta con un mando.
3. **Intención y realidad están separadas.** `patch()` y `masterTune()` devuelven *lo pedido aunque no
   se haya atendido* —es lo que la interfaz debe enseñar— y `activePatch()` / `activeMasterTune()`,
   lo que está sonando. Sin esa distinción el panel parpadea.
4. **Las vías síncronas siguen existiendo, y son trampa.** `setPatch()`, `setMasterTune()` y
   `allNotesOff()` corren el emulador en el acto. Son para el arranque y para las pruebas; usarlas
   con `render()` vivo en otro hilo es una carrera.

Hay una excepción de orden que importa: **`prepare()` atiende una petición pendiente *antes* de
arrancar el firmware**, en vez de dejársela a `render()`. Es porque re-llamar a `setPatch()` después
de `prepare()` cambia el audio; el orden bueno es seleccionar parche → preparar.

`RdEngineStats` son contadores no atómicos que la UI lee cuando quiere: el núcleo no imprime desde el
hilo de audio, así que lo que iría a un log es un contador.

---

## 7. Ciclo de vida completo de una nota

Poniendo todas las piezas juntas, esto es lo que pasa entre que el usuario pulsa una tecla y sale
sonido:

```
 1. El host entrega  90 3C 64  en el midiBuffer de processBlock()

 2. engine->pushMidi(frame, 0x90, 0x3C, 0x64)  →  cola fija de 512 eventos

 3. render() → synthesise(): al llegar el bucle a la muestra que toca,
    sendTracked() lo apunta en heldVelocity[0x3C] y lo traduce:
      commandPort  ←  [0xC0, 0x3C, 0x64]     (protocolo interno, NO MIDI)

 4. generate_next_sample():
      a) SoundChip::update() produce la muestra de ESTE instante
      b) 100 iteraciones de execute_run():
           - la cola no está vacía → se asierta la línea TIN
           - el firmware entra por el vector ICI (0xFFF6)
           - la rutina en 0xE12B/0xE15E/0xE168 lee el puerto 1
           - RdBoard::read reconoce el PC y sirve 0xC0, luego 0x3C, luego 0x64

 5. El firmware (código Roland de 1986, sin modificar) decide:
      - qué voz libre asignar
      - qué parche/muestra corresponde a la nota 0x3C
      - qué envolvente aplicar según la velocidad 0x64
    y escribe ~40 bytes en 0x1000-0x1FFF

 6. RdBoard::write enruta esas escrituras a SoundChip::write(), que las
    decodifica en (voz, parte, campo) y rellena los SA_Part

 7. En las siguientes llamadas a update(), esos slots ya no cumplen
    (env_value == 0 && env_dest == 0) → se procesan por IC19/IC9/IC8
    y empiezan a contribuir a `result`

 8. Cuando un segmento de envolvente termina, IC19 pone m_irq_triggered,
    execute_run() asierta IRQ1, el firmware atiende, lee m_irq_id para
    saber qué slot fue, y programa el siguiente segmento

 9. Las muestras salen por chorus → phaser → resampler → ganancia →
    trémolo → EQ → el buffer del host
```

El paso 5 es lo que hace especial a este proyecto: la asignación de voces, el robo de voces, las
curvas de velocidad y el comportamiento del pedal **no están implementados en RdPiano**. Son el
firmware original tomando decisiones.

---

## 8. Pruebas

Tres suites, todas en ctest:

| Suite | Qué es | Tamaño |
|---|---|---|
| `rdpiano_tests` | Unitaria, `librdpiano/test/unit/` | 53 suites, 502 comprobaciones, ~8 s |
| `rdpiano_e2e` | Harness bit-exacto contra `golden.txt` | 16 parches, ~4 s |
| `rdpiano_plugin_tests` | Presets, buses, latencia y cola | 8 suites, 108 comprobaciones |

El **e2e** ([e2e.cpp](../librdpiano/test/e2e.cpp)) corre headless a ~40× tiempo real: arranque
silencioso, nota, acorde, extinción tras note-off (detector de voces colgadas), polifonía 16, rango
de pico y un **hash bit-exacto por parche**. `--patch N` para iterar en ~0,2 s. Cambios en
`sound_chip.cpp`, en los `unscramble_*` o en el MCU mueven el hash.

De la unitaria, la pieza clave es **`test_engine.cpp`**: un simulador de host (bloques irregulares,
22–96 kHz, cambios de parche en caliente, extremos de parámetros) y lo único que verifica **cero
reservas en `render()`**, sustituyendo el `operator new` global y vigilando `stats.resamplerOpens`.
Su red de transitorios cubre lo que ningún hash puede: `engine_effect_tail`,
`engine_effect_bypass_ramp`, `engine_program_change`, `engine_patch_declick`,
`engine_tune_held_notes`, `engine_volume_ramp`, `engine_latency`, `engine_lfo_rate`,
`engine_tremolo` y `engine_tail_length`.

**Lo que ninguna prueba juzga es el timbre.** Los efectos y el emulador están congelados por hash: el
hash detecta cualquier cambio, pero no dice si suena mejor o peor. La verificación es auditiva, con
`test/standalone.cpp` o el plugin en un DAW. Tampoco está cubierta la UI.

**Golden y vectores no se regeneran para poner algo en verde.** Si el cambio de audio es
intencionado: `--wav-dir DIR`, escuchar, y sólo entonces `--write-golden`.

---

## 9. Lo que es independiente de la plataforma

Casi todo el árbol es portable; lo que sigue no depende de macOS más que por ser la única plataforma
que se compila:

**Código.** `librdpiano/` completo (CPU, placa, chip de sonido, motor, ROMs, LUT), `lsp/`,
`resample/`, `lcd/`, `PluginProcessor`, `PluginEditor`. No hay un solo `#ifdef _WIN32` /
`__APPLE__` / `__linux__` en el código del proyecto. La única condicional de plataforma en todo el
árbol está en la librería de terceros: `resample_defs.h` incluye `config.h` salvo en
`WIN32`/`__CYGWIN__`.

**El núcleo no conoce ni JUCE ni `stdio`.** Tampoco `rd_engine.cpp`. Eso es lo que permite que la
suite unitaria y el harness compilen sin JUCE, y lo que haría viable otro anfitrión.

**Toolchain.** C++23 en todo el árbol, con C23 para las fuentes C de `resample/`. Módulos de JUCE
9.0.1, y `librdpiano/include` como usage requirement del target del núcleo.

**Recursos.** Las ROMs y los PNG se empotran como `BinaryData` en el binario: no hay archivos
externos que instalar ni rutas que resolver en tiempo de ejecución. El plugin es autocontenido.

**Estado y persistencia.** `getStateInformation`/`setStateInformation` serializan a XML: el árbol del
`AudioProcessorValueTreeState` con los diez parámetros, más `masterTune` y `currentPatch` como
propiedades de la raíz `<RdPiano>` — los mismos nombres de atributo de siempre, para que las sesiones
guardadas se sigan abriendo.

---

## 10. Plataforma: macOS

El proyecto compila **sólo para macOS**, y produce cinco formatos:

```
build/plugin/rdpiano_juce/rdpiano_juce_artefacts/Release/
  AU/rdpiano_juce.component      VST3/rdpiano_juce.vst3
  Standalone/rdpiano_juce.app    AUv3/rdpiano_juce.appex
  LV2/rdpiano_juce.lv2
```

**Particularidades reales de esta plataforma:**

- **AU y AUv3 sólo existen aquí.** Son formatos de Apple; `RDPN` (plugin code) y `GlZs`
  (manufacturer code) son los identificadores de cuatro caracteres que exige el registro de Audio
  Units. Los cinco formatos comparten `aumu RDPN GlZs`.
- **El generador es Xcode a propósito:** `juce_add_plugin` sólo crea el objetivo AUv3 con él. El
  deployment target es 11.0, y hay que pasárselo a `juceaide` por `MACOSX_DEPLOYMENT_TARGET`, porque
  es una invocación anidada que no hereda la caché.
- **Siempre universal** (`arm64;x86_64`). Hubo un modo nativo y se quitó: ahorraba compilación y nada
  en ejecución —macOS carga una sola rebanada del universal, y no hay flags por arquitectura en
  ningún `CMakeLists`— y su producto no cargaba bajo Rosetta.
- **Firma y cuarentena.** Los binarios del CI **no están firmados ni notarizados**; Gatekeeper los
  bloquea. El remedio está en el README: `sudo xattr -rd com.apple.quarantine <ruta>`. Es el punto de
  fricción número uno para los usuarios.
- **Rutas de instalación** (`sh scripts/build-osx.sh AU install`, que pide la contraseña una sola vez
  con `sudo -v` y copia con `ditto` sobre el destino ya borrado):

  | Formato | Destino |
  |---|---|
  | AU | `/Library/Audio/Plug-Ins/Components` |
  | VST3 | `/Library/Audio/Plug-Ins/VST3` |
  | LV2 | `/Library/Audio/Plug-Ins/LV2` |
  | Standalone | `/Applications` |

  **AUv3 no tiene destino propio:** JUCE lo empotra en el `.app` del Standalone
  (`XCODE_EMBED_APP_EXTENSIONS`), así que se registra al instalar la aplicación. No hay VST2 (JUCE 9
  lo quitó).
- **Empaquetado:** los artefactos son *bundles* (directorios), por eso el CI los comprime con `zip -r`
  antes de subirlos — subir un directorio sin comprimir rompería permisos y enlaces internos.
- **Ojo con el Standalone sin instalar.** Abrir el `.app` desde `build/` registra su AUv3 empotrado, y
  a partir de ahí el sistema resuelve `aumu RDPN GlZs` a ese `.appex` del directorio de compilación,
  que eclipsa el `.component` instalado: Logic deja de cargar la AU. Está detallado en la trampa 13
  de [CLAUDE.md](../CLAUDE.md), con cómo deshacerlo.

---

## 11. Build y CI

**Sólo CMake.** No hay Projucer ni `.jucer`: el `CMakeLists.txt` de la raíz cuelga de sí
`librdpiano/` (librería, harness, suite unitaria, standalone SDL) y `rdpiano_juce/` (los cinco
formatos y `rdpiano_plugin_tests`). El plugin **enlaza** el target `librdpiano` en vez de listar y
recompilar sus fuentes, así que añadir un `.cpp` al núcleo es **una línea** en
`librdpiano/CMakeLists.txt` y nada más.

```bash
sh scripts/download-juce.sh   # JUCE 9.0.1 → build/juce
sh scripts/build-osx.sh ALL   # cinco formatos universales
sh scripts/build-osx.sh AU install
```

Los dos scripts son POSIX `sh` sobre `scripts/common.sh` (paleta, log, `paso`, `cmd`, `fatal`, `fin`
y el trap de salida). **No imprimen la salida de CMake ni de Xcode**: una etiqueta por paso y, al
final, tiempo, número de avisos *distintos* y ruta de los productos; el resto va a `logs/`, donde se
conservan los 10 últimos por script. Si un paso falla, vuelcan las últimas 40 líneas y salen con el
mismo estado. El color se apaga solo si la salida no es un terminal, que es lo que ve la CI.

Ninguno repite trabajo hecho: `download-juce.sh` no baja nada si `build/juce` ya es la versión
correcta (según el `project(JUCE VERSION …)` de su raíz), y `build-osx.sh` sólo reconfigura si la
caché del binary dir no sirve.

**Todo lo generado vive bajo `build/`** (`juce/`, `plugin/`, `core/`, `core-asan/`), de modo que
`rm -rf build` deja el árbol como recién clonado y `rm -rf build/plugin build/core*` limpia sin
re-bajar JUCE.

Para trabajar en el núcleo no hace falta JUCE:

```bash
cmake -S librdpiano -B build/core -DRDPIANO_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --target rdpiano_tests rdpiano_e2e
ctest --test-dir build/core --output-on-failure
```

Configurar `librdpiano/` suelto fuerza `-fsanitize=address` (intencional: es un banco de pruebas, no
un artefacto de distribución); desde la raíz, OFF.

**CI** ([.github/workflows/main.yml](../.github/workflows/main.yml)): cuatro jobs — `ctest` sin ASan,
`ctest` con ASan, build de macOS (que incluye `rdpiano_plugin_tests`) y, en `master`, una release
rodante con tag `latest` que depende de los tres anteriores. Los runners de macOS son `macos-26` y el
binario es universal. `-Wall -Wextra` están en `librdpiano/CMakeLists.txt` y el núcleo compila con
cero avisos.

**La versión oficial** es el `VERSION` de `juce_add_plugin` en
[rdpiano_juce/CMakeLists.txt](../rdpiano_juce/CMakeLists.txt), y no hay otra: de ahí salen los
`CFBundleShortVersionString`/`CFBundleVersion` de los bundles, las versiones del `.ttl` del LV2 y las
macros `JucePlugin_Version*`. Publicar = cambiar esa línea y añadir la entrada al
[CHANGELOG](../CHANGELOG.md).

---

## 12. Material de ingeniería inversa (`re_stuff/`)

**No se compila y no forma parte del producto.** Es el rastro del proceso que hizo posible el
emulador, conservado por valor documental:

| Directorio | Contenido |
|---|---|
| `disasm/` | Desensamblados de los firmwares (RD200 A/B, MK80 B, MKS20 B). Aquí es donde se localizaron las direcciones `0xE12B`/`0xE15E`/`0xE168`/`0xE15A` del handshake |
| `verilog/` | Modelos Verilog de IC8/IC9/IC19 + testbenches. Su propio README advierte: *"probably most of them wrong"* — material de investigación, **no fuente de verdad** |
| `luts/` | Scripts para caracterizar las ROM LUT internas (IC10, IC11). El README documenta el hallazgo clave: ambas se reproducen perfectamente con una exponencial, mientras que las LUT implementadas con puertas lógicas dentro de los gate arrays sólo se pueden evaluar bit a bit — que es exactamente lo que hace `sa_tables.cpp` |
| `silicon_tooling/` | Cadena JS/Python para extraer celdas de fotos del silicio y convertirlas a Verilog/KiCad/ELK |
| `ident_cells/` | Recortes fotográficos de celdas estándar Fujitsu, clasificados por tipo |
| `rom_tools/` | Utilidades de desencriptado de ROMs (`descramble.c`, `descramble_wave.py`) — versiones sueltas de lo que hoy vive en `rom_loader.h` |

La relación práctica con el código de producción: `re_stuff/luts/` explica **por qué** las tablas de
`sa_tables.cpp` son expresiones lógicas y no fórmulas, y `re_stuff/disasm/` es el sitio donde hay que
volver si alguna vez se quiere soportar otro firmware.

---

## 13. Puntos frágiles de la arquitectura

No es la lista de bugs —esa está en [REVISION-CODIGO.md](REVISION-CODIGO.md)— sino las decisiones de
diseño que condicionan cualquier trabajo futuro:

1. **Un solo firmware soportado.** El handshake por direcciones absolutas ata el proyecto a
   `RD200_B.bin`. Los dumps del MKS-20 y del MK-80 están en `roms/` pero inertes. Soportar otro
   firmware = volver al desensamblado. Una alternativa estructural sería emular de verdad el
   protocolo de los puertos en vez de reconocer PCs, pero eso exige entender el handshake completo,
   no sólo dónde lee el firmware.

2. **Cambiar de parche o afinar sigue costando notas.** El modelo sin cerrojo quitó el problema de
   concurrencia, pero no el de fondo: el program change que hace falta para que el firmware relea la
   página de parámetros **apaga las voces dentro del firmware**. Todo el aparato de declick, espejo
   de lo pulsado y reentrada compensada en nivel existe para disimular eso. Es reconstrucción, no
   continuidad: lo sostenido sólo por el pedal no vuelve, y la reentrada queda en ±6 dB.

3. **El acoplamiento entre parche y frecuencia de muestreo.** Cambiar de parche puede cambiar
   `sourceRate` de 20 k a 32 k. Ya no obliga a reabrir los resamplers —se abren para todo el rango en
   `prepare()`— pero sí decide el ritmo de los LFO de chorus y phaser, y obliga a dimensionar los
   búferes para el peor caso. Mientras el bit real del puerto 2 no funcione, esta información vive
   duplicada: en `patchSampleRates[]` y, teóricamente, en el emulador.

4. **Los dos hacks de `SoundChip::update()`** son parches sobre síntomas de un modelo que aún no es
   del todo correcto. El propio código dice `investigate`. El atajo de rendimiento, además, tiene
   efecto funcional: decide qué slots se procesan.

5. **Ninguna prueba juzga el timbre.** El golden y los vectores detectan *cambios*, no *regresiones*:
   un cambio en `sound_chip.cpp` que pase el harness pero mueva el hash es de alto riesgo tímbrico y
   hay que decirlo explícitamente. La única verificación real es escuchar.

6. **Huella de memoria por instancia.** `SoundChip` son 1,5 MB de muestras (tres ranuras de 512 KB,
   una por juego de ROM), `RdPianoEngine` 512 KB de páginas de params ya descifradas (16 × 32 KB),
   `RdBoard` 128 KB de params ROM + 8 KB de firmware + 4 KB de RAM, y `SpaceD::eram` otros 256 KB.
   Total ≈ 2,4 MB por instancia, más los 320 KB de LUT que **se comparten** entre todas. Es el precio
   de que cambiar de parche cueste microsegundos; cada instancia del plugin lo paga íntegro.

7. **El bus de entrada que no se puede quitar.** Un instrumento con bus de entrada estéreo que nunca
   lee es incorrecto de libro, y `auval` valida igual sin él — pero Logic no carga la AU sin bus de
   entrada. Está probado, revertido y fijado por `plugin_bus_layout`. No volver a intentarlo.

---

*Documento escrito contra `develop` @ `0e23248`. Cuando el código y este documento discrepen, manda
el código; las trampas operativas están en [CLAUDE.md](../CLAUDE.md), que se mantiene al día con
cada cambio.*
