# CLAUDE.md

Emulador a nivel de hardware de pianos Roland SA-synthesis (MKS-20 / RD-1000 / Rhodes MK-80).
Ejecuta el **firmware original** sobre una CPU HD63701 emulada + reimplementación gate-level de
los chips custom de síntesis. C++17. Sin tests automatizados.

## Layout

| Ruta | Qué es |
|---|---|
| `librdpiano/` | Núcleo del emulador, **sin dependencias**. Aquí vive la lógica real. |
| `rdpiano_juce/` | Plugin JUCE 8.0.1 (VST3/AU/AUv3/LV2/Standalone), **solo macOS**: UI, efectos, resampling. |
| `roms/` | Dumps de ROM, empotrados como `BinaryData` vía el `.jucer`. |
| `re_stuff/` | Artefactos de ingeniería inversa (Verilog, disasm, silicon tooling). **No se compila.** |
| `ui/`, `docs/` | Assets del panel; capturas. |

## Modelo mental

```
MIDI → Mcu::sendMidiCmd() → commands_queue → firmware original (program_rom)
                                                 ↓ escribe 0x1000-0x1FFF
                                            SoundChip (IC19→IC9→IC8)
                                                 ↓
                                   Mcu::generate_next_sample() → s32
```

- El **reloj maestro es el audio**: `generate_next_sample()` genera 1 muestra y luego corre
  **100 ciclos de CPU** (62 si el parche es de 32 kHz). No existe un bucle de CPU independiente.
- `SoundChip::update()` = 16 voces × 10 partes, cada una atravesando tres bloques que replican
  sumadores reales: **IC19** (envolvente, dispara IRQ al terminar un segmento) → **IC9**
  (acumulador de fase / dirección de wave ROM) → **IC8** (suma logarítmica volumen+muestra).
- Un **parche** = ROMs de onda (IC5/6/7) + offset en la params ROM + sample rate.
  `Mcu::loadSounds()` reubica la página alta de `params_rom` y **parchea los bytes 0x00–0x02**
  para redirigir al firmware.

## Mapa de memoria (`Mcu::read_byte` / `write_byte`, [mcu.cpp:456](librdpiano/src/mcu.cpp#L456))

```
0x0000-0x001F  registros MCU (puerto1=0x02 datos, puerto2=0x03 control, TCSR=0x08)
0x0000-0x0FFF  RAM
0x1000-0x1FFF  SoundChip
0x2000-0x3FFF  latch de banco (2 bits)
0x4000-0xBFFF  params ROM, bancada por latch_val & 0b11
0xC000-0xFFFF  program ROM (firmware, 8 KB)
```

