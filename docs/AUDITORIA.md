# Auditoría de código — RdPiano

**Fecha:** 2026-08-26 · **Rama:** `main` @ `995e067` · **Alcance:** `librdpiano/`, `rdpiano_juce/Source/`, build y CI.
**Criterio:** solo problemas graves. Se excluyen estilo, nomenclatura, warnings cosméticos y los
`HACK:`/`TODO:` que el propio proyecto ya documenta como comportamiento conocido.

Cada hallazgo marcado **[verificado]** se comprobó ejecutando el núcleo del emulador con las ROMs
reales del repositorio, compilado con ASan/UBSan e instrumentado. El apartado
[§20](#20-cómo-reproducir-las-mediciones) explica cómo reproducirlo.

> **Estado a 2026-09-01 — los tres CRÍTICOS y los seis ALTOS están resueltos.** Las líneas que cita
> el informe son las del commit auditado; desde entonces la cadena de audio se sacó del plugin a
> `RdPianoEngine` ([librdpiano/src/rd_engine.cpp](../librdpiano/src/rd_engine.cpp)), así que los
> enlaces a `PluginProcessor.cpp` ya no apuntan a donde estaba el problema. Ver
> [§1](#1-crítico--silencio-total-por-debajo-de-32-khz),
> [§2](#2-crítico--el-resampler-se-construye-dentro-del-hilo-de-audio--verificado) y
> [§3](#3-crítico--loadsounds-bajo-spinlock-desde-el-hilo-de-ui--verificado).
>
> Los ALTOS §4, §5, §8 y §9 cayeron con esa misma extracción; §6, §7 y §16 se cerraron después y
> llevan su propia nota al pie de cada apartado. Lo que queda abierto son los MEDIOS (§10–§15, §17)
> y los detalles de §18.

---

## 0. Resumen

| # | Severidad | Problema | Ubicación |
|---|---|---|---|
| 1 | ~~**CRÍTICO**~~ **RESUELTO** | Silencio total del plugin a cualquier frecuencia de muestreo < 32 kHz (buffer mal dimensionado) | [PluginProcessor.cpp:337](rdpiano_juce/Source/PluginProcessor.cpp#L337) |
| 2 | ~~**CRÍTICO**~~ **RESUELTO** | `resample_open()` (2,5–3,2 ms × 2 + ~1,2 MB de `malloc`) se ejecuta en el hilo de audio | [PluginProcessor.cpp:497](rdpiano_juce/Source/PluginProcessor.cpp#L497) |
| 3 | ~~**CRÍTICO**~~ **RESUELTO** | `loadSounds()` (1,7–2,7 ms) corre desde el hilo de UI con un **spinlock** que bloquea al hilo de audio | [PluginProcessor.cpp:250-259](rdpiano_juce/Source/PluginProcessor.cpp#L250-L259) |
| 4 | ~~**ALTO**~~ **RESUELTO** | Desbordamiento de escritura si `buffer.getNumSamples() > samplesPerBlock` | [PluginProcessor.cpp:428](rdpiano_juce/Source/PluginProcessor.cpp#L428) |
| 5 | ~~**ALTO**~~ **RESUELTO** | Toda la temporización MIDI se colapsa al inicio del bloque (condición invertida) + `O(n²)` con `std::vector` en el hilo de audio | [PluginProcessor.cpp:433-457](rdpiano_juce/Source/PluginProcessor.cpp#L433-L457) |
| 6 | ~~**ALTO**~~ **RESUELTO** | `SA_Part` sin inicializar → índice de wave ROM fuera de rango (0x52FEC sobre un array de 0x20000) | [sound_chip.h:38-45](librdpiano/include/sound_chip.h#L38-L45), [sound_chip.cpp:284](librdpiano/src/sound_chip.cpp#L284) |
| 7 | ~~**ALTO**~~ **RESUELTO** | `printf()` + `fflush()` en el hilo de audio (emulador y plugin) | [mcu.cpp:501](librdpiano/src/mcu.cpp#L501), [PluginProcessor.cpp:413](rdpiano_juce/Source/PluginProcessor.cpp#L413) |
| 8 | ~~**ALTO**~~ **RESUELTO** | `return` temprano en `processBlock` sin limpiar el buffer de salida | [PluginProcessor.cpp:411-423](rdpiano_juce/Source/PluginProcessor.cpp#L411-L423) |
| 9 | ~~**ALTO**~~ **RESUELTO** | Fuga de ~1,2 MB por instancia: los resamplers nunca se cierran | [PluginProcessor.cpp:210](rdpiano_juce/Source/PluginProcessor.cpp#L210) |
| 10 | **MEDIO** | Desbordamiento de entero con signo en `m_icount` a los ~5,9 min de reproducción | [mcu.cpp:372](librdpiano/src/mcu.cpp#L372) |
| 11 | **MEDIO** | Coeficientes IIR reasignados (con `malloc`) en cada bloque de audio | [PluginProcessor.cpp:541](rdpiano_juce/Source/PluginProcessor.cpp#L541) |
| 12 | **MEDIO** | Fuga en `prepareToPlay()` si el host no intercala `releaseResources()` | [PluginProcessor.cpp:339-342](rdpiano_juce/Source/PluginProcessor.cpp#L339-L342) |
| 13 | **MEDIO** | `Lcd::setText()` copia 34 bytes fijos de una `juce::String` arbitraria; `"\xff"` genera UTF-8 inválido | [Lcd.cpp:21](rdpiano_juce/Source/lcd/Lcd.cpp#L21) |
| 14 | **MEDIO** | Decodificación de registros del chip: `offset % 8` pliega los bytes +8..+F sobre +0..+7 | [sound_chip.cpp:175](librdpiano/src/sound_chip.cpp#L175) |
| 15 | **MEDIO** | `Mcu::reset()` no reinicia el `SoundChip`, la RAM, el latch, el timer ni la cola de comandos | [mcu.cpp:262](librdpiano/src/mcu.cpp#L262) |
| 16 | ~~**ALTO** (standalone)~~ **RESUELTO** | Carrera de datos y *use-after-free* en `test/standalone.cpp` | [standalone.cpp:131,204](librdpiano/test/standalone.cpp#L131) |
| 17 | **MEDIO** | CI: acción de release de terceros archivada y sin fijar (`@latest`) con `GITHUB_TOKEN` | [.github/workflows/main.yml](.github/workflows/main.yml) |

---

## 1. CRÍTICO — Silencio total por debajo de 32 kHz

[`prepareToPlay`, PluginProcessor.cpp:337](rdpiano_juce/Source/PluginProcessor.cpp#L337)

```cpp
double ratio = sampleRate / 32000;                       // <-- invertido
emu_sample_buffer_size = ceil(samplesPerBlock * ratio);
```

El búfer intermedio guarda muestras a la **frecuencia del emulador** (20 000 o 32 000 Hz) generadas
a partir de `samplesPerBlock` muestras del **host**. La cantidad necesaria es
`samplesPerBlock × sourceSampleRate / hostSampleRate`, es decir el factor **inverso** del que se usa.

Cuando `hostSampleRate ≥ 32 kHz` el factor sobredimensiona el búfer y el error queda oculto. Por
debajo de 32 kHz el búfer se queda corto, salta la guarda de
[PluginProcessor.cpp:417](rdpiano_juce/Source/PluginProcessor.cpp#L417) y `processBlock` **retorna sin
escribir nada**, en todos los bloques:

```
host SR   spb  buf_emu  necesita@20k  necesita@32k  resultado
  48000   512      768           214           342  OK
  44100   512      706           233           372  OK
  32000   512      512           320           512  OK
  22050   512      353           465           744  SILENCIO (todos los patches)
  16000   512      256           640          1024  SILENCIO
   8000   512      128          1280          2048  SILENCIO
```

**Impacto.** El plugin es completamente mudo a 22,05 / 16 / 11,025 / 8 kHz, escupiendo
`"Too many samples to render"` por `stdout` en cada bloque. Afecta a renders offline a baja
frecuencia, AUv3 en iOS y modos de bajo consumo.

**Corrección.** `double ratio = 32000.0 / sampleRate;` — y dimensionar `emu_resampled_*` con el
tamaño real de bloque, no con `samplesPerBlock` (ver [§4](#4-alto--desbordamiento-de-escritura-en-los-búferes-de-salida)).

> **RESUELTO.** `RdPianoEngine::prepare()` dimensiona el búfer del emulador con
> `worstRatio = 32000.0 / hostRate` —el factor en el sentido bueno, y por el peor caso de 32 kHz,
> porque el parche cambia sin volver a preparar— y el de salida por el tamaño de bloque preparado,
> con la guarda `blockTooLarge` de `render()` para el bloque que se pase (eso cierra también §4 y
> §8: los retornos tempranos limpian la salida). `test_engine.cpp` barre de 22 a 96 kHz.

---

## 2. CRÍTICO — El resampler se construye dentro del hilo de audio  **[verificado]**

[PluginProcessor.cpp:492-499](rdpiano_juce/Source/PluginProcessor.cpp#L492-L499)

```cpp
if (savedDestSampleRate != destSampleRate || savedSourceSampleRate != sourceSampleRate) {
  ...
  resampleL = resample_open(1, ratio, ratio);   // en processBlock()
  resampleR = resample_open(1, ratio, ratio);
}
```

`resample_open(highQuality=1)` calcula un filtro Kaiser de `Npc·(Nmult-1)/2 = 69 632` coeficientes
(función de Bessel I₀ por coeficiente) y reserva 5 bloques con `malloc`.

**Medido en esta máquina:**

```
resample_open(highQuality=1): media 2,53 ms, peor 3,22 ms
presupuesto de un bloque de 128 frames @48k = 2,67 ms
```

Son **dos** llamadas (L y R) ≈ **5–6 ms de cómputo + ~1,2 MB de `malloc`** en el hilo de tiempo real.
Ocurre en el primer bloque y **cada vez que el usuario cambia entre un patch de 20 kHz y uno de
32 kHz** (p. ej. Piano 1 → Harpsichord), que es un clic de botón normal.

**Impacto.** Xrun garantizado y audible en cada cambio de patch que cruce frecuencias.

**Corrección.** Abrir los dos resamplers en `prepareToPlay()` con `minFactor`/`maxFactor` que cubran
todo el rango (`hostSR/32000` … `hostSR/20000`) y no volver a tocarlos; `resample_process()` ya acepta
un `factor` variable dentro de ese rango.

> **RESUELTO** exactamente así: los dos `resample_open(1, hostRate/32000, hostRate/20000)` viven en
> `RdPianoEngine::prepare()` y `release()` los cierra (eso cierra también §9, la fuga de 1,2 MB).
> `render()` no los vuelve a abrir; la suite `engine_no_alloc_in_render` lo vigila con
> `stats.resamplerOpens`, porque libresample usa `malloc` y el `operator new` sustituido no lo vería.

---

## 3. CRÍTICO — `loadSounds()` bajo spinlock desde el hilo de UI  **[verificado]**

[`setCurrentProgram`, PluginProcessor.cpp:250-259](rdpiano_juce/Source/PluginProcessor.cpp#L250-L259) ·
[`setStateInformation`, PluginProcessor.cpp:642-650](rdpiano_juce/Source/PluginProcessor.cpp#L642-L650)

```cpp
mcuLock.enter();                       // juce::SpinLock
mcu->loadSounds(...);                  // 3 × 128 K de desmezclado + 2 × 128 K de copia
mcu->commands_queue.push(0x31);
mcuLock.exit();
```

**Medido:** `loadSounds` tarda **1,71 ms de media y 2,73 ms en el peor caso**, frente a un
presupuesto de 2,67 ms para un bloque de 128 frames a 48 kHz.

Dos problemas se suman:

1. **`juce::SpinLock` es un cerrojo de espera activa.** Mientras el hilo de UI ejecuta `loadSounds`,
   el hilo de audio —que corre con prioridad de tiempo real— quema su quantum girando. Si el
   planificador desaloja al hilo de UI durante esa ventana, se produce una **inversión de prioridad**
   clásica: el hilo RT no puede progresar y no cede la CPU.
2. `loadSounds` reserva además **384 KB en la pila** (`u8 ic5[0x20000]` ×3 en
   [sound_chip.cpp:353-355](librdpiano/src/sound_chip.cpp#L353)). En el hilo principal (8 MB) no hay
   problema, pero la función es alcanzable desde cualquier hilo que llame a `setCurrentProgram`, y
   los hilos secundarios en macOS tienen 512 KB por omisión.

Un cambio de patch dispara **§2 + §3 juntos**: ≈ 8 ms de parada.

`setMasterTune()` ([PluginProcessor.cpp:277](rdpiano_juce/Source/PluginProcessor.cpp#L277)) tiene la
misma forma más suave: **0,18 ms × 2 = 0,36 ms** de emulación bajo el mismo spinlock, pero se invoca
**en cada evento de arrastre del dial** desde
[PluginEditor.cpp:257](rdpiano_juce/Source/PluginEditor.cpp#L257).

**Corrección.** Preparar el nuevo estado fuera del cerrojo y publicarlo con un intercambio de
puntero atómico (doble búfer), o hacer el trabajo pesado en un hilo de fondo y entregarlo al hilo de
audio por una FIFO sin bloqueo. Si se conserva un cerrojo, debe ser `tryEnter()` desde el lado de
audio con un camino de repliegue, nunca una espera activa.

> **RESUELTO** por la primera vía —doble búfer— más el repliegue del lado de audio:
>
> 1. **La parte cara sale del cerrojo.** `SoundChip` tiene ahora dos juegos de tablas de onda:
>    `decode_samples()` descifra contra el de reserva, que `update()` no lee, y `publish_samples()`
>    lo activa intercambiando el puntero. Eso sube por `RdBoard::prepareRomSet`/`publishRomSet` y
>    `Mcu` hasta `RdPianoEngine::prepareRomSetFor()`, que `setCurrentProgram` llama **antes** de
>    tomar el cerrojo. Bajo cerrojo quedan el intercambio de punteros, `selectPatch()` y
>    `reloadPatch()`: de ~2,9 ms a ~0,03 ms. Cuesta 768 KB por instancia.
> 2. **El hilo de audio ya no hace espera activa sin límite.** `processBlock` usa
>    `acquireEngineLock()`: `tryEnter()` y, si falla, reintentos con `Thread::yield()` hasta un
>    plazo de **un cuarto del bloque**; agotado el plazo devuelve silencio y suma
>    `blocksPreempted`. La inversión de prioridad deja de ser ilimitada, y el plazo cubre de sobra
>    al único que puede tener el cerrojo un rato (`setMasterTune`, ~0,36 ms), así que en uso normal
>    no se pierde ningún bloque. Los eventos MIDI ya están encolados en el motor cuando esto pasa:
>    el siguiente `render()` los entrega, no se pierden.
> 3. **El dial deja de martillear.** `setCurrentProgram` y `setMasterTune` salen antes si el valor
>    no cambia: el dial del panel los dispara en cada evento de arrastre y la mayoría repetían el
>    valor puesto.
>
> Los 384 KB de pila de `loadSounds` ya no existen: `decode_samples()` descifra byte a byte, sin
> temporales. La suite `engine_patch_prepare` de `test_engine.cpp` fija que partir la carga en dos
> fases dé el **mismo audio muestra a muestra** que hacerla de una.

---

## 4. ALTO — Desbordamiento de escritura en los búferes de salida

[PluginProcessor.cpp:428-431](rdpiano_juce/Source/PluginProcessor.cpp#L428-L431) y
[519-521](rdpiano_juce/Source/PluginProcessor.cpp#L519-L521)

```cpp
emu_resampled_sample_bufferL = new float[samplesPerBlock];   // prepareToPlay
...
for (size_t i = 0; i < buffer.getNumSamples(); i++)          // processBlock
  emu_resampled_sample_bufferL[i] = 0;                       // sin ninguna guarda
```

`maximumExpectedSamplesPerBlock` es un **valor esperado**, no un máximo garantizado por el contrato
de JUCE; varios hosts entregan bloques mayores (cambios de dispositivo, render offline, freeze de
pista). La guarda de [PluginProcessor.cpp:417](rdpiano_juce/Source/PluginProcessor.cpp#L417) solo
comprueba `emu_sample_buffer_size` (el búfer de *entrada* del resampler), **nunca** el tamaño de los
búferes remuestreados. Un bloque mayor que `samplesPerBlock` escribe fuera del montículo.

**Corrección.** Guardar `preparedBlockSize` y salir limpiamente (o reasignar) si
`buffer.getNumSamples()` lo supera; y dimensionar por `getNumSamples()`, no por el valor de
`prepareToPlay`.

> **RESUELTO.** `RdPianoEngine::prepare()` guarda el tamaño de bloque preparado (`maxBlock`,
> legible por `preparedBlockSize()`) y dimensiona con él tanto los búferes de salida
> (`outCapacity`) como el del emulador. `render()` comprueba `numFrames > outCapacity` **antes** de
> escribir nada: devuelve silencio y suma `stats.blockTooLarge`. `test_engine.cpp` entrega bloques
> irregulares y por encima del preparado.

---

## 5. ALTO — La temporización MIDI está rota, y el reparto es `O(n²)` con reservas en RT

[PluginProcessor.cpp:433-457](rdpiano_juce/Source/PluginProcessor.cpp#L433-L457)

```cpp
std::vector<int> processedEvents;                     // reserva en el hilo de audio
...
for (int i = 0; i < renderBufferFrames; i++) {
  for (const auto metadata : midiMessages) {
    if (metadata.samplePosition >= i && std::find(...) == processedEvents.end()) {
      mcu->sendMidiCmd(...);
      processedEvents.push_back(evI);                 // reserva en el hilo de audio
    }
```

Tres defectos superpuestos:

1. **Condición invertida.** En `i == 0` se cumple `samplePosition >= 0` para *todos* los eventos,
   así que **el bloque entero de MIDI se consume en la primera muestra**. La resolución
   intra-bloque se pierde por completo: a 48 kHz con bloques de 512, todo se cuantiza a ~10,7 ms.
   La comparación correcta es `samplePosition <= i`.
2. **Unidades mezcladas.** `metadata.samplePosition` está en muestras del *host*; `i` recorre
   muestras del *emulador* (20/32 kHz). Aunque se arregle el sentido de la comparación, hay que
   convertir: `i_host = samplePosition * sourceSampleRate / destSampleRate`.
3. **Coste y reservas.** `std::vector` + `std::find` dentro de un bucle de ~700 iteraciones da
   `O(renderBufferFrames × nEventos)` con `push_back` (que puede reasignar) en el hilo de audio.
   `juce::MidiBuffer` ya viene ordenado por `samplePosition`: basta un iterador que avance en
   paralelo al bucle de muestras, sin contenedor auxiliar ni búsqueda.

El "flush" final de [PluginProcessor.cpp:548-566](rdpiano_juce/Source/PluginProcessor.cpp#L548-L566)
existe únicamente para tapar (1); con la comparación correcta sobra.

**Relacionado:** [PluginProcessor.cpp:452](rdpiano_juce/Source/PluginProcessor.cpp#L452) lee
`getRawData()[1]` y `[2]` sin mirar `getRawDataSize()`. Para mensajes cortos JUCE usa
almacenamiento interno de 4 bytes y no hay lectura fuera de límites, pero un SysEx de 2 bytes
(`F0 F7`) sí reside en el montículo y se leería un byte de más.

> **RESUELTO.** El reparto vive ahora en `RdPianoEngine::render()`: un índice `nextEvent` que
> avanza en paralelo al bucle de muestras, sin contenedor auxiliar ni `std::find` —y sin reservas,
> porque la cola es un anillo fijo (`kMidiQueueSize`) que llena `pushMidi()`. La comparación va en
> el sentido bueno y convierte de muestras de host a muestras de emulador con
> `hostToEmu = sourceRate/destSampleRate`, así que la resolución intra-bloque se conserva. El
> vaciado final solo recoge lo que quede fuera de orden. En el plugin, `processBlock` mira
> `metadata.numBytes` antes de leer `data[1]`/`data[2]`.

---

## 6. ALTO — Campos de `SA_Part` sin inicializar → índice de wave ROM fuera de rango  **[verificado]**

[sound_chip.h:34-46](librdpiano/include/sound_chip.h#L34-L46)

```cpp
struct SA_Part {
  uint32_t sub_phase = 0;
  uint32_t env_value = 0;
  uint16_t pitch_lut_i;      // <-- sin inicializador
  uint8_t  wave_addr_loop;   // <--
  uint8_t  wave_addr_high;   // <--
  uint8_t  env_dest;         // <--
  ...
};
```

Solo dos de los diez campos tienen inicializador. La guarda de salida rápida de
[sound_chip.cpp:225](librdpiano/src/sound_chip.cpp#L225) exige `env_value == 0 && env_dest == 0`;
con `env_dest` indeterminado, la parte se procesa con datos basura antes de que el firmware llegue a
escribir sus registros. La primera llamada a `update()` sucede **antes** de los primeros ciclos de
CPU dentro de `generate_next_sample()`, así que esa ventana siempre existe.

Y el cálculo de dirección de [sound_chip.cpp:284](librdpiano/src/sound_chip.cpp#L284) no está
enmascarado:

```cpp
waverom_addr = (part.wave_addr_high << 11) | ((part.sub_phase >> 9) & 0x7ff);
```

`wave_addr_high` es un `uint8_t` completo → la dirección puede llegar a **0x7FFFF**, mientras que
`samples_exp[]`, `samples_delta[]` y los dos arrays de signo son de **0x20000**. El bus de direcciones
real de la wave ROM tiene 17 bits (lo confirma el uso de `BIT(waverom_addr, 16)` en la línea
siguiente): falta un `& 0x1FFFF`.

**Comprobación con UBSan** (construyendo el `Mcu` sobre memoria rellena con `0xA5`, es decir memoria
reciclada en lugar de páginas nuevas):

```
sound_chip.cpp:307: runtime error: index 338246 out of bounds for type 'uint16_t[131072]'
sound_chip.cpp:309: runtime error: index 338246 out of bounds for type 'bool[131072]'
sound_chip.cpp:251: runtime error: load of value 165, which is not a valid value for type 'bool'
[64 muestras tras reset, heap sucio] max_waverom=052fec  OOB(>=0x20000)=3031  total=5120
```

**Calibración honesta del riesgo.** Con el asignador por omisión de macOS, `new Mcu` (1 455 216 bytes)
va a `mmap` y devuelve páginas a cero, de modo que hoy los campos salen en 0 y el fallo no se
manifiesta — lo comprobé también reutilizando la misma región tras destruir una instancia previa, y
volvió limpia. El defecto es por tanto **latente, no activo**: es UB formal, la máscara falta de
forma objetiva, y salta en cuanto la memoria no venga a cero (asignador con *pooling*, `placement new`,
otra plataforma, o cualquier valor de `wave_addr_high > 0x3F` proveniente del firmware). Los índices
desbordados caen dentro del propio objeto `Mcu`, así que ASan no los detecta: se leerían otras LUT
como si fueran muestras, con salida de amplitud arbitraria.

**Corrección (dos líneas).** Inicializar todos los miembros de `SA_Part` a `0`, y enmascarar
`waverom_addr &= 0x1FFFF`.

> **RESUELTO**, las dos líneas, en [sa_blocks.h](../librdpiano/include/sa_blocks.h): los diez
> miembros de `SA_Part` llevan inicializador y `tick_ic9()` cierra la dirección con `& 0x1ffff`.
> La máscara no toca `sel_sample_type` ni `phase_hi` (solo miran bits ≤ 16) y en el camino nominal
> el firmware nunca pasa de `wave_addr_high = 0x3F`, así que **no mueve el audio**: los 16 hashes
> de `golden.txt` y los 2.256 vectores de `ic_blocks.txt` siguen en verde sin tocarlos.
>
> Comprobado con la misma sonda de §20 —`placement new` sobre un búfer relleno de `0xA5`, 4.096
> muestras, `-fsanitize=undefined -fno-sanitize-recover=all`—: el código anterior emite las cargas
> de `bool` inválidas (`load of value 165`) en cinco puntos de `sa_blocks.h`; con el arreglo la
> sonda termina limpia.

---

## 7. ALTO — `printf()` en el hilo de audio

| Ubicación | Disparador |
|---|---|
| [mcu.cpp:501](librdpiano/src/mcu.cpp#L501), [517](librdpiano/src/mcu.cpp#L517), [549](librdpiano/src/mcu.cpp#L549) | acceso a dirección no mapeada desde el firmware |
| [mcu_ops.h:21,26,32,39,47](librdpiano/include/mcu_ops.h#L21) | `#define logerror printf` — opcode ilegal / `trap` |
| [sound_chip.cpp:179](librdpiano/src/sound_chip.cpp#L179) | escritura inválida al chip |
| [PluginProcessor.cpp:413,419](rdpiano_juce/Source/PluginProcessor.cpp#L413) | `printf` + **`fflush`** en `processBlock` |
| [PluginProcessor.cpp:512,558](rdpiano_juce/Source/PluginProcessor.cpp#L512) | `"click: %d"`, `"leftover midi"` |

Todos estos caminos son alcanzables desde `generate_next_sample()` / `processBlock()`. `printf` toma
el cerrojo de `stdout` y `fflush` fuerza una llamada al sistema; ambos pueden bloquear
indefinidamente en el hilo de tiempo real. En el propio banco de pruebas los vi dispararse:

```
0000: unk device read 0000
m6800: illegal opcode: address 0003, op 00
```

Peor aún, `printf("Too many samples to render")` se ejecuta **en cada bloque** en el escenario de §1.

**Corrección.** Sustituir por un contador atómico consultado desde la UI, o compilar la traza solo en
`DEBUG`. La condición de §1 debe además silenciar la salida en lugar de dejarla intacta (§8).

**Nota menor de la misma familia:** en [mcu.cpp:501](librdpiano/src/mcu.cpp#L501) los argumentos están
intercambiados respecto a las otras dos trazas (`addr` y `PCD` al revés), lo que hace engañoso el
diagnóstico.

> **RESUELTO.** No queda un solo `printf` en el núcleo ni en el plugin: toda la traza sale por
> `RD_TRACE` ([rd_trace.h](../librdpiano/include/rd_trace.h)), que sin `-DRDPIANO_TRACE` compila a
> nada —los argumentos ni se evalúan— y con ella va a donde diga `rdpiano_set_trace_sink()`, no a
> `stdout`. `mcu_ops.h` conserva su `logerror` de MAME, redefinido a `RD_TRACE`. Los caminos de §1
> y §8 son ahora contadores en `RdEngineStats` (`blockTooLarge`, `tooFewFrames`, `tooManyFrames`,
> `midiDropped`) que la UI puede consultar. La nota menor también: la traza de lectura no mapeada
> imprime ya `PCD` primero, como las otras dos.

---

## 8. ALTO — Retorno temprano sin limpiar la salida

[PluginProcessor.cpp:411-423](rdpiano_juce/Source/PluginProcessor.cpp#L411-L423)

```cpp
if (renderBufferFrames < 2)  { printf(...); return; }
if (renderBufferFrames > 20000 || renderBufferFrames > emu_sample_buffer_size) { printf(...); return; }
```

El plugin declara un **bus de entrada estéreo**
([PluginProcessor.cpp:113](rdpiano_juce/Source/PluginProcessor.cpp#L113)) pese a estar marcado como
`pluginIsSynth` en el `.jucer`, y el bucle de limpieza de
[PluginProcessor.cpp:404](rdpiano_juce/Source/PluginProcessor.cpp#L404) solo borra canales *por encima*
del número de entradas — es decir, ninguno. Al retornar, `buffer` conserva lo que puso el host: la
entrada, o contenido rancio del bloque anterior. En el escenario de §1 eso significa que en vez de
silencio se emite basura o realimentación en cada bloque.

**Corrección.** `buffer.clear(); return;` en ambos caminos, y eliminar el bus de entrada si el
plugin es un sintetizador puro.

> **RESUELTO.** Los cuatro retornos tempranos de `RdPianoEngine::render()` llaman a `silence()`
> sobre el bloque entero antes de salir, y en el plugin los dos de `processBlock` —bloque
> preemptado por el cerrojo, y buffer sin los dos canales— hacen `buffer.clear()`.

---

## 9. ALTO — Fuga de ~1,2 MB por instancia (resamplers nunca cerrados)

[PluginProcessor.cpp:210-219](rdpiano_juce/Source/PluginProcessor.cpp#L210-L219)

El destructor libera `mcu`, `spaceD` y `phaser`, pero **nunca** llama a `resample_close()`;
`releaseResources()` tampoco. Los dos manejadores solo se cierran dentro de `processBlock` al cambiar
de frecuencia. Cada manejador con `highQuality=1` reserva:

```
Imp  + ImpD : 2 × 69 632 floats  ≈ 557 KB
X           : (4096 + Xoff)      ≈  17 KB
Y           :  XSize·maxFactor   ≈  25 KB
                                 ─────────
                          ≈ 600 KB × 2 manejadores ≈ 1,2 MB por instancia destruida
```

En un DAW que instancia, escanea y destruye plugins repetidamente (o al duplicar pistas) esto se
acumula sin techo.

**Corrección.** Cerrar ambos en el destructor y en `releaseResources()`, poniéndolos a `nullptr`.

> **RESUELTO.** `RdPianoEngine::release()` cierra los dos manejadores con `resample_close()` y los
> pone a `nullptr`; la llaman tanto `releaseResources()` como el destructor del motor, y
> `prepare()` de entrada, así que un `prepareToPlay()` repetido tampoco fuga (eso cubre §12 por el
> lado del motor).

---

## 10. MEDIO — Desbordamiento de entero con signo en `m_icount`  **[verificado]**

[mcu.cpp:370-373](librdpiano/src/mcu.cpp#L370-L373)

```cpp
void Mcu::increment_counter(int amount) { m_icount -= amount; }
```

`m_icount` solo se decrementa. Su único consumidor, `eat_cycles()`, quedó **muerto** al comentarse el
bucle `do/while` de [`execute_run`, mcu.cpp:410-421](librdpiano/src/mcu.cpp#L410-L421), y nadie lo
reinicia jamás.

**Medido:** el emulador consume **6 102 961 ciclos por segundo de audio**, de modo que `m_icount`
cruza `INT_MIN` a los **351,9 s ≈ 5,9 minutos** de reproducción continua. UBSan lo confirma:

```
mcu.cpp:372:12: runtime error: signed integer overflow: -2147483645 - 5 cannot be represented in type 'int'
```

Hoy el valor no se lee, así que el efecto práctico es nulo; pero es **comportamiento indefinido**, y
`librdpiano/CMakeLists.txt` ya compila con sanitizadores, de modo que cualquiera que añada
`-fsanitize=undefined` verá abortar el standalone tras seis minutos.

**Corrección.** O bien reactivar el modelo de ciclos (`m_icount` como presupuesto por muestra, con
`eat_cycles`), o bien eliminar el campo. La segunda opción es coherente con lo que el código hace
realmente hoy.

**Observación de precisión relacionada.** `generate_next_sample()`
([mcu.cpp:576-586](librdpiano/src/mcu.cpp#L576-L586)) dice ejecutar «100 ciclos de CPU» pero
`execute_run()` ejecuta **una instrucción**, no un ciclo. A ~3,5 ciclos por instrucción, la CPU
emulada corre a **~6,1 MHz efectivos** frente a los 2 MHz reales que documenta el comentario —
un factor 3× en toda la temporización del firmware (servicio de envolventes, handshake MIDI).
Puede ser un ajuste deliberado «de oído», pero el comentario y el nombre de la variable afirman lo
contrario y `m_icount` existe precisamente para hacerlo bien.

---

## 11. MEDIO — Coeficientes IIR reconstruidos en cada bloque

[PluginProcessor.cpp:536-542](rdpiano_juce/Source/PluginProcessor.cpp#L536-L542)

```cpp
const float midFreq = 350.0f;   // constantes
const float q = 0.2f;
const float gainDB = 8;
*midEQ.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(...);  // cada bloque
```

`makePeakFilter` devuelve un `ReferenceCountedObjectPtr` recién construido: **una reserva de montículo
por bloque de audio**, para producir siempre los mismos coeficientes. Debe calcularse una vez en
`prepareToPlay()`.

---

## 12. MEDIO — Fuga en `prepareToPlay()`

[PluginProcessor.cpp:339-342](rdpiano_juce/Source/PluginProcessor.cpp#L339-L342) hace cuatro `new
float[]` sin liberar los anteriores. El contrato de JUCE no garantiza un `releaseResources()`
intercalado entre dos `prepareToPlay()`, y hay hosts que llaman a `prepareToPlay` repetidamente al
cambiar el tamaño de bloque. Usar `std::unique_ptr<float[]>` o `juce::HeapBlock` resuelve el caso.

---

## 13. MEDIO — `Lcd::setText()` copia una longitud fija, y el marcador es UTF-8 inválido

[Lcd.cpp:21](rdpiano_juce/Source/lcd/Lcd.cpp#L21)

```cpp
void Lcd::setText(const juce::String &text) { memcpy(LCD_Data, text.begin(), 17 * 2); }
```

Copia **siempre 34 bytes** del búfer interno de una `juce::String` arbitraria, sin consultar su
longitud. El destino (`LCD_Data[80]`) está bien; el **origen** no está acotado. Hoy todos los
llamantes de [PluginEditor.cpp](rdpiano_juce/Source/PluginEditor.cpp#L330) construyen exactamente 34
caracteres, así que la invariante se cumple por casualidad y una sola cadena mal contada en el futuro
produce lectura fuera del búfer.

Salvo por un detalle: el marcador de posición se inserta como

```cpp
paramString = paramString.replaceSection(17 + 1 + *audioProcessor.chorusRate, 1, "\xff");
```

`0xFF` **no es UTF-8 válido** (lo rechaza `CharPointer_UTF8::isValidString`, que JUCE evalúa en el
`jassert` del constructor de `StringRef`). En una compilación Debug el aserto salta; en Release la
cadena queda con un byte inválido, la longitud en *caracteres* que calcula `replaceSection` deja de
corresponder a la longitud en *bytes*, y el `memcpy` de 34 bytes puede leer más allá del búfer.

**Corrección.** Pasar la longitud real (`juce::jmin(34, text.getNumBytesAsUTF8())`) y usar un punto de
código válido (p. ej. `juce::String::charToString(0x2588)` o un carácter ASCII reservado) para el
marcador, mapeándolo al glifo 0xFF dentro de `LCD_FontRenderStandard`.

*(No hay problema en `lcd_font[240][10]`: la guarda `if (ch < 16) return;` con `ch` de tipo `uint8_t`
acota el índice a 0..239, exactamente el tamaño de la tabla.)*

---

## 14. MEDIO — Decodificación de registros: `offset % 8` pliega los bytes altos  **[verificado]**

[sound_chip.cpp:171-182](librdpiano/src/sound_chip.cpp#L171-L182)

```cpp
uint8_t partI = offset % 0x100 / 0x10;   // 16 bytes por "part"
uint8_t field = offset % 8;              // ...pero solo se decodifican 8
if (... || field >= 8) { ... }           // guarda muerta: field ∈ [0,7] por construcción
```

Cada *part* ocupa 16 bytes en el mapa, pero solo se decodifican los 8 primeros: los offsets **+8..+F
se pliegan silenciosamente sobre +0..+7**, escribiendo en `pitch_lut_i`, `wave_addr_high`, `env_dest`,
etc. con datos destinados a otros registros.

Instrumenté las escrituras del firmware real durante el arranque y un acorde de 21 notas:

```
escrituras por byte dentro de la part (offset & 0xF):
  +0..+3 : 722 c/u     +4,+5 : 2065 c/u     +6,+7 : 759 c/u
  +8..+F : 256 c/u     <-- todas con valor 00, todas en la muestra 0
```

**Calibración.** En la práctica esas 256 escrituras (16 voces × 16 parts) son un **borrado de arranque
a cero**: plegadas sobre +0..+7 vuelven a poner a cero los registros reales, lo cual es inocuo — y de
hecho es lo que enmascara el defecto §6 a partir de la primera muestra. El problema es que la
equivalencia depende por completo de que el valor sea `0x00`: no está documentada, la guarda que
debería detectarlo es código muerto, y cualquier firmware distinto (MKS-20, MK-80) que escriba datos
reales en +8..+F corromperá los registros de síntesis en silencio.

**Corrección.** Decodificar `field = offset % 0x10` y tratar explícitamente +8..+F (ignorar, o
modelar el espejo si se confirma en el silicio), en lugar de plegar por accidente aritmético.

*(Nota defensiva: `uint8_t voiceI = offset / 0x100` trunca. El único llamante acota `offset < 0x1000`,
pero con un offset de 0x10000 el truncamiento daría `voiceI = 0` y pasaría la guarda.)*

---

## 15. MEDIO — `Mcu::reset()` deja estado sucio

[mcu.cpp:262-289](librdpiano/src/mcu.cpp#L262-L289) reinicia los registros de la CPU, pero **no** toca:

- `sound_chip` — las 160 `SA_Part` conservan envolventes y fases de antes del reset (voces colgadas);
- `ram[]`, `latch_val` — el banco de la params ROM y toda la RAM del firmware;
- `m_tcsr`, `m_counter`, `m_pending_tcsr`, `m_input_capture`, `m_icount`;
- `commands_queue` — **comandos a medio entregar sobreviven al reset**.

Este último es el más molesto: el handshake del bus depende de que la cola se consuma en direcciones
concretas del firmware ([mcu.cpp:470](librdpiano/src/mcu.cpp#L470)); un comando huérfano de antes del
reset se entrega en el punto equivocado de la nueva secuencia de arranque. `mcuReset()` en el plugin
llama a `reset()` y acto seguido empuja el handshake completo, así que la cola sucia se mezcla con él.

**Corrección.** Vaciar `commands_queue`, poner `sound_chip` en estado conocido y limpiar RAM/latch/timer.

---

## 16. ALTO (solo standalone) — Carrera de datos y *use-after-free*

[librdpiano/test/standalone.cpp](librdpiano/test/standalone.cpp)

| Línea | Problema |
|---|---|
| [131](librdpiano/test/standalone.cpp#L131) | `mcu->sendMidiCmd()` desde el hilo principal mientras el callback de SDL llama a `generate_next_sample()`: `push`/`pop` concurrentes sobre el mismo `std::queue`, **sin ningún cerrojo**. Corrupción del contenedor, no solo lectura rancia. |
| [106](librdpiano/test/standalone.cpp#L106) + [204](librdpiano/test/standalone.cpp#L204) | `SDL_CloseAudio()` (API antigua) no cierra el dispositivo abierto con `SDL_OpenAudioDevice`; el `delete mcu` posterior puede ejecutarse con el callback aún activo → **use-after-free**. Debe ser `SDL_CloseAudioDevice(sdl_audio)`. |
| [131](librdpiano/test/standalone.cpp#L131) | `while (Pm_Read(...))` — `Pm_Read` devuelve **negativo** en error, que es *truthy*: bucle infinito ante cualquier fallo del dispositivo MIDI. |
| [22](librdpiano/test/standalone.cpp#L22) | `s16 sample = mcu->generate_next_sample();` trunca un `s32`. **Medido:** el rango real de salida con un acorde de tres notas es **[-83 371 .. 69 938]**, muy por encima de ±32 767 → *wraparound* audible (chasquidos duros) en lugar de recorte. |
| [146](librdpiano/test/standalone.cpp#L146) | `fread` sin comprobar el retorno: una ROM truncada o ausente produce basura silenciosa. |
| [178-192](librdpiano/test/standalone.cpp#L178) | Bucle principal sin espera: 100 % de un núcleo girando. |
| [128](librdpiano/test/standalone.cpp#L128) | `MIDI_Init()` devuelve `1` incondicionalmente; el camino de error del `main` es inalcanzable, y `Pm_OpenInput` puede dejar `midiInStream` nulo. |

Es una herramienta de desarrollo, pero es **el único mecanismo de verificación que tiene el proyecto**
(no hay tests): que produzca artefactos propios contamina el criterio auditivo con el que se validan
los cambios en `sound_chip.cpp`.

> **RESUELTO**, los siete. Ya no es el único mecanismo de verificación —hoy hay 39 suites unitarias
> y el harness e2e con hash por parche—, pero sigue siendo el oído del proyecto y tenía que dejar
> de meter ruido propio:
>
> | Antes | Ahora |
> |---|---|
> | `sendMidiCmd()` concurrente con el callback | `SDL_LockAudioDevice`/`Unlock` alrededor del envío |
> | `SDL_CloseAudio()` no cierra el dispositivo | `SDL_CloseAudioDevice(sdl_audio)`, y a 0 |
> | `while (Pm_Read(...))` — negativo es cierto | `> 0`, y `MIDI_Quit()` cierra el stream |
> | `s16 sample = generate_next_sample()` | `s32` con recorte a ±32767 (el rango real llega a −83.371) |
> | `fread` sin comprobar | se compara con `len` y aborta |
> | bucle principal girando al 100 % | `SDL_Delay(1)` |
> | `MIDI_Init()` devolvía 1 siempre | comprueba `Pm_Initialize`, `Pm_CreateVirtualInput` y `Pm_OpenInput`; `MIDI_Update()` sale si el stream es nulo |
>
> El *use-after-free* era real: el callback seguía activo durante el `delete mcu` del final.

---

## 17. MEDIO — Cadena de suministro en CI

[.github/workflows/main.yml](.github/workflows/main.yml)

- **`marvinpinto/action-automatic-releases@latest`** — acción de terceros **archivada por su autor**,
  referenciada por una etiqueta **mutable** (`@latest`, no un SHA) y ejecutada con `GITHUB_TOKEN` con
  permiso de escritura sobre el repositorio. Quien controle esa etiqueta puede publicar artefactos
  arbitrarios en la release `latest` que descargan los usuarios. Fijar por SHA o sustituir por
  `softprops/action-gh-release` / `gh release`.
- El *job* `release` no declara bloque `permissions:`, así que hereda los permisos por omisión del
  repositorio en lugar del mínimo (`contents: write`).
- `actions/checkout@v3.3.0` está desactualizado (Node 16, ya retirado en runners recientes).

**Nota legal, no técnica.** El `.jucer` empotra 13 ficheros de ROM (~1,5 MB) de firmware y wave ROM
propiedad de Roland dentro del binario que la CI publica automáticamente en cada push a `main`. Es
una decisión del proyecto, pero conviene que sea consciente: distribuir el binario distribuye las ROM.
Las licencias de código sí son compatibles entre sí (GPL del proyecto, BSD-3 de `mcu_ops.h`/MAME,
LGPL/BSD de libresample).

---

## 18. Carreras de datos menores, para completar el cuadro

- `sourceSampleRate` se escribe desde el hilo de mensajes
  ([PluginProcessor.cpp:261](rdpiano_juce/Source/PluginProcessor.cpp#L261),
  [652](rdpiano_juce/Source/PluginProcessor.cpp#L652), **fuera** del cerrojo) y se lee desde el hilo
  de audio ([435](rdpiano_juce/Source/PluginProcessor.cpp#L435)). Igual `currentPatch`. Son `int`
  alineados, así que en la práctica no hay rasgado, pero formalmente es UB; `std::atomic<int>` cuesta
  lo mismo.
- [`getProgramName`, PluginProcessor.cpp:266](rdpiano_juce/Source/PluginProcessor.cpp#L266) comprueba
  `index >= getNumPrograms()` pero no `index < 0` (a diferencia de `setCurrentProgram`, que sí lo hace).
- `spaceDDepth()` ([spaced.h:20](librdpiano/include/lsp/spaced.h#L20)) indexa
  `spaceDDepthTable[floor(amount * 0x80)]`: con `amount == 1.0` el índice es 128 sobre una tabla de
  128 entradas. El único llamante pasa `chorusDepth/15.0f ≤ 0.933`, así que no se alcanza — pero la
  función es pública y no se acota a sí misma.
- `lcd_font[240][10]` se **define** (no se declara) en una cabecera
  ([lcd_font.h:5](rdpiano_juce/Source/lcd/lcd_font.h#L5)): incluirla desde una segunda unidad de
  traducción da error de símbolo duplicado. Debería ser `static constexpr`.

---

## 19. Prioridad sugerida

**Antes de la próxima release** (rompen el producto en uso normal): ~~§1 (mudo <32 kHz)~~ ·
~~§2 y §3 (dropout garantizado al cambiar patch)~~ · ~~§5 (temporización MIDI)~~ ·
~~§8 (basura en la salida)~~ · ~~§9 (fuga de 1,2 MB)~~ — **todos resueltos**.

**A continuación** (UB latente y robustez): ~~§4~~ · ~~§6~~ · ~~§7~~ — **resueltos**; quedan
§12 · §15.

**Cuando se toque esa zona:** §10 · §11 · §13 · §14 · ~~§16~~ (**resuelto**) · §17.

> **Advertencia del propio proyecto.** Cuando se escribió esto no existía suite de tests y la
> verificación era auditiva; hoy la red son los 16 hashes de `golden.txt` y los 2.256 vectores de
> `ic_blocks.txt`. §6 ya se corrigió con esa red puesta y no movió ni un hash, como se esperaba: el
> camino nominal nunca pasaba por la parte rota. §14 sigue tocando `sound_chip.cpp` y §10 el modelo
> de temporización de la CPU. §10, además, **cambiaría la temporización efectiva del firmware y con
> ella el sonido**: no es una simple corrección de UB, y el golden lo detectaría pero no lo
> juzgaría — ahí hace falta oído.

---

## 20. Cómo reproducir las mediciones

Los bancos de prueba usados están en el directorio temporal de la sesión y no se han añadido al
repositorio. Para rehacerlos, basta compilar el núcleo (no necesita SDL2 ni portmidi) contra las ROM
de `roms/`:

```bash
clang++ -std=c++17 -O1 -fsanitize=undefined -fno-sanitize-recover=signed-integer-overflow \
        -Wno-constant-logical-operand \
        -o probe main.cpp librdpiano/src/mcu.cpp librdpiano/src/sound_chip.cpp
```

- **§6** — construir el `Mcu` con `placement new` sobre un búfer relleno con `0xA5` (simula memoria
  reciclada) y generar 64 muestras: UBSan reporta los índices fuera de rango y las cargas de `bool`
  inválidas.
- **§10** — generar 20 000 × 600 muestras (10 min de audio); el desbordamiento salta a los ~352 s.
- **§3** — cronometrar `mcu->loadSounds(...)` en bucle con `std::chrono::high_resolution_clock`.
- **§2** — compilar `Source/resample/*.c` sueltos y cronometrar `resample_open(1, 1.5, 1.5)`.
- **§14** — instrumentar `SoundChip::write()` con un histograma de `offset & 0xF`.
- **§16** — el rango de salida se obtiene acumulando mín/máx sobre `generate_next_sample()` tras
  enviar tres `sendMidiCmd(0x90, n, 100)`.
