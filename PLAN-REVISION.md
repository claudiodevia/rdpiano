# Plan de revisión por capas

**Fecha:** 2026-10-14, `develop` @ `919aa06`. Los números de línea son de ese commit y se moverán al aplicar
cambios: buscar por el símbolo, no por la línea.

**Qué es:** el análisis de cohesión, acoplamiento, refactorizaciones, Doxygen y comentarios interlineados de
todo el proyecto, con las reglas de [CLAUDE.md](CLAUDE.md), y el plan para aplicarlo **una capa por pasada**.
Revisar todo a la vez deja la revisión en la superficie; cada pasada de abajo tiene su alcance cerrado, sus
hallazgos, sus lotes de cambios y su verificación.

**Relación con [docs/REVISION-CODIGO.md](docs/REVISION-CODIGO.md):** lo que ya figura allí (§1 IRQ por
instrucción, §2 comentarios del emulador, §3 `--bench` y trampa 3) **no se repite aquí**; sigue abierto y se cita por
su sección. Al cerrar cada pasada, lo que quede pendiente de ella se pasa a REVISION-CODIGO.md y su sección de
este archivo se borra: este plan es temporal, aquel es la fuente única de lo pendiente.

**Fuera de alcance en todas las pasadas:** `mcu_ops.h`, `mame_utils.h`, `lsp/`, `resample/`, `re_stuff/`
(trampa 5 y exceptuados de formato).

---

## Orden de las pasadas

| # | Pasada | Por qué en este orden | Riesgo de audio |
|---|---|---|---|
| 1 | Pruebas y scripts — **hecha** (2026-10-14; lo que quedó, en REVISION-CODIGO.md §3.2 y §3.3) | Pone la red que necesitan las demás: hash del motor (`engine_golden`), suite de `Mcu` (`test_mcu.cpp`), `board_select_patch_page`. | Ninguno |
| 2 | Plugin — **hecha** (2026-10-14; lo que quedó, en REVISION-CODIGO.md §4) | Independiente del núcleo; tiene fallos reales y locales. | Ninguno (sí de comportamiento ante el host: gestos) |
| 3 | Motor — **hecha** (2026-10-14; lo que quedó, en REVISION-CODIGO.md §5) | El refactor grande (partir `RdPianoEngine`); exige el hash `engine_golden` de la pasada 1. | Bajo, con `engine_golden` |
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
  orden. La pasada 3 añadió a `Mcu` lo que usa el motor (`noteOn`/`noteOff`/`sustain`, `selectWaveSet`,
  `activatePatch`, `setSampleRate`/`nextSample`): si se hace `RdMachine`, pasan ahí.
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
   `part_contribution()` compartido con `test_sound_chip_blocks` (cierra REVISION-CODIGO.md §3.2). Golden y
   vectores deben quedar idénticos.
7. **ALTO RIESGO**: `RdBoard::pollLines()` junto con REVISION §1, medido con `--bench` (§3.1).
8. Handshake RD200 como datos (`FirmwareHandshake`).
9. `RdMachine` y nombres (camelCase, `SaPart`, `ic5`): cambia la API de todo el núcleo; solo si se decide.
10. Doxygen y comentarios restantes. Actualizar ARQUITECTURA (clases y mapa de memoria) y la trampa 3.

**Diferido:** `SaVoiceFlags` por voz (obliga a editar los vectores y toca el bucle caliente).
