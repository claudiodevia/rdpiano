# CLAUDE.md

Emulador HW de pianos Roland SA (MKS-20 / RD-1000 / Rhodes MK-80): firmware original sobre CPU HD63701
emulada (core MAME) + chips de síntesis reimplementados gate-level. C++23; `resample/` en C23. Cómo está
construido (componentes, mapa de memoria, clases, diagramas): [docs/ARQUITECTURA.md](docs/ARQUITECTURA.md).

## Layout

| Ruta | Qué es |
|---|---|
| `librdpiano/` | Núcleo sin dependencias: emulador + `RdPianoEngine` (cadena de audio entera, incl. `lsp/` y `resample/`). La lógica real vive aquí; pruebas en `test/`. |
| `rdpiano_juce/` | Plugin JUCE 9.0.1 (VST3/AU/AUv3/LV2/Standalone), solo macOS; pruebas en `test/`. |
| `roms/` | Dumps. Los usados (3 juegos de onda + params, y `RD200_B.bin`) van empotrados como `BinaryData`. |
| `re_stuff/` | Ingeniería inversa (Verilog, disasm). No se compila. |
| `scripts/` | `download-juce.sh`, `build-osx.sh` y `verificar.sh` (la CI llama a sus modos: lo de la CI y lo local es lo mismo), sobre `common.sh`. |
| `ui/` | Assets del panel. |
| `docs/` y `*.md` de la raíz | Ver *Documentación: una sola fuente*. |
| `logs/` | `<script>-<fecha>-<hora>.log` por ejecución. Fuera de git; borrable entero. |
| `build/` | Solo lo generado: `juce/`, `plugin/`, `core/`, `core-asan/`. `rm -rf build` = recién clonado; `rm -rf build/plugin build/core*` limpia sin re-bajar JUCE. |

## Cadena y contratos

`processBlock` → `RdPianoEngine::pushMidi/render` → `Mcu::sendMidiCmd` → `CommandPort` → firmware → `RdBoard`
escribe `0x1000-0x1FFF` → `SoundChip` (IC19 envolvente con IRQ al fin de segmento, IC9 fase/dirección de wave
ROM, IC8 suma log vol+muestra; `tick_ic19/ic9/ic8` inline en `sa_blocks.h`, 16 voces × 10 partes) → `s32` →
escalado seco, SpaceD, Phaser → resample, ×`patchOutputGain`, trémolo, EQ medio.
**Reloj maestro = audio**: 1 muestra → 100 **instrucciones** de CPU (62 a 32 kHz); no hay bucle de CPU aparte.

| Contrato | |
|---|---|
| Frontera motor/plugin | `RdPianoEngine` ([rd_engine.h](librdpiano/include/rd_engine.h)), sin JUCE. `prepare()` reserva todo; `render()` no reserva, no bloquea, no imprime. |
| Sin cerrojo | Tocar el emulador desde fuera = publicar una petición, que atiende `serviceRequests()` al entrar en `render()`. |
| `requestPatch()`/`requestMasterTune()` | `store` atómico, repeticiones colapsadas (necesario con un dial). |
| `setPatch()`/`setMasterTune()`/`allNotesOff()` | Corren el emulador en el acto → solo arranque y pruebas, nunca con `render()` vivo en otro hilo. |
| `patch()`/`masterTune()` vs `activePatch()` | Intención (lo pedido aunque no atendido) vs lo que suena. |
| Protocolo | Bytes del bus (0x30/0x31/0xE0/0x50…) solo en `command_port.h`; fuera, por intención (`boot()`, `selectPatch()`, `sendMidiCmd()`…). Cola = anillo fijo, cero reservas en RT. |

### Cambio de parche y de afinación

Ambos **apagan las voces y sueltan el pedal dentro del firmware**: `applyPatch()` manda el program change de
`reloadPatch()` (única forma de que el firmware relea la página recién mapeada; sin él el timbre sale mal,
medido) y el switcharoo de `Mcu::setMasterTune()` (trampa 4), dos más.

