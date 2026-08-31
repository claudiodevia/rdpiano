# CLAUDE.md

Emulador hardware de pianos Roland SA (MKS-20 / RD-1000 / Rhodes MK-80): firmware original sobre
CPU HD63701 emulada + chips de síntesis reimplementados gate-level. C++23 (`resample/` en C23).

## Layout

| Ruta | Qué es |
|---|---|
| `librdpiano/` | Núcleo sin dependencias: emulador + `RdPianoEngine` (cadena de audio entera, incl. `lsp/` y `resample/`). La lógica real vive aquí. |
| `rdpiano_juce/` | Plugin JUCE 9.0.1 (VST3/AU/AUv3/LV2/Standalone), solo macOS. |
| `roms/` | Dumps, empotrados como `BinaryData` vía `juce_add_binary_data`. |
| `re_stuff/` | Ingeniería inversa (Verilog, disasm). **No se compila.** |
| `scripts/` | `download-juce.sh`, `build-osx.sh` (los mismos que la CI). |
| `ui/`, `docs/` | Assets del panel; capturas. |
| `build/` | Todo lo generado y nada más: `juce/` (la descarga), `plugin/`, `core/`, `core-asan/`. Ignorado por git: `rm -rf build` deja el árbol como recién clonado; `rm -rf build/plugin build/core*` limpia lo compilado sin volver a bajar JUCE. |

## Cadena

```
processBlock → RdPianoEngine::pushMidi/render
  → Mcu::sendMidiCmd() → CommandPort → firmware → RdBoard escribe 0x1000-0x1FFF
  → SoundChip (IC19 envolvente, dispara IRQ al fin de segmento → IC9 fase/dirección wave ROM
              → IC8 suma log vol+muestra; Ic19Out/Ic9Out = bus real entre chips)
  → Mcu::generate_next_sample() → s32
  → escalado seco → SpaceD (chorus) → Phaser → resample → ×patchOutputGain → trémolo → EQ medio
```

- `RdPianoEngine` ([rd_engine.h](librdpiano/include/rd_engine.h)) es la frontera motor/plugin, sin
  JUCE. Contrato: `prepare()` reserva todo; `render()` no reserva, no bloquea, no imprime;
  `setPatch()`/`setMasterTune()` corren el emulador y los serializa `mcuLock` (`juce::SpinLock`).
- **Reloj maestro = audio**: 1 muestra → 100 ciclos de CPU (62 si el parche es de 32 kHz). No hay
  bucle de CPU independiente. `SoundChip::update()` = 16 voces × 10 partes por los tres bloques
  (`tick_ic19/ic9/ic8`, inline en `sa_blocks.h`; LUT IC10/IC11 compartidas en `sa_tables.h`).
- **Parche** = ROMs de onda (IC5/6/7) + offset en `params_rom` + sample rate. Carga en dos
  pasos por coste:
   `loadRomSet()` (caro) + `selectPatch()` (barato, reubica página alta de params y parchea bytes
  0x00–0x02). `loadSounds()` = ambos.
- El **protocolo del firmware** (0x30/0x31/0xE0/0x50…) solo en `command_port.h`; fuera se habla por
  intención (`boot()`, `selectPatch()`, `sendMidiCmd()`…). Cola = anillo fijo, cero reservas en RT.
- `patchOutputGain[]` ([patches.h](librdpiano/include/patches.h)) normaliza los 16 parches a **+3
  dBFS** con acorde de 16 notas a velocity 127. Se aplica en la salida, después del emulador y de
  `lsp/` (aritmética entera del hardware) → no mueve golden ni hashes de `test_lsp.cpp`. **No hay
  limitador detrás**: con chorus de fábrica el peor caso medido llega a +7,9 dBFS. Se regenera con
  `rdpiano_e2e --headroom` (idempotente).
- `RdBiquad` (EQ medio) replica `juce::dsp::IIR::Filter<float>` salvo `snapToZero` (no-op
  fuera de Intel).
- Plugin = 3 archivos + 2 tablas: `PluginParams.h` (10 parámetros, valores de fábrica desde
  `RdEngineParams`), `PluginProcessor` (APVTS, serializa presets), `PluginEditor` (tablas
  `ButtonSpec` ×17 y `ModeSpec` ×8).

## Mapa de memoria (`RdBoard::read`/`write`, [rd_board.h](librdpiano/include/rd_board.h))

