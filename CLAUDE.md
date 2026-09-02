# CLAUDE.md

Emulador HW de pianos Roland SA (MKS-20 / RD-1000 / Rhodes MK-80): firmware original sobre CPU
HD63701 emulada (core MAME) + chips de síntesis reimplementados gate-level. C++23; `resample/` en C23.

## Layout

| Ruta | Qué es |
|---|---|
| `librdpiano/` | Núcleo sin dependencias: emulador + `RdPianoEngine` (cadena de audio entera, incl. `lsp/` y `resample/`). La lógica real vive aquí. |
| `rdpiano_juce/` | Plugin JUCE 9.0.1 (VST3/AU/AUv3/LV2/Standalone), solo macOS. |
| `roms/` | Dumps, empotrados como `BinaryData` vía `juce_add_binary_data`. |
| `re_stuff/` | Ingeniería inversa (Verilog, disasm). No se compila. |
| `scripts/` | `download-juce.sh`, `build-osx.sh` (los de la CI, POSIX `sh`) sobre `common.sh`. |
| `ui/`, `docs/` | Assets del panel; capturas + `ARQUITECTURA.md` y `FIRMWARE.md`. |
| `logs/` | `<script>-<fecha>-<hora>.log` por ejecución. Ignorado por git; borrable entero. |
| `build/` | Todo lo generado y nada más: `juce/`, `plugin/`, `core/`, `core-asan/`. `rm -rf build` = árbol recién clonado; `rm -rf build/plugin build/core*` limpia sin re-bajar JUCE. |

## Cadena

```
processBlock → RdPianoEngine::pushMidi/render
  → Mcu::sendMidiCmd() → CommandPort → firmware → RdBoard escribe 0x1000-0x1FFF
  → SoundChip (IC19 envolvente, IRQ al fin de segmento → IC9 fase/dirección wave ROM
              → IC8 suma log vol+muestra; Ic19Out/Ic9Out = bus real entre chips)
  → Mcu::generate_next_sample() → s32
  → escalado seco → SpaceD (chorus) → Phaser → resample → ×patchOutputGain → trémolo → EQ medio
```

**Frontera motor/plugin** = `RdPianoEngine` ([rd_engine.h](librdpiano/include/rd_engine.h)), sin
JUCE. `prepare()` reserva todo; `render()` no reserva, no bloquea, no imprime.

**Sin cerrojo.** Correr el emulador desde fuera = publicar una petición que atiende
`serviceRequests()` al principio de `render()`.
- `requestPatch()`/`requestMasterTune()` = `store` atómico; repeticiones se colapsan (lo que hace
  falta con un dial).
- `setPatch()`/`setMasterTune()`/`allNotesOff()` corren el emulador **en el acto** → solo arranque y
  pruebas, nunca con `render()` vivo en otro hilo.
- `patch()`/`masterTune()` = intención (lo pedido aunque no atendido); `activePatch()` = lo que suena.

**Declick de cambio de parche**: rampa a cero en 3 ms → aplicar → subida en 15 ms. Siempre **entre
bloques**, porque la tasa del emulador cambia con el parche y el bloque entero depende de ella.
`processBlock` no espera a nadie → el plugin no pierde bloques.

**LFO de los dos efectos = ritmo del emulador** → los 5 parches de 32 kHz modulan 1,6× más rápido
con el mismo ajuste de panel. Deliberado: escalar por `20000/sourceRate` se implementó, se escuchó y
se descartó (sonaba peor), pese a que un documento de rendimiento ya borrado lo proponía. Lo fija
`engine_lfo_rate`: "arreglarlo" rompe el test. El **trémolo** no: va detrás del remuestreador, con
fase propia acotada a 2π, y modula a `rate/2` Hz de reloj del host sea cual sea el parche
(`engine_tremolo`).

**Efectos siempre corriendo**, encendidos o no: `SpaceD::process()`/`Phaser::process()` son lo único
que avanza sus líneas de retardo; saltárselos las congela y sueltan el audio viejo entero al
reactivar. Bypass = mezcla en rampa de 10 ms (`chorusMix`, `efxMix`); con mezcla 0 o 1 la salida es
bit a bit la de antes. Coste +0,03 ms/bloque, constante.

