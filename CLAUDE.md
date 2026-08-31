# CLAUDE.md

Emulador a nivel de hardware de pianos Roland SA-synthesis (MKS-20 / RD-1000 / Rhodes MK-80).
Ejecuta el **firmware original** sobre una CPU HD63701 emulada + reimplementación gate-level de
los chips custom de síntesis. C++17.

## Layout

| Ruta | Qué es |
|---|---|
| `librdpiano/` | Núcleo, **sin dependencias**: emulador + `RdPianoEngine` (cadena de audio completa, incluidos `lsp/` y `resample/`). Aquí vive la lógica real. |
| `rdpiano_juce/` | Plugin JUCE 8.0.1 (VST3/AU/AUv3/LV2/Standalone), **solo macOS**: UI, parámetros, presets. |
| `roms/` | Dumps de ROM, empotrados como `BinaryData` vía `juce_add_binary_data`. |
| `re_stuff/` | Artefactos de ingeniería inversa (Verilog, disasm, silicon tooling). **No se compila.** |
| `ui/`, `docs/` | Assets del panel; capturas. |

## Modelo mental

```
processBlock → RdPianoEngine::pushMidi/render   (rd_engine.h, sin JUCE)
                 ↓
   Mcu::sendMidiCmd() → CommandPort → firmware original (program_rom)
                                       ↓ RdBoard: escribe 0x1000-0x1FFF
                                  SoundChip (IC19→IC9→IC8)
                                       ↓
                         Mcu::generate_next_sample() → s32
                 ↓
   escalado seco → SpaceD (chorus) → Phaser → resample → trémolo → EQ medio
```

- **`RdPianoEngine`** ([rd_engine.h](librdpiano/include/rd_engine.h)) es la frontera motor/plugin
  (REFACTORIZACION §1). Contiene la cadena entera y no conoce JUCE; `processBlock` son 35 líneas
  que vuelcan el MIDI y llaman a `render()`. Contrato: **`prepare()` reserva todo**, `render()` no
  reserva, no bloquea y no imprime; `setPatch()`/`setMasterTune()` corren el emulador y los
  serializa el integrador (`mcuLock`). Se prueba headless en `test/unit/test_engine.cpp`.
- El **EQ medio** es un biquad propio (`RdBiquad`) con los coeficientes y el orden de operaciones
  de `juce::dsp::IIR::Filter<float>`, para que salir de JUCE no cambiara el timbre. Lo único que no
  se replica es el `snapToZero` de JUCE, que ya era un no-op fuera de Intel.

- El **reloj maestro es el audio**: `generate_next_sample()` genera 1 muestra y luego corre
  **100 ciclos de CPU** (62 si el parche es de 32 kHz). No existe un bucle de CPU independiente.
- `SoundChip::update()` = 16 voces × 10 partes, cada una atravesando tres bloques que replican
  sumadores reales: **IC19** (envolvente, dispara IRQ al terminar un segmento) → **IC9**
  (acumulador de fase / dirección de wave ROM) → **IC8** (suma logarítmica volumen+muestra).
  Son `tick_ic19`/`tick_ic9`/`tick_ic8`, `inline` en `sa_blocks.h`, y `Ic19Out`/`Ic9Out` son el
  bus real entre chips. Las dos LUT de IC10/IC11 son función pura del índice y se comparten entre
  instancias (`sa_tables.h`).
- Un **parche** = ROMs de onda (IC5/6/7) + offset en la params ROM + sample rate. Se carga en dos
  pasos, por coste: `Mcu::loadRomSet()` (caro, por juego de ROMs) y `Mcu::selectPatch()` (barato,
  por parche) — este último reubica la página alta de `params_rom` y **parchea los bytes
  0x00–0x02** para redirigir al firmware. `loadSounds()` es los dos seguidos.
