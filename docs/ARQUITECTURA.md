# Arquitectura de RdPiano

**Alcance:** `librdpiano/`, `rdpiano_juce/`, `roms/`, build y CI.
**Rama analizada:** `main` @ `995e067`.
**Complemento:** los defectos concretos están en [AUDITORIA.md](docs/AUDITORIA.md); este documento
describe *cómo está construido el sistema*, no qué está roto.

---

## Índice

1. [Qué es y qué emula](#1-qué-es-y-qué-emula)
2. [Vista de capas](#2-vista-de-capas)
3. [Grafo de dependencias real](#3-grafo-de-dependencias-real)
4. [El núcleo: `librdpiano`](#4-el-núcleo-librdpiano)
   - 4.1 [`Mcu` — la CPU HD63701 y el bus](#41-mcu--la-cpu-hd63701-y-el-bus)
   - 4.2 [El handshake CPU-A ↔ CPU-B](#42-el-handshake-cpu-a--cpu-b)
   - 4.3 [`SoundChip` — IC19 / IC9 / IC8](#43-soundchip--ic19--ic9--ic8)
   - 4.4 [ROMs y desencriptado de líneas](#44-roms-y-desencriptado-de-líneas)
   - 4.5 [Qué es un "parche"](#45-qué-es-un-parche)
5. [La capa plugin: `rdpiano_juce`](#5-la-capa-plugin-rdpiano_juce)
   - 5.1 [`PluginProcessor` — el orquestador](#51-pluginprocessor--el-orquestador)
   - 5.2 [Cadena de señal completa](#52-cadena-de-señal-completa)
   - 5.3 [Efectos `lsp/`](#53-efectos-lsp)
   - 5.4 [Resampling](#54-resampling)
   - 5.5 [`PluginEditor` y `Lcd`](#55-plugineditor-y-lcd)
6. [Modelo de concurrencia](#6-modelo-de-concurrencia)
7. [Ciclo de vida completo de una nota](#7-ciclo-de-vida-completo-de-una-nota)
8. [Lo que es independiente de la plataforma](#8-lo-que-es-independiente-de-la-plataforma)
9. [Plataforma: macOS](#9-plataforma-macos)
10. [Build y CI](#10-build-y-ci)
11. [Material de ingeniería inversa (`re_stuff/`)](#11-material-de-ingeniería-inversa-re_stuff)
12. [Puntos frágiles de la arquitectura](#12-puntos-frágiles-de-la-arquitectura)

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
número fijo de pasos ([mcu.cpp:581](/librdpiano/src/mcu.cpp#L581)).

---

## 2. Vista de capas

```
┌──────────────────────────────────────────────────────────────────────┐
│  HOST (DAW / Standalone)                                             │
│  VST3 · AU · AUv3 · LV2 · Standalone                                 │
└───────────────┬──────────────────────────────────────────────────────┘
                │ processBlock(buffer, midiMessages)  [hilo de audio]
┌───────────────▼──────────────────────────────────────────────────────┐
│  rdpiano_juce  —  CONOCE JUCE                                        │
│                                                                      │
│  PluginProcessor ── parámetros DAW, patches, estado, orquestación     │
│       │                                                              │
│       ├── lsp/       SpaceD (chorus BBD), Phaser  · int32 punto fijo  │
│       ├── resample/  libresample (Mazzoni/Smith)  · C, sinc windowed  │
│       ├── juce::dsp  IIR peak EQ + tremolo                           │
│       └── PluginEditor + lcd/  ── UI [hilo de mensajes]              │
└───────────────┬──────────────────────────────────────────────────────┘
                │ mcu->sendMidiCmd() / mcu->generate_next_sample()
┌───────────────▼──────────────────────────────────────────────────────┐
│  librdpiano  —  SIN DEPENDENCIAS  (<stdio.h>, <queue>, <cmath>)      │
│                                                                      │
│   Mcu ─────────────────────────► SoundChip                           │
│   HD63701 + bus + ROMs           IC19 → IC9 → IC8                    │
│   mcu_ops.h (2358 líneas, MAME)  16 voces × 10 partes                │
└──────────────────────────────────────────────────────────────────────┘
                                  ▲
                                  │  también consumido por
                       librdpiano/test/standalone.cpp  (SDL2 + portmidi)
```

La frontera entre las dos capas es dura y deliberada: `librdpiano` **no incluye JUCE**, y el plugin
llega a él a través de exactamente cuatro puntos de entrada públicos
([mcu.h:38-43](/librdpiano/include/mcu.h#L38-L43)):

| Punto de entrada | Quién lo llama | Desde qué hilo |
|---|---|---|
| `sendMidiCmd(u8,u8,u8)` | `processBlock` | audio |
| `generate_next_sample(bool)` | `processBlock`, `setMasterTune`, `mcuReset` | audio **y** UI |
| `loadSounds(...)` | `setCurrentProgram`, `setStateInformation` | UI |
| `reset()` / `commands_queue` | `mcuReset` y cambios de patch | UI |

Esa doble procedencia de hilos es la razón de existir de `mcuLock` (§6).

---

## 3. Grafo de dependencias real

```
PluginProcessor.cpp ──┬─► ../librdpiano/include/mcu.h ──► sound_chip.h ──► mame_utils.h
                      │        (headerPath="../librdpiano/include" en el .jucer)
                      ├─► lsp/spaced.h, lsp/phaser.h
                      ├─► resample/libresample.h       (C, extern "C")
                      └─► JuceHeader.h  ──► BinaryData (ROMs + PNGs empotrados)

PluginEditor.cpp ─────┬─► PluginProcessor.h
                      ├─► lcd/Lcd.h ──► lcd/lcd_font.h
                      └─► JuceHeader.h

mcu.cpp ──────────────┬─► mcu.h
                      └─► mcu_ops.h   (#include a mitad del .cpp, línea 176 —
                                       no es un header autónomo: depende de las
                                       macros PC/A/B/CC/RM/WM definidas arriba)

sound_chip.cpp ───────► sound_chip.h + <cmath>
```

Dos detalles no obvios:

- **`mcu_ops.h` no es un header normal.** Se incluye en medio de `mcu.cpp`
  ([mcu.cpp:176](/librdpiano/src/mcu.cpp#L176)) porque los ~250 handlers de opcodes usan las macros
  (`PC`, `EAD`, `SET_NZ8`, `RM`, `WM`…) definidas justo antes. Es código derivado de MAME (BSD-3);
  no debe reescribirse por estilo.
- **Las ROMs no se leen de disco en el plugin.** Están empotradas como `BinaryData` vía el `.jucer`
  (`juce_add_binary_data` en [rdpiano_juce/CMakeLists.txt](/rdpiano_juce/CMakeLists.txt); hasta la
  fase 3 eran recursos del `.jucer`). Solo el standalone de SDL
  las carga con `fopen` desde el directorio de trabajo
  ([standalone.cpp:157-161](/librdpiano/test/standalone.cpp#L157-L161)).

---

## 4. El núcleo: `librdpiano`

### 4.1 `Mcu` — la CPU HD63701 y el bus

`Mcu` es a la vez el core de CPU **y** la placa. La CPU es un HD63701 (variante del 6801) portado
desde MAME: tabla de 256 punteros a método (`hd63701_insn`), tabla de ciclos `cycles_63701`, flags
`flags8i/flags8d`, registros en `PAIR` endian-safe.

El bus está en `read_byte` / `write_byte`
([mcu.cpp:456](/librdpiano/src/mcu.cpp#L456), [mcu.cpp:521](/librdpiano/src/mcu.cpp#L521)):

```
0x0000  registros MCU  ┌ 0x00/0x01  dirección de puertos (no-op)
0x001F                 │ 0x02       PUERTO 1 — bus de datos CPU-A ↔ CPU-B
                       │ 0x03       PUERTO 2 — control / handshake
                       │ 0x08       TCSR (timer control & status)
                       └ 0x0D/0x0E  input capture (16 bits)
0x0020
  ...   RAM interna   (array `ram[]`, solo se usan 4 KB)
0x0FFF
0x1000  SoundChip     (16 voces × 256 B; escritura decodificada, lectura = m_irq_id)
0x1FFF
0x2000  latch de banco (cualquier escritura aquí fija `latch_val`, se usan 2 bits)
0x3FFF
0x4000  params ROM    (ventana de 32 KB dentro de 128 KB, banco = latch_val & 0b11)
0xBFFF
0xC000  program ROM   (8 KB espejados dos veces: (addr-0xC000) & 0xDFFF)
0xFFFF
```

El espejado de la program ROM merece atención: el array es de `0x2000` bytes pero la ventana es de
16 KB, y la máscara `& 0xdfff` hace que `0xE000-0xFFFF` recaiga sobre `0x0000-0x1FFF`. Por eso los
vectores de interrupción (`0xFFF6` ICI, `0xFFF8` IRQ1, `0xFFFC` NMI, `0xFFFE` RESET) se resuelven
sobre los últimos bytes del dump de 8 KB.

**Interrupciones.** Hay dos fuentes vivas:

| Fuente | Vector | Cómo se dispara |
|---|---|---|
| **ICI** (input capture) | `0xFFF6` | `commands_queue` no vacía → `execute_set_input(M6801_TIN_LINE, ASSERT)` en [mcu.cpp:408-409](/librdpiano/src/mcu.cpp#L408-L409) |
| **IRQ1** | `0xFFF8` | `SoundChip::m_irq_triggered` — un segmento de envolvente terminó |

`execute_run()` sondea ambas antes de **cada instrucción**
([mcu.cpp:406-413](/librdpiano/src/mcu.cpp#L406-L413)). El firmware limpia la IRQ del chip de sonido
escribiendo en su espacio de registros, lo cual `write_byte` intercepta para bajar la línea
([mcu.cpp:563-565](/librdpiano/src/mcu.cpp#L563-L565)).

**Temporización.** Nótese una imprecisión deliberada del modelo:

```cpp
// mcu.cpp:581 — el comentario dice "ciclos", el bucle ejecuta instrucciones
for (size_t cycle = 0; cycle < (sampleRate32 ? 62 : 100); cycle++)
    execute_run();          // execute_run() → execute_one() = UNA instrucción
```

`m_icount` se decrementa con los ciclos reales de cada opcode pero nunca se usa como presupuesto
(el bucle `do/while` que lo consumía está comentado, [mcu.cpp:415-427](/librdpiano/src/mcu.cpp#L415-L427)).
Es decir: el emulador ejecuta **100 instrucciones por muestra** a 20 kHz, no 100 ciclos. Funciona
porque el firmware es un bucle de servicio, no código sensible a ciclo exacto — pero es un detalle
que hay que tener presente antes de "arreglar" nada aquí.

### 4.2 El handshake CPU-A ↔ CPU-B

Esta es la trampa más importante del proyecto. En la máquina real hay dos CPUs: la **CPU-A** lee el
teclado y el MIDI, la **CPU-B** (la emulada) sintetiza. Se hablan por un bus paralelo de 8 bits con
handshake por los puertos 1 y 2.

RdPiano no emula la CPU-A. En su lugar, `read_byte` **reconoce el contador de programa** dentro de la
rutina de lectura del firmware y le sirve el siguiente byte de la cola:

```cpp
// mcu.cpp:466 — HACK: only works with the RD200 ROM
if (!commands_queue.empty() && (PCD == 0xE12B || PCD == 0xE15E || PCD == 0xE168))
    data_comm_bus = commands_queue.front(), commands_queue.pop();
...
// mcu.cpp:483 — puerto 2: "hay dato disponible"
if (PCD == 0xE15A) return 0xFF;
```

Cuatro direcciones absolutas, codificadas a mano. **De ahí se sigue que solo se carga
`RD200_B.bin`** aunque `roms/` contenga los dumps de firmware del MKS-20 (`mks20_cpub_1.0.bin`) y del
MK-80 (`MK80_B.bin`): cambiar de firmware exige volver a desensamblar y recalcular esas cuatro
direcciones. Las líneas comentadas justo debajo de cada `if` son precisamente el juego de direcciones
del firmware MKS-20, dejadas como constancia.

El "protocolo" que se encola en `sendMidiCmd()`
([mcu.cpp:588](/librdpiano/src/mcu.cpp#L588)) **no es MIDI**, son bytes del bus interno:

| Evento MIDI | Bytes encolados |
|---|---|
| Program Change `Cn pp` | `0x30 \| (pp & 0xF)` |
| Note On `9n nn vv` | `0xC0`, `nn`, `vv` |
| Note Off `8n nn` / `9n nn 00` | `0xB0`, `nn`, `0x00` |
| Sustain (CC 64) | `0x50 \| (0xF ó 0x0)` |
| Master tune (desde la UI) | `0xE0`, msb, lsb |

Todo lo demás (pitch bend, otros CC, aftertouch) se descarta silenciosamente.

### 4.3 `SoundChip` — IC19 / IC9 / IC8

`SoundChip::update()` ([sound_chip.cpp:213](/librdpiano/src/sound_chip.cpp#L213)) produce **una
muestra mono** sumando 16 voces × 10 partes = **160 slots por muestra** (3,2 M actualizaciones de
parte por segundo a 20 kHz). Cada slot atraviesa tres bloques que replican los sumadores reales de
cada chip:

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
                              │ volume
┌─ IC9 · FASE / DIRECCIÓN ───▼───────────────────────────────────────┐
│  adder1: sub_phase += phase_exp_table[pitch_lut_i]   (24 bits)      │
│  adder2: bucle — si cruza wave_addr_loop, reengancha                │
│  ► waverom_addr = (wave_addr_high << 11) | ((sub_phase >> 9) & 0x7FF)│
│  ► ag3_sel_sample_type, ag1_phase_hi  (líneas de control a IC8)     │
└────────────────────────────────────────────────────────────────────┘
                              │ waverom_addr
┌─ IC8 · SUMA LOGARÍTMICA ───▼───────────────────────────────────────┐
│  De la wave ROM salen DOS valores por dirección:                    │
│    · samples_exp   (14 bits + signo) — la muestra                   │
│    · samples_delta ( 9 bits + signo) — pendiente para interpolar    │
│  adder1: volume + exp                 → tmp_1                       │
│  adder3: addr_table[(sub_phase>>5)&0xF] + delta                     │
│  adder1': volume + (adder3 << 5)      → tmp_2                       │
│  antilog: samples_exp_table[...]  ×2, se restan 0x8000 si negativo  │
│  ► result += exp_val1 + exp_val2                                    │
└────────────────────────────────────────────────────────────────────┘
```

La clave conceptual: **todo el camino de señal es logarítmico**. El volumen no se *multiplica* por la
muestra, se *suma* a su exponente, y una LUT de antilogaritmo (`samples_exp_table`, 32 K entradas)
vuelve al dominio lineal. La interpolación entre muestras de la wave ROM se hace con un segundo tap
(`samples_delta` escalado por `addr_table`, indexado por los bits 5-8 de la subfase), no con
interpolación lineal en software.

**Las LUTs se generan en el constructor, no se leen de ROM**
([sound_chip.cpp:59-164](/librdpiano/src/sound_chip.cpp#L59-L164)): `phase_exp_table` (64 K entradas)
y `samples_exp_table` (32 K) se reconstruyen evaluando *expresiones lógicas bit a bit* que replican
las ROMs internas IC11/IC10 y las tablas cableadas dentro de los gate arrays. El comentario del
autor es explícito: *"This is bit accurate, but I want to believe there is a better way"*. Coste:
0x18000 evaluaciones de expresiones lógicas en cada construcción, y ~1,06 MB de tablas residentes
por instancia de `SoundChip` (256 + 128 + 256 + 128 KB de muestras/signos + 256 KB de
`phase_exp_table` + 64 KB de `samples_exp_table`).

**Decodificación de escrituras** ([sound_chip.cpp:171](/librdpiano/src/sound_chip.cpp#L171)):

```
offset (0x000-0xFFF dentro del chip)
  ├ voz    = offset / 0x100          (16)
  ├ parte  = (offset % 0x100) / 0x10 (16 en memoria, 10 activas)
  └ campo  = offset % 8              (0..7)
```

Campos: `0/1` = pitch (hi/lo), `2` = loop, `3` = dirección alta de onda, `4` = destino de envolvente,
`5` = velocidad, `6` = flags (se escriben siempre en la parte 0 — son de voz, no de parte),
`7` = offset de envolvente. La lectura ignora el offset y devuelve siempre `m_irq_id`, que codifica
`parte | (voz << 4)`: así el firmware sabe *qué* slot pidió atención.

**Dos hacks conscientes en `update()`**, ambos marcados en el código:

1. `if (part.env_value == 0 && part.env_dest == 0) continue;` — atajo de rendimiento; sin él se
   procesarían los 160 slots incluso en silencio.
2. `if (part.env_value != 0) result += exp_val;` — silenciado condicional *"to prevent voices ringing
   when env value is 0, investigate"*. Es un parche sobre un síntoma; el propio código lo admite.

### 4.4 ROMs y desencriptado de líneas

En la placa real las líneas de dirección y de datos de las ROMs están permutadas (por trazado del
PCB, no por protección). Al cargar hay que deshacer la permutación, y cada ROM tiene la suya:

| ROM | Contenido | Tamaño | Desencriptado |
|---|---|---|---|
| Program (`RD200_B.bin`) | Firmware CPU-B | 8 KB | `UNSCRAMBLE_ADDR_CPUB` (bitswap 14) + `UNSCRAMBLE_DATA_CPUB` (bitswap 8) — [mcu.cpp:241](/librdpiano/src/mcu.cpp#L241) |
| Params (`IC18`) | Parámetros de parche | 128 KB | `UNSCRAMBLE_ADDR_PARAMS` (bitswap 17) + mismo bitswap de datos |
| Wave (`IC5`/`IC6`/`IC7`) | Muestras | 3 × 128 KB | `UNSCRAMBLE_ADDR_WAVE` (permutación explícita bit a bit) + inversión adicional de bits de dirección en `load_samples` — [sound_chip.cpp:49](/librdpiano/src/sound_chip.cpp#L49) |

Las tres ROMs de onda no contienen una muestra cada una: **cada muestra se reparte entre las tres**.
`load_samples` ([sound_chip.cpp:353](/librdpiano/src/sound_chip.cpp#L353)) reensambla, por cada
dirección, un valor exponente de 14 bits + signo y un valor delta de 9 bits + signo tomando bits
sueltos (algunos invertidos) de `ic5`, `ic6` e `ic7`. Es una transcripción directa del ruteo de pines.

> **Regla operativa:** cualquier cambio en los `UNSCRAMBLE_*` o en `load_samples` es de altísimo
> riesgo y **no tiene red de seguridad** — no hay tests. Se verifica de oído.

### 4.5 Qué es un "parche"

Un parche = **ROMs de onda** (IC5/6/7) + **offset dentro de la params ROM** + **frecuencia de
muestreo**. Las tres cosas viven en tablas paralelas del plugin
([PluginProcessor.cpp:30-74](/rdpiano_juce/Source/PluginProcessor.cpp#L30-L74)):

```
patchToRomSet[16]  →  qué juego de ROMs (MKS-20 A, MKS-20 B, MK-80)
patchToOffset[16]  →  dónde empieza el parche dentro de los 128 KB de params
sampleRates[16]    →  20000 ó 32000 Hz
```

`Mcu::loadSounds()` ([mcu.cpp:617](/librdpiano/src/mcu.cpp#L617)) hace algo bastante astuto:

1. Desencripta la params ROM completa a `params_rom_tmp`.
2. Rellena `params_rom` con `0xFF`.
3. Copia la **página de 32 KB alineada** que contiene el offset pedido a `params_rom[0x8000]`
   (es decir, al banco 1 → visible en `0x4000-0xBFFF` cuando `latch_val == 1`).
4. **Parchea los bytes `0x00-0x02`** (banco 0, visibles en `0x4000-0x4002`) con
   `{0x01, target_hi, target_lo}` — un puntero *banco + dirección* que redirige al firmware al
   parche elegido.

Es decir: en vez de tocar el firmware, se le miente sobre dónde están sus datos. El firmware sigue
su camino normal, lee el puntero en `0x4000` y aterriza donde queremos.

---

## 5. La capa plugin: `rdpiano_juce`

### 5.1 `PluginProcessor` — el orquestador

Concentra: parámetros expuestos al DAW, gestión de patches, persistencia de estado, resampling,
efectos y el bucle de render. Los parámetros son 11 `juce::AudioParameter*`
([PluginProcessor.h:67-77](/rdpiano_juce/Source/PluginProcessor.h#L67-L77)) — volumen, chorus
(on/rate/depth), tremolo (on/rate/depth), EFX (on/phaser rate/depth). Los de reverb están declarados
pero comentados: el `Reverb` de `lsp/reverb.cpp` **es un archivo vacío**.

`getNumPrograms()` devuelve 16 = 8 parches MKS-20 + 8 parches MK-80, y el cambio de programa hace
mucho más que mover un índice ([PluginProcessor.cpp:246](/rdpiano_juce/Source/PluginProcessor.cpp#L246)):
recarga las tres wave ROMs (`loadSounds` desencripta 3 × 128 KB), reescribe la params ROM, encola
`0x31`/`0x30` y actualiza `sourceSampleRate`. **Todo bajo `mcuLock`, desde el hilo de UI** (§6).

`setMasterTune()` ([PluginProcessor.cpp:277](/rdpiano_juce/Source/PluginProcessor.cpp#L277)) es el
caso más peculiar: para afinar hay que **correr el emulador desde el hilo de UI**, en un baile de
tres pasos —

```
push 0x30  →  100 muestras  →  push 0xE0,msb,lsb  →  100 muestras  →  push 0x30
```

con el comentario `TODO: we need to do this horrible switcharoo since changing the tuning on patches
different than 0 doesn't work`. El primer `0x30` fuerza al firmware al parche 0, se afina ahí, y el
último `0x30` restaura. Las "100 muestras" son el tiempo mínimo para que el firmware consuma la cola.

### 5.2 Cadena de señal completa

```
  MIDI del host
       │  sendMidiCmd() → commands_queue
       ▼
┌──────────────────────────────────────────────────────────────────┐
│  BUCLE INTERNO — corre a sourceSampleRate (20 000 ó 32 000 Hz)   │
│                                                                  │
│  mcu->generate_next_sample(mode32khz)   →  s32 mono              │
│       │  << 5                                                    │
│       ▼                                                          │
│  SpaceD (chorus BBD)   [si chorusEnabled; si no, bypass directo]  │
│       │  >> 6                                                    │
│       ▼                                                          │
│  Phaser (EFX)          [si efxEnabled]   << 5 … >> 6             │
│       │                                                          │
│       ▼  / 65536.0f  * volume                                    │
│  emu_sample_buffer L/R   (float, ya estéreo por duplicación)     │
└───────────────────────┬──────────────────────────────────────────┘
                        │  libresample, factor = destRate/sourceRate
                        ▼
┌──────────────────────────────────────────────────────────────────┐
│  A LA FRECUENCIA DEL HOST                                        │
│    × 0.5 (scaling fijo)                                          │
│    tremolo — dos senos en contrafase (L/R), por muestra          │
│    juce::dsp IIR peak filter: 350 Hz, Q 0.2, +8 dB               │
│      "tuned by ear listening to the MKS-20"                      │
└───────────────────────┬──────────────────────────────────────────┘
                        ▼
                  buffer de salida del host
```

Detalles con consecuencias:

- **El estéreo es falso hasta el chorus.** El emulador es mono (`s32`); L y R se duplican y solo se
  separan al pasar por SpaceD, que sí tiene taps distintos por canal. Con el chorus desactivado la
  salida es mono duplicado.
- **El tremolo va después del resampler**, a la frecuencia del host, con un contador de fase
  (`tremoloPhase`) que avanza por muestra de salida. Los otros efectos van *antes*, a la del emulador.
- Los desplazamientos `<<5` / `>>6` adaptan el rango del emulador al punto fijo de 24 bits que usan
  los efectos `lsp` — no son ganancia arbitraria.

### 5.3 Efectos `lsp/`

`spaced.cpp`, `phaser.cpp` y `enhancer.cpp` no son DSP escrito a mano: son **transcripciones del
microcódigo del DSP de efectos original**. Se reconoce al instante por la forma:

```cpp
accA = (readMemOffs(120) * 127) >> 7;      // multiplicar-acumular con coeficiente
writeMemOffs(117, clamp_24(accA_1));       // saturación a 24 bits
```

Un acumulador (`accA`/`accB`), una IRAM circular de 128 posiciones direccionada por
`(memOffs + bufferPos) & 0x7f`, y —en SpaceD— una ERAM de 64 K palabras para las líneas de retardo
largas del chorus BBD. Las variables se llaman `accA_0`, `accA_1`, `accA_370`… porque son *pasos del
microprograma*, no conceptos. Las tablas `spaceDRateTable`, `spaceDDepthTable`, `phaserRateTable`,
`phaserDepthTable`, `phaserResonanceTable` son volcados de las tablas de la máquina.

Estado del código:

| Archivo | Estado |
|---|---|
| `spaced.cpp` | Activo — chorus, único efecto por defecto (`chorusEnabled = true`) |
| `phaser.cpp` | Activo — EFX, desactivado por defecto |
| `enhancer.cpp` | **Compilado pero nunca instanciado** en el plugin |
| `reverb.cpp` | **Vacío** (solo el `#include`); los parámetros de reverb están comentados |

Nota de coste: `SpaceD::eram` son `int32_t[0x10000]` = **256 KB por instancia**, y `reset()` lo pone
a cero entero con `memset` — que se llama desde `prepareToPlay()`.

### 5.4 Resampling

Se usa **libresample** de Dominic Mazzoni (basada en `resample-1.7` de Julius O. Smith): C puro,
interpolación sinc con ventana, calidad alta (`resample_open(1, ratio, ratio)`).

Es necesario porque el emulador solo puede correr a 20 000 o 32 000 Hz —son las frecuencias reales de
la máquina y están ligadas al parche—, mientras que el host pide 44 100, 48 000, 96 000…
El plugin gestiona el desfase acumulado con `samplesError`
([PluginProcessor.cpp:400-409](/rdpiano_juce/Source/PluginProcessor.cpp#L400-L409)): cada bloque
calcula cuántas muestras del emulador *debería* haber producido (fraccionario), redondea hacia
arriba, y corrige en bloques posteriores cuando el error acumulado supera un cuarto de bloque.

Los resamplers se reabren cuando cambia cualquiera de las dos frecuencias — es decir, **también al
cambiar de parche**, porque `sourceSampleRate` salta entre 20 k y 32 k
([PluginProcessor.cpp:489-498](/rdpiano_juce/Source/PluginProcessor.cpp#L489-L498)).

> Detalle histórico: el bit del puerto 2 que en la máquina real selecciona la frecuencia
> (`current_sample_rate`, [mcu.cpp:538](/librdpiano/src/mcu.cpp#L538)) está marcado como
> `TODO: Currently not working`. La frecuencia real sale de la tabla `sampleRates[]` del plugin, no
> del emulador.

### 5.5 `PluginEditor` y `Lcd`

La UI es una **reproducción fotográfica del panel**: un PNG de 6140 × 1503 px escalado por 5
(1228 × 300 lógicos), con tamaño fijo (`setResizeLimits` con mínimo == máximo). Los controles son
recortes de un segundo PNG (`interactable.png`) dibujados sobre el fondo; los botones simulan la
pulsación desplazando el recorte 8 px en diagonal y pintan un rectángulo rojo cuando están activos
([PluginEditor.h:40-68](/rdpiano_juce/Source/PluginEditor.h#L40-L68)).

El **dial alfa** es modal, igual que en la máquina real: su efecto depende de qué modo esté activo
(`tuneMode`, `chorusRateMode`, `chorusDepthMode`, `tremoloRateMode`, …). Los botones "params" ciclan
entre modos (rate → depth → ninguno), y `updateValues()` es un `if/else` largo que decide qué
mostrar en el LCD y a qué posición llevar el dial.

`Lcd` ([lcd/Lcd.cpp](/rdpiano_juce/Source/lcd/Lcd.cpp)) emula un display de caracteres 2 × 17 con una
fuente de 5 × 7 píxeles (`lcd_font.h`), dibujando **cada píxel como un `fillRect`** con dos colores
(encendido `0xFF233336`, apagado `0xFF73A5A9`) — 2 × 17 × 35 = 1190 rectángulos por repintado. El
carácter `\xff` se usa como marcador de posición en las barras de nivel (`_______________`).

La sincronización UI ↔ procesador va por `juce::ChangeBroadcaster`: `PluginProcessor` hereda de
`ChangeBroadcaster` y de `AudioProcessorParameter::Listener`, así que cualquier cambio de parámetro
(venga del DAW, de la automatización o de la UI) dispara `sendChangeMessage()` →
`changeListenerCallback()` → `updateValues()` → `repaint()`.

---

## 6. Modelo de concurrencia

Hay **dos hilos** que tocan el emulador, y un solo candado: `juce::SpinLock mcuLock`.

```
  HILO DE AUDIO (tiempo real, el host manda)         HILO DE MENSAJES (UI)
  ────────────────────────────────────────           ──────────────────────
  processBlock()                                      buttonClicked()
    └─ mcuLock.enter()                                  ├─ setCurrentProgram()
       for (renderBufferFrames)                         │    └─ mcuLock.enter()
         ├─ sendMidiCmd()                               │       loadSounds()  ← 1,7-2,7 ms
         ├─ generate_next_sample()                      │       mcuLock.exit()
         ├─ SpaceD::process()                           │
         └─ Phaser::process()                           ├─ setMasterTune()
       mcuLock.exit()                                   │    └─ mcuLock.enter()
    resample_process()                                  │       200 × generate_next_sample()
    tremolo + midEQ                                     │       mcuLock.exit()
                                                        └─ mcuReset()
                                                             └─ 1024 × generate_next_sample()
```

Tres propiedades del diseño que conviene entender antes de tocarlo:

1. **El candado cubre el bucle de render entero**, no muestra a muestra. Un bloque de 512 muestras a
   48 kHz son ~213 muestras del emulador con toda su CPU emulada dentro de la sección crítica.
2. **Es un spinlock.** Cuando el hilo de UI lo toma para hacer `loadSounds()` (que desencripta
   3 × 128 KB de wave ROM + 128 KB de params), el hilo de audio **gira en vacío** hasta que termine.
   Un `juce::SpinLock` no cede el procesador.
3. **El hilo de UI ejecuta el emulador.** No es una llamada a un setter: `setMasterTune` corre 200
   muestras completas de síntesis, y `mcuReset` corre 1024. Es la única forma de hacer avanzar el
   firmware, porque no hay bucle de CPU independiente (§1).

Fuera de `mcuLock` no hay más sincronización: los `juce::AudioParameter*` se leen directamente desde
el hilo de audio (son atómicos internamente), y `SpaceD`/`Phaser` solo los tocan sus respectivos
`process()`.

---

## 7. Ciclo de vida completo de una nota

Poniendo todas las piezas juntas, esto es lo que pasa entre que el usuario pulsa una tecla y sale
sonido:

```
 1. El host entrega  90 3C 64  en el midiBuffer de processBlock()

 2. sendMidiCmd(0x90, 0x3C, 0x64)  →  commands_queue ← [0xC0, 0x3C, 0x64]
                                       (protocolo interno, NO MIDI)

 3. generate_next_sample():
      a) SoundChip::update() produce la muestra de ESTE instante
      b) 100 iteraciones de execute_run():
           - la cola no está vacía → se asierta la línea TIN
           - el firmware entra por el vector ICI (0xFFF6)
           - la rutina en 0xE12B/0xE15E/0xE168 lee el puerto 1
           - read_byte reconoce el PC y sirve 0xC0, luego 0x3C, luego 0x64

 4. El firmware (código Roland de 1986, sin modificar) decide:
      - qué voz libre asignar
      - qué parche/muestra corresponde a la nota 0x3C
      - qué envolvente aplicar según la velocidad 0x64
    y escribe ~40 bytes en 0x1000-0x1FFF

 5. write_byte enruta esas escrituras a SoundChip::write(), que las decodifica
    en (voz, parte, campo) y rellena los SA_Part

 6. En las siguientes llamadas a update(), esos slots ya no cumplen
    (env_value == 0 && env_dest == 0) → se procesan por IC19/IC9/IC8
    y empiezan a contribuir a `result`

 7. Cuando un segmento de envolvente termina, IC19 pone m_irq_triggered,
    execute_run() asierta IRQ1, el firmware atiende, lee m_irq_id para
    saber qué slot fue, y programa el siguiente segmento

 8. Las muestras salen por la cadena de efectos → resampler → host
```

El paso 4 es lo que hace especial a este proyecto: la asignación de voces, el robo de voces, las
curvas de velocidad y el comportamiento del pedal **no están implementados en RdPiano**. Son el
firmware original tomando decisiones.

---

## 8. Lo que es independiente de la plataforma

Casi todo el árbol es portable; lo que sigue no depende de macOS más que por ser la única
plataforma que se compila:

**Código.** `librdpiano/` completo (CPU, chip de sonido, ROMs, LUTs), `lsp/`, `lcd/`, `resample/`,
`PluginProcessor`, `PluginEditor`. No hay un solo `#ifdef _WIN32`/`__APPLE__`/`__linux__` en el
código del proyecto. La única condicional de plataforma en todo el árbol está en la librería de
terceros: `resample_defs.h` incluye `config.h` salvo en `WIN32`/`__CYGWIN__`
([resample_defs.h:17](/librdpiano/src/resample/resample_defs.h#L17)).

**Toolchain.** Hasta la fase 3, los exportadores salían del `.jucer` vía Projucer 8.0.1
(`--resave`); desde entonces, de `juce_add_plugin`. Los mismos módulos JUCE, el mismo C++20 y el
mismo `librdpiano/include`, ahora como usage requirement del target del núcleo.

**Recursos.** Las ROMs y los PNGs se empotran como `BinaryData` en el binario: no hay archivos
externos que instalar ni rutas que resolver en tiempo de ejecución. El plugin es autocontenido.

**Flujo de audio.** El host llama a `processBlock`, el emulador corre a 20/32 kHz, libresample
adapta a la frecuencia del host. Ninguna parte del pipeline usa SIMD, APIs de sistema ni aceleración
específica de plataforma — todo es C++ escalar y `juce::dsp` portable.

**Estado y persistencia.** `getStateInformation`/`setStateInformation` serializan a XML: desde la
fase 3, el árbol del `AudioProcessorValueTreeState` con los diez parámetros, más `masterTune` y
`currentPatch` como propiedades de la raíz `<RdPiano>` — los mismos nombres de atributo de antes,
para que las sesiones guardadas se sigan abriendo.

**CI.** El job de [`.github/workflows/main.yml`](.github/workflows/main.yml) sigue la receta
[`download-juce.sh`](/scripts/download-juce.sh) → `build-osx.sh` → subir artefactos. En
`master`, un segundo job publica una release rodante con tag `latest`.

---

## 9. Plataforma: macOS

> **Actualizado en la fase 3** (REFACTORIZACION §16.3). El Projucer y el `.jucer` ya no existen: el
> plugin sale de `juce_add_plugin` en
> [rdpiano_juce/CMakeLists.txt](/rdpiano_juce/CMakeLists.txt), colgando del
> [`CMakeLists.txt`](/CMakeLists.txt) de la raíz que construye también el núcleo y las pruebas.
> Lo que sigue valiendo de este apartado son las particularidades de la plataforma —AU/AUv3, firma,
> cuarentena, rutas de instalación, bundles— y los identificadores, que se conservaron uno a uno.
> Lo que cambia:
>
> ```
> build-osx.sh
>   └─ cmake -S . -B build/plugin -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
>   └─ cmake --build build/plugin --config Release --target rdpiano_juce_All
>
> Artefactos → build/plugin/rdpiano_juce/rdpiano_juce_artefacts/Release/
>   AU/rdpiano_juce.component      VST3/rdpiano_juce.vst3
>   Standalone/rdpiano_juce.app    AUv3/rdpiano_juce.appex
>   LV2/rdpiano_juce.lv2           (antes RdPiano.lv2; el URI no cambia)
> ```
>
> El generador es Xcode porque `juce_add_plugin` sólo crea el objetivo AUv3 con ese generador, y el
> objetivo de despliegue se fija en 10.13 —el mismo que ponía el Projucer— porque JUCE 8.0.1 llama a
> `CGWindowListCreateImage`, no disponible en el SDK de macOS 15+. El CI corre en `macos-15` y el
> binario es universal arm64+x86_64.

El proyecto compila **solo para macOS**. Los exportadores de Windows (`VS2022`) y Linux
(`LINUX_MAKE`), sus scripts de build y sus jobs de CI se eliminaron del `.jucer`, de
`rdpiano_juce/build/` y del workflow.

Produce los cuatro formatos declarados en el `.jucer`.

```
build-osx.sh
  └─ JUCE/Projucer.app/Contents/MacOS/Projucer --resave rdpiano_juce.jucer
  └─ cd Builds/MacOSX && xcodebuild -configuration Release

Artefactos → Builds/MacOSX/build/Release/
  rdpiano_juce.component   (AU  — Logic Pro, GarageBand, MainStage)
  rdpiano_juce.vst3        (VST3 — Live, Reaper, Bitwig, Cubase)
  rdpiano_juce.app         (Standalone, CoreAudio + CoreMIDI)
  AUv3                     (declarado en el .jucer; el CI no lo empaqueta)
```

**Particularidades reales de esta plataforma:**

- **AU y AUv3 solo existen aquí.** Son formatos de Apple; el `pluginCode="RDPN"` y
  `pluginManufacturerCode="GlZs"` del `.jucer` son los identificadores de cuatro caracteres que exige
  el registro de Audio Units.
- **Firma y cuarentena.** Los binarios del CI **no están firmados ni notarizados**. Gatekeeper los
  bloquea. El README documenta el remedio:
  `sudo xattr -rd com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/<plugin>.component`.
  Es el punto de fricción número uno para usuarios de macOS.
- **El CI usa `macos-13` (Intel).** El artefacto que se publica es x86_64 (o universal, según lo que
  decida Xcode por defecto en ese runner); en Apple Silicon corre bajo Rosetta salvo que el host lo
  cargue en modo Intel. Compilando localmente en un Mac ARM sí se obtiene un binario nativo.
- **Rutas de instalación:** `~/Library/Audio/Plug-Ins/Components/` (AU) y
  `~/Library/Audio/Plug-Ins/VST3/` (VST3).
- **Empaquetado:** los tres artefactos son *bundles* (directorios), por eso el CI los comprime con
  `zip -r` antes de subirlos — subir un directorio sin comprimir rompería los permisos y los enlaces
  simbólicos internos del bundle.
- El exportador `XCODE_IPHONE` (`Builds/iOS`) existe en el `.jucer` pero **no está en el CI**: es
  para AUv3 en iOS/iPadOS, sin soporte activo.

**Local y CI usan el mismo script.** [`download-juce.sh`](/scripts/download-juce.sh) descarga
el ZIP de release de macOS —que ya trae módulos *y* Projucer precompilado— y lo descomprime en
`build/juce`. Antes había dos pasos (`git clone` del repo + `download-projucer.sh` para bajar además el
ZIP): descargaban lo mismo dos veces, porque el ZIP de release es un superconjunto del repo.

> **Actualizado.** El árbol descargado vivía en `rdpiano_juce/JUCE` —donde el `.jucer` buscaba los
> módulos (`MODULEPATH path="./JUCE/modules"`) y `build-osx.sh` el Projucer—. Sin `.jucer`, esa
> ruta ya no la exige nadie: JUCE es una descarga, no fuente del proyecto, y se guarda con el resto
> de lo generado bajo `build/` (`build/juce`, `build/plugin`, `build/core`, `build/core-asan`), de
> modo que `rm -rf build` sea la limpieza completa. La ruta es la variable de caché
> `RDPIANO_JUCE_DIR` del [`CMakeLists.txt`](/CMakeLists.txt) de la raíz.

---

## 10. Build y CI

> **Actualizado en la fase 3.** Ya no hay dos sistemas: `CMakeLists.txt` en la raíz →
> `librdpiano/` (librería, harness, suite unitaria, standalone SDL) + `rdpiano_juce/` (los cinco
> formatos y `rdpiano_plugin_tests`). El plugin **enlaza** el target `librdpiano` en vez de listar y
> recompilar sus fuentes, así que añadir un `.cpp` al núcleo es una línea en
> `librdpiano/CMakeLists.txt` y nada más. `librdpiano/` se sigue pudiendo configurar por su cuenta
> —es lo que hace el CI del núcleo, que no necesita JUCE— y sólo entonces fuerza ASan.
>
> Tampoco es cierto ya lo de "no hay tests automatizados": desde las fases 0-3 hay 38 suites y 427
> comprobaciones de núcleo, el harness bit-exacto contra `golden.txt`, y 5 suites y 95
> comprobaciones del plugin. La tabla de riesgo de más abajo sigue valiendo para el **timbre**, que
> es lo único que ninguna prueba juzga.

**El `.jucer` era la fuente de verdad, no CMake.** Projucer genera los proyectos nativos a partir de
él, así que **añadir un archivo fuente o un recurso exige editar `rdpiano_juce.jucer`**, no basta con
ponerlo en el disco. Un `.cpp` nuevo en `Source/` que no aparezca en el `<MAINGROUP>` sencillamente no
se compila, sin error ni aviso.

`librdpiano` sí tiene su propio [`CMakeLists.txt`](/librdpiano/CMakeLists.txt), pero **solo para
desarrollo del núcleo**: construye la librería y el standalone de SDL, requiere SDL2 + portmidi, y
fuerza `-fsanitize=address` de forma incondicional (intencional: es un banco de pruebas, no un
artefacto de distribución).

Ese standalone ([test/standalone.cpp](/librdpiano/test/standalone.cpp)) es el único "entorno de
prueba" del proyecto: crea un puerto MIDI virtual con `Pm_CreateVirtualInput("RdPiano", ...)`, abre
SDL a 20 kHz y llena el buffer de audio llamando a `generate_next_sample()` directamente desde el
callback. Carga las ROMs con `fopen` desde el directorio de trabajo, así que hay que ejecutarlo desde
donde estén los `.BIN`.

**Verificación.** No hay tests automatizados de ningún tipo. La verificación es **auditiva**. Esto
tiene una consecuencia directa sobre el riesgo de cada zona del código:

| Zona | Riesgo de tocarla |
|---|---|
| `sound_chip.cpp` (IC19/IC9/IC8, LUTs) | **Muy alto** — un bit mal y el timbre cambia de forma sutil e imposible de detectar sin comparar de oído con una máquina real |
| `UNSCRAMBLE_*`, `load_samples` | **Muy alto** — ruido o silencio inmediato, o peor: sonido casi correcto |
| Direcciones del handshake (`0xE12B`…) | **Muy alto** — el firmware deja de recibir comandos |
| `PluginProcessor` (buffers, resample, hilos) | Medio — los fallos son audibles (clicks, silencio) y reproducibles |
| `PluginEditor`, `Lcd` | Bajo — visual, sin efecto sobre el audio |

---

## 11. Material de ingeniería inversa (`re_stuff/`)

**No se compila y no forma parte del producto.** Es el rastro del proceso que hizo posible el
emulador, conservado por valor documental:

| Directorio | Contenido |
|---|---|
| `disasm/` | Desensamblados de los firmwares (RD200 A/B, MK80 B, MKS20 B). Aquí es donde se localizaron las direcciones `0xE12B`/`0xE15E`/`0xE168`/`0xE15A` del handshake |
| `verilog/` | Modelos Verilog de IC8/IC9/IC19 + testbenches. Su propio README advierte: *"probably most of them wrong"* — material de investigación, **no fuente de verdad** |
| `luts/` | Scripts para caracterizar las ROMs LUT internas (IC10, IC11). El README documenta el hallazgo clave: ambas se reproducen perfectamente con una exponencial, mientras que las cuatro LUTs implementadas con puertas lógicas dentro de los gate arrays solo se pueden evaluar bit a bit — que es exactamente lo que hace el constructor de `SoundChip` |
| `silicon_tooling/` | Cadena JS/Python para extraer celdas de fotos del silicio y convertirlas a Verilog/KiCad/ELK |
| `ident_cells/` | Recortes fotográficos de celdas estándar Fujitsu, clasificados por tipo |
| `rom_tools/` | Utilidades de desencriptado de ROMs (`descramble.c`, `descramble_wave.py`) — versiones sueltas de lo que hoy vive en los macros `UNSCRAMBLE_*` |

La relación práctica con el código de producción: `re_stuff/luts/` explica **por qué** las tablas de
`sound_chip.cpp` son expresiones lógicas y no fórmulas, y `re_stuff/disasm/` es el sitio donde hay que
volver si alguna vez se quiere soportar otro firmware.

---

## 12. Puntos frágiles de la arquitectura

Esto no es la lista de bugs (esa está en [AUDITORIA.md](docs/AUDITORIA.md)), sino las decisiones de
diseño que condicionan cualquier trabajo futuro:

1. **Un solo firmware soportado.** El handshake por direcciones absolutas ata el proyecto a
   `RD200_B.bin`. Los dumps del MKS-20 y del MK-80 están en `roms/` y empotrados en el plugin, pero
   inertes.
   Soportar otro firmware = volver al desensamblado. Una alternativa estructural sería emular de
   verdad el protocolo de los puertos en vez de reconocer PCs, pero eso exige entender el handshake
   completo, no solo dónde lee el firmware.

2. **El hilo de UI ejecuta el emulador.** Es inevitable dado que no hay bucle de CPU independiente,
   pero convierte cualquier acción de la UI (cambiar de parche, afinar) en trabajo pesado bajo un
   spinlock compartido con el hilo de audio.

3. **El acoplamiento entre parche y frecuencia de muestreo.** Cambiar de parche puede cambiar
   `sourceSampleRate`, lo que obliga a reabrir los resamplers. Mientras `current_sample_rate` (el bit
   real del puerto 2) no funcione, esta información vive duplicada: en la tabla del plugin y,
   teóricamente, en el emulador.

4. **Los dos hacks de `SoundChip::update()`** son parches sobre síntomas de un modelo que aún no es
   del todo correcto. El propio código dice `investigate`. El atajo de rendimiento, además, tiene
   efecto funcional: decide qué slots se procesan.

5. **La ausencia total de tests** sobre código donde un bit invertido produce un cambio tímbrico
   sutil. Sería viable —y de alto valor— una prueba de regresión que renderice una secuencia MIDI
   fija y compare el hash del PCM resultante: no valida corrección, pero detecta cualquier cambio
   involuntario en el camino de señal.

6. **Huella de memoria por instancia.** `Mcu` tiene `params_rom[0x20000]` + `params_rom_tmp[0x20000]` +
   `ram[0x10000]` (de la que se usan 4 KB) = 328 KB; `SoundChip` añade ~1,06 MB de tablas y muestras,
   y `SpaceD::eram` otros 256 KB. Total ≈ 1,7 MB de estado residente por instancia. Cada
   instancia del plugin paga ese coste íntegro; instanciarlo 8 veces en un proyecto no es gratis.

---

*Documento generado a partir del análisis del código en `main` @ `995e067`. Los enlaces a
`archivo:línea` corresponden a ese estado del árbol.*