| Pieza | Regla y cifras medidas |
|---|---|
| Declick | Siempre entre bloques (la tasa del emulador depende del parche; `processBlock` nunca espera). Bajada 6 ms → aplicar → subida 15 ms, u 80 ms si hubo redisparo (tapa el golpe de martillo). Curva smoothstep `g²(3−2g)` (`declick_shape`). Medido: hueco 45 ms, mínimo −16 dB, pico de reentrada −0,4 dB. Con 90 ms al cuadrado bombeaba (−20 dB hasta los 60 ms, sin recuperar a los 100); sin declick ni espejo, el dial TUNE cortaba en seco (RMS 0). |
| Orden | Afinación pendiente → `declickTune`. Con parche y afinación pendientes, en el cero de la rampa **afina primero y cambia después** (al revés, los PC del afinado matan las notas devueltas). Restaurar + elegir subida = `finishChange()`. |
| Espejo | `heldVelocity[128]` + `sustainDown`, en `sendTracked()` (único camino de salida MIDI) → devuelve pedal y teclas aún pulsadas. Lo sostenido **solo por el pedal** no se resucita (decenas de ataques a la vez). `allNotesOff()` lo limpia. |
| Reentrada | Al nivel alcanzado, no al del ataque (si no, +8 a +14 dB de golpe no dado). `onsetSq`/`levelSq` (ataque / nivel actual de la salida cruda, en RMS²: con picos, el ataque de un acorde suma en fase) → decaimiento; ÷ `kDbPerVelocityUnit` = 0,228 dB (recta velocidad→nivel de v16 a v120) → velocidad a restar. Queda en ±6 dB. |
| Program change MIDI | `pushMidi()` lo intercepta → `requestPatch()` (reenviado al firmware cambiaba el número, no la página: motor mudo). Programa = parche 0–15; mayor → ignorado, en `stats.programIgnored` (con `& 0x0f` el 20 sonaba como el 4). Ventana `kPatchSettleMs` = 150 ms: el primero sale en el acto (esperar haría sonar la nota siguiente con el parche viejo) y de los que llegan con ella abierta se cobra solo el último (cada cambio apaga el firmware y redispara lo pulsado). |
| Coste | `selectRomSet()` (un puntero) + `selectPatchPage()` (memcpy 32 KB) ≈ µs → viable en el hilo de audio; todo se descifra al construir (~9 ms, 2 MB). `prepareRomSetFor()` = no-op. |

Lo fijan `engine_patch_declick`, `engine_patch_held_notes`, `engine_tune_held_notes`, `board_select_patch_page`
(página cacheada ≡ descifrada), `engine_program_change_range` y `engine_patch_settle`.

### Efectos y salida

| Tema | Regla y cifras |
|---|---|
| LFO de chorus y phaser | Al ritmo del **emulador** → los 5 parches de 32 kHz modulan 1,6× más rápido. Deliberado: escalar por `20000/sourceRate` se probó y sonaba peor; "arreglarlo" rompe `engine_lfo_rate`. |
| Trémolo | Tras el remuestreador, fase propia acotada a 2π, `rate/2` Hz del reloj del **host** con cualquier parche (`engine_tremolo`). |
| Efectos siempre corriendo | `SpaceD::process()`/`Phaser::process()` son lo único que avanza sus líneas; saltárselos las congela y sueltan audio viejo al reactivar. Bypass = mezcla en rampa de 10 ms (`chorusMix`, `efxMix`); con 0 o 1, bit a bit igual. +0,03 ms/bloque. |
| Ganancia | `patchOutputGain[]` ([patches.h](librdpiano/include/patches.h)) normaliza los 16 parches a +6 dBFS (acorde de 16 notas, vel 127), en la salida, tras emulador y `lsp/` (aritmética entera) → no mueve golden ni `test_lsp.cpp`. Él y `volume` se interpolan **dentro** del bloque (si no, zíper y escalón de hasta 12 dB); quietos, salida idéntica. **Sin limitador**: peor caso +10,8 dBFS con chorus de fábrica. Regenerar: `rdpiano_e2e --headroom` (idempotente; reescribe la tabla, no es prueba). |
| `RdBiquad` | = `juce::dsp::IIR::Filter<float>` salvo `snapToZero` (no-op fuera de Intel). |
| Latencia | `latencySamples()` = retardo de grupo del remuestreador (`Xoff` a tasa del host; 67 muestras @48 kHz) → `setLatencySamples()`; peor caso (20 kHz) a propósito: no se renegocia al cambiar de sonido. |
| Cola | `kTailSeconds` = 3 s, doble de la cola real más larga (1,45 s a −60 dBFS, parche 5; `engine_tail_length`). Con 0 s el host cortaba el final al exportar o congelar. |