- El **protocolo del firmware** (0x30/0x31/0xE0/0x50…) vive solo en `command_port.h`. Fuera de ahí
  se habla por intención: `mcu->boot()`, `selectPatch()`, `reloadPatch()`, `setMasterTune()`,
  `allNotesOff()`, `sendMidiCmd()`. La cola es un anillo fijo: nada reserva memoria en RT.
- `Mcu` es **CPU + placa**: el core HD63701 derivado de MAME más un `RdBoard` con todo lo que
  cuelga del bus. Su API pública —arranque, protocolo, carga de ROM— es la misma de antes y la
  reenvía a quien corresponda.
- El **plugin son tres archivos y dos tablas**: `PluginParams.h` declara los diez parámetros una vez
  —con los valores de fábrica sacados de `RdEngineParams`, para que plugin y motor no puedan
  discrepar—, `PluginProcessor` los mete en un `AudioProcessorValueTreeState` que serializa los
  presets solo, y `PluginEditor` recorre una tabla de `ButtonSpec` (17 botones) y otra de `ModeSpec`
  (8 modos del display) en vez de repetir un bloque por control (REFACTORIZACION §§9, 10).

## Mapa de memoria (`RdBoard::read` / `write`, [rd_board.h](librdpiano/include/rd_board.h))

```
0x0000-0x001F  registros MCU (puerto1=0x02 datos, puerto2=0x03 control, TCSR=0x08)
0x0000-0x0FFF  RAM
0x1000-0x1FFF  SoundChip
0x2000-0x3FFF  latch de banco (2 bits)
0x4000-0xBFFF  params ROM, bancada por latch_val & 0b11
0xC000-0xFFFF  program ROM (firmware, 8 KB)
```

Desde la fase 3 el mapa vive en `RdBoard` y no dentro de `mcu.cpp`, que es el core derivado de MAME
y no hay que tocar (trampa 5). Los dos se acoplan por `RdBoardCpu`, y sólo en las dos direcciones en
que se acoplan de verdad: el handshake mira el contador de programa (trampa 1) y escribir en el
puerto 2 baja la línea TIN. `test_board.cpp` prueba el mapa entero sin CPU ni firmware.

Las ROMs vienen con líneas de dirección/datos permutadas en el PCB; se deshace al cargar con
`bitswap<>` (los `unscramble_*` de
[rom_loader.h](librdpiano/include/rom_loader.h)). **No tocar sin verificar audio**: `test_rom_loader.cpp`
comprueba que siguen siendo biyectivas y fija el `params_rom` de los 16 parches por hash.

## Trampas conocidas — leer antes de modificar

1. **El handshake del bus depende de direcciones fijas del firmware RD200.** `RdBoard::read`
   compara el contador de programa contra `0xE12B/0xE15E/0xE168/0xE15A` para entregar bytes de la
   cola de comandos. Por eso
   solo se carga `RD200_B.bin` aunque haya dumps de MKS-20/MK-80. Cambiar firmware = recalcular:
   las direcciones equivalentes del firmware de MKS-20 están en [docs/FIRMWARE.md](docs/FIRMWARE.md).
2. El **bit de sample rate del puerto 2** no funciona (nunca funcionó); el rate real sale de
   `patchSampleRates[]` en `patches.h`. Ver [docs/FIRMWARE.md §3](docs/FIRMWARE.md).
3. Hay **dos hacks en `SoundChip::update()`**: early-out con `env_value==0 && env_dest==0`
   (rendimiento) y silenciado condicional para evitar voces colgadas (marcado `investigate`).
   Los dos están en el bucle, a la vista; los vectores de `test/vectors/ic_blocks.txt` los fijan.
4. `Mcu::setMasterTune()` corre el emulador; el plugin lo llama **desde el hilo de UI** bajo
   `mcuLock` (`juce::SpinLock`). Dentro lleva un "switcharoo" 0x30 → tuning → 0x30 porque afinar
   parches ≠ 0 falla.
5. `mcu_ops.h` (2.358 líneas) y `mame_utils.h` son **código derivado de MAME** (BSD-3) — no
   reescribir por estilo; mantener la atribución.