```
0x0000-0x001F  registros MCU (p1=0x02 datos, p2=0x03 control, TCSR=0x08)
0x0000-0x0FFF  RAM
0x1000-0x1FFF  SoundChip
0x2000-0x3FFF  latch de banco (2 bits)
0x4000-0xBFFF  params ROM, bancada por latch_val & 0b11
0xC000-0xFFFF  program ROM (firmware, 8 KB)
```

`Mcu` = CPU (core MAME) + `RdBoard`. Acople solo en dos puntos vía `RdBoardCpu`: el handshake mira
el PC y escribir en puerto 2 baja TIN. Las ROMs vienen con líneas permutadas en el PCB; se deshace
con los `unscramble_*` de [rom_loader.h](librdpiano/include/rom_loader.h) — **no tocar sin
verificar audio**.

## Trampas — leer antes de modificar

1. **Handshake atado a direcciones fijas del firmware RD200**: `RdBoard::read` compara el PC contra
   `0xE12B/0xE15E/0xE168/0xE15A`. Por eso solo se carga `RD200_B.bin`. Equivalentes de MKS-20 en
   [docs/FIRMWARE.md](docs/FIRMWARE.md).
2. El bit de sample rate del puerto 2 **no funciona** (nunca funcionó); el rate sale de
   `patchSampleRates[]`. Ver [docs/FIRMWARE.md §3](docs/FIRMWARE.md).
3. Dos hacks en `SoundChip::update()`: early-out `env_value==0 && env_dest==0` y silenciado
   condicional contra voces colgadas (`investigate`). Fijados por `test/vectors/ic_blocks.txt`.
4. `setMasterTune()` corre el emulador (~200 muestras + `programChange(0)`); el plugin lo llama
   desde el hilo de UI bajo `mcuLock`. Lleva
   "switcharoo" 0x30 → tuning → 0x30 porque afinar parches ≠ 0 falla.
5. `mcu_ops.h` y `mame_utils.h` son código derivado de MAME (BSD-3): no reescribir por estilo,
   mantener atribución.
6. `re_stuff/verilog/` está declarado por su propio README como *"probably most of them wrong"*:
   material de investigación, no fuente de verdad.
7. **Plugin y harness arrancan distinto** en parches de 32 kHz: el motor pasa 20 kHz siempre, el
   harness el del parche. Cerrarlo movería el golden de 5 parches = cambio de audio.
8. **`boot()` no pierde el parche** pero re-llamar `setPatch()` tras `prepare()` **sí cambia el
   audio**. Orden bueno: seleccionar parche → preparar.
9. `emuCapacity` se dimensiona para 32 kHz (el parche cambia sin re-preparar) + `maxBlock/4` de
   margen por el corrector de deriva.
10. `masterTune` y `currentPatch` **no son automatizables** a propósito (correrían emulador en el
   hilo de audio). Viajan en el preset como propiedades del árbol APVTS, con los nombres de atributo
   de siempre.
11. El `.lv2` se llama ahora `rdpiano_juce.lv2` (antes `RdPiano.lv2`: `juce_add_plugin` usa
    `PRODUCT_NAME`); el URI no cambió. Los otros
    cuatro formatos conservan nombre, bundle id y códigos.

## Build (solo CMake; no hay Projucer ni `.jucer`)

```bash
bash scripts/download-juce.sh   # JUCE 9.0.1 → build/juce (var de caché RDPIANO_JUCE_DIR)
bash scripts/build-osx.sh ALL   # cinco formatos, generador Xcode
# productos en build/plugin/rdpiano_juce/rdpiano_juce_artefacts/Release/<FORMATO>/
```
Generador **Xcode** a propósito (AUv3 solo existe con él). Deployment target 11.0, y hay que
pasárselo a `juceaide` por `MACOSX_DEPLOYMENT_TARGET` (invocación anidada que no hereda la caché).
El plugin **enlaza** el target `librdpiano`: añadir un `.cpp` al núcleo es una línea en
`librdpiano/CMakeLists.txt`.

Núcleo + standalone SDL (requiere SDL2 + portmidi):
```bash
cmake -S librdpiano -B build/core && cmake --build build/core
```
Configurar `librdpiano/` suelto fuerza `-fsanitize=address` (intencional); desde la raíz, OFF.

CI (`.github/workflows/main.yml`): `ctest` sin ASan, `ctest` con ASan, build macOS (+
`rdpiano_plugin_tests`); en `master` publica release rodante con tag `latest` y `release`
depende de los tres.

## Verificar cambios

```bash
cmake -S librdpiano -B build/core -DRDPIANO_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --target rdpiano_tests rdpiano_e2e
ctest --test-dir build/core --output-on-failure
```