**Reloj maestro = audio**: 1 muestra → 100 ciclos de CPU (62 en parche de 32 kHz); no hay bucle de
CPU independiente. `SoundChip::update()` = 16 voces × 10 partes × 3 bloques (`tick_ic19/ic9/ic8`,
inline en `sa_blocks.h`; LUT IC10/IC11 compartidas en `sa_tables.h`).

**Parche** = ROMs de onda (IC5/6/7) + offset en `params_rom` + sample rate. Dos pasos por coste:
`loadRomSet()` (caro) + `selectPatch()` (barato: reubica página alta de params, parchea bytes
0x00–0x02). `loadSounds()` = ambos.

**Todo lo caro se descifra al construir el motor** (~9 ms, 2 MB): `SoundChip` guarda una ranura
de tablas de onda por juego de ROM (3 × 512 KB, montón; cada dirección es un `WaveEntry` de
`exp`+`delta` con el signo en el bit 15, no cuatro tablas paralelas), `RdPianoEngine` las 16 páginas
de params ya descifradas (32 KB c/u). Cambio de parche = `selectRomSet()` (un puntero) + `selectPatchPage()`
(memcpy 32 KB) ≈ µs → viable desde el hilo de audio. `decodeRomSet`/`selectRomSet` suben por
`RdBoard` y `Mcu`; `prepareRomSetFor()` sobrevive como no-op. Página cacheada ≡ descifrado:
`engine_patch_prepare`.

**Protocolo del firmware** (0x30/0x31/0xE0/0x50…) solo en `command_port.h`; fuera se habla por
intención (`boot()`, `selectPatch()`, `sendMidiCmd()`…). Cola = anillo fijo, cero reservas en RT.

**Ganancia**: `patchOutputGain[]` ([patches.h](librdpiano/include/patches.h)) normaliza los 16
parches a +6 dBFS (acorde de 16 notas, vel 127). Se aplica en la salida, tras emulador y `lsp/`
(aritmética entera del hardware) → no mueve golden ni hashes de `test_lsp.cpp`. Él y `volume` se
interpolan **dentro** del bloque (una lectura por bloque = zíper al mover el mando y escalón de
hasta 12 dB al cambiar de parche); valor quieto → paso 0, salida idéntica. **Sin limitador detrás**:
peor caso medido +10,8 dBFS con chorus de fábrica. Regenerar: `rdpiano_e2e --headroom` (idempotente).

`RdBiquad` (EQ medio) = `juce::dsp::IIR::Filter<float>` salvo `snapToZero` (no-op fuera de Intel).

**Program change MIDI**: lo intercepta `pushMidi()` → `requestPatch()`. Reenviarlo al firmware
dejaba el motor mudo (cambiaba el número de parche, no la página mapeada).

**Latencia declarada**: `latencySamples()` = retardo de grupo del remuestreador (`Xoff` a la tasa del
host; 67 muestras @48 kHz); `prepareToPlay` → `setLatencySamples()`. Peor caso (parche de 20 kHz) a
propósito: no se renegocia al cambiar de sonido.

**Cola declarada**: `tailLengthSeconds()` = 3 s (`RdPianoEngine::kTailSeconds`), el doble de la cola
real más larga de los 16 parches (1,45 s a −60 dBFS, parche 5), que mide `engine_tail_length`. Con
los 0 s de antes el anfitrión dejaba de pedir bloques al soltar la tecla y cortaba el final de la
nota al exportar o al congelar la pista.

**Plugin** = 3 archivos + 2 tablas: `PluginParams.h` (10 parámetros, fábrica desde `RdEngineParams`),
`PluginProcessor` (APVTS, serializa presets), `PluginEditor` (`ButtonSpec`×17, `ModeSpec`×8). El
procesador es además `juce::Timer` @10 Hz: recoge el parche que el motor cambie por su cuenta
(program change MIDI) para espejo, preset y panel. El dial de parches cambia de sonido **al
soltarlo** (`sliderDragEnded`); arrastrando solo enseña el nombre. `updateValues()` repinta el fondo
solo si se movió el fader; cada control se repinta solo. Las tres hojas de arte se decodifican una
sola vez en el constructor del editor y se reparten a los botones y al dial (nada de `ImageCache`
dentro de un `paint()`), y el display se dibuja a una `juce::Image` que sólo se rehace al cambiar el
texto, la escala o el tamaño: son 1.190 rectángulos de 5×7 píxeles por repintado.