### Plugin

`PluginParams.h` (10 parámetros, fábrica desde `RdEngineParams`), `PluginProcessor` (APVTS, programas; el
motor es miembro privado), `PluginState` (parche y afinación en el preset), `PluginEditor` (`ButtonSpec`×17) y
las piezas del panel: `PanelLayout.h` (coordenadas del arte), `PanelDisplay` (texto del display y mapeos del
dial, puro), `PanelButton`, `PanelLookAndFeel` (dial y fader) y `lcd/`.

| Tema | Regla |
|---|---|
| Timer 10 Hz | Único paso del hilo de audio a la interfaz: recoge el parche que cambie el motor solo (program change MIDI) → `updateHostDisplay()` y panel, y los cambios de parámetro que marcó `parameterChanged()` (corre en el hilo de audio con automatización: ahí no se puede `sendChangeMessage()`). Parche y afinación se leen del motor (`patch()`/`masterTune()`): sin espejos. |
| Gestos | Todo cambio desde el panel llega al host dentro de un gesto (si no, touch/latch no lo graban): fader por `SliderParameterAttachment`, conmutadores por `setParamValue()` (gesto completo), dial en modo parámetro con begin/end en `sliderDragStarted/Ended`. Fija `plugin_param_gesture`. |
| Diales parche/afinación | Aplican **al soltar** (`sliderDragEnded`); arrastrando solo enseñan nombre o Hz. |
| Tamaño fijo | `setResizeLimits` con mín = máx → **Logic escala la ventana él mismo**; el arte escala por `artScale()` (`PanelLayout.h`: todo en px del fondo de 6140×1503; `toUi()` trunca como `setBounds` con `float`, redondear movería un píxel). Con `setResizable` Logic deja de escalar y el wrapper AU solo acepta cambios de dentro (`EditorCompHolder::resizeHostWindow()`) → haría falta un asa dibujada. Probado y revertido el 2026-09-11. |
| Repintado | Cada control se repinta solo (el tirador del fader lo dibuja `FaderLookAndFeel`) y el fondo, solo bajo lo repintado; las tres hojas de arte se decodifican una vez en el constructor (nada de `ImageCache` en `paint()`); el display es una `juce::Image` rehecha solo al cambiar texto o tamaño, con la unidad de celda sacada del ancho (los 17 caracteres lo llenan). |

## Trampas — leer antes de modificar

