# Plan de revisión por capas

**Fecha:** 2026-10-14, `develop` @ `919aa06`. Los números de línea son de ese commit y se moverán al aplicar
cambios: buscar por el símbolo, no por la línea.

**Qué es:** el análisis de cohesión, acoplamiento, refactorizaciones, Doxygen y comentarios interlineados de
todo el proyecto, con las reglas de [CLAUDE.md](CLAUDE.md), y el plan para aplicarlo **una capa por pasada**.
Revisar todo a la vez deja la revisión en la superficie; cada pasada de abajo tiene su alcance cerrado, sus
hallazgos, sus lotes de cambios y su verificación.

**Relación con [docs/REVISION-CODIGO.md](docs/REVISION-CODIGO.md):** lo que ya figura allí (§1 IRQ por
instrucción, §2 motor, §3 `--bench` y suite de `RdBiquad`) **no se repite aquí**; sigue abierto y se cita por
su sección. Al cerrar cada pasada, lo que quede pendiente de ella se pasa a REVISION-CODIGO.md y su sección de
este archivo se borra: este plan es temporal, aquel es la fuente única de lo pendiente.

**Fuera de alcance en todas las pasadas:** `mcu_ops.h`, `mame_utils.h`, `lsp/`, `resample/`, `re_stuff/`
(trampa 5 y exceptuados de formato).

---

## Orden de las pasadas