6. `re_stuff/verilog/` está declarado por su propio README como *"probably most of them wrong"*.
   Es material de investigación, no fuente de verdad.
7. **El plugin y el harness arrancan distinto** en los parches de 32 kHz: `Mcu::boot()` pide el
   ritmo del margen de arranque, y el motor pasa 20 kHz siempre mientras el harness pasa el del
   parche. No tiene valor por defecto para que la divergencia se vea en cada llamada. Cerrarla
   movería el golden de los cinco parches de 32 kHz: es un cambio de audio, hay que escucharlo.
8. **`boot()` no pierde el parche.** Reinicia el firmware, pero no el mapeo de la página de params
   que hizo `selectPatch()`. Volver a llamar a `setPatch()` después de `prepare()` **sí cambia el
   audio** (medido: hash distinto ya en el parche 0, por el 0x31/0x30 de más). El orden bueno es
   seleccionar el parche y luego preparar, que es el del harness.
9. **El `emuCapacity` del motor se dimensiona para 32 kHz**, no para el parche actual: el parche
   cambia sin volver a preparar. Y lleva `maxBlock/4` de margen porque el corrector de deriva de
   `render()` puede pedir esas muestras de más.
10. **`masterTune` y `currentPatch` no son parámetros automatizables**, y es a propósito:
   `setMasterTune()` corre ~200 muestras de emulador y termina con un `programChange(0)`
   (trampa 4). Automatizarlo pondría eso en el hilo de audio. Viajan en el preset como propiedades
   del árbol del `AudioProcessorValueTreeState`, con los mismos nombres de atributo que antes de la
   fase 3, para que las sesiones guardadas los sigan recuperando.
11. **El `.lv2` cambió de nombre.** El Projucer lo llamaba `RdPiano.lv2` y `juce_add_plugin` lo
   llama `rdpiano_juce.lv2` (usa `PRODUCT_NAME`). El URI —lo que de verdad identifica un plugin
   LV2— no ha cambiado. Los otros cuatro formatos conservan nombre, bundle id y códigos.

## Build

**Un solo sistema, CMake.** Desde la fase 3 no hay Projucer ni `.jucer`: el `CMakeLists.txt` de la
raíz construye el núcleo, sus pruebas y el plugin, y el plugin **enlaza** el target `librdpiano`
en vez de recompilar sus fuentes. Añadir un `.cpp` al núcleo es una línea en
`librdpiano/CMakeLists.txt` y nada más.

**Plugin** (el producto):
```bash
bash rdpiano_juce/download-juce.sh          # JUCE 8.0.1 en rdpiano_juce/JUCE
bash rdpiano_juce/build/build-osx.sh        # configura y compila los cinco formatos
```
Los productos salen en `build/rdpiano_juce/rdpiano_juce_artefacts/Release/<FORMATO>/`. El script
usa el **generador Xcode** a propósito: `juce_add_plugin` sólo crea el objetivo AUv3 con ese
generador. Dos detalles que no son cosméticos y están comentados en `rdpiano_juce/CMakeLists.txt`:
el objetivo de despliegue es 10.13 (con el del anfitrión, JUCE 8.0.1 no compila contra el SDK de
macOS 15+), y hay que pasárselo a `juceaide` por la variable de entorno `MACOSX_DEPLOYMENT_TARGET`
porque se construye en una invocación anidada de CMake que no hereda la caché.

**Núcleo + standalone SDL** (para iterar sobre el emulador, sin necesidad de JUCE):
```bash
cd librdpiano && cmake -B build && cmake --build build   # requiere SDL2 + portmidi
```
Configurar `librdpiano/` por su cuenta fuerza `-fsanitize=address` — es intencional para
desarrollo. Desde la raíz el defecto es OFF: ese build produce el plugin.

CI (`.github/workflows/main.yml`): tres jobs —`ctest` del núcleo sin ASan y con ASan, y el build de
macOS, que además corre `rdpiano_plugin_tests`—; en `master` publica release rodante con tag
`latest`, y `release` depende de los tres.