| # | Trampa |
|---|---|
| 1 | **Handshake atado al firmware RD200**: `RdBoard::read` compara el PC con `0xE12B/0xE15E/0xE168/0xE15A` → solo sirve `RD200_B.bin`. Equivalentes MKS-20 en [FIRMWARE §2](docs/FIRMWARE.md). |
| 2 | El bit de sample rate del puerto 2 **nunca funcionó**; la tasa sale de `patchSampleRates[]` ([FIRMWARE §3](docs/FIRMWARE.md)). |
| 3 | Dos hacks en `SoundChip::update()`: early-out `env_value==0 && env_dest==0` y silenciado condicional contra voces colgadas (`investigate`). **Solo los vigila el golden de e2e**: `test_sound_chip_blocks` reimplementa el silenciado en la prueba en vez de ejercitar `update()`, y ningún vector cubre el early-out. |
| 4 | `setMasterTune()` corre el emulador (~200 muestras + `programChange(0)`, ~0,16 ms): lo corre `render()` al atender la petición, no la UI. Switcharoo 0x30 → afinación → 0x30 porque afinar parches ≠ 0 falla; esos PC apagan voces y pedal → declick y devolución. |
| 5 | `mcu_ops.h` y `mame_utils.h` derivan de MAME (BSD-3): no reescribir por estilo, mantener atribución. |
| 6 | `re_stuff/verilog/` es, según su README, *"probably most of them wrong"*: investigación, no verdad. |
| 7 | **Plugin y harness arrancan distinto** en parches de 32 kHz: el motor calienta a 20 kHz siempre, el harness al del parche. Cerrarlo movería el golden de 5 parches. |
| 8 | `Mcu::reset()` reinicia **todo** el estado (registros, temporizador, RAM, latch, cola, 160 `SA_Part`) y conserva ROMs, página de params y tablas de onda: `boot()` **no** pierde el parche, pero re-llamar `setPatch()` tras `prepare()` **sí cambia el audio**. Orden: parche → preparar; por eso `prepare()` atiende la petición pendiente **antes** de arrancar (`engine_prepare_pending_change`). |
| 9 | `emuCapacity` se dimensiona para 32 kHz (el parche cambia sin re-preparar) + `maxBlock/4` por el corrector de deriva. |
| 10 | `masterTune` y `currentPatch` **no son automatizables** a propósito (el parche es el *programa* del host; la afinación no es un mando de mezcla): viajan en el preset como propiedades del árbol APVTS, con los atributos de siempre. |
| 11 | El `.lv2` se llama `rdpiano_juce.lv2` (antes `RdPiano.lv2`; `juce_add_plugin` usa `PRODUCT_NAME`); el URI no cambió. Los otros formatos conservan nombre, bundle id y códigos. |
| 12 | **El bus de entrada (estéreo, nunca leído) no se toca**: sin él `auval` valida, pero **Logic no carga la AU** (la inserta sin interfaz y muda). Probado y revertido el 2026-09-02. Fija `plugin_bus_layout`. |
| 13 | **Abrir el Standalone desde `build/` rompe la AU en Logic**: los cinco formatos comparten `aumu RDPN GlZs` y el AUv3 empotrado en el `.app` eclipsa el `.component` instalado (Logic: *"No se ha podido cargar el módulo Audio Unit RdPiano"*, triángulo amarillo; `auval -v aumu RDPN GlZs` dice *version 3* en vez de *2*). Deshacer: `pluginkit -r <ruta>/rdpiano_juce.app/Contents/PlugIns/rdpiano_juce.appex` + "Restablecer y volver a explorar". Desde `/Applications` no estorba. |
| 14 | **Note-on repetido en la misma tecla sin note-off cuelga una voz para siempre** (el firmware deja dos voces, el note-off apaga una y la otra suena sola, cruda y muy digital, sin que la callen note-off ni pánico). Medido de la nota 24 a la 84, a cualquier distancia, con y sin pedal; en un DAW, con notas solapadas. `sendTracked()` suelta la tecla antes de volver a pulsarla. Fija `engine_repeated_note`. |

Además: ROMs con líneas permutadas en el PCB (`unscramble_*` de [rom_loader.h](librdpiano/include/rom_loader.h)
y `SoundChip::decode_samples()`) — **no tocar sin verificar audio**.

## Build (solo CMake; no hay Projucer ni `.jucer`)

```bash
sh scripts/download-juce.sh   # JUCE 9.0.1 → build/juce (var de caché RDPIANO_JUCE_DIR)
sh scripts/build-osx.sh ALL   # cinco formatos universales, generador Xcode
# productos en build/plugin/rdpiano_juce/rdpiano_juce_artefacts/Release/<FORMATO>/
```