Las ROMs vienen con líneas de dirección/datos permutadas en el PCB; se deshace al cargar con
`bitswap<>` (`UNSCRAMBLE_*` en [mcu.cpp:241](librdpiano/src/mcu.cpp#L241) y
[sound_chip.cpp:49](librdpiano/src/sound_chip.cpp#L49)). **No tocar sin verificar audio.**

## Trampas conocidas — leer antes de modificar

1. **El handshake del bus depende de direcciones fijas del firmware RD200.** `read_byte` compara
   `PCD` contra `0xE12B/0xE15E/0xE168/0xE15A` para entregar bytes de `commands_queue`. Por eso
   solo se carga `RD200_B.bin` aunque haya dumps de MKS-20/MK-80. Cambiar firmware = recalcular:
   las direcciones equivalentes del firmware de MKS-20 están en [docs/FIRMWARE.md](docs/FIRMWARE.md).
2. El **bit de sample rate del puerto 2** no funciona (nunca funcionó); el rate real sale de
   `patchSampleRates[]` en `patches.h`. Ver [docs/FIRMWARE.md §3](docs/FIRMWARE.md).
3. Hay **dos hacks en `SoundChip::update()`**: early-out con `env_value==0 && env_dest==0`
   (rendimiento) y silenciado condicional para evitar voces colgadas (marcado `investigate`).
4. `setMasterTune()` corre el emulador **desde el hilo de UI** bajo `mcuLock` (`juce::SpinLock`),
   con un "switcharoo" 0x30 → tuning → 0x30 porque afinar parches ≠ 0 falla.
5. `mcu_ops.h` (2.358 líneas) y `mame_utils.h` son **código derivado de MAME** (BSD-3) — no
   reescribir por estilo; mantener la atribución.
6. `re_stuff/verilog/` está declarado por su propio README como *"probably most of them wrong"*.
   Es material de investigación, no fuente de verdad.

## Build

**Plugin** (el producto):
```bash
cd rdpiano_juce
bash ./download-juce.sh                     # modules + Projucer (macOS) en ./JUCE
bash ./build/build-osx.sh                   # resave del .jucer + xcodebuild
```
El proyecto se define en `rdpiano_juce.jucer` (**Projucer, no CMake**). Añadir un archivo fuente
o un recurso requiere editar el `.jucer`, no solo el disco. Solo quedan los exportadores de Apple
(`XCODE_MAC` y `XCODE_IPHONE`); Windows y Linux se eliminaron del proyecto.

**Núcleo + standalone SDL** (para iterar sobre el emulador):
```bash
cd librdpiano && cmake -B build && cmake --build build   # requiere SDL2 + portmidi
```
El CMake fuerza `-fsanitize=address` — es intencional para desarrollo.

CI (`.github/workflows/main.yml`): compila macOS en cada push; en `master` publica release rodante
con tag `latest`.

## Cómo verificar cambios

**Primera parada: el harness e2e** (`librdpiano/test/e2e.cpp`). Headless, sin dependencias
externas (no necesita SDL, portmidi ni JUCE) y ~40x tiempo real: arranca el firmware, recorre los
16 parches, inyecta MIDI fijo y mide el audio.

```bash
cd librdpiano && cmake -B build -DRDPIANO_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target rdpiano_e2e
cd .. && ./librdpiano/build/rdpiano_e2e --roms roms --golden librdpiano/test/golden.txt
```

Los 16 parches tardan ~3 s. Comprueba: arranque silencioso, que la nota suene, el acorde, que la
cola se extinga tras el note-off (**detector de voces colgadas**, ver trampa 3), polifonía de 16
voces, rango de pico — y un **hash bit-exacto** del stream por parche contra
`librdpiano/test/golden.txt`. Cualquier cambio en `sound_chip.cpp`, en los `UNSCRAMBLE_*` o en el
MCU mueve el hash. Sale con código 1 si algo falla.

Si el cambio de audio es **intencionado**: renderiza WAVs (`--wav-dir DIR`, un WAV por parche con
el escalado seco del plugin), escúchalos, y solo entonces regenera con `--write-golden`. Nunca
regenerar el golden para "arreglar" un fallo sin escuchar antes.

`--patch N` limita a un parche (~0.2 s) para iterar rápido.

Lo que el harness **no** cubre: efectos del plugin (chorus/phaser/tremolo/EQ), resampling,
`setMasterTune()` y la UI. Eso sigue siendo verificación auditiva con `librdpiano/test/standalone.cpp`
(app SDL+portmidi interactiva) o con el plugin en un DAW. Un cambio en `sound_chip.cpp` que pase el
harness sigue siendo de alto riesgo tímbrico si el hash cambió: decírselo al usuario, no asumir.

La tabla de parches (offsets, sample rates, nombres, ROM set) vive en `librdpiano/include/patches.h`
y la comparten plugin y harness — si se toca, ambos cambian a la vez.

## Convenciones

- Indentación 2 espacios, llave en la misma línea; `librdpiano` usa tabs en zonas heredadas de MAME.
- El núcleo NO conoce JUCE: `librdpiano` solo incluye `<stdio.h>`, `<queue>`, `<cmath>`. Mantenerlo así.
- Tipos cortos de MAME (`u8/s16/u32`) en el núcleo; tipos JUCE en el plugin.
- Los comentarios `HACK:` / `TODO:` marcan comportamiento conocido-incorrecto: son contexto, no ruido.