**Buses**: entrada estéreo + salida estéreo. La entrada **no se lee nunca** (`processBlock` la
sobrescribe entera) y en un instrumento sobra, pero **quitarla rompe Logic** → trampa 12. Lo fija
`plugin_bus_layout`, que además comprueba que lo que el host traiga en el búfer no se oye.

## Mapa de memoria (`RdBoard::read`/`write`, [rd_board.h](librdpiano/include/rd_board.h))

```
0x0000-0x001F  registros MCU (p1=0x02 datos, p2=0x03 control, TCSR=0x08)
0x0000-0x0FFF  RAM
0x1000-0x1FFF  SoundChip
0x2000-0x3FFF  latch de banco (2 bits)
0x4000-0xBFFF  params ROM, bancada por latch_val & 0b11
0xC000-0xFFFF  program ROM (firmware, 8 KB)
```

- `Mcu` = CPU (core MAME) + `RdBoard`. Acople solo en dos puntos vía `RdBoardCpu`: el handshake mira
  el PC; escribir en puerto 2 baja TIN.
- ROMs con líneas permutadas en el PCB; se deshace con los `unscramble_*` de
  [rom_loader.h](librdpiano/include/rom_loader.h) — **no tocar sin verificar audio**.
- `Mcu::reset()` reinicia **todo** el estado: sus registros y temporizador, y vía `RdBoard::reset()`
  la RAM, el latch, la cola de comandos y las 160 `SA_Part`. Lo que no es estado (las dos ROM, la
  página de params mapeada, las tablas de onda descifradas) sobrevive → trampa 8.
- Cada part ocupa 16 bytes del mapa pero solo tiene 8 registros; +8..+F se descartan (el firmware
  RD200 solo escribe ahí ceros de arranque).

## Trampas — leer antes de modificar

1. **Handshake atado a direcciones fijas del firmware RD200**: `RdBoard::read` compara el PC contra
   `0xE12B/0xE15E/0xE168/0xE15A` → solo se carga `RD200_B.bin`. Equivalentes MKS-20 en
   [docs/FIRMWARE.md](docs/FIRMWARE.md).
2. El bit de sample rate del puerto 2 **no funciona** (nunca funcionó); el rate sale de
   `patchSampleRates[]`. Ver [docs/FIRMWARE.md §3](docs/FIRMWARE.md).
3. Dos hacks en `SoundChip::update()`: early-out `env_value==0 && env_dest==0`, y silenciado
   condicional contra voces colgadas (`investigate`). Fijados por `test/vectors/ic_blocks.txt`.
4. `setMasterTune()` corre el emulador (~200 muestras + `programChange(0)`, ~0,16 ms) — lo corre
   `render()` al atender la petición, no el hilo de UI. Lleva "switcharoo" 0x30 → tuning → 0x30
   porque afinar parches ≠ 0 falla.
5. `mcu_ops.h` y `mame_utils.h` derivan de MAME (BSD-3): no reescribir por estilo, mantener atribución.
6. `re_stuff/verilog/` es, según su propio README, *"probably most of them wrong"*: investigación,
   no fuente de verdad.
7. **Plugin y harness arrancan distinto** en parches de 32 kHz: el motor pasa 20 kHz siempre, el
   harness el del parche. Cerrarlo movería el golden de 5 parches = cambio de audio.
8. `boot()` **no** pierde el parche, pero re-llamar `setPatch()` tras `prepare()` **sí cambia el
   audio**. Orden bueno: seleccionar parche → preparar. Por eso `prepare()` atiende una petición
   pendiente **antes** de arrancar el firmware, en vez de dejársela a `render()`.
9. `emuCapacity` se dimensiona para 32 kHz (el parche cambia sin re-preparar) + `maxBlock/4` de
   margen por el corrector de deriva.