| Tema | Regla |
|---|---|
| Scripts | POSIX `sh`; incluyen `scripts/common.sh` con `.` (paleta, log, `paso`, `cmd`, `fatal`, `fin`, `minusculas`, `exigir_juce`, trap de salida —también con INT/TERM, que `dash` no pasa por EXIT— que llama a `limpiar()` si existe; `download-juce.sh` borra ahí zip y temporal). La CI invoca `sh ./scripts/…`. CMake/Xcode **solo al log**; en pantalla una etiqueta por paso y al final tiempo, avisos *distintos* (en universal salen doblados) y ruta; fallo → últimas 40 líneas y mismo estado. Color solo en terminal (`NO_COLOR`/`TERM=dumb` lo quitan, `FORCE_COLOR=1` lo impone). 10 logs por script (`LOGS_QUE_QUEDAN`). |
| Sin repetir trabajo | `download-juce.sh` no baja si `build/juce` ya es la versión (`project(JUCE VERSION …)` de su raíz; `--forzar` baja igual) y comprueba el zip contra `SHA256=` (el resumen de GitHub; cambiarlo con `VERSION`). `build-osx.sh` solo configura si la caché no sirve (no existe, otro generador o arquitecturas, sin JUCE o sin plugin) o hay un `CMakeLists` más nuevo que ella: CMake regenera el proyecto Xcode al compilar, pero xcodebuild ya cargó el viejo y no ve un fuente recién añadido. ~3,4 s por invocación sin cambios. |
| Universal siempre | `arm64;x86_64`; `universal` = no-op. Sin modo nativo: no ahorra nada en ejecución (macOS carga una rebanada, mismo código, sin flags por arquitectura) y no carga bajo Rosetta. |
| `install` | `build-osx.sh AU install`: `ditto` sobre el destino ya borrado (in situ deja restos), un `sudo -v`. AU → `/Library/Audio/Plug-Ins/Components`, VST3 → `…/VST3`, LV2 → `…/LV2`, Standalone → `/Applications`; AUv3 va dentro del `.app` (`XCODE_EMBED_APP_EXTENSIONS`). Sin VST2 (JUCE 9): `/Library/Audio/Plug-Ins/VST` no se toca. Sin la palabra, nada fuera de `build/` (lo que hace la CI). |
| Xcode | A propósito (AUv3 solo existe con él). Deployment target 11.0, pasado a `juceaide` por `MACOSX_DEPLOYMENT_TARGET` (invocación anidada sin caché). |
| Fuentes | `.cpp` nuevo en el núcleo = una línea en `librdpiano/CMakeLists.txt` (el plugin enlaza ese target). |
| ASan | `RDPIANO_SANITIZE`: ON por defecto al configurar `librdpiano/` suelto (intencional), OFF desde la raíz. CI y `verificar.sh` lo fijan: `build/core` sin, `build/core-asan` con. |
| Standalone SDL | `rdpiano_standalone [--roms DIR]` (`test/standalone.cpp`: parche 0 a 20 kHz, mismo `boot()` que el plugin), solo con SDL2 + portmidi; `verificar.sh` lo compila si CMake los encuentra, y la CI los instala. |

CI (`.github/workflows/main.yml`), en cada push, un trabajo por modo de `verificar.sh`: `nucleo`, `asan` y
`plugin` (con `logs/` como artefacto si fallan); en `master`, si pasan los tres, release rodante con tag `latest`.

## Verificar cambios

**Antes de dar un cambio por terminado: `sh scripts/verificar.sh` en verde** (los tres trabajos de prueba de
la CI, en sus mismos directorios; se detiene en el primer fallo).

```bash
sh scripts/verificar.sh          # núcleo, núcleo con ASan y plugin (cinco formatos + pruebas); necesita build/juce
sh scripts/verificar.sh rapido   # solo núcleo sin ASan (unit + e2e), ~15 s: para iterar (= nucleo)
sh scripts/verificar.sh asan     # o plugin: un solo trabajo de la CI
ctest --test-dir build/core -R unit --output-on-failure   # una suite suelta, ya compilado
```

