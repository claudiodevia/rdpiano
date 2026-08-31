# Análisis de refactorización — RdPiano

**Fecha:** 2026-08-30 · **Rama:** `limpieza` @ `3e18b84` · **Alcance:** `librdpiano/`,
`rdpiano_juce/Source/`, `patches.h`, build y CI.

**Criterio.** Este documento habla de **diseño**: acoplamiento, responsabilidades, duplicación,
código muerto, coste de instancia y testabilidad. **No repite** los defectos funcionales ya
catalogados en [AUDITORIA.md](AUDITORIA.md) (17 bugs) ni en
[FIABILIDAD-DIRECTO.md](FIABILIDAD-DIRECTO.md) (13 riesgos de directo); cuando un problema de
estructura *causa* uno de aquellos, se enlaza en vez de re-explicarlo.

Todas las medidas de esta página se tomaron en esta máquina, con las ROM del repositorio y
`clang++ -O2`; el apartado [§21](#21-cómo-reproducir-las-medidas) explica cómo repetirlas.

**Punto de partida verificado:** el harness e2e pasa limpio antes de tocar nada — 16 parches,
0 comprobaciones fallidas, 0 hashes distintos del golden, 3,5 s. Ese es el suelo contra el que se
mide cualquier refactor de esta lista.

---

## 0. Resumen

| # | Tema | Impacto | Esfuerzo | Ubicación |
|---|---|---|---|---|
| 1 | No existe frontera *motor de audio* ↔ *plugin*: toda la cadena vive dentro de `processBlock` | **Alto** — bloquea toda prueba automática de la cadena real | Alto | [PluginProcessor.cpp:367-575](../rdpiano_juce/Source/PluginProcessor.cpp#L367-L575) |
| 2 | `Mcu` acumula cuatro responsabilidades (CPU, placa, protocolo, ROMs) | **Alto** — cambiar una toca las otras | Alto | [mcu.cpp](../librdpiano/src/mcu.cpp) |
| 3 | El protocolo del firmware (`0x30/0x31/0xE0/0x50…`) está esparcido por tres capas vía `commands_queue` pública | **Alto** — 22 `push` de bytes crudos fuera del núcleo | Medio | [mcu.h:36](../librdpiano/include/mcu.h#L36) |
| 4 | 320 KB de LUT deterministas se recalculan por instancia: **15,8 ms** y memoria duplicada | **Alto** | Bajo | [sound_chip.cpp:59-164](../librdpiano/src/sound_chip.cpp#L59-L164) |
| 5 | `SoundChip::update()`: 135 líneas, tres bloques anónimos con estado compartido por variables sueltas | Medio — es el código más delicado del proyecto | Bajo | [sound_chip.cpp:213-351](../librdpiano/src/sound_chip.cpp#L213-L351) |
| 6 | `loadSounds()` hace dos cosas: recargar wave ROM (2,06 ms) y reubicar el parche (0,8 ms) | Medio — el 72 % del coste sobra al cambiar de parche dentro del mismo set | Bajo | [mcu.cpp:617](../librdpiano/src/mcu.cpp#L617) |
| 7 | 188 KB por instancia en buffers que no hacen falta (`ram` 16× sobredimensionada, `params_rom_tmp` temporal como miembro) | Medio | Bajo | [mcu.h:52-56](../librdpiano/include/mcu.h#L52-L56) |
| 8 | `processBlock` hace ocho trabajos distintos en una función | Medio | Medio | [PluginProcessor.cpp:367](../rdpiano_juce/Source/PluginProcessor.cpp#L367) |
| 9 | Parámetros a mano (punteros públicos + XML manual + `sendChangeMessage`) en vez de `APVTS` | Medio — ~120 líneas repetidas y estado que se puede desincronizar | Medio | [PluginProcessor.h:66-113](../rdpiano_juce/Source/PluginProcessor.h#L66-L113) |
| 10 | `PluginEditor`: 17 botones y 6 modos de parámetro escritos a mano uno por uno | Medio — ~250 de 413 líneas son copia-pega | Medio | [PluginEditor.cpp](../rdpiano_juce/Source/PluginEditor.cpp) |
| 11 | Tablas de datos duplicadas entre plugin, harness y editor (nombres, ROM sets, codificación de tune) | Medio — ya han divergido | Bajo | [patches.h](../librdpiano/include/patches.h) |
| 12 | `lsp/`: `spaced` y `phaser` repiten tabla, utilidades y acceso a IRAM | Bajo | Bajo | [spaced.cpp](../rdpiano_juce/Source/lsp/spaced.cpp), [phaser.cpp](../rdpiano_juce/Source/lsp/phaser.cpp) |
| 13 | Código muerto y campos vestigiales (`chorusRateToDepthChange`, `midiMessageCount`, `current_sample_rate`, bloques comentados) | Bajo — pero engaña al lector | Bajo | varios |
| 14 | Propiedad manual: `new`/`delete` crudos, punteros públicos, tipos de 1,4 MB copiables por defecto | Medio | Bajo | [PluginProcessor.h:82-105](../rdpiano_juce/Source/PluginProcessor.h#L82-L105) |
| 15 | El núcleo escribe en `stdout` desde la ruta de audio | Medio — impide usarlo en RT sin parchearlo | Bajo | [mcu.cpp:501,517](../librdpiano/src/mcu.cpp#L501) |
| 16 | Dos sistemas de build sin relación; la CI compila pero **no ejecuta** el harness | **Alto** — el golden no protege nada en CI | Medio | [.jucer](../rdpiano_juce/rdpiano_juce.jucer), [main.yml](../.github/workflows/main.yml) |
| 17 | No hay pruebas unitarias: la única red es un test agregado que necesita arrancar el firmware entero | **Alto** — ninguna clase nueva de esta lista nace con algo que la proteja | Medio | [test/](../librdpiano/test/) |

Los números 1, 3, 16 y 17 son los que de verdad limitan el proyecto: mientras el motor no exista
como objeto separado, no haya pruebas por unidad y la CI no ejecute nada, cada cambio se sigue
validando de oído. El [§17](#17-las-pruebas-que-faltan-una-por-clase) recorre clase por clase qué
prueba le corresponde a cada refactor de esta tabla: sin eso, mover código de sitio solo cambia el
sitio.

---

## 1. El problema de fondo: no hay frontera entre *motor* y *plugin*

`librdpiano` está limpiamente desacoplado de JUCE (bien), pero **solo cubre el emulador**. Todo lo
demás — escalados, chorus, phaser, trémolo, EQ, resampling, reparto temporal del MIDI, gestión de
error de muestras — vive dentro de `processBlock`, mezclado con el acceso a `juce::AudioBuffer` y a
`juce::MidiBuffer`:

```
processBlock()  ← 209 líneas
  ├── limpiar canales
  ├── calcular renderBufferFrames + samplesError   ← lógica pura, no verificable hoy
  ├── guardas de tamaño (dos returns tempranos)
  ├── configurar spaceD/phaser desde los parámetros
  ├── bucle por muestra: MIDI + emulador + chorus + phaser + escalado
  ├── (re)abrir resampler + resample_process
  ├── escribir salida + trémolo
  ├── EQ media
  └── flush de MIDI sobrante
```

Consecuencia directa: **la mitad del riesgo real del producto no es alcanzable desde ningún test.**
El propio [FIABILIDAD-DIRECTO §17.1](FIABILIDAD-DIRECTO.md#171-un-simulador-de-host-el-que-más-fallos-habría-cazado)
propone escribir un "simulador de host" que *reproduzca literalmente las líneas 382–513 de
`PluginProcessor.cpp`". Copiar 130 líneas de lógica a un test es exactamente el síntoma: si esa
lógica viviera en una clase propia, el simulador la instanciaría en lugar de duplicarla.

**Propuesta.** Extraer un `RdPianoEngine` sin dependencias de JUCE, en `librdpiano/` o en un
`rdpiano_dsp/` nuevo:

```cpp
class RdPianoEngine {                        // sin JUCE, sin asignaciones en render()
public:
  void prepare(double hostSampleRate, int maxBlockSize);   // reserva TODO aquí
  void setPatch(int patch);                                // no bloqueante
  void setMasterTune(int cents);
  void pushMidi(int frameOffset, u8 status, u8 d1, u8 d2); // cola RT-safe
  void render(float* left, float* right, int numFrames);   // sin locks, sin malloc, sin printf
  Params params;                                           // POD, escrito por el hilo de UI
};
```

`processBlock` queda entonces en unas 20 líneas: volcar `midiMessages` a `pushMidi`, llamar a
`render`, y ya. La clase `RdPianoEngine` es la que se prueba de forma headless, con bloques
irregulares, con cambios de frecuencia y con ASan.

Esto además coloca `resample/` y `lsp/` donde corresponden (son C/C++ puro, no tienen nada de JUCE)
y deja `rdpiano_juce/Source/` con lo que de verdad es específico del plugin: `PluginProcessor`,
`PluginEditor`, `Lcd`.

---

## 2. `Mcu` hace cuatro trabajos

[mcu.cpp](../librdpiano/src/mcu.cpp) (638 líneas + 2.358 de `mcu_ops.h`) mezcla:

1. **Núcleo de CPU HD63701** — derivado de MAME: `execute_one`, tablas de ciclos, flags, TCSR.
2. **Placa** — el mapa de memoria de `read_byte`/`write_byte`, el latch de banco, el `SoundChip`.
3. **Protocolo con la CPU-A** — el *handshake* por PC (`0xE12B/0xE15E/0xE168/0xE15A`) y
   `sendMidiCmd()`, que traduce MIDI a comandos del firmware.
4. **Gestión de ROMs** — descifrado de líneas y reubicación de parches (`loadSounds`).

Las cuatro cambian por razones distintas y a ritmos distintos: (1) es código heredado que **no
debe tocarse** ([CLAUDE.md](../CLAUDE.md), trampa 5); (3) es donde está la deuda real (trampa 1);
(4) se ejecuta al cambiar de parche desde la UI.

**Propuesta** (incremental, sin reescribir MAME):

| Nueva unidad | Qué se lleva | Notas |
|---|---|---|
| `Hd63701Cpu` | `execute_one`, `check_irq_lines`, flags, `mcu_ops.h`, tablas | Habla con un `MemoryBus&` abstracto (dos llamadas virtuales o, mejor, plantilla para no pagar indirección por ciclo) |
| `RdBoard` | `read_byte`/`write_byte`, latch, RAM, mapeo del `SoundChip` | Implementa `MemoryBus` |
| `RomLoader` | `UNSCRAMBLE_*`, descifrado, reubicación de la página de params | Funciones libres, testables sin CPU |
| `CommandPort` | cola de comandos + *handshake* por PC + `sendMidiCmd` | Ver [§3](#3-el-protocolo-del-firmware-está-esparcido-por-tres-capas) |

Lo importante no es el número de clases, sino que **el fichero que no hay que tocar quede separado
del que sí**. Hoy quien edita el mapa de memoria está editando el mismo fichero que el core de MAME.

Detalle menor del mismo fichero: [mcu.cpp:460](../librdpiano/src/mcu.cpp#L460) acota la ROM de
programa con `& 0xdfff` sobre un array de `0x2000`. Funciona (para 0..0x3FFF, limpiar el bit 13 da
0..0x1FFF), pero es una coincidencia aritmética que se lee como un error. Debería ser `& 0x1fff`.

---

## 3. El protocolo del firmware está esparcido por tres capas

`commands_queue` es un miembro **público** de `Mcu`. Resultado: los bytes del protocolo interno del
RD-1000 aparecen crudos en el plugin, en el harness y en el standalone.

```
PluginProcessor.cpp   15 push  (0x30, 0x31, 0xE0, tuneMsb, tuneLsb)
e2e.cpp                6 push  (con el comentario "Mismo handshake de arranque que mcuReset()")
standalone.cpp         1 push  (0x30)
mcu.cpp                7 push  (sendMidiCmd)
```

Tres consecuencias medibles:

1. **El harness no prueba el arranque real.** [e2e.cpp:222-230](../librdpiano/test/e2e.cpp#L222-L230)
   *reimplementa* `mcuReset()` en vez de llamarlo. Si alguien arregla el arranque en el plugin, el
   golden sigue verde y no se entera nadie.
2. **La codificación del master tune está duplicada literalmente** en
   [PluginProcessor.cpp:254-259](../rdpiano_juce/Source/PluginProcessor.cpp#L254-L259) y
   [289-293](../rdpiano_juce/Source/PluginProcessor.cpp#L289-L293) — mismas cuatro líneas, dos
   copias, cero comentarios de por qué `0x3c`, `0x48` o `×16×4`.
3. **No hay un sitio donde añadir lo que falta.** El pánico MIDI (CC 120/123), el filtro de canal y
   el pitch bend ausentes ([FIABILIDAD §3](FIABILIDAD-DIRECTO.md#3-n2--crítico--no-existe-panic-cc-120123121-se-ignoran-medido),
   [§9](FIABILIDAD-DIRECTO.md#9-n7--medio--modo-omni-sin-filtro-de-canal-midi),
   [§10](FIABILIDAD-DIRECTO.md#10-n8--medio--pitch-bend-modulación-y-expresión-se-descartan))
   son todos "una línea más en `sendMidiCmd`", pero hoy la mitad del protocolo está fuera de esa
   función.

**Propuesta.** Hacer `commands_queue` privada y exponer la intención, no los bytes:

```cpp
class Mcu {
public:
  void boot(int masterTuneCents);   // el handshake completo, un solo sitio
  void selectPatch(int patch);      // 0x31 / 0x30
  void setMasterTune(int cents);    // 0xE0 + la codificación, documentada una vez
  void allNotesOff();               // hoy no existe
  void sendMidiCmd(u8 status, u8 d1, u8 d2);
private:
  CommandQueue commands_queue;      // ring buffer fijo: sin malloc en RT
};
```

El "switcharoo" `0x30 → tuning → 0x30` de
[PluginProcessor.cpp:261-275](../rdpiano_juce/Source/PluginProcessor.cpp#L261-L275) —que además
corre el emulador desde el hilo de UI— pasa a ser un detalle interno de `setMasterTune`, con su
comentario `TODO:` donde toca. El plugin deja de saber que existe.

Como efecto colateral, la cola pasa a ser un buffer fijo y desaparece la reserva de memoria en el
hilo de audio de [FIABILIDAD §12](FIABILIDAD-DIRECTO.md#12-n10--medio--commands_queue-reserva-memoria-en-el-hilo-de-audio).

---

## 4. 320 KB de LUT deterministas, recalculadas por instancia

El constructor de `SoundChip` genera dos tablas:

| Tabla | Tamaño | Depende de |
|---|---|---|
| `phase_exp_table[0x10000]` | 256 KB | solo del índice `i` |
| `samples_exp_table[0x8000]` | 64 KB | solo del índice `i` |

Ninguna de las dos depende de las ROM: son la transcripción a nivel de puertas de IC10/IC11
([sound_chip.cpp:59-164](../librdpiano/src/sound_chip.cpp#L59-L164)), función pura de `i`. Aun así
se calculan enteras cada vez que se construye un `Mcu`.

**Medido:**

```
sizeof(Mcu)         = 1.455.216 bytes (1,39 MB)
sizeof(SoundChip)   = 1.119.240 bytes (1,07 MB)
SoundChip ctor      = 17,88 ms   ← de los cuales ~15,8 ms son las dos LUT
load_samples (x1)   =  2,06 ms
```

En un DAW que instancia el plugin para escanear, o al duplicar una pista, se pagan 15,8 ms y 320 KB
por copia sin ninguna razón.

**Propuesta,** por orden de rentabilidad:

1. **Compartir las tablas.** Generarlas una sola vez (`static` con `std::call_once`, o un objeto
   `SaTables` que el `Mcu` reciba por referencia). Ahorro inmediato: 15,8 ms y 320 KB por instancia
   a partir de la segunda. Riesgo de audio: **nulo** — mismos valores, mismo hash.
2. **Precalcularlas en tiempo de compilación** y volcarlas como blob, con un pequeño generador en
   `re_stuff/` y una prueba que compare blob contra generador. Elimina también los 15,8 ms de la
   primera instancia. Los dos `TODO: I want to believe there is a better way to compute this` de
   [sound_chip.cpp:63](../librdpiano/src/sound_chip.cpp#L63) y
   [116](../librdpiano/src/sound_chip.cpp#L116) dejan de importar: el código ilegible se ejecuta
   una vez, offline, y su salida queda fijada por un test.
3. **Empaquetar los signos.** `samples_exp_sign[0x20000]` y `samples_delta_sign[0x20000]` son
   `bool[]`: 128 KB cada uno para un bit. `samples_exp` usa 14 bits de un `uint16_t` y
   `samples_delta` usa 9: el signo cabe en el bit 15 de cada uno. Ahorro: 256 KB más, a cambio de
   un `& 0x7fff` en el punto más caliente del bucle — medir antes de aceptarlo.

Con (1) y (3), `sizeof(Mcu)` baja de 1,39 MB a ~0,65 MB.

---

## 5. `SoundChip::update()`: tres bloques que piden ser tres funciones

[sound_chip.cpp:213-351](../librdpiano/src/sound_chip.cpp#L213-L351) es un bucle de 135 líneas con
tres bloques delimitados por comentarios `// IC19`, `// IC9`, `// IC8` y llaves anónimas. El estado
entre bloques viaja en cuatro variables declaradas arriba (`volume`, `waverom_addr`,
`ag3_sel_sample_type`, `ag1_phase_hi`), lo cual documenta accidentalmente **cuál es el bus real
entre chips** — y eso es justamente lo que debería estar en una firma:

```cpp
struct Ic19Out { uint32_t volume; bool irq; };
struct Ic9Out  { uint32_t waverom_addr; bool sel_sample_type; bool phase_hi; };

inline Ic19Out tick_ic19(SA_Part& part, const SA_Part& flags) const;
inline Ic9Out  tick_ic9 (SA_Part& part, const SA_Part& flags) const;
inline s32     tick_ic8 (const SA_Part& part, const Ic19Out&, const Ic9Out&) const;
```

Es un refactor mecánico, `inline`, sin cambio de aritmética, y hace que los dos `HACK:` conocidos
(early-out por `env_value==0 && env_dest==0` y el silenciado condicional marcado `investigate`)
queden aislados en un bloque concreto en lugar de perdidos en mitad del bucle.

**Este es el único refactor de la lista que puede mover el hash del golden.** No debería —pero el
compilador puede reordenar operaciones—, así que la regla es: aplicarlo solo, sin nada más en el
mismo commit, y comprobar que los 16 hashes siguen idénticos. Si cambian, se revierte; no se
regenera el golden.

Dos detalles del mismo fichero:

- `SoundChip::read(size_t offset)` ignora `offset` y devuelve siempre `m_irq_id`
  ([sound_chip.cpp:166](../librdpiano/src/sound_chip.cpp#L166)). O el parámetro sobra, o falta una
  comprobación: hoy no se sabe cuál.
- La decodificación de registros en `write()` usa `voiceI`/`partI`/`field` calculados con
  divisiones y módulos sobre el offset crudo. Un `enum class SaReg : u8 { PitchHi = 0, PitchLo, WaveLoop, WaveHigh, EnvDest, EnvSpeed, Flags, EnvOffset }`
  más una cadena `if/else if` sustituida por `switch` haría evidente el bug de plegado ya descrito
  en [AUDITORIA §14](AUDITORIA.md#14-medio--decodificación-de-registros-offset--8-pliega-los-bytes-altos).

---

## 6. `loadSounds()` hace dos cosas independientes

```cpp
void Mcu::loadSounds(ic5, ic6, ic7, paramsrom, from_addr) {
  sound_chip.load_samples(ic5, ic6, ic7);   // 2,06 ms — depende del ROM SET
  // descifrar 0x20000 bytes de params                0,8 ms — depende del ROM SET
  // rellenar params_rom con 0xff + copiar página + parchear 0x00-0x02  — depende del PARCHE
}
```

Los dos primeros pasos dependen del **ROM set** (tres en total); el tercero depende del **parche**
(dieciséis). Cambiar de "Piano 1" a "Piano 2" —mismo set— repite 2,86 ms de trabajo idéntico del
que solo ~0,03 ms era necesario.

El propio código lo sabe: en
[PluginProcessor.cpp:225](../rdpiano_juce/Source/PluginProcessor.cpp#L225) hay un
`// if (patchToRomSet[index] != patchToRomSet[currentPatch]) {` comentado, es decir, alguien intentó
esta optimización desde el lado equivocado de la frontera y la abandonó.

**Propuesta.** Partir en dos, y quedarse el estado en `Mcu`:

```cpp
void Mcu::loadRomSet(const RomSet& roms);   // caro, solo si cambia el set
void Mcu::selectPatch(size_t from_addr);    // barato, siempre
```

Esto es también el arreglo de raíz de
[FIABILIDAD §6 (el dial recarga las ROM en cada evento de arrastre)](FIABILIDAD-DIRECTO.md#6-n5--alto--el-dial-de-parches-recarga-las-rom-en-cada-evento-de-arrastre):
con la separación hecha, arrastrar el dial dentro del mismo set cuesta microsegundos.

Además, `load_samples` reserva **384 KB en la pila** (`u8 ic5[0x20000]` ×3, ya señalado en
[AUDITORIA §3](AUDITORIA.md#3-crítico--loadsounds-bajo-spinlock-desde-el-hilo-de-ui--verificado)).
Al separar responsabilidades, esos buffers pasan naturalmente a ser miembros o a desaparecer: el
descifrado se puede hacer directamente sobre el destino, sin copia intermedia.

---

## 7. Memoria que no hace nada

```cpp
u8 params_rom[0x20000];       // 128 KB — usado
u8 params_rom_tmp[0x20000];   // 128 KB — temporal de loadSounds, vive para siempre
u8 ram[0x10000] = {0};        //  64 KB — el mapa solo direcciona 0x0000-0x0FFF
```

- `params_rom_tmp` solo se usa dentro de `loadSounds`
  ([mcu.cpp:622-631](../librdpiano/src/mcu.cpp#L622-L631)). Es un temporal promovido a miembro:
  128 KB permanentes por instancia para ahorrar una reserva que ocurre al cambiar de parche.
- `ram` está declarada de 64 KB pero `read_byte`/`write_byte` solo la usan para `addr < 0x1000`
  ([mcu.cpp:506](../librdpiano/src/mcu.cpp#L506),
  [553](../librdpiano/src/mcu.cpp#L553)): 60 KB inalcanzables. El tamaño correcto es `0x1000`, y
  entonces el propio array documenta el mapa de memoria.

Sumado a las LUT compartidas de [§4](#4-320-kb-de-lut-deterministas-recalculadas-por-instancia),
la instancia baja de 1,39 MB a ~0,53 MB sin tocar una sola operación aritmética.

---

## 8. `processBlock`: ocho trabajos, una función

Más allá de la extracción del motor ([§1](#1-el-problema-de-fondo-no-hay-frontera-entre-motor-y-plugin)),
la función tiene problemas de forma que valen por sí solos:

- **Dos bucles anidados para el reparto de MIDI** con `std::find` sobre un `std::vector` que crece
  dentro del bucle: `O(n²)` y con reservas en el hilo de audio
  ([PluginProcessor.cpp:436-453](../rdpiano_juce/Source/PluginProcessor.cpp#L436-L453)). La forma
  natural es un solo recorrido de `midiMessages` con un iterador que avanza mientras
  `metadata.samplePosition <= i`, sin contenedor auxiliar. (El bug de temporización que esto
  esconde está en [AUDITORIA §5](AUDITORIA.md#5-alto--la-temporización-midi-está-rota-y-el-reparto-es-on²-con-reservas-en-rt).)
- **Constantes mágicas sin nombre**: `<< 5`, `>> 6`, `/ 65536.0f`, `scaling = 0.5f`, `3.14159265359`
  escrito tres veces con tres precisiones distintas
  ([PluginProcessor.cpp:527-531](../rdpiano_juce/Source/PluginProcessor.cpp#L527-L531)). El escalado
  seco `(sample << 5 >> 6) / 65536 × 0.5` está además **replicado en el harness**
  ([e2e.cpp:122](../librdpiano/test/e2e.cpp#L122)): dos definiciones de "cuánto suena esto".
- **El trémolo y la EQ son efectos** pero están escritos inline en el bucle de salida, mientras que
  chorus y phaser son clases. Cuatro efectos, dos estilos.
- **Coeficientes IIR reconstruidos en cada bloque** desde constantes fijas
  ([PluginProcessor.cpp:539-544](../rdpiano_juce/Source/PluginProcessor.cpp#L539-L544)): son
  `const`, así que el sitio correcto es `prepareToPlay`.

---

## 9. Parámetros: hacerlo a mano cuesta ~120 líneas y se desincroniza

Hoy conviven tres mecanismos para la misma información:

1. Once punteros `juce::AudioParameter*` **públicos**, creados uno a uno con `addParameter` +
   `addListener` (60 líneas de constructor, [PluginProcessor.cpp:96-168](../rdpiano_juce/Source/PluginProcessor.cpp#L96-L168)).
2. `getStateInformation`/`setStateInformation` con XML escrito a mano, atributo por atributo, más
   una lista de validaciones con **valores por defecto que no coinciden con los del constructor**:
   `chorusRate` arranca en `5` pero se restaura a `1`; `chorusDepth` arranca en `14` y se restaura
   a `3` ([PluginProcessor.cpp:106-119](../rdpiano_juce/Source/PluginProcessor.cpp#L106-L119) vs
   [619-640](../rdpiano_juce/Source/PluginProcessor.cpp#L619-L640)). Cargar una sesión antigua sin
   esos atributos cambia el sonido.
3. `ChangeBroadcaster` → `updateValues()` en el editor, que reescribe *todos* los widgets en cada
   notificación.

`juce::AudioProcessorValueTreeState` cubre exactamente los tres: layout de parámetros declarado una
vez, serialización automática (con valores por defecto imposibles de desincronizar) y
`SliderAttachment`/`ButtonAttachment` que sustituyen a `sliderValueChanged` y a media
`updateValues()`. La estimación es que desaparecen ~120 líneas y con ellas dos clases enteras de
error.

Caso aparte: `currentPatch` y `masterTune` **no son parámetros** — son enteros públicos sueltos
guardados en el mismo XML. `currentPatch` debería ser el programa del `AudioProcessor` (ya lo es a
medias, vía `setCurrentProgram`) y `masterTune` un parámetro más, para que el host pueda
automatizarlo.

---

## 10. `PluginEditor`: 250 de 413 líneas son copia-pega

Tres bloques repetidos con la misma forma:

- **17 botones** declarados, hechos visibles y suscritos uno a uno (líneas 26-59), luego posicionados
  dos veces cada uno con coordenadas mágicas ligeramente distintas entre `setBounds` y `position`
  (líneas 94-128). Las líneas [314-315](../rdpiano_juce/Source/PluginEditor.cpp#L314-L315) son
  duplicado literal de las 312-313 — nadie lo ha notado porque el bloque es ilegible.
- **`buttonClicked`**: ocho ramas idénticas salvo el índice
  (`setCurrentProgram(N + (currentPatch >= 8 ? 8 : 0))`, líneas 185-208), y cuatro máquinas de
  estado de tres posiciones escritas cuatro veces (chorus, trémolo, efx, tune).
- **`updateValues`**: seis bloques de 10 líneas que construyen la misma cadena de LCD cambiando
  etiqueta y variable (líneas 344-403), incluida seis veces la misma expresión de padding y el mismo
  `replaceSection(17 + 1 + value, 1, "\xff")`.

**Propuesta.** Un descriptor por control y bucles:

```cpp
struct ButtonSpec { MksButton* btn; juce::Rectangle<int> bounds; juce::Rectangle<int> art; Action action; };
struct ParamMode  { const char* label; juce::RangedAudioParameter* param; int steps; };
```

Los 17 botones pasan a un `std::array<ButtonSpec, 17>` recorrido en el constructor, en `resized` y
en `updateValues`; los 6 modos, a una tabla `ParamMode` con una única función `renderParamLine()`.
Se van ~250 líneas y las coordenadas quedan en un solo sitio, que es lo que hace falta el día que se
retoque el panel.

Nota: el bug de `Lcd::setText` (copia 34 bytes fijos de una `juce::String` arbitraria y el marcador
`"\xff"` genera UTF-8 inválido, [AUDITORIA §13](AUDITORIA.md#13-medio--lcdsettext-copia-una-longitud-fija-y-el-marcador-es-utf-8-inválido))
desaparece solo si el renderizado de la línea pasa por una función única que trabaje con
`std::array<uint8_t, 34>` en vez de con `juce::String`.

---

## 11. Tablas de datos duplicadas

[patches.h](../librdpiano/include/patches.h) se creó para compartir la tabla de parches entre
plugin y harness — buena decisión, pero se quedó a medio camino:

| Dato | Copia A | Copia B | Estado |
|---|---|---|---|
| Nombres de parche | `patchNames[]` en patches.h | `displayPatchNames[]` en [PluginEditor.cpp:298](../rdpiano_juce/Source/PluginEditor.cpp#L298) | **Ya divergen** (una con `:`, otra con padding a 17 columnas) |
| Identidad de las ROM | `RomSet` con `BinaryData` en [PluginProcessor.cpp:15-35](../rdpiano_juce/Source/PluginProcessor.cpp#L15-L35) | `RomSetFiles` con nombres de fichero en [e2e.cpp:32-49](../librdpiano/test/e2e.cpp#L32-L49) | Dos tablas que **deben** estar en el mismo orden que `ROMSET_*`, sin nada que lo garantice |
| Codificación de master tune | [PluginProcessor.cpp:254](../rdpiano_juce/Source/PluginProcessor.cpp#L254) | [PluginProcessor.cpp:289](../rdpiano_juce/Source/PluginProcessor.cpp#L289) | Copia literal |
| Escalado seco de salida | [PluginProcessor.cpp:481](../rdpiano_juce/Source/PluginProcessor.cpp#L481) + `scaling` | [e2e.cpp:122](../librdpiano/test/e2e.cpp#L122) | Copia con comentario "El plugin escala así…" |

**Propuesta.** Que `patches.h` describa el parche completo, una fila por parche:

```cpp
struct Patch { const char* name; const char* displayName; RomSetId romSet; size_t offset; int sampleRate; };
inline constexpr Patch kPatches[NUM_PATCHES] = { … };
inline constexpr const char* kRomSetFiles[ROMSET_COUNT][4] = { … };  // nombres canónicos
```

Con una fila por parche se acaban los cinco arrays paralelos que hay que mantener alineados a mano,
y el editor deja de tener su propia lista. Los nombres de fichero canónicos sirven al harness
directamente y al plugin para verificar que el `BinaryData` que enlaza es el que cree.

Detalle de forma: las tablas de `patches.h` son `static const` en una cabecera, así que **cada
unidad de traducción recibe su propia copia**. Con `inline constexpr` (C++17, que ya es el estándar
del proyecto) hay una sola. El mismo problema, en su versión grave —tablas `int32_t` no-`static` en
cabecera— ya está descrito en [AUDITORIA §18](AUDITORIA.md#18-carreras-de-datos-menores-para-completar-el-cuadro).

---

## 12. `lsp/`: dos transcripciones con la misma infraestructura

`spaced.cpp` (364 líneas) y `phaser.cpp` (485) son transcripciones máquina a máquina del DSP
original: cientos de `accA_NNN` numerados por ciclo. **Ese cuerpo no se debe reescribir a mano** —
igual que `mcu_ops.h`, es código derivado cuya corrección está en la transcripción literal.

Lo que sí sobra es el andamiaje repetido:

- `phaserRateTable[]` y `spaceDRateTable[]` son **la misma tabla de 128 valores**, byte a byte.
- `DATA_BITS`, `MIN_VAL`, `MAX_VAL`, `clamp_24`, `sign_extend_24`: definidos dos veces, idénticos.
- `writeMemOffs`/`readMemOffs` sobre `iram[0x200]`: definidos dos veces, idénticos
  ([spaced.h:56-63](../rdpiano_juce/Source/lsp/spaced.h#L56-L63),
  [phaser.h:41-48](../rdpiano_juce/Source/lsp/phaser.h#L41-L48)).

Un `lsp/lsp_common.h` con la tabla, las utilidades y una pequeña base `LspUnit` con la IRAM y
`bufferPos` elimina la duplicación sin tocar ni una línea de los cuerpos transcritos. De paso, las
tablas deberían ser `static constexpr` y no `int32_t x[]` globales mutables.

`readMemOffs` devuelve `int64_t` mientras `iram` es `int32_t[]` — no es un bug (evita desbordar en
las multiplicaciones), pero merece un comentario de una línea, porque la asimetría con
`writeMemOffs` parece un descuido y no lo es.

---

## 13. Código muerto y campos vestigiales

Nada de esto rompe nada; todo desorienta a quien lee.

| Elemento | Ubicación | Estado |
|---|---|---|
| `chorusRateToDepthChange[15]` | [PluginProcessor.cpp:61](../rdpiano_juce/Source/PluginProcessor.cpp#L61) | Declarada, **nunca usada** |
| `midiMessageCount` | [PluginProcessor.h:102](../rdpiano_juce/Source/PluginProcessor.h#L102) | Se incrementa; su único lector está comentado |
| `lastMidiMessageCount`, `MidiMessageTimer` | [PluginEditor.h:103-114](../rdpiano_juce/Source/PluginEditor.h#L103-L114) | Clase entera comentada |
| `efxReverb*` (parámetros, modos, ramas) | Processor + Editor, ~20 líneas | Comentado en cinco sitios distintos |
| `Mcu::current_sample_rate` | [mcu.h:39](../librdpiano/include/mcu.h#L39) | Público, se escribe, **no funciona** (trampa 2); el plugin lo lee comentado en [PluginProcessor.cpp:379](../rdpiano_juce/Source/PluginProcessor.cpp#L379) |
| Bucle `do/while` de `execute_run` | [mcu.cpp:415-425](../librdpiano/src/mcu.cpp#L415-L425) | Comentado, incluye un "failsafe" que quizá haga falta |
| Direcciones de handshake alternativas | [mcu.cpp:468](../librdpiano/src/mcu.cpp#L468), [485](../librdpiano/src/mcu.cpp#L485) | Comentadas: son las del **otro** firmware |
| ~10 `printf` comentados de traza | `mcu.cpp` | Ver [§15](#15-el-núcleo-escribe-en-stdout) |
| `mks20_cpub_1.0.bin` | [.jucer](../rdpiano_juce/rdpiano_juce.jucer) | Empotrada en el binario, nunca referenciada desde el código |

Los tres últimos casos no son basura: son **conocimiento** (qué firmware alternativo existe, qué
trazas hicieron falta, qué failsafe se probó). Lo que corresponde no es borrarlos sin más, sino
convertirlos en lo que son: una nota en `docs/` para las direcciones del firmware alternativo, una
macro `RDPIANO_TRACE` para las trazas, y borrar el resto. Un bloque comentado no dice si está ahí
porque se abandonó o porque hará falta mañana.

---

## 14. Propiedad y ciclo de vida

```cpp
Mcu    *mcu    = 0;      // públicos, new en el constructor, delete en el destructor
SpaceD *spaceD = 0;
Phaser *phaser = 0;
void   *resampleL = 0;   // handles de C, nunca cerrados  → AUDITORIA §9
float  *emu_sample_bufferL = 0;   // new[]/delete[] a mano en prepare/release
```

- `Mcu`, `SpaceD` y `Phaser` **no necesitan ser punteros**: son miembros de por vida, sin
  polimorfismo. Como miembros por valor (o `std::unique_ptr` si molesta el 1,4 MB en el objeto del
  processor) desaparecen tres `new`, tres `delete` y la posibilidad de usarlos tras liberarlos.
- Los cuatro buffers `float*` son `std::vector<float>` de manual, reservados en `prepareToPlay` y
  liberados en `releaseResources`, con la fuga documentada en
  [AUDITORIA §12](AUDITORIA.md#12-medio--fuga-en-preparetoplay) si el host no intercala la llamada.
  `std::vector` con `.assign(n, 0.0f)` resuelve fuga, doble reserva y el borrado manual.
- `Mcu` y `SoundChip` son **copiables por defecto**: nadie las copia hoy, pero un `auto mcu = *p`
  accidental copia 1,4 MB y produce un emulador silenciosamente divergente. `= delete` en copia y
  movimiento cuesta dos líneas.
- El `// memset(mcu, 0, sizeof(Mcu));` comentado antes del `delete mcu`
  ([PluginProcessor.cpp:172](../rdpiano_juce/Source/PluginProcessor.cpp#L172)) sugiere que hubo un
  problema de destrucción que se "resolvió" comentando la línea. Merece una nota o su desaparición.

---

## 15. El núcleo escribe en `stdout`

`librdpiano` se define a sí mismo como "sin dependencias" y con un contrato claro
([CLAUDE.md](../CLAUDE.md): *"El núcleo NO conoce JUCE"*). Pero sí conoce `stdio`: hay cuatro
`printf` activos en rutas que se ejecutan **desde el hilo de audio**
([mcu.cpp:501](../librdpiano/src/mcu.cpp#L501), [517](../librdpiano/src/mcu.cpp#L517),
[549](../librdpiano/src/mcu.cpp#L549), [sound_chip.cpp:179](../librdpiano/src/sound_chip.cpp#L179))
y una decena comentados.

Un `printf` desde el hilo de audio toma un lock de `stdio` y puede bloquear: es el defecto de
[AUDITORIA §7](AUDITORIA.md#7-alto--printf-en-el-hilo-de-audio). Pero el problema de diseño es
anterior: **el núcleo decide por su cuenta a dónde va la traza.** El arreglo estructural es un punto
único de salida:

```cpp
#ifdef RDPIANO_TRACE
  #define RD_TRACE(...) rdpiano_trace(__VA_ARGS__)   // callback inyectado, o printf en debug
#else
  #define RD_TRACE(...) ((void)0)
#endif
```

Con eso, las diez trazas comentadas vuelven al código —donde son útiles— sin coste en release, y
`librdpiano` deja de necesitar `<stdio.h>` en `mcu.h`.

---

## 16. Build: dos sistemas sin relación, y una CI que no verifica nada

```
librdpiano/CMakeLists.txt   → librdpiano + rdpiano_e2e + rdpiano_standalone
rdpiano_juce.jucer          → Projucer → Xcode → VST3/AU/AUv3/LV2/Standalone
```

El `.jucer` lista **explícitamente** `mcu.cpp` y `sound_chip.cpp` como ficheros del proyecto: los
dos sistemas compilan las mismas fuentes por caminos distintos, con flags distintos, y añadir un
fichero al núcleo obliga a editar un XML con `id` generados a mano. Es la razón por la que
[CLAUDE.md](../CLAUDE.md) tiene que advertir *"Añadir un archivo fuente requiere editar el `.jucer`,
no solo el disco"*.

Y lo más caro: **la CI compila el plugin pero nunca ejecuta `rdpiano_e2e`**
([main.yml](../.github/workflows/main.yml)). Existe un harness bit-exacto de 3,5 s, con golden
versionado, y ningún push lo ejecuta; sin embargo `master` publica release rodante en cada empujón
(ver [FIABILIDAD §16](FIABILIDAD-DIRECTO.md#16-n6--alto--la-ci-publica-releases-que-nadie-ha-verificado)).

**Propuesta, por orden de valor:**

1. **Añadir el harness a la CI ya, sin refactor previo** — cuatro líneas de YAML: configurar
   `librdpiano` con `-DRDPIANO_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release`, construir `rdpiano_e2e`,
   ejecutarlo con `--golden`. Es el cambio con mejor relación valor/esfuerzo de todo el documento:
   a partir de ahí, ningún cambio de audio no intencionado llega a un release.
2. Un segundo job con ASan (`-DRDPIANO_SANITIZE=ON`, un solo parche, `--patch 0`) para que los
   accesos fuera de rango de [AUDITORIA §6](AUDITORIA.md#6-alto--campos-de-sa_part-sin-inicializar--índice-de-wave-rom-fuera-de-rango-verificado)
   no puedan volver. La suite unitaria de [§17](#17-las-pruebas-que-faltan-una-por-clase) cabe
   entera en este job: tarda menos de un segundo, así que bajo ASan sigue siendo gratis.
   Con `enable_testing()` en el `CMakeLists.txt`, los dos jobs se reducen a `ctest --output-on-failure`.
3. **Migrar el plugin a la API CMake de JUCE** (`juce_add_plugin`), con `librdpiano` como
   `target_link_libraries`. Un solo sistema, sin Projucer, sin `JuceLibraryCode/` generado en el
   repo, sin editar XML para añadir un `.cpp`. Es el punto más invasivo de la lista y el que hay que
   hacer cuando no haya otro trabajo en vuelo — pero es el que convierte los pasos 1 y 2 en algo
   natural en lugar de un job aparte.

---

## 17. Las pruebas que faltan: una por clase

Todo lo anterior propone clases nuevas. Una clase nueva sin prueba propia es solo el mismo código
en otro fichero: el refactor solo se cobra si, al terminar, hay algo que **falla cuando alguien
rompe esa unidad**. Este apartado dice qué prueba corresponde a cada clase, con qué se comprueba y
en qué orden se escribe.

**Punto de partida.** Hoy existe exactamente un ejecutable de verificación, `rdpiano_e2e`, y es
agregado: arranca el firmware entero, toca, y compara un hash del stream completo. Es una red
excelente para lo que cubre y **no se toca**, pero tiene tres límites estructurales:

| Límite | Consecuencia |
|---|---|
| Necesita firmware + 4 ROM + CPU emulada para cualquier comprobación | No se puede probar el descifrado de una ROM, ni la codificación del master tune, sin arrancar un piano |
| Un solo hash por parche | Cuando cambia, dice *que* algo cambió, no *qué* bloque |
| Termina en `generate_next_sample()` | Efectos, resampling, reparto de MIDI y parámetros —la mitad del riesgo, [§1](#1-el-problema-de-fondo-no-hay-frontera-entre-motor-y-plugin)— quedan fuera |

### 17.1 La regla: caracterizar antes de mover

Para cada unidad que se extrae, el orden es siempre el mismo:

1. Escribir la prueba **contra el código de hoy**, tal como está, sin refactorizar nada.
2. Verla verde. Si no se puede escribir sin mover código antes, capturar la salida actual como
   vector fijo (*golden* de bloque) — igual que se hizo con `golden.txt`.
3. Mover el código.
4. La prueba tiene que seguir verde **sin editarla**. Si hay que retocarla para que pase, el
   refactor cambió comportamiento: se revierte, no se ajusta la prueba.

Esto es lo que convierte cada paso de [§19](#19-plan-por-fases) en algo reversible. Y aplica con
especial dureza a [§5](#5-soundchipupdate-tres-bloques-que-piden-ser-tres-funciones), el único
refactor que puede mover el hash.

### 17.2 Infraestructura: 40 líneas, cero dependencias

`librdpiano` no tiene dependencias y no debería ganarlas por esto: nada de GoogleTest ni Catch2. El
propio `e2e.cpp` ya tiene el patrón (`struct Check { const char* name; bool ok; std::string detail; }`,
[e2e.cpp:310](../librdpiano/test/e2e.cpp#L310)); basta con extraerlo a una cabecera y que lo compartan
los dos ejecutables.

```
librdpiano/test/
  check.h              # CHECK / CHECK_EQ / CHECK_NEAR / CHECK_HASH + contador y resumen  (~40 líneas)
  e2e.cpp              # lo que ya hay; pasa a usar check.h
  golden.txt
  standalone.cpp
  unit/
    main.cpp           # registro de suites + --filter NOMBRE + --roms DIR
    test_rom_loader.cpp
    test_sa_tables.cpp
    test_sound_chip_blocks.cpp
    test_command_port.cpp
    test_board.cpp
    test_patches.cpp
    test_engine.cpp    # llega en la fase 2, con RdPianoEngine
    vectors/           # vectores capturados (golden de bloque), versionados
```

```cmake
add_executable(rdpiano_tests
    test/unit/main.cpp test/unit/test_rom_loader.cpp test/unit/test_sa_tables.cpp
    test/unit/test_sound_chip_blocks.cpp test/unit/test_command_port.cpp
    test/unit/test_board.cpp test/unit/test_patches.cpp)      # lista explícita, sin glob
target_link_libraries(rdpiano_tests librdpiano)
target_include_directories(rdpiano_tests PRIVATE test)

enable_testing()
add_test(NAME unit COMMAND rdpiano_tests --roms ${CMAKE_CURRENT_SOURCE_DIR}/../roms)
add_test(NAME e2e  COMMAND rdpiano_e2e  --roms ${CMAKE_CURRENT_SOURCE_DIR}/../roms
                                        --golden ${CMAKE_CURRENT_SOURCE_DIR}/test/golden.txt)
```

Las ROM están en el repositorio (`roms/`, 22 ficheros), así que las dos suites corren en CI sin
secretos ni descargas. A diferencia del harness, la suite unitaria **no emula audio**: debe
terminar en menos de un segundo, para que se ejecute en cada guardado y no solo en cada push.

### 17.3 Qué prueba cada clase

| Unidad (§ que la crea) | Prueba | Qué caza | ¿ROM? | ¿Emula? |
|---|---|---|---|---|
| `RomLoader` ([§2](#2-mcu-hace-cuatro-trabajos), [§6](#6-loadsounds-hace-dos-cosas-independientes)) | `test_rom_loader.cpp` | Descifrado y reubicación de parches | Sí | No |
| `SaTables` ([§4](#4-320-kb-de-lut-deterministas-recalculadas-por-instancia)) | `test_sa_tables.cpp` | Que compartir/precalcular las LUT no cambie un solo valor | No | No |
| `tick_ic19/ic9/ic8` ([§5](#5-soundchipupdate-tres-bloques-que-piden-ser-tres-funciones)) | `test_sound_chip_blocks.cpp` | Que la extracción de los tres bloques sea aritméticamente neutra | No | No |
| `CommandPort` ([§3](#3-el-protocolo-del-firmware-está-esparcido-por-tres-capas)) | `test_command_port.cpp` | Protocolo, codificación del tune, cola sin reservas | No | No |
| `RdBoard` ([§2](#2-mcu-hace-cuatro-trabajos)) | `test_board.cpp` | Mapa de memoria y latch de banco | Sí | No |
| `patches.h` ([§11](#11-tablas-de-datos-duplicadas)) | `test_patches.cpp` + `static_assert` | Tablas paralelas desalineadas | Sí (existencia) | No |
| `RdPianoEngine` ([§1](#1-el-problema-de-fondo-no-hay-frontera-entre-motor-y-plugin), [§8](#8-processblock-ocho-trabajos-una-función)) | `test_engine.cpp` | **Lo que hoy no ve nadie**: bloques, tasas, clics, NaN, reservas en RT | Sí | Sí |
| `LspUnit` / `SpaceD` / `Phaser` ([§12](#12-lsp-dos-transcripciones-con-la-misma-infraestructura)) | `test_lsp.cpp` | Que compartir el andamiaje no toque las transcripciones | No | No |
| Envoltorio del resampler ([§1](#1-el-problema-de-fondo-no-hay-frontera-entre-motor-y-plugin)) | `test_resampler.cpp` | Longitudes, deriva, handles fugados | No | No |
| Estado del plugin ([§9](#9-parámetros-hacerlo-a-mano-cuesta-120-líneas-y-se-desincroniza)) | `test_plugin_state.cpp` | Ida y vuelta de `get/setStateInformation` | No | No |

Las siete primeras filas viven en `librdpiano` y no necesitan JUCE. Las dos de `lsp/` y el
resampler solo lo necesitan si esos ficheros siguen en `rdpiano_juce/Source/`; con [§1](#1-el-problema-de-fondo-no-hay-frontera-entre-motor-y-plugin)
aplicado se mudan al núcleo y quedan igual de accesibles. La última necesita JUCE de verdad y solo
sale barata después de [§16.3](#16-build-dos-sistemas-sin-relación-y-una-ci-que-no-verifica-nada).

### 17.4 Núcleo: `RomLoader`, `RdBoard`, `CommandPort`

**`RomLoader`.** Es el candidato ideal a prueba unitaria: funciones puras sobre `u8[]`, sin CPU y
sin audio.

- Los `UNSCRAMBLE_*` son permutaciones de bits, luego son **biyectivas**: aplicarlas a los 256
  valores de un byte tiene que dar los 256, sin repetir. Es una propiedad, no un vector, y caza
  cualquier reordenación accidental de [§20](#20-qué-no-tocar) sin necesidad de escuchar nada.
- Hash FNV-1a del `params_rom` resultante para cada uno de los **16 offsets** de `patchToOffset`,
  capturado del código de hoy. 16 líneas en `vectors/params_rom.txt` que congelan `loadSounds`
  entero en milisegundos.
- Los bytes `0x00–0x02` parcheados apuntan al offset del parche seleccionado (hoy es un efecto
  lateral no comprobado de [mcu.cpp:617](../librdpiano/src/mcu.cpp#L617)).
- **Equivalencia del refactor de [§6](#6-loadsounds-hace-dos-cosas-independientes):**
  `loadRomSet(set); selectPatch(off)` debe dar byte a byte el mismo `params_rom` que el
  `loadSounds()` monolítico. Esa es la prueba que autoriza a partir la función.

**`RdBoard`.** El mapa de memoria se prueba escribiendo y leyendo, sin ejecutar una sola
instrucción: RAM por debajo de `0x1000`, `SoundChip` en `0x1000-0x1FFF`, latch en `0x2000-0x3FFF`,
la página de params conmutando con `latch_val & 0b11`, y la ROM de programa en `0xC000-0xFFFF`.
Un bucle sobre `0x0000-0x3FFF` comprobando que `& 0x1fff` y `& 0xdfff` coinciden documenta —y
retira— la coincidencia aritmética señalada en [§2](#2-mcu-hace-cuatro-trabajos).

**`CommandPort`.** Aquí la prueba tiene además valor de descubrimiento:

- Capturar la secuencia de bytes que emite `boot(0)` y compararla con la que hoy escriben a mano
  `mcuReset()` y [e2e.cpp:222-230](../librdpiano/test/e2e.cpp#L222-L230). **No son la misma
  secuencia**, y esa desigualdad es justamente el defecto 1 de [§3](#3-el-protocolo-del-firmware-está-esparcido-por-tres-capas):
  la prueba lo convierte en un fallo rojo en lugar de en un párrafo de este documento.
- Tabla de casos para `setMasterTune`: `-50`, `-1`, `0`, `+1`, `+50` y los bordes → pareja
  `(msb, lsb)` esperada. Una sola definición de por qué `0x3c`, `0x48` y `×16×4`, con las dos
  copias de [PluginProcessor.cpp:254](../rdpiano_juce/Source/PluginProcessor.cpp#L254) y
  [289](../rdpiano_juce/Source/PluginProcessor.cpp#L289) reducidas a una llamada.
- `selectPatch()` emite `0x31`/`0x30`; `allNotesOff()` emite algo (hoy no existe, y la prueba nace
  roja a propósito: es la especificación de [FIABILIDAD §3](FIABILIDAD-DIRECTO.md#3-n2--crítico--no-existe-panic-cc-120123121-se-ignoran-medido)).
- El anillo fijo: FIFO en orden, comportamiento definido al desbordar (descartar el más antiguo o
  el nuevo — pero *decidido*, no accidental), y **cero reservas** con el contador de `operator new`
  de [§17.6](#176-lo-que-solo-se-puede-probar-con-un-motor-de-verdad).

### 17.5 `SoundChip`: las LUT y los tres bloques

**LUT ([§4](#4-320-kb-de-lut-deterministas-recalculadas-por-instancia)).** Dos hashes, uno por
tabla, capturados hoy. Con eso:

- Compartirlas entre instancias queda demostrado idéntico (opción 1).
- Precalcularlas como blob queda demostrado idéntico al generador (opción 2) — y los dos
  `TODO: I want to believe there is a better way to compute this` dejan de dar miedo: el código
  ilegible queda fijado por su salida.
- Empaquetar los signos en el bit 15 (opción 3) se comprueba valor a valor sobre los `0x20000`
  índices, antes de medir si compensa.

**Los tres bloques ([§5](#5-soundchipupdate-tres-bloques-que-piden-ser-tres-funciones)).** No se
pueden probar por inspección: hay que capturarlos. El procedimiento, en el código de hoy y antes de
tocar nada:

1. Instrumentar `update()` para volcar, cada N muestras de un parche representativo, la `SA_Part`
   de entrada y las cuatro variables de salida (`volume`, `waverom_addr`, `ag3_sel_sample_type`,
   `ag1_phase_hi`) más el `s32` final.
2. Guardar ~2.000 casos en `vectors/ic_blocks.txt`. Cubren *por construcción* el arranque, el
   sostenido y la extinción; añadir a mano los bordes: envolvente que termina y dispara IRQ, el
   early-out `env_value==0 && env_dest==0`, el silenciado marcado `investigate`, y el wrap de fase
   en `0x3fff`.
3. Retirar la instrumentación, extraer `tick_ic19/ic9/ic8`, y comprobar los 2.000 casos.

Si los vectores pasan **y** los 16 hashes del golden siguen idénticos, la extracción fue neutra. Si
los hashes se mueven pero los vectores pasan, el cambio está en el orden de evaluación del bucle,
no en la aritmética — y ahí la regla de [§5](#5-soundchipupdate-tres-bloques-que-piden-ser-tres-funciones)
sigue mandando: se revierte, no se regenera el golden.

El bonus barato: `read(offset)` ignora su parámetro
([sound_chip.cpp:166](../librdpiano/src/sound_chip.cpp#L166)). Una prueba de dos líneas obliga a
decidir cuál de las dos lecturas es la correcta, en vez de dejar la ambigüedad escrita.

### 17.6 Lo que solo se puede probar con un motor de verdad

`test_engine.cpp` es la razón de ser de [§1](#1-el-problema-de-fondo-no-hay-frontera-entre-motor-y-plugin):
cada línea de esta lista es hoy inalcanzable, y todas describen fallos que se han visto o se pueden
ver en un DAW.

| Prueba | Qué comprueba |
|---|---|
| **Invariancia de bloque** | `render()` de 4.096 muestras de una vez, contra 7+13+1+512+… hasta 4.096: el stream tiene que ser **bit a bit el mismo**. Caza cualquier estado que se quede en una variable local del bloque |
| **Bloques de borde** | `numFrames` = 0, 1, `maxBlockSize` y por encima: sin desbordes, con ASan activo |
| **Tasas del host** | 44,1 / 48 / 88,2 / 96 kHz: longitud de salida exacta, `samplesError` acotado y **sin deriva** tras 10 minutos simulados |
| **Sin reservas en RT** | `operator new` global sustituido durante `render()` por uno que cuenta (y falla la prueba si se llama). Caza [FIABILIDAD §12](FIABILIDAD-DIRECTO.md#12-n10--medio--commands_queue-reserva-memoria-en-el-hilo-de-audio) y el `std::vector` del reparto de MIDI de [§8](#8-processblock-ocho-trabajos-una-función) |
| **Finitud** | Ni un `NaN` ni un `Inf` en 30 s con los cuatro efectos activos y todos los parámetros en sus extremos |
| **Cambio de parche en caliente** | `setPatch()` entre bloques: sin silencio inesperado y sin discontinuidad por encima de un umbral (detector de clics) |
| **Temporización del MIDI** | `pushMidi(frameOffset, …)` → la nota empieza a sonar en ese frame ±1. Es el test que fija [AUDITORIA §5](AUDITORIA.md#5-alto--la-temporización-midi-está-rota-y-el-reparto-es-on²-con-reservas-en-rt) |
| **Headroom** | Pico de 16 voces a velocidad 127 por debajo de fondo de escala, con el escalado seco **definido en un solo sitio** (hoy está en el plugin y copiado en [e2e.cpp:122](../librdpiano/test/e2e.cpp#L122)) |

Ninguna necesita JUCE ni un host: `RdPianoEngine` es C++ puro. Esto es exactamente el "simulador de
host" de [FIABILIDAD §17.1](FIABILIDAD-DIRECTO.md#171-un-simulador-de-host-el-que-más-fallos-habría-cazado),
pero **instanciando** la lógica en vez de copiar 130 líneas de `processBlock` dentro de un test.

**Efectos y resampler.** `SpaceD` y `Phaser` son deterministas y sin estado externo: un hash de la
respuesta a un impulso (y a un barrido), por cada tasa soportada, congela las transcripciones
*antes* de que [§12](#12-lsp-dos-transcripciones-con-la-misma-infraestructura) toque el andamiaje
común. Que `phaserRateTable` y `spaceDRateTable` sean idénticas pasa de ser un `diff` a mano en
[§21](#21-cómo-reproducir-las-medidas) a un `static_assert`. Del resampler interesan tres cosas: la
longitud de salida para cada ratio, la ausencia de `NaN` en los bordes, y que abrir y cerrar mil
veces no acumule handles — la fuga de [AUDITORIA §9](AUDITORIA.md#9-alto--fuga-de-12-mb-por-instancia-resamplers-nunca-cerrados).

### 17.7 Datos y plugin

**`patches.h`.** Con la fila única por parche de [§11](#11-tablas-de-datos-duplicadas), casi todo se
comprueba en compilación y no cuesta nada en ejecución:

```cpp
static_assert(std::size(kPatches) == NUM_PATCHES);
static_assert(all_of(kPatches, [](const Patch& p){ return p.offset < 0x20000; }));
static_assert(all_of(kPatches, [](const Patch& p){ return p.romSet < ROMSET_COUNT; }));
static_assert(all_of(kPatches, [](const Patch& p){ return p.sampleRate == 32000 || p.sampleRate == 44100; }));
```

Lo que no cabe en `static_assert` va a `test_patches.cpp`: que los cuatro ficheros de cada ROM set
existan en `roms/` y midan `0x20000` bytes, y que los nombres canónicos coincidan con los recursos
que el `.jucer` empotra. Es la prueba que impide que plugin, editor y harness vuelvan a discrepar.

**Plugin.** Con `APVTS` ([§9](#9-parámetros-hacerlo-a-mano-cuesta-120-líneas-y-se-desincroniza)) la
prueba que merece la pena es una sola y es barata: fijar los 20-y-pico parámetros a valores no por
defecto, `getStateInformation`, construir un processor nuevo, `setStateInformation`, y comprobar
que todos vuelven — incluidos parche y master tune. Un preset roto es de los fallos más caros para
un usuario y de los más fáciles de cazar. Requiere enlazar JUCE en un ejecutable de consola, lo
que es trivial con `juce_add_console_app` y prácticamente imposible con el Projucer: es una razón
más para [§16.3](#16-build-dos-sistemas-sin-relación-y-una-ci-que-no-verifica-nada).

`PluginEditor` no se prueba automáticamente. Con la tabla de [§10](#10-plugineditor-250-de-413-líneas-son-copia-pega),
lo que sí se puede comprobar sin abrir una ventana es que la tabla de botones cubra todos los
parámetros y no tenga índices duplicados.

### 17.8 Lo que estas pruebas **no** cubren

Conviene decirlo explícitamente, porque la tentación al ver una suite verde es creer que ya está:

- **El timbre.** Ninguna prueba de esta lista sabe si el piano suena bien. Eso sigue siendo el hash
  del golden (¿cambió?) más el oído (¿mejoró o empeoró?), con la regla intacta: si el hash se mueve,
  se renderizan los WAV, se escuchan, y solo entonces se regenera.
- **El comportamiento del host real.** Automatización de parámetros desde el DAW, suspensión,
  cambios de tasa en caliente, escaneo de plugins. Se aproximan con el motor, no se sustituyen.
- **La UI.**

Una suite unitaria verde con el golden roto es un cambio de audio no verificado, no un éxito.

### 17.9 Coste

| Fichero | Líneas aprox. | Cuándo | Tiempo de ejecución |
|---|---|---|---|
| `check.h` | 40 | Fase 0 | — |
| `test_patches.cpp` | 60 | Fase 0 | < 10 ms |
| `test_rom_loader.cpp` | 150 | Fase 1 | ~50 ms |
| `test_sa_tables.cpp` | 80 | Fase 1 | ~20 ms |
| `test_command_port.cpp` | 150 | Fase 1 | < 10 ms |
| `test_board.cpp` | 120 | Fase 1 | < 10 ms |
| `test_sound_chip_blocks.cpp` + vectores | 200 + datos | Fase 1 | ~30 ms |
| `test_lsp.cpp`, `test_resampler.cpp` | 150 | Fase 2 | ~100 ms |
| `test_engine.cpp` | 350 | Fase 2 | ~2 s |
| `test_plugin_state.cpp` | 80 | Fase 3 | < 10 ms |

Menos de 1.400 líneas en total, escritas a lo largo de las tres fases, y una suite que —salvo el
motor— termina en décimas de segundo. Frente a las 209 líneas de `processBlock` que hoy solo se
verifican abriendo un DAW, la proporción es favorable.

---

## 18. Qué habilita cada refactor (por qué merecen la pena)

| Refactor | Lo que pasa a ser verificable |
|---|---|
| [§1](#1-el-problema-de-fondo-no-hay-frontera-entre-motor-y-plugin) `RdPianoEngine` | Silencio bajo 32 kHz, clics al cambiar de parche, desbordes de buffer, headroom, guarda de NaN — hoy todo eso solo se ve en un DAW |
| [§3](#3-el-protocolo-del-firmware-está-esparcido-por-tres-capas) `CommandPort` | Que el harness pruebe **el arranque real**, no una copia; y que exista un sitio donde meter el panic MIDI |
| [§4](#4-320-kb-de-lut-deterministas-recalculadas-por-instancia) LUT compartidas | Un test que compare la tabla generada con el blob: la parte más ilegible del proyecto queda fijada |
| [§5](#5-soundchipupdate-tres-bloques-que-piden-ser-tres-funciones) IC19/IC9/IC8 | Tests por bloque con vectores conocidos, en vez de solo el hash agregado |
| [§6](#6-loadsounds-hace-dos-cosas-independientes) partir `loadSounds` | Medir el coste real de cambiar de parche |
| [§11](#11-tablas-de-datos-duplicadas) `patches.h` completo | Que plugin, editor y harness no puedan discrepar sobre qué es un parche |
| [§16](#16-build-dos-sistemas-sin-relación-y-una-ci-que-no-verifica-nada) CI | Todo lo anterior, en cada push |
| [§17](#17-las-pruebas-que-faltan-una-por-clase) suite unitaria | Que un fallo diga **qué** unidad se rompió, en vez de que un hash diga que algo cambió |

---

## 19. Plan por fases

Los pasos marcados **T** son de prueba. No son un apéndice de cada fase: van **antes** del refactor
al que acompañan, por la regla de [§17.1](#171-la-regla-caracterizar-antes-de-mover). Un paso de
código cuyo **T** todavía no está verde no se empieza.

**Fase 0 — sin riesgo de audio, sin tocar lógica** *(un par de tardes)*

1. Añadir `rdpiano_e2e` a la CI ([§16.1](#16-build-dos-sistemas-sin-relación-y-una-ci-que-no-verifica-nada)). Primero esto: es la red del resto.
2. **T** — `check.h` extraído de `e2e.cpp`, esqueleto `rdpiano_tests`, `enable_testing()`, y los dos ejecutables corriendo por `ctest` en la CI ([§17.2](#172-infraestructura-40-líneas-cero-dependencias)).
3. Borrar código muerto y convertir en documentación lo que es conocimiento ([§13](#13-código-muerto-y-campos-vestigiales)).
4. `ram[0x1000]`, `params_rom_tmp` como temporal, `& 0x1fff` ([§7](#7-memoria-que-no-hace-nada), [§2](#2-mcu-hace-cuatro-trabajos)) — con `test_board.cpp` escrito antes del cambio de máscara ([§17.4](#174-núcleo-romloader-rdboard-commandport)).
5. `inline constexpr` en `patches.h`; `static constexpr` en las tablas de `lsp/` ([§11](#11-tablas-de-datos-duplicadas), [§12](#12-lsp-dos-transcripciones-con-la-misma-infraestructura)).
6. **T** — `test_patches.cpp` y los `static_assert` de coherencia de la tabla de parches ([§17.7](#177-datos-y-plugin)).
7. Copia/movimiento `= delete` en `Mcu` y `SoundChip`; punteros crudos → miembros o `unique_ptr`; buffers → `std::vector` ([§14](#14-propiedad-y-ciclo-de-vida)).

**Fase 1 — estructura del núcleo** *(el hash no debe moverse)*

8. **T** — `test_sa_tables.cpp`: hash de las dos LUT tal como se generan hoy. Luego, LUT compartidas ([§4.1](#4-320-kb-de-lut-deterministas-recalculadas-por-instancia)).
9. **T** — `test_rom_loader.cpp`: biyección de los `UNSCRAMBLE_*` y hash del `params_rom` de los 16 offsets. Luego, `loadRomSet` / `selectPatch` ([§6](#6-loadsounds-hace-dos-cosas-independientes)), comprobando la equivalencia con el `loadSounds` monolítico.
10. **T** — `test_command_port.cpp`, incluida la comparación de la secuencia de arranque del plugin con la del harness (nace roja). Luego, `CommandPort`: `commands_queue` privada, `boot()`/`selectPatch()`/`setMasterTune()`/`allNotesOff()`; el harness pasa a llamarlas ([§3](#3-el-protocolo-del-firmware-está-esparcido-por-tres-capas)).
11. `RD_TRACE` ([§15](#15-el-núcleo-escribe-en-stdout)).
12. **T** — capturar los ~2.000 vectores de IC19/IC9/IC8 con el código actual instrumentado ([§17.5](#175-soundchip-las-lut-y-los-tres-bloques)).
13. Extraer IC19/IC9/IC8 — **commit aislado**, comprobando los vectores **y** los 16 hashes ([§5](#5-soundchipupdate-tres-bloques-que-piden-ser-tres-funciones)).

**Fase 2 — la frontera del motor** *(el cambio de fondo)*

14. **T** — `test_lsp.cpp` y `test_resampler.cpp`: respuesta a impulso de `SpaceD` y `Phaser` congelada antes de tocar el andamiaje común ([§12](#12-lsp-dos-transcripciones-con-la-misma-infraestructura), [§17.6](#176-lo-que-solo-se-puede-probar-con-un-motor-de-verdad)).
15. `RdPianoEngine` sin JUCE: mover ahí escalados, efectos, resampler y reparto de MIDI ([§1](#1-el-problema-de-fondo-no-hay-frontera-entre-motor-y-plugin), [§8](#8-processblock-ocho-trabajos-una-función)).
16. **T** — `test_engine.cpp` completo: invariancia de bloque, tasas del host, cero reservas en `render()`, finitud, clics al cambiar de parche, temporización del MIDI ([§17.6](#176-lo-que-solo-se-puede-probar-con-un-motor-de-verdad)). Es el "simulador de host" de [FIABILIDAD §17.1](FIABILIDAD-DIRECTO.md#171-un-simulador-de-host-el-que-más-fallos-habría-cazado), instanciando en vez de copiando.
17. Arreglar sobre el motor los defectos de `processBlock` de AUDITORIA §§1, 2, 4, 5, 8, 9, 11 — cada arreglo entra con la prueba que lo cubre, que debe fallar antes y pasar después.

**Fase 3 — plugin y build**

18. `APVTS` ([§9](#9-parámetros-hacerlo-a-mano-cuesta-120-líneas-y-se-desincroniza)).
19. Editor guiado por tablas ([§10](#10-plugineditor-250-de-413-líneas-son-copia-pega)).
20. `juce_add_plugin` y retirada del Projucer ([§16.3](#16-build-dos-sistemas-sin-relación-y-una-ci-que-no-verifica-nada)).
21. **T** — `test_plugin_state.cpp`: ida y vuelta de presets con `juce_add_console_app`, ya posible sin Projucer ([§17.7](#177-datos-y-plugin)).
22. Separar `Hd63701Cpu` / `RdBoard` ([§2](#2-mcu-hace-cuatro-trabajos)) — lo último, cuando ya haya pruebas por bloque.

---

## 20. Qué NO tocar

- **`mcu_ops.h` y `mame_utils.h`** — código derivado de MAME (BSD-3). No reescribir por estilo;
  mantener la atribución. Si algún día se separa el core de CPU, se mueve entero, sin editar.
- **Los cuerpos transcritos de `spaced.cpp` y `phaser.cpp`** — los `accA_NNN` son la transcripción
  ciclo a ciclo del DSP original. Se comparte el andamiaje ([§12](#12-lsp-dos-transcripciones-con-la-misma-infraestructura)); el cuerpo se deja intacto.
- **La aritmética de `SoundChip::update()`** — se puede *mover* a funciones, nunca "simplificar".
  Cada `& 0x3fff`, cada `+ 1`, cada `|= 0x3c00` replica un sumador real.
- **Los `UNSCRAMBLE_*`** — permutaciones de pines del PCB. Ninguna reordenación es cosmética.
- **Las direcciones de handshake `0xE12B/0xE15E/0xE168/0xE15A`** — atadas al firmware RD200_B.
  Se pueden mover a constantes con nombre; no se pueden cambiar.
- **Los vectores capturados y `golden.txt`** — se regeneran cuando el cambio de audio es
  *intencionado* y se ha escuchado ([§17.1](#171-la-regla-caracterizar-antes-de-mover)), nunca para
  que una prueba en rojo se ponga verde.
- **`re_stuff/`** — material de investigación, no fuente de verdad; su propio README avisa de que
  buena parte está mal. No se compila y no debe entrar en ningún build.

---

## 21. Cómo reproducir las medidas

**Línea base del harness** (lo que hay que ver verde antes y después de cada paso):

```bash
cd librdpiano && cmake -B build -DRDPIANO_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target rdpiano_e2e
cd .. && ./librdpiano/build/rdpiano_e2e --roms roms --golden librdpiano/test/golden.txt
# → 16 parche(s), 0 comprobacion(es) fallida(s), 0 hash(es) distinto(s) del golden   (~3,5 s)
```

**Suite unitaria** ([§17](#17-las-pruebas-que-faltan-una-por-clase)) — todavía no existe; una vez
esté, con `enable_testing()` en el `CMakeLists.txt` las dos redes se lanzan juntas y es lo único que
hay que recordar:

```bash
cd librdpiano/build && ctest --output-on-failure       # unit (< 1 s) + e2e (~3,5 s)
./rdpiano_tests --filter rom_loader                    # una sola suite, al iterar
```

**Tamaño de instancia y coste de construcción** ([§4](#4-320-kb-de-lut-deterministas-recalculadas-por-instancia), [§6](#6-loadsounds-hace-dos-cosas-independientes), [§7](#7-memoria-que-no-hace-nada)) — programa de ~25 líneas contra el núcleo:

```cpp
printf("%zu\n", sizeof(Mcu));                    // 1.455.216
printf("%zu\n", sizeof(SoundChip));              // 1.119.240
// cronometrar: new SoundChip(...)               // 17,88 ms
// cronometrar: sc->load_samples(...) x10 / 10   //  2,06 ms
// cronometrar: new Mcu(...) y mcu->loadSounds() // 21,66 ms / 2,86 ms
```

```bash
clang++ -std=c++17 -O2 -Wno-constant-logical-operand -Ilibrdpiano/include \
        librdpiano/src/*.cpp medida.cpp -o medida && ./medida
```

**Conteo de duplicación** ([§3](#3-el-protocolo-del-firmware-está-esparcido-por-tres-capas), [§11](#11-tablas-de-datos-duplicadas)):

```bash
grep -rn "commands_queue" rdpiano_juce/Source librdpiano | grep -v Builds   # 22 push fuera del núcleo
grep -n "tuneLsb" rdpiano_juce/Source/PluginProcessor.cpp                   # dos copias del cifrado
diff <(sed -n '16,26p' rdpiano_juce/Source/lsp/spaced.cpp) \
     <(sed -n '16,26p' rdpiano_juce/Source/lsp/phaser.cpp)   # sin salida: tablas idénticas
```

Las medidas de tiempo son de esta máquina (macOS, Apple Silicon, `-O2`); lo que importa no es el
valor absoluto sino la proporción: **el 88 % del coste de construir un `SoundChip` son tablas que no
dependen de nada.**