10. `masterTune` y `currentPatch` **no son automatizables** a propósito: el parche es el *programa*
    del anfitrión y la afinación no es un mando de mezcla. Viajan en el preset como propiedades del
    árbol APVTS, con los nombres de atributo de siempre.
11. El `.lv2` se llama `rdpiano_juce.lv2` (antes `RdPiano.lv2`: `juce_add_plugin` usa
    `PRODUCT_NAME`); el URI no cambió. Los otros cuatro formatos conservan nombre, bundle id y códigos.
12. **El bus de entrada del plugin no se toca.** Es un instrumento y nunca lee la entrada, así que
    lo canónico sería no declararla —y `auval` valida igual sin ella—, pero **Logic no carga la AU
    sin bus de entrada**: la inserta, no enseña la interfaz y no suena. Probado y revertido el
    2026-09-02 (hallazgo F13 de [docs/REVISION-CODIGO.md](docs/REVISION-CODIGO.md), que ya avisaba de
    probarlo en un DAW). Lo fija `plugin_bus_layout`.
13. **Abrir el Standalone desde `build/` rompe la AU en Logic.** Los cinco formatos comparten
    `aumu RDPN GlZs`, y arrancar el `.app` registra su AUv3 empotrado: a partir de ahí el sistema
    resuelve esos códigos al **AUv3 del directorio de compilación**, que eclipsa el `.component`
    instalado. Logic entonces dice *"No se ha podido cargar el módulo Audio Unit RdPiano"* y lo marca
    con el triángulo amarillo. `auval -v aumu RDPN GlZs` lo delata en una línea: *"This AudioUnit is
    a version 3 implementation"* cuando debería decir *version 2*. Se deshace con
    `pluginkit -r <ruta>/rdpiano_juce.app/Contents/PlugIns/rdpiano_juce.appex` y un
    "Restablecer y volver a explorar" en el gestor de módulos. Instalado en `/Applications` no
    estorba: lo que no se puede cargar es un `.appex` que vive en `build/`.

## Build (solo CMake; no hay Projucer ni `.jucer`)

```bash
sh scripts/download-juce.sh   # JUCE 9.0.1 → build/juce (var de caché RDPIANO_JUCE_DIR)
sh scripts/build-osx.sh ALL   # cinco formatos universales, generador Xcode
# productos en build/plugin/rdpiano_juce/rdpiano_juce_artefacts/Release/<FORMATO>/
```

- Los dos scripts son POSIX `sh` y **no imprimen la salida de CMake/Xcode**: una etiqueta por paso
  (`==> Configurando CMake…`) y, al final, tiempo, número de avisos y ruta de los productos; el
  resto al log. Si un paso falla, vuelcan las últimas 40 líneas del log y salen con el mismo estado.
- `scripts/common.sh` (incluido con `.`) tiene la paleta, la apertura del log, `paso`, `cmd`,
  `fatal`, `fin` y el trap de salida; cada script solo su lógica. Color solo si la salida es
  terminal (`NO_COLOR`, `TERM=dumb` o redirigida → texto pelado, que es lo que ve la CI;
  `FORCE_COLOR=1` lo impone). Si el script define `limpiar()`, el trap la llama al salir (así borra
  `download-juce.sh` el zip y el temporal). La CI invoca `sh ./scripts/…` (antes `bash -ex`, traza).
- Deja **10 logs por script** (`LOGS_QUE_QUEDAN`), poda el resto al abrir uno nuevo, y cuenta avisos
  *distintos* (en universal cada uno sale una vez por arquitectura y el total salía doblado).
- Ninguno repite trabajo hecho:
  - `download-juce.sh` no baja nada si `build/juce` ya es la versión correcta (según el
    `project(JUCE VERSION …)` de su raíz, no un sello aparte); `--forzar` lo baja igualmente.
  - `build-osx.sh` solo configura CMake si la caché del binary dir no sirve (no existe, otro
    generador, otras arquitecturas, o configurada sin JUCE y sin plugin); el proyecto Xcode lo
    regenera CMake al cambiar un `CMakeLists`. ~3,4 s por invocación.