| Suite | Qué fija |
|---|---|
| **e2e** (`librdpiano/test/e2e.cpp`) | Headless, ~40× tiempo real: 16 parches en ~3 s (`--patch N` ~0,2 s). Arranque silencioso, nota, acorde, extinción tras note-off (voces colgadas), polifonía 16, pico y **hash bit-exacto por parche** contra `test/golden.txt`. Lo mueven `sound_chip.cpp`, `unscramble_*` y el MCU; no `lsp/`, `rd_engine.cpp` ni el plugin (mide el emulador desnudo). |
| **Unitaria** (`librdpiano/test/unit/`) | 61 suites, 576 checks, ~10 s; incluye `test_sound_chip_blocks` (2.256 vectores), `test_lsp` (impulso congelado), `test_mcu` (traducción del MIDI y tiempos de `boot()` y del switcharoo, por la cola de comandos) y `board_select_patch_page`. Suite nueva = `TEST_SUITE(nombre)` + una línea en el CMakeLists; andamiaje propio (`test/check.h`, `test/rom_files.h`, `test/test_runner.h`, sin dependencias). La prueba se escribe **antes** del refactor y pasa sin editarla. |
| `test_engine_*.cpp` | Simulador de host sobre `engine_fixture.h` (bloques irregulares, 22–96 kHz, cambios de parche en caliente, extremos), partido en `host`, `midi`, `patch`, `effects` y `golden`. **`engine_golden`**: hash bit-exacto de la salida del motor entero ante un guion fijo (tres juegos de ROM, afinación, ráfaga de program change, pedal, efectos, trémolo y volumen moviéndose) a 44,1 y 48 kHz con bloques regulares e irregulares; es la red de cualquier refactor de `rd_engine.cpp`. `alloc_counter.cpp` sustituye todas las formas de `operator new` y es, con `stats.resamplerOpens` (libresample usa `malloc`), lo único que verifica **cero reservas en `render()`**. Red de transitorios: los `engine_*` citados arriba (`engine_tune_held_notes`: acorde y pedal sobreviven al afinado, sin salto ni golpe; `engine_patch_pedal_only_not_restored`), más `engine_effect_tail` (efecto encendido en silencio → silencio), `engine_effect_bypass_ramp`, `engine_program_change`, `engine_volume_ramp`, `engine_latency`, `engine_prepare_release_cycle` (`prepare()` idempotente y `release()`→`prepare()` ≡ motor nuevo), `engine_lfo_rate` (periodo del LFO del chorus por autocorrelación de la diferencia wet-dry) y `engine_tremolo` (lo mismo sobre wet/dry por canal: periodo, oposición de fase, profundidad, ritmo del host). Umbrales con nombre y su valor en dBFS en `engine_fixture.h`. |
| **Plugin** (`rdpiano_juce/test/`) | 18 suites, 255 checks: presets ida y vuelta, fábrica, programas, preset corrupto (con afinación fuera del int16), `PluginState` sin procesador, latencia, cola, buses, MIDI por `processBlock`, program change MIDI recogido por el timer, volver al parche anterior antes del timer, gestos de automatización y trampa 10; `test_panel_display.cpp`, el texto del display y los mapeos del dial sin abrir ventana. |

- **Golden y vectores no se regeneran para poner algo en verde.** Cambio de audio intencionado: `--wav-dir DIR`,
  escuchar, y solo entonces `--write-golden`. Golden movido + vectores en verde = cambio en el orden de
  evaluación del bucle → revertir.
- Sin cubrir: el dibujo del panel (su texto sí, `plugin_display_*`) y sobre todo el **timbre** (los hashes detectan, no juzgan): de oído con `rdpiano_standalone`
  o el plugin en un DAW. Un cambio en `sound_chip.cpp` que mueva el hash es de **alto riesgo tímbrico:
  decírselo al usuario**.

## Convenciones

- **Idioma**: respuestas, documentación, comentarios y mensajes de los scripts en español estándar, sin voseo.
  Identificadores de C/C++ en inglés, como el código existente; los de shell en español (`paso`, `limpiar`).
- **Archivos**: núcleo y pruebas en `snake_case` (`rd_engine.h`); plugin en `PascalCase` (`PluginProcessor.h`);
  scripts en `kebab-case` o una palabra.
- **Formato**: `.clang-format` (4 espacios, Allman, 120 columnas). Todo archivo C/C++ nuevo o tocado se formatea
  (`⇧⌥F`); en masa, solo en commit aislado:
  `git diff --name-only --diff-filter=ACMR -- '*.c' '*.h' '*.cpp' | xargs ~/.vscode/extensions/ms-vscode.cpptools-*/LLVM/bin/clang-format -i`.
- **Exceptuados de formateo y documentación**: `mcu_ops.h`, `mame_utils.h`, `lsp/`, `resample/`, `re_stuff/`.
- El núcleo no conoce JUCE ni `stdio` (tampoco `rd_engine.cpp`, `lsp/`, `resample/`); sus cabeceras solo
  incluyen `<stddef.h>`/`<cstdint>` (y `<atomic>` en `rd_engine.h`); traza por `RD_TRACE` (`rd_trace.h`), no-op
  sin `-DRDPIANO_TRACE`. Tipos MAME (`u8/s16/u32`) en el núcleo, JUCE en el plugin.