## Cómo verificar cambios

**Primera parada: el harness e2e** (`librdpiano/test/e2e.cpp`). Headless, sin dependencias
externas (no necesita SDL, portmidi ni JUCE) y ~40x tiempo real: arranca el firmware, recorre los
16 parches, inyecta MIDI fijo y mide el audio.

```bash
cd librdpiano && cmake -B build -DRDPIANO_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target rdpiano_tests rdpiano_e2e
ctest --test-dir build --output-on-failure     # suite unitaria + harness
```

`ctest` corre dos ejecutables: `rdpiano_tests` (unitario, ~2,6 s desde que el motor entró en él) y
`rdpiano_e2e` (el harness). La CI ejecuta exactamente eso en cada push —sin ASan y con ASan, en dos
jobs—, y `release` depende de los dos.

Los 16 parches tardan ~3 s. Comprueba: arranque silencioso, que la nota suene, el acorde, que la
cola se extinga tras el note-off (**detector de voces colgadas**, ver trampa 3), polifonía de 16
voces, rango de pico — y un **hash bit-exacto** del stream por parche contra
`librdpiano/test/golden.txt`. Cualquier cambio en `sound_chip.cpp`, en los `unscramble_*` o en el
MCU mueve el hash. Sale con código 1 si algo falla.

Si el cambio de audio es **intencionado**: renderiza WAVs (`--wav-dir DIR`, un WAV por parche con
el escalado seco del plugin), escúchalos, y solo entonces regenera con `--write-golden`. Nunca
regenerar el golden para "arreglar" un fallo sin escuchar antes.

`--patch N` limita a un parche (~0.2 s) para iterar rápido.

**La suite unitaria** (`librdpiano/test/unit/`) — 38 suites, 427 comprobaciones, 2,6 s: el mapa de
memoria y el latch de banco (`test_board.cpp`, que desde la fase 3 escribe y lee sobre un
`RdBoard` con una CPU de mentira), tabla de parches y sus ROMs (`test_patches.cpp`), las dos LUT
(`test_sa_tables.cpp`), descifrado de ROM y páginas de params (`test_rom_loader.cpp`), el protocolo
del firmware (`test_command_port.cpp`), los tres bloques de `SoundChip` contra 2.256 vectores
capturados (`test_sound_chip_blocks.cpp`), la respuesta congelada de SpaceD y Phaser
(`test_lsp.cpp`), el resampler (`test_resampler.cpp`) y **el motor entero** (`test_engine.cpp`).
Se añaden suites con `TEST_SUITE(nombre)` y una línea en el `CMakeLists.txt` — sin globs, sin
dependencias; el andamiaje entero es `test/check.h`. Regla: la prueba se escribe **antes** del
refactor que acompaña y tiene que pasar sin editarla después.

`test_engine.cpp` es el **simulador de host**: instancia `RdPianoEngine` y le pide bloques
irregulares, tasas de host de 22 a 96 kHz, cambios de parche en caliente y parámetros en sus
extremos. Comprueba longitud de salida exacta, finitud, temporización del MIDI, detector de clics,
headroom y —lo que ningún otro test ve— **cero reservas en `render()`**: sustituye el
`operator new` global y vigila `stats.resamplerOpens`, porque libresample reserva con `malloc` y no
pasa por `new`. Es lento comparado con el resto (2,2 de los 2,6 s) porque emula audio de verdad.

Los vectores de `test/vectors/` y `golden.txt` se tratan igual: **no se regeneran para poner algo
en verde**. Cuando el golden dice que el audio cambió y los vectores dicen qué bloque, el bloque es
la respuesta; cuando el golden cambia y los vectores pasan, el cambio está en el orden de
evaluación del bucle y se revierte.