| # | Pasada | Por qué en este orden | Riesgo de audio |
|---|---|---|---|
| 1 | Pruebas y scripts — **hecha** (2026-10-14; lo que quedó, en REVISION-CODIGO.md §3.3 y §3.4) | Pone la red que necesitan las demás: hash del motor (`engine_golden`), suite de `Mcu` (`test_mcu.cpp`), `board_select_patch_page`. | Ninguno |
| 2 | Plugin — **hecha** (2026-10-14; lo que quedó, en REVISION-CODIGO.md §4) | Independiente del núcleo; tiene fallos reales y locales. | Ninguno (sí de comportamiento ante el host: gestos) |
| 3 | [Motor](#pasada-3--motor-rd_engine-patchesh) | El refactor grande (partir `RdPianoEngine`); exige el hash `engine_golden` de la pasada 1. | Bajo, con `engine_golden` |
| 4 | [Emulador](#pasada-4--emulador-mcu-rd_board-command_port-sound_chip-sa_-rom_loader) | La de más riesgo tímbrico; va la última, con todo lo demás estable. | **Alto** en los lotes marcados |

### Reglas comunes a todas las pasadas

1. **Prueba antes que refactor**: si un cambio necesita red, la prueba se escribe primero y pasa sin editarla
   durante el resto de la pasada.
2. **Un lote = un cambio aislado** que el usuario revisa y commitea (Claude no commitea). Formato con
   `.clang-format` solo de lo tocado; el formateo en masa, en lote propio.
3. **Golden y vectores no se regeneran para poner algo en verde.** Si un lote los mueve sin ser un cambio de
   audio intencionado, se revierte.
4. Cierre de cada lote: `sh scripts/verificar.sh rapido`; cierre de la pasada: `sh scripts/verificar.sh`
   completo en verde.
5. Al cambiar una decisión o una cifra, actualizar CLAUDE.md y ARQUITECTURA a la vez.

### Plantilla de encargo para cada pasada

```text
Pasada N de PLAN-REVISION.md: <capa>. Alcance cerrado a <archivos>; no toques nada fuera de él.
Antes de cambiar nada, comprueba en el código cada hallazgo de esa sección (las líneas son de 919aa06)
y descarta los que ya no apliquen. Aplica los lotes en orden, uno por vez, con verificar.sh rapido
al final de cada uno y verificar.sh completo al final de la pasada. Lo marcado ALTO RIESGO TÍMBRICO
no lo apliques: avísame. No hagas commit. Al terminar, mueve lo pendiente a docs/REVISION-CODIGO.md,
borra la sección de la pasada de este archivo y dime qué tocaste, lote por lote.
```

---

## Pasada 3 — Motor (`rd_engine.*`, `patches.h`)

**Alcance:** `librdpiano/include/rd_engine.h`, `librdpiano/src/rd_engine.cpp`, `librdpiano/include/patches.h`;
las firmas nuevas de `Mcu` que pida la partición, sin tocar su implementación interna.
**Prerrequisito:** `engine_golden` (pasada 1). Sin él, ningún cambio de orden en coma flotante se detecta.
**Abierto en REVISION-CODIGO.md §2** (no se repite): `inUsed` compartido, `resamplerOpens += 2`,
`dryL`/`dryR`, `powf` doble, `offsets_in_range`, `RdEngineStats` no atómico. Se resuelven dentro de los lotes.

### Fallos

| Dónde | Problema | Arreglo |
|---|---|---|
| `rd_engine.cpp:652` | **Comprobado.** `renderBufferFrames > 20000` sobra frente a `emuCapacity`, que puede pasar de 20000: parche de 32 kHz con host a 22,05 kHz y bloque de 16384 (o 44,1 kHz y 32768) pide 23 778 muestras → bloque abortado en silencio y `tooManyFrames++` aunque el búfer cabe. | Quitar `> 20000`; caso nuevo en `engine_edge_blocks`/`engine_host_rates`. Lote propio. |
| `rd_engine.cpp:614-618` | **Comprobado.** `abortBlock` vacía la cola MIDI: un note-off o pedal arriba de ese bloque se pierde; la nota queda en firmware y espejo, y el siguiente cambio de parche la resucita. | Entregar la cola por `sendTracked` también al abortar (la `CommandQueue` es acotada y cuenta lo que descarta); corregir su doc (:606-608). |
| `rd_engine.h:39` | **Comprobado.** El contrato de hilo de `RdEngineParams` es falso: dice "el hilo de UI escribe", y el plugin escribe en `processBlock` (`syncParamsToEngine`, `PluginProcessor.cpp:172, 228`). Quien siga el doc tendrá una carrera. | "Se escribe en el hilo de render(), entre bloques". |
| `rd_engine.h:259-271` | `pushMidi` se anuncia "RT-safe" pero no es seguro entre hilos (`midiCount` es `int`). | "Llamar desde el hilo de render(), antes de render()". |

### Cohesión: partición de `RdPianoEngine`

Hoy (`rd_engine.h:288-407`) tiene unos 45 campos privados y ocho responsabilidades. Propuesta: structs internos
en `librdpiano/src/engine/` (por valor; `render()` sigue sin reservas; las atómicas solo en `ChangeScheduler`).

| Pieza | Campos | Métodos |
|---|---|---|
| `PatchBank` | `romSets`, `paramPages`, bucle de descifrado del constructor | `page(p)`, `activate(Mcu&, from, to)` (select + page + reload) |
| `MidiQueue` | `midiQueue`, `midiCount`, `kMidiQueueSize` | `push() → bool`, `clear()`, `size()`, `operator[]` |
| `HeldNotes` | `heldVelocity[128]`, `sustainDown` | `track() → bool noteOn`, `isHeld(n)`, `clear()`, `restore(Mcu&, deltaVel)` |
| `RetriggerMeter` | `levelSq`, `onsetSq`, `levelSmooth`, `onsetHold`, `kLevelTauMs`, `kOnsetWindowMs`, `kDbPerVelocityUnit`, `kMaxRetriggerAttenDb` | `setSourceRate`, `reset`, `startOnset`, `feed(s32)` inline, `velocityDrop()` |
| `ChangeScheduler` | `patchRequest`, `tuneRequest`, `latest*`, `settle*`, `declick*`, `kNoTuneRequest`, `kChangeFade*`, `kPatchSettleMs` | `request`, `requestTune`, `prepare(hostRate)`, `takePending`, `cancelPatch`, `cancelTune`, `service(…) → bool`, `finish(retriggered)`, `nextGain()` inline |
| `EffectsChain` | `spaceD`, `phaser`, `chorusMix`, `efxMix`, `effectMixStep`, `chorusRateToMsPeriod`, `kEmu*Shift` | `reset`, `setSourceRate`, `snapTo`, `beginBlock`, `process(s32, s32&, s32&)` inline |
| `RateConverter` | `resampleL/R`, `emuL/R`, `outL/R`, capacidades, `samplesError`, `latencyFrames`, `kWorstSourceRate` | `open`, `close`, `plan() → emuFrames`, `process`, `latency()` (resuelve `inUsedL/R` y `resamplerOpens`) |
| `OutputStage` | `outputGainSmoothed`, `tremoloPhase`, `MidEq` | `prepare`, `process(…)` conservando `out * gain * declick` en ese orden |

En `RdPianoEngine` queda la orquestación: `prepare`, `release`, `render`, `applyPatch`, `applyMasterTune`,
`finishChange` (afinar primero, cambiar después), `sendTracked`, `pushMidi`.

Acoplamiento con el emulador que la partición corrige:

- `rd_engine.cpp:166-175` (+ `rd_board.cpp:16`, `sound_chip.cpp:20`): la ranura 0 se descifra **tres veces**
  al construir (~5,8 ms de ~9). Empezar el bucle en `s=1` con
  `static_assert(ROMSET_COUNT <= SoundChip::NUM_WAVE_SLOTS)`, sin cambiar el camino del harness.
- `rd_engine.cpp:185, 339`: `selectRomSet(slot, ic18)` pasa una ROM de params que el motor nunca usa →
  `Mcu::selectWaveSet(slot)`.
- `rd_engine.cpp:335-344, 177-180`: el motor arma `selectRomSet` → `selectPatchPage` → `reloadPatch()` y
  conoce el orden obligatorio → `Mcu::activatePatch(slot, page, offset)`.
- `rd_engine.cpp:673, 713`: `mode32khz = sourceRate == 32000` traduce la tasa a "62 o 100" →
  `Mcu::setSampleRate(hz)`; el `false` de `boot` sigue visible (trampa 7).
- `rd_engine.cpp:368-385` frente a `mcu.cpp:553-580`: dos decodificadores MIDI que deben coincidir;
  `restoreHeldNotes` emite MIDI crudo (:417, :439) → `Mcu::noteOn/noteOff/sustain`; `sendMidiCmd` queda para
  e2e.
- `rd_engine.cpp:377-379`: `trackMidi` abre además la ventana de ataque del medidor → devolver "es note-on" y
  que el motor llame a `meter.startOnset()`.
- Opcional, último: pimpl para sacar `lsp/` y `RdBiquad` de la cabecera pública (`rd_engine.h:22-23, 294-297`).

### Refactorizaciones

- `prepare()` 111 líneas (:219-329) → `openResamplers`, `absorbPendingRequests` (con el comentario de la
  trampa 8 como su doc), `resetChangeState`, `resetDsp`.
- `synthesise()` 112 líneas (:671-782) → `effects.beginBlock`, `meter.feed`, efecto inline copiando las
  expresiones literalmente. **Solo con `engine_golden`.**
- Duplicados: `effectMixStep` y `levelSmooth` (:290/347, :307/351) → `onSourceRateChanged(rate)`; vaciado del
  espejo (:303-305, :556-558) → `clearHeldNotes()`; `kOutputScaling * patchOutputGain[currentPatch]`
  (:317, :838) → `patchGainTarget()`; `(int)(ms * 0.001f * rate)` (:297, :378) → `ms_to_frames()` en `float`;
  `clamp_index`/`clamp_step` (:94-120) → `std::clamp`; el bucle de limpieza de `resampleBlock` (:796-800)
  repite `silence()`.
- `static` sueltos (:15-132), pi dos veces (:47, :50) → espacio anónimo, `constexpr`, `kTwoPi`.
- Tasas cableadas (:92, 230, 247-248, 673) → `kMinPatchRate`/`kMaxPatchRate` en `patches.h`.
- `spaceD.audioOutL/R` como acumulador de toda la cadena (:744-776) → locales `s32 outL, outR` (verificado:
  `process()` solo escribe `audioOut`).
- Números sin nombre, **solo nombrar, no cambiar**: `/4` de `spaceDRateFromMs(1000.0f / periodMs / 4.0f)`
  (:681; el argumento son Hz/4, no ms), `/15.0f` del chorus frente a `/14.0f` del trémolo (:682, :829), `0x7f`
  de tabla (:684-685 → `std::size(…) - 1`), nibbles MIDI, `64` del CC del pedal, `127` (:370-384, 396-397,
  417, 439, 570).
- Deriva calculada en `framesForBlock` y acumulada en `resampleBlock` por un parámetro de salida (:626-661,
  :790-814, :896-905) → `RateConverter::plan()`/`commit()`.
- `prepareRomSetFor` (`rd_engine.h:161-170`, `.cpp:333`) es un no-op que el plugin no usa → quitar la API y
  reescribir `engine_patch_prepare` (lote propio: edita una prueba).
- `patches.h:138` `WAVE_ROM_SIZE` duplica `WAVE_ROM_BYTES` (`rom_loader.h:44`) y se usa para cargar la IC18
  (`test_engine.cpp:96`, `e2e.cpp:196, 409`, `test_board.cpp:156`) → `PARAMS_ROM_BYTES`; cierra la entrada
  `offsets_in_range` de §2.

### Doxygen

- Contratos de hilo incompletos: `activePatch()`, `activeMasterTune()` y getters (`rd_engine.h:189-190,
  229-230, 281-283`) → "solo en el hilo de render() o con el motor parado"; `sourceSampleRate()` sin doc (cambia
  con el parche); `requestPatch` (:193-201) omite la ventana de 150 ms.
- Campos públicos sin `///<`: `rd_engine.h:47, 51, 55, 64-66, 77-78, 285-286` (`params` y `stats`, justo los que
  llevan contrato de hilo); "en Hz/2" (:52) → "Hz = valor / 2".
- Docs de privados en la definición del `.cpp` → a la declaración: `finishChange` :354-361,
  `restoreHeldNotes` :403-413, `serviceRequests` :474-477, `abortBlock` :603-613, `framesForBlock` :620-625,
  `synthesise` :663-670, `resampleBlock` :784-789, `outputStage` :816-821. Los `///` de grupo
  (`rd_engine.h:321-335, 343, 354, 365, 372, 377, 389, 394`) se pegan al primer miembro → `///@{ … ///@}` o
  `///<`.
- Narran: `prepareRomSetFor` (:161-170), `declick_shape` (cpp:106-109), `serviceRequests` (cpp:474),
  `rd_engine.h:398-399`. El coste de `resample_open` está en `rd_engine.h:125-127` y `cpp:243-246`: un sitio.
- Helpers sin doc: `clamp_index`, `clamp_step`, `silence` (:94, :113, :594).

### Comentarios interlineados

- Historia: :502-504, :563-569 (duplica `rd_engine.h:262-264`), :675-680 (basta "Deliberado: lo fija
  `engine_lfo_rate`"), :728-733, :824-828.
- Duplican la cabecera o `patches.h`: :695-698, :715-718, :833-837, :841-844, :450 y :543.
- Desaparecen al nombrar helpers: :288-289, 301-302, 312-313, 349-350, 377, 554-555, 877-878, 887-888, 908-910.
- Trampa 14 (:389-395) → "Trampa 14 de CLAUDE.md; lo fija `engine_repeated_note`".

### Lotes

0. Confirmar que `engine_golden` (pasada 1) existe y pasa. Debe pasar **sin editarlo** en todos los lotes.
1. Fallos de doc de hilos (`RdEngineParams`, `pushMidi`, getters).
2. Mecánicos: espacio anónimo, `constexpr`, helpers deduplicados, nombres de constantes, locales en vez de
   `spaceD.audioOut*`, las seis entradas de REVISION §2.
3. `RdBiquad` + `MidEq` a `rd_biquad.h/.cpp` y la suite pendiente de REVISION §3.2.
4. `MidiQueue`.
5. `HeldNotes` + `RetriggerMeter` (separar `startOnset` de `trackMidi`).
6. `ChangeScheduler`.
7. `RateConverter` (resuelve `inUsed` y `resamplerOpens`). En lote aparte: quitar el tope 20000 con su caso
   de prueba nuevo.
8. `EffectsChain`.
9. `OutputStage`.
10. `PatchBank` + `Mcu::activatePatch`/`selectWaveSet`/`setSampleRate`/`noteOn…`; descifrado triple. **e2e
    sin moverse.**
11. `abortBlock` entrega MIDI; quitar `prepareRomSetFor` y rehacer `engine_patch_prepare`.
12. Doxygen y comentarios restantes. Actualizar ARQUITECTURA (diagrama de clases del motor).
13. Opcional: pimpl.

**Diferido:** unificar el volumen (aplicado a tasa del emulador, `cpp:775-777`) con la ganancia de parche
(tras el remuestreador, `cpp:853-855`). No es bit a bit igual: solo con escucha y `engine_golden` rebaselinado
a propósito. Recomendación: dejarlo como está.

---

## Pasada 4 — Emulador (`mcu`, `rd_board`, `command_port`, `sound_chip`, `sa_*`, `rom_loader`)

**Alcance:** `librdpiano/include/{mcu,rd_board,command_port,sound_chip,sa_blocks,sa_tables,rom_loader,rd_trace}.h`
y sus `.cpp`.
**Prerrequisito:** `test_mcu.cpp` (pasada 1). **Abierto en REVISION-CODIGO.md** (no se repite): §1
`execute_run`, §2 magic static de `sa_tables()` y `SoundChip::read()`.
**Bien:** ningún byte del bus fuera de `command_port.h`; `stdio` solo en `rd_trace.cpp` bajo `RDPIANO_TRACE`.

> **ALTO RIESGO TÍMBRICO** en los lotes 6 y 7: tocan el bucle caliente de `sound_chip`/`sa_blocks`. Si mueven
> el golden, es cambio de audio: avisar, escuchar con `--wav-dir` y solo entonces decidir.

### Fallos de documentación (el código está bien; el doc engaña)

| Dónde | Problema |
|---|---|
| `mcu.h:44` | **Comprobado.** `execute_run` "ejecuta instrucciones hasta que toca la siguiente muestra": ejecuta **una** instrucción tras mirar las líneas. |
| `mcu.h:49`, `mcu.cpp:544` | "62 ciclos por muestra" y "2000kHz CPU clock": son **instrucciones** (CLAUDE.md). |
| `mcu.h:25`, `rd_board.h:82` | "1,2 MB de estado" / "casi 800 KB": medido con `sizeof`, `Mcu` = 149 656 B, `RdBoard` = 149 600 B, `SoundChip` = 5 160 B (ondas en el montón). |
| `rd_board.h:114` | **Comprobado.** `loadRomSet` "mapea la página de un parche": no llama a `selectPatch` (`rd_board.cpp:31-35`). |
| `rd_board.h:133` | "en una ranura que el emulador no lee": `loadRomSet` descifra en la 0, la activa. |
| `sound_chip.h:58-59` | Menciona un "juego de reserva que espera publicación" que no existe. |
| `mcu.cpp` | **Comprobado.** Sin la cabecera `// license:BSD-3-Clause` / `copyright-holders`, aunque contiene macros, tablas, `check_irq_lines`, `enter_interrupt`, `execute_set_input` y `tcsr_*` de MAME (trampa 5). Añadirla en `mcu.cpp` y probablemente en `mcu.h`. |

### Cohesión y acoplamiento

- `sound_chip.h:93`: `m_irq_triggered` público; lo lee `Mcu` (`mcu.cpp:475`) y lo lee y limpia `RdBoard`
  (`rd_board.h:287-289`) → privado con `irqPending()` y `acknowledgeIrq()` inline, mismo orden.
- `mcu.cpp:472-476` frente a `rd_board.h:269, 287-291`: las líneas se activan en `Mcu` atravesando la placa
  (`board.commandPort().queue().empty()`, `board.soundChip()…`) y se limpian en `RdBoard` →
  `RdBoard::pollLines()` inline con las dos comprobaciones **en el mismo orden**; va junto con REVISION §1 y
  medido con el golden. IRQ como `0` literal (:476, `rd_board.h:290`) → `M6800_IRQ_LINE`.
- `mcu.h:19-159`: `Mcu` es CPU + protocolo con tiempos (`boot`, `setMasterTune`, traducción MIDI) + fachada
  de ROM (6 reenvíos, `mcu.cpp:615-636`) → una `RdMachine` que posea `RdBoard` y `Mcu` con fachada y
  protocolo; `Mcu` solo CPU. Cambia la API (motor, e2e, standalone, pruebas); golden quieto si se conserva el
  orden. **Coordinar con el lote 10 de la pasada 3**, que añade métodos a `Mcu`: si se hace `RdMachine`, esos
  métodos nacen ahí.
- Visibilidad: `op_func`, `execute_one`, `execute_set_input`, `execute_run` y los cuatro `override` de
  `RdBoardCpu` (`mcu.h:32-45, 156-159`) → privados; `loadRomSet`, `selectPatch` (`mcu.h:102, 108`) solo los
  usa `loadSounds` → e2e; `SoundChip::load_samples` (`sound_chip.h:91`) solo el constructor.
- `rd_board.h:205-214, 226`: la placa saca de la cola con `front()`/`pop()` y compara los PC del handshake RD200
  con literales repartidos → `constexpr struct FirmwareHandshake` junto al protocolo y
  `CommandPort::busReadData(pc)`/`busReadStatus(pc)`; la trampa 1 pasa a ser un cambio de datos para MKS-20.
- `rd_board.h:294-298`: cualquier escritura ≥ 0x2000 (incluida la ROM) cambia `latch_val`, y el mapa (:193) dice
  0x2000-0x3FFF → **solo documentarlo** (puede ser el decodificado parcial real; acotarlo movería el golden).
- `command_port.h:55-66, 93`: `dropped()` solo lo leen las pruebas → a `RdEngineStats`.
- `sa_blocks.h:36-37`: `flags_0`/`flags_1` están en cada `SA_Part` pero solo cuentan los de la parte 0 →
  `SaVoiceFlags` por voz. **Alto riesgo** y obliga a editar `test_sound_chip_blocks.cpp`: diferido.

### Refactorizaciones

- Cabecera de página de params duplicada (`rd_board.cpp:43-46` y `rom_loader.cpp:34-37`) y `0x8000` literal
  (`rd_board.cpp:41`, `rom_loader.cpp:32`) → `write_params_header()` y `PARAMS_PAGE_OFFSET` en `rom_loader.h`.
- Permutaciones de la wave ROM en `sound_chip.cpp:154-181` (contradice `rom_loader.h:12-14` y el comentario de
  `sound_chip.cpp:14`) → `wave_table_index(i)` y `decode_wave_entry(b5, b6, b7)` en `rom_loader.h`. Zona "no
  tocar sin verificar audio": bit a bit idéntico o se revierte.
- **Comprobado.** `decode_wave_rom` (`rom_loader.h:58`, `.cpp:14-18`) es código muerto → borrar; usar
  `WAVE_ROM_BYTES` en vez de `0x20000` (`sound_chip.h:117`, `sound_chip.cpp:154`).
- `mcu.cpp:545, 589, 603, 607`: `62`/`100` literales, `cycle` que cuenta instrucciones o muestras, bucle de
  muestras tres veces → `kInstructionsPerSample20k/32k`, `runSamples(n, rate32)`.
- `generate_next_sample(bool sampleRate32 = false)` (`mcu.h:52`): el valor por defecto esconde que
  `setMasterTune` corre a 20 kHz → quitarlo y pasar `false` a la vista.
- Números y tipos: `1024 * 8` (`mcu.cpp:368`) → `kResetWarmupInstructions`; `m_irq_state[5]` (`mcu.h:191`) →
  `M6801_LINE_MAX`; `data1 & 0xF` duplica la máscara de `programChange()` (`mcu.cpp:560`); `uint8_t` frente a
  `u8` (:555); registros `0x08/0x0d/0x0e` con `// tcsr` (:508-537) → `kRegTcsr`, `kRegIcrHigh`, `kRegIcrLow`,
  **sin "corregir" el orden alto/bajo** (MAME, golden); puertos literales (`rd_board.h:202, 220, 250, 255, 261`)
  → `kPort1Data`, `kPort2Control`… sin cambiar el orden de las ramas (la ROM de programa va primero: no tocar).
- `sa_blocks.h:198-223`: suma saturada a 14 bits y lectura con signo de `samples_exp` duplicadas, nombres
  opacos (`tmp_1`, `sign_pa`) → `sat14_add`, `exp_lookup`. **Alto riesgo**: bucle caliente.
- `sa_tables.cpp:12-478`: una función de ~470 líneas con dos mitades → `generate_phase_exp()` y
  `generate_samples_exp()` (lo fija `test_sa_tables` byte a byte). `:47-48`: término siempre falso transcrito
  del silicio → no tocar, `HACK:` de una línea.
- Nombres: camelCase frente a snake_case (`decodeRomSet` / `decode_samples`, `generate_next_sample` /
  `sendMidiCmd`), prefijo `m_` en unas clases sí y en otras no, `SA_Part` frente a `SaTables`, `temp_*` en los
  constructores (son dumps) → camelCase en la API no derivada de MAME, `SaPart`, `ic5`. Cambia la API.
- `env_table`/`addr_table` (`sa_blocks.h:42, 69`): globales con nombres genéricos visibles desde `mcu.h` →
  espacio de nombres o prefijo `sa_`.
- `command_port.cpp:18`: `(int8_t)(floor(...) * 4) & 0xff` → aritmética entera (quita `<math.h>`). **Antes**,
  tabla exhaustiva de igualdad en `test_command_port` (hoy solo comprueba rangos, `:119`): la afinación es audio.
- `rd_trace.cpp:15-16`: dos globales para tres estados → sink por defecto a stderr, `nullptr` = descartar.
- `sound_chip.cpp:4, 7`: `sa_blocks.h` incluido dos veces, `<cmath>` sin usar.

### Doxygen

- Narran: `sound_chip.h:100-103`, `mcu.h:113-114`, `mcu.h:209-211`.
- Falta: campos de `SA_Part` (`sa_blocks.h:28-38`: `flags_0/1`, `pitch_lut_i`, `env_offset`), de `WaveEntry`
  (`sound_chip.h:110-111`); `reset()` (`mcu.h:148-153`) no dice que ejecuta 8192 instrucciones de firmware;
  el mapa de memoria (`rd_board.h:187-195`) es un `//` suelto → al Doxygen de `read()`, con la nota del latch.

### Comentarios interlineados

- `mcu.cpp`: código de MAME comentado (:33-59, :392, :398-399, :454); huérfano :335; historia :479-480; ramas
  `// program change`… (:557-575) que sobran con constantes MIDI.
- `sound_chip.cpp`: :14 (huérfano y falso), :39-40 y :150-151 (historia), :52-53 (basta la mitad del hardware),
  :81 (pregunta en inglés → `TODO:` en español); **:103 y :116 son los hacks de la trampa 3 sin `HACK:` y en
  inglés** → `// HACK: …` en español.
- `sa_blocks.h:150-154` → al `///<` de `Ic9Out::waverom_addr`; `rd_board.h:207, 225` (`HACK:` en inglés);
  `command_port.cpp:12-13` (desaparece con la aritmética entera); `sa_tables.cpp:15-16, 19, 41, 202-203, 206,
  226` (inglés, errata "Exp table to for", un solo `TODO:` por tabla).

### Lotes

1. Docs falsos y licencia MAME en `mcu.cpp`.
2. Sin riesgo y local: `decode_wave_rom` fuera, includes, `WAVE_ROM_BYTES`, constantes con nombre (registros,
   puertos, instrucciones por muestra, `kResetWarmupInstructions`, `M6801_LINE_MAX`, `M6800_IRQ_LINE`),
   `write_params_header`, `generate_next_sample` sin valor por defecto, `dropped()` a stats, `rd_trace`.
3. Encapsular `m_irq_triggered` y visibilidad de `Mcu`/`SoundChip`. Golden quieto.
4. `sa_tables` partida en dos funciones; `HACK:` del término inerte.
5. `command_port` con aritmética entera, **después** de su tabla exhaustiva.
6. **ALTO RIESGO**: permutaciones de la wave ROM a `rom_loader.h`; `sat14_add`/`exp_lookup` en `sa_blocks.h`;
   `part_contribution()` compartido con `test_sound_chip_blocks` (cierra REVISION-CODIGO.md §3.3). Golden y
   vectores deben quedar idénticos.
7. **ALTO RIESGO**: `RdBoard::pollLines()` junto con REVISION §1, medido con `--bench` (§3.1).
8. Handshake RD200 como datos (`FirmwareHandshake`).
9. `RdMachine` y nombres (camelCase, `SaPart`, `ic5`): cambia la API de todo el núcleo; solo si se decide.
10. Doxygen y comentarios restantes. Actualizar ARQUITECTURA (clases y mapa de memoria) y la trampa 3.

**Diferido:** `SaVoiceFlags` por voz (obliga a editar los vectores y toca el bucle caliente).