- **Siempre universal** (`arm64;x86_64`, en `build/plugin`); no hay modo de arquitectura. Hubo un
  `nativo` (`build/plugin-nativo`) y se quitó: ahorraba compilación y nada en ejecución (macOS carga
  una sola rebanada del universal, mismo código, sin flags por arquitectura en ningún `CMakeLists`)
  y su producto no cargaba bajo Rosetta. La palabra `universal` se acepta como no-op.
- Argumento `install` (`sh scripts/build-osx.sh AU install`) → instala al terminar, reemplazando lo
  que hubiera: AU → `/Library/Audio/Plug-Ins/Components`, VST3 → `.../VST3`, LV2 → `.../LV2`,
  Standalone → `/Applications`. Pide contraseña de administrador una sola vez (`sudo -v`) y copia
  con `ditto` sobre el destino ya borrado (actualizar un bundle in situ deja restos). **AUv3 no
  tiene destino propio**: JUCE lo empotra en el `.app` del Standalone
  (`XCODE_EMBED_APP_EXTENSIONS`) → se registra al instalar la aplicación. No hay VST2 (JUCE 9 lo
  quitó) → `/Library/Audio/Plug-Ins/VST` no se toca. Sin la palabra no se escribe nada fuera de
  `build/`, que es lo que hace la CI.
- Generador **Xcode** a propósito (AUv3 solo existe con él). Deployment target 11.0, y hay que
  pasárselo a `juceaide` por `MACOSX_DEPLOYMENT_TARGET` (invocación anidada que no hereda la caché).
- El plugin **enlaza** el target `librdpiano`: añadir un `.cpp` al núcleo = una línea en
  `librdpiano/CMakeLists.txt`.

Núcleo + standalone SDL (requiere SDL2 + portmidi):
```bash
cmake -S librdpiano -B build/core && cmake --build build/core
```
Configurar `librdpiano/` suelto fuerza `-fsanitize=address` (intencional); desde la raíz, OFF.

CI (`.github/workflows/main.yml`): `ctest` sin ASan, `ctest` con ASan, build macOS (+
`rdpiano_plugin_tests`); en `master` publica release rodante con tag `latest`, y `release` depende
de los tres.

## Verificar cambios

```bash
cmake -S librdpiano -B build/core -DRDPIANO_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --target rdpiano_tests rdpiano_e2e
ctest --test-dir build/core --output-on-failure
```

- **e2e** (`test/e2e.cpp`, headless, ~40× tiempo real, 16 parches en ~3 s): arranque silencioso,
  nota, acorde, extinción tras note-off (detector de voces colgadas), polifonía 16, rango de pico y
  **hash bit-exacto por parche** contra `test/golden.txt`. `--patch N` para iterar (~0,2 s). Cambios
  en `sound_chip.cpp`, `unscramble_*` o el MCU mueven el hash.
- **Unitario** (`test/unit/`, 50 suites, 482 checks, 4,5 s): `test_board`, `test_patches`,
  `test_sa_tables`, `test_rom_loader`, `test_command_port`, `test_sound_chip_blocks` (2.256
  vectores), `test_lsp` (respuesta a impulso congelada), `test_resampler`, `test_engine`. Se añade
  con `TEST_SUITE(nombre)` + una línea en el CMakeLists; andamiaje = `test/check.h`. Regla: la
  prueba se escribe **antes** del refactor y pasa sin editarla después.
- `test_engine.cpp` = simulador de host (bloques irregulares, 22–96 kHz, cambios de parche en
  caliente, extremos de parámetros) y lo único que verifica **cero reservas en `render()`**
  (sustituye `operator new` global + vigila `stats.resamplerOpens`, porque libresample usa `malloc`).
  Red de transitorios: `engine_effect_tail` (encender un efecto en silencio → silencio),
  `engine_effect_bypass_ramp`, `engine_program_change`, `engine_patch_declick`, `engine_volume_ramp`,
  `engine_latency`, `engine_lfo_rate` (periodo del LFO del chorus por autocorrelación de la
  diferencia wet-dry; fija que dependa de la tasa del parche), `engine_tremolo` (el mismo método
  sobre la razón wet/dry por canal: periodo, oposición de fase entre canales, profundidad, y que el
  trémolo —al revés que el chorus— vaya al ritmo del **host**) y `engine_tail_length` (la cola real
  de los 16 parches contra la declarada).