Lo que **nada** de esto cubre: `setMasterTune()`, la UI, y sobre todo el **timbre**. Las pruebas
del motor dicen que la cadena no revienta, no que suene bien: los efectos se congelan por hash de
respuesta a impulso, que detecta cambios pero no los juzga. Eso sigue siendo verificación auditiva
con `librdpiano/test/standalone.cpp` (app SDL+portmidi interactiva) o con el plugin en un DAW. Un
cambio en `sound_chip.cpp` que pase el harness sigue siendo de alto riesgo tímbrico si el hash
cambió: decírselo al usuario, no asumir.

Tocar `librdpiano/src/lsp/`, `librdpiano/src/rd_engine.cpp` o `rdpiano_juce/` **no** mueve el golden
del e2e —el harness mide el emulador desnudo, sin la cadena— así que ahí la red que manda es
`test_engine.cpp` y `test_lsp.cpp`, no `golden.txt`.

La tabla de parches (offsets, sample rates, nombres, ROM set) vive en `librdpiano/include/patches.h`
y la comparten plugin y harness — si se toca, ambos cambian a la vez.

**El plugin tiene su propia prueba** (`rdpiano_juce/test/`, ejecutable `rdpiano_plugin_tests`,
5 suites y 95 comprobaciones): instancia el `AudioProcessor` de verdad y comprueba la ida y vuelta
de presets, los valores de fábrica, los programas y qué pasa con un preset corrupto. Está en el
`ctest` de la raíz —no en el de `librdpiano/`, que no conoce JUCE—:
```bash
cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --target rdpiano_tests rdpiano_e2e rdpiano_plugin_tests
ctest --test-dir build -C Release --output-on-failure     # las tres suites
```
Lo que sigue sin cubrir es la UI: `PluginEditor` no lo toca ninguna prueba.

## Convenciones

- Indentación 2 espacios, **llave en línea aparte** (Allman) y 80 columnas; `librdpiano` usa tabs
  en zonas heredadas de MAME (`mcu.h`), y `sound_chip.cpp`/`patches.h` siguen a 4 espacios: deuda
  de formato, se salda al tocarlos.
- **Todo archivo C/C++ nuevo o modificado se formatea con el formateador de VS Code** (Format
  Document, `⇧⌥F`, del C/C++ extension) antes de darlo por terminado. El estilo lo fija
  `.clang-format` en la raíz —calibrado contra el código existente, no elegido a ojo— así que
  `⇧⌥F` y la CLI dan lo mismo. Excepciones: `mcu_ops.h` y `mame_utils.h` (derivados de MAME, ver
  trampa 5), `lsp/` y `resample/` (terceros) — no reformatear en bloque.
  Masivo, si alguna vez hace falta, en commit aislado para no ensuciar el `git blame`:
  ```bash
  CF=~/.vscode/extensions/ms-vscode.cpptools-*/LLVM/bin/clang-format   # el mismo que usa VS Code
  git diff --name-only --diff-filter=ACMR -- '*.c' '*.h' '*.cpp' | xargs $CF -i   # solo lo tocado
  ```
- El núcleo NO conoce JUCE ni `stdio` —tampoco `rd_engine.cpp`, `lsp/` ni `resample/`, que desde la
  fase 2 son núcleo—: las cabeceras de `librdpiano` no incluyen nada de la
  biblioteca estándar salvo `<stddef.h>`/`<cstdint>`, y la traza sale por `RD_TRACE` (`rd_trace.h`),
  que sin `-DRDPIANO_TRACE` no compila a nada. Mantenerlo así.
- Tipos cortos de MAME (`u8/s16/u32`) en el núcleo; tipos JUCE en el plugin.
- Los comentarios `HACK:` / `TODO:` marcan comportamiento conocido-incorrecto: son contexto, no ruido.

## Git: no commitear

Nunca hacer `git commit`, `git push`, `git add` ni crear ramas o tags. Al terminar un cambio,
dejar los archivos modificados en el árbol de trabajo y decir qué se tocó — el usuario revisa y
commitea a mano. Vale usar git en modo lectura (`status`, `diff`, `log`, `show`).