- `HACK:` / `TODO:` marcan comportamiento conocido-incorrecto: son contexto, no ruido.
- **Versión** = el `VERSION` de `juce_add_plugin` ([rdpiano_juce/CMakeLists.txt](rdpiano_juce/CMakeLists.txt)),
  única fuente de bundle, `.ttl` y `JucePlugin_Version*`; a mano (omitirla hereda el `1.0` del `project()`).
  Publicar = esa línea + entrada en `CHANGELOG.md`: una por versión, la más nueva arriba, breve y sin
  tecnicismos (lo que nota quien toca; ni funciones, ni rutas, ni hashes).

## Documentación: una sola fuente

Cada tipo de información vive en un sitio; los demás enlazan, no copian.

| Información | Dónde |
|---|---|
| Qué hace un archivo, clase o función | Su Doxygen |
| Cómo está construido y por qué | [docs/ARQUITECTURA.md](docs/ARQUITECTURA.md) |
| Firmware: qué corre, handshake por PC, bit de sample rate | [docs/FIRMWARE.md](docs/FIRMWARE.md) |
| Reglas de trabajo, trampas, cifras medidas, build, convenciones | Este archivo |
| Lo pendiente de la revisión de código | [docs/REVISION-CODIGO.md](docs/REVISION-CODIGO.md) |
| Para quien toca el plugin: presentación e instalación / compilar / novedades | [README.md](README.md) / [COMPILAR.md](COMPILAR.md) / [CHANGELOG.md](CHANGELOG.md) |
| Historia: cómo se llegó, fallos corregidos, alternativas descartadas | Mensajes de commit |

Al cambiar una decisión, actualizar este archivo y ARQUITECTURA a la vez (comparten declick, ganancia, cola,
trampas). Aquí va **qué no se debe revertir y la cifra que lo prueba**; «probado y revertido» solo cuando eso
evita repetir el error (trampas 12 y 13, tamaño fijo).

### Doxygen

En español, breve, sobre la **declaración** (nunca en el cuerpo): `/** */` con `@brief` en la primera línea;
`@param`/`@return` solo si el nombre no lo dice; `///<` para campos y constantes; sin `#` (nombre a secas o
backticks). Lo que no se deduce del código va aquí o a `docs/`, no a comentarios largos.

```cpp
/**
 * @brief Qué es la clase, en una línea.
 *
 * Por qué existe o qué contrato tiene, si no se deduce. Dos o tres líneas como mucho.
 */
class Ejemplo
{
  public:
    static constexpr int kTope = 16; ///< Qué acota.

    /**
     * @brief Qué hace, en imperativo.
     * @param patch Qué significa, no de qué tipo es.
     * @return Qué devuelve, y qué quiere decir el caso raro.
     */
    bool selectPatch(int patch);
};
```

| Regla | |
|---|---|
| Describe, no narra | Qué hace, qué recibe, qué devuelve y la restricción vigente. Nunca versiones anteriores, fallos corregidos, alternativas descartadas ni cómo se llegó. |
| Qué se documenta | Todo archivo (`@file` + `@brief`), clase, struct y enum, y los métodos públicos. No los `override` de JUCE, getters obvios ni destructores; constructores solo si el orden de llamada importa. |
| El `.cpp` | No repite la cabecera: solo `HACK:`/`TODO:` y la razón de una constante medida; documenta sus helpers en anónimo. |
| Sin comentarios interlineados | Si el código no se explica solo, se renombra o se parte. Excepciones: `HACK:`/`TODO:`, trampas, números mágicos del hardware (direcciones, bytes del protocolo, opcodes) y valores medidos. |
| Pruebas | `@file` por archivo y `@brief` sobre cada `TEST_SUITE` diciendo qué fija; dentro, se comenta el invariante que un `CHECK` no dice solo. |

## Git: no commitear

Nunca `git commit`, `push`, `add`, ni crear ramas o tags: dejar los cambios en el árbol de trabajo y decir qué
se tocó. Git en modo lectura (`status`, `diff`, `log`, `show`) sí.