- **e2e** (`test/e2e.cpp`, headless, ~40× tiempo real, 16 parches en ~3 s): arranque silencioso,
  nota, acorde, extinción tras note-off (detector de voces colgadas), polifonía 16, rango de pico y
  **hash bit-exacto por parche** contra `test/golden.txt`. `--patch N` para iterar (~0,2 s).
  Cambios en `sound_chip.cpp`, `unscramble_*` o el MCU mueven el hash.
- **Unitario** (`test/unit/`, 38 suites, 427 checks, 2,6 s): `test_board`, `test_patches`,
  `test_sa_tables`, `test_rom_loader`, `test_command_port`, `test_sound_chip_blocks` (2.256
  vectores), `test_lsp` (respuesta a impulso congelada), `test_resampler`, `test_engine`.
  Se añade con `TEST_SUITE(nombre)` + una línea en el CMakeLists; andamiaje = `test/check.h`.
  Regla: la prueba se escribe **antes** del refactor y pasa sin editarla después.
- `test_engine.cpp` = simulador de host (bloques irregulares, 22–96 kHz, cambios de parche en
  caliente, extremos de parámetros) y lo único que verifica **cero reservas en `render()`**
  (sustituye `operator new` global + vigila `stats.resamplerOpens`, porque libresample usa `malloc`).
- **Plugin** (`rdpiano_juce/test/`, `rdpiano_plugin_tests`, 5 suites, 95 checks): presets ida y
  vuelta, valores de fábrica, programas, preset corrupto. Está en el ctest de la raíz:
  ```bash
  cmake -B build/plugin -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
  cmake --build build/plugin --config Release --target rdpiano_tests rdpiano_e2e rdpiano_plugin_tests
  ctest --test-dir build/plugin -C Release --output-on-failure
  ```

**Golden y vectores no se regeneran para poner algo en verde.** Si el cambio de audio es
intencionado: `--wav-dir DIR`, escuchar, y solo entonces `--write-golden`. Golden movido + vectores
en verde = cambio en el orden de evaluación del bucle → revertir.

Tocar `lsp/`, `rd_engine.cpp` o `rdpiano_juce/` **no** mueve el golden (el harness mide el emulador
desnudo): ahí la red es `test_engine.cpp` y `test_lsp.cpp`.

Sin cubrir: `setMasterTune()`, la UI y sobre todo el **timbre** — los efectos se congelan por hash,
que detecta pero no juzga. Verificación auditiva con `test/standalone.cpp` o el plugin en un DAW.
Un cambio en `sound_chip.cpp` que pase el harness pero mueva el hash es de **alto riesgo tímbrico:
decírselo al usuario**.

`rdpiano_e2e --headroom` no es una comprobación ni entra en ctest: mide y reescribe `patchOutputGain[]`.

## Convenciones

- 2 espacios, llave aparte (Allman), 80 columnas. Deuda conocida: tabs heredados de MAME (`mcu.h`),
  4 espacios en `sound_chip.cpp`/`patches.h` — se salda al tocarlos.
- **Todo archivo C/C++ nuevo o modificado se formatea** con Format Document (`⇧⌥F`) antes de darlo
  por terminado; `.clang-format` de la raíz da lo mismo por CLI. Excepciones: `mcu_ops.h`,
  `mame_utils.h`, `lsp/`, `resample/`. Masivo solo en commit aislado:
  ```bash
  CF=~/.vscode/extensions/ms-vscode.cpptools-*/LLVM/bin/clang-format
  git diff --name-only --diff-filter=ACMR -- '*.c' '*.h' '*.cpp' | xargs $CF -i
  ```
- El núcleo no conoce JUCE ni `stdio` (tampoco `rd_engine.cpp`, `lsp/`, `resample/`); las cabeceras
  no incluyen nada de la stdlib salvo `<stddef.h>`/`<cstdint>`; la traza sale por `RD_TRACE`
  (`rd_trace.h`), no-op sin `-DRDPIANO_TRACE`.
- Tipos MAME (`u8/s16/u32`) en el núcleo, tipos JUCE en el plugin.
- `HACK:` / `TODO:` marcan comportamiento conocido-incorrecto: son contexto, no ruido.
- `patches.h` (offsets, sample rates, nombres, ROM set, headroom) lo comparten plugin y harness.

## Git: no commitear

Nunca `git commit`, `push`, `add`, ni crear ramas o tags. Dejar los cambios en el árbol de trabajo y
decir qué se tocó. Git en modo lectura (`status`, `diff`, `log`, `show`) sí.