- **Plugin** (`rdpiano_juce/test/`, `rdpiano_plugin_tests`, 8 suites, 108 checks): presets ida y
  vuelta, valores de fábrica, programas, preset corrupto, latencia y cola declaradas, y buses
  (`plugin_bus_layout`: entrada y salida estéreo, y el búfer del host no se oye). Está en el
  ctest de la raíz:
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

Sin cubrir: `setMasterTune()`, la UI (incluido el dial, que aplica el parche al soltar) y sobre todo
el **timbre** — los efectos se congelan por hash, que detecta pero no juzga. Verificación auditiva
con `test/standalone.cpp` o el plugin en un DAW. Un cambio en `sound_chip.cpp` que pase el harness
pero mueva el hash es de **alto riesgo tímbrico: decírselo al usuario**.

`rdpiano_e2e --headroom` no es comprobación ni entra en ctest: mide y reescribe `patchOutputGain[]`.

## Versión y changelog

- **Versión oficial** = el `VERSION` de `juce_add_plugin`
  ([rdpiano_juce/CMakeLists.txt](rdpiano_juce/CMakeLists.txt)), y no hay otra: de ahí salen
  `CFBundleShortVersionString`/`CFBundleVersion` de los cuatro bundles, las versiones del `.ttl` del
  LV2 y las macros `JucePlugin_Version*`. Está a mano a propósito: omitirla la hace heredar del
  `project()` vigente (el de la raíz, hoy `1.0`), que es versión de proyecto CMake, no del producto.
  Publicar = cambiar esa línea + añadir la entrada al changelog.
- `CHANGELOG.md` (raíz): **una entrada por versión, la más nueva arriba**, en **español estándar,
  breve y conciso, con el menor lenguaje técnico posible** — lo que nota quien toca el plugin, no
  cómo está hecho por dentro. Nada de nombres de función, rutas ni hashes de commit: eso vive en
  este CLAUDE.md y en `docs/`.

## Convenciones

- 4 espacios, llave aparte (Allman), 120 columnas: lo que dice `.clang-format`, y así está ya todo
  el árbol propio (formateo masivo). Solo quedan tabs de MAME en los archivos exceptuados.
- **Todo archivo C/C++ nuevo o modificado se formatea** con Format Document (`⇧⌥F`) antes de darlo
  por terminado; `.clang-format` de la raíz da lo mismo por CLI. Excepciones: `mcu_ops.h`,
  `mame_utils.h`, `lsp/`, `resample/`, `re_stuff/`. Masivo solo en commit aislado:
  ```bash
  CF=~/.vscode/extensions/ms-vscode.cpptools-*/LLVM/bin/clang-format
  git diff --name-only --diff-filter=ACMR -- '*.c' '*.h' '*.cpp' | xargs $CF -i
  ```
- El núcleo no conoce JUCE ni `stdio` (tampoco `rd_engine.cpp`, `lsp/`, `resample/`); las cabeceras
  no incluyen nada de la stdlib salvo `<stddef.h>`/`<cstdint>` y el `<atomic>` de `rd_engine.h` (las
  peticiones que sustituyen al cerrojo); la traza sale por `RD_TRACE` (`rd_trace.h`), no-op sin
  `-DRDPIANO_TRACE`.
- Tipos MAME (`u8/s16/u32`) en el núcleo, tipos JUCE en el plugin.
- `HACK:` / `TODO:` marcan comportamiento conocido-incorrecto: son contexto, no ruido.
- **Documentación de clases: breve y concisa.** Una o dos líneas de qué es y por qué existe; el
  detalle que no se deduce del código (contratos de RT, trampas) va aquí en CLAUDE.md, no en
  comentarios largos sobre la declaración.
- `patches.h` (offsets, sample rates, nombres, ROM set, headroom) lo comparten plugin y harness.

## Git: no commitear

Nunca `git commit`, `push`, `add`, ni crear ramas o tags. Dejar los cambios en el árbol de trabajo y
decir qué se tocó. Git en modo lectura (`status`, `diff`, `log`, `show`) sí.
