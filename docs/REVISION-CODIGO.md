# Revisión de código — legibilidad, refactorizaciones, rendimiento y memoria

**Alcance:** `librdpiano/` (núcleo, motor y pruebas) y `rdpiano_juce/` (plugin y panel).
**Revisión original:** `develop` @ `dcf66c9`, árbol limpio.
**Estado al día:** 2026-09-02, `develop` @ `a5ef3df` más los cambios todavía en el árbol de trabajo
(F5, F9 y la reversión de F13).
**Excluidos del juicio de estilo:** `mcu_ops.h`, `mame_utils.h`, `lsp/`, `resample/` y `re_stuff/`
(código de MAME, transcripciones de silicio o de terceros; ver trampa 5 de CLAUDE.md). Sí se
comentan sus **datos** —tamaños de tabla, memoria residente— porque eso no es estilo.

**Complemento:** [ARQUITECTURA.md](ARQUITECTURA.md) describe cómo está construido el sistema; este
documento es lo que se podía mejorar sin cambiarlo de sitio, y lo que se hizo con ello.

**Cómo leerlo:** cada hallazgo conserva el análisis con el que se escribió y termina con lo que pasó
después. De los quince: **trece aplicados**, **uno descartado tras probarlo en un DAW** (F13, que
resultó romper Logic) y **uno abierto** (F12, el único de riesgo alto).

---

## 0. Estado

### 0.1 Hoy, medido

```
rdpiano_tests           50 suite(s), 482 comprobacion(es), 0 fallida(s)      4,1 s
rdpiano_e2e             16 parche(s), 0 fallidas, 0 hash(es) distinto(s)     2,0 s
rdpiano_plugin_tests     8 suite(s), 108 comprobacion(es), 0 fallida(s)      0,8 s
```

`-Wall -Wextra` ya no hay que activarlos a mano: están en `librdpiano/CMakeLists.txt` y el núcleo
compila con **cero avisos** (los quince de la revisión eran F14 y F1). El build del plugin sólo saca
dos avisos de `libtool` sobre objetos de JUCE sin símbolos, que no son nuestros.

No hay ni un `use-after-free`, ni un puntero colgante, ni una fuga real en el núcleo ni en el
plugin: los cuatro objetos con memoria propia (`RdPianoEngine`, `SoundChip`, `Mcu`, `RdBoard`)
liberan lo que reservan, son no copiables y no movibles a propósito, y el plugin los guarda en
`std::unique_ptr`. El editor devuelve el `LookAndFeel` a `nullptr` antes de destruirse, que es el
error clásico de JUCE en este sitio.

### 0.2 Los quince hallazgos

| # | Hallazgo | Dónde | Tipo | Estado |
|---|---|---|---|---|
| **F1** | `render()` limpia un búfer que sobrescribe entero después | `rd_engine.cpp` | Rendimiento | **Hecho** — bit a bit igual |
| **F2** | Dos `sin()` de doble precisión por muestra en el trémolo | `rd_engine.cpp` | Rendimiento | **Hecho** — fase incremental, un solo `sin()` |
| **F3** | `getTailLengthSeconds()` devuelve 0 y el piano tiene cola de segundos | `PluginProcessor.cpp` | Corrección | **Hecho** — 3 s declarados y medidos |
| **F4** | Miembros sin inicializar en `MksButton` | `PluginEditor.h` | Corrección | **Hecho** |
| **F5** | Las tablas de onda ocupan 2,25 MB pudiendo ocupar 1,5 MB | `sound_chip.h` | Memoria | **Hecho** — golden intacto |
| **F6** | `decode_samples()` repite tres veces el mismo cálculo de dirección | `sound_chip.cpp` | Rendimiento | **Hecho** (el `bitswap<17>`, no: ver 1.5) |
| **F7** | `ImageCache::getFromMemory()` dentro de cada `paint()` | `PluginEditor` | Rendimiento (UI) | **Hecho** |
| **F8** | `Lcd::paint()` dibuja 1.190 rectángulos por repintado | `lcd/Lcd.cpp` | Rendimiento (UI) | **Hecho** — imagen cacheada |
| **F9** | `render()` hace seis cosas en 140 líneas | `rd_engine.cpp` | Legibilidad | **Hecho** — cinco privados, orden intacto |
| **F10** | `new`/`delete` crudos en las pruebas | `test_engine.cpp`, `e2e.cpp` | Memoria (pruebas) | **Hecho** |
| **F11** | Copia byte a byte de 32 KB al cambiar de parche | `rd_board.cpp` | Rendimiento | **Hecho** |
| **F12** | `execute_run()` es el 19 % del tiempo: contabilidad de IRQ por instrucción | `mcu.cpp:471` | Rendimiento | **Abierto** — alto riesgo (mueve el golden) |
| **F13** | Bus de entrada estéreo declarado en un instrumento | `PluginProcessor.cpp` | Corrección | **Descartado** — quitarlo rompe Logic |
| **F14** | 14 avisos de `PAIR m_x = {0, 0}` | `mcu.h` | Legibilidad | **Hecho** |
| **F15** | Cabos sueltos varios (nombres, `override`, `= default`, duplicados) | varios | Legibilidad | **Hecho** salvo los menores de 3.4 |

---

## 1. Rendimiento

### 1.1 De dónde sale el tiempo, medido

Perfil de `rdpiano_e2e` re-medido sobre el árbol de hoy (Release, sin ASan, `sample` de macOS, 1.521
muestras de pila, Apple Silicon). Los 16 parches emulan ~110 s de audio en **2,0 s** de CPU: unas
**55×** tiempo real, o sea que una instancia del plugin come del orden del **1,8 % de un núcleo** a
20 kHz.

| Función (cima de pila) | Muestras | % |
|---|---:|---:|
| `SoundChip::update()` | 437 | 28,7 % |
| `Mcu::read_byte()` → `RdBoard::read()` | 370 | 24,3 % |
| `Mcu::execute_run()` | 292 | 19,2 % |
| Opcodes sueltos (`cpx_di`, `bcs`, `beq`, `cli`, `lda_ix`…) | ~294 | 19,3 % |
| `Mcu::generate_next_sample()` | 76 | 5,0 % |
| `SoundChip::decode_samples()` (construcción, no bucle) | 34 | 2,2 % |

La lectura importante no ha cambiado: **el chip de sonido es el 28 % y la CPU emulada el 68 %**.
Cualquier trabajo de optimización que no toque uno de esos dos sitios no se va a notar, y los dos
son justamente los que el golden congela.

Sobre el reloj total: la revisión de partida midió 2,65 s y hoy salen 2,0 s, pero **la comparación
no es limpia** —son dos sesiones de medida distintas, en una máquina que no estaba en el mismo
estado— y el harness e2e no pasa por el motor, así que F1 y F9 ni le tocan. Esto es exactamente lo
que sigue faltando y lo que pide §5.2: una cifra comparable entre commits (`--bench`).

### 1.2 F1 — `render()` limpiaba un búfer que iba a sobrescribir entero

Era **trabajo muerto, entero**: el bucle de síntesis *asigna* (no acumula) `emuL[i]`/`emuR[i]` para
todo `i < renderBufferFrames`, y `resample_process()` sólo lee esas posiciones. Encima recorría
`emuCapacity`, dimensionado para el peor caso (32 kHz más el margen de deriva): a 48 kHz con bloques
de 512 se limpiaban 474 posiciones por canal para usar 213.

**Hecho.** El bucle desapareció y el porqué quedó escrito donde se echa en falta, en
`resampleBlock()`. La limpieza de `outL`/`outR` sigue —`resample_process()` puede devolver menos de
`numFrames` y la cola se lee igual— y ésa recorre `numFrames`, no la capacidad. Salida idéntica bit
a bit; lo vigilan los `engine_*` de `test_engine.cpp`.

### 1.3 F2 — dos `sin()` de doble precisión por muestra en el trémolo

El código de partida llamaba dos veces a `sin()` por muestra, multiplicaba un contador absoluto
—cientos de miles de radianes, con salto discontinuo cada ~25 h de audio a 48 kHz— y dividía por la
tasa del host en cada muestra.

**Hecho** (`rd_engine.cpp`, `outputStage()`): la fase avanza `tremoloStep` por muestra y se acota a
2π, y el canal derecho sale del mismo seno cambiado de signo, porque `sen(π + x) = −sen(x)`:

```cpp
const float half = 0.5f * (float)sin(tremoloPhase);
left[i]  *= (1.0f - depth) + ((0.5f + half) * depth);
right[i] *= (1.0f - depth) + ((0.5f - half) * depth);
```

Lo cubre `engine_tremolo`, que mide el periodo por
autocorrelación de la razón wet/dry, la oposición de fase entre canales, la profundidad y que el
trémolo vaya al ritmo del **host** (al revés que el chorus).

### 1.4 F5 — las tablas de onda: de 2,25 MB a 1,5 MB

Eran cuatro arrays paralelos —`exp`, `exp_sign`, `delta`, `delta_sign`— de 0x20000 entradas: 768 KB
por ranura × 3 ranuras = 2,25 MB por instancia, y cada muestra sintetizada hacía 160 lecturas
(16 voces × 10 partes) en cuatro sitios distintos con el mismo índice.

Los signos son un bit y vivían en un `bool` de un byte; `exp` usa 14 bits y `delta` 9, así que el
bit 15 de cada `uint16_t` estaba libre. **Hecho** (`sound_chip.h`):

```cpp
struct WaveEntry
{
    uint16_t exp;   // bit 15 = signo
    uint16_t delta; // bit 15 = signo
};
WaveEntry entries[0x20000];   // 512 KB por ranura
```

| | Antes | Ahora |
|---|---|---|
| Hashes distintos del golden | 0 | **0** |
| `rdpiano_tests` | 474/474 | **482/482** |
| Residente en tablas de onda | 2,25 MB | **1,5 MB** |
| `rdpiano_e2e` | 2,65 s | 2,63 s (medido en su día: sin cambio) |

Es decir: **bit a bit idéntico y 768 KB menos**. La hipótesis de que la localidad de caché daría
además un empujón resultó falsa —el acceso a la wave ROM es suficientemente disperso como para
fallar igual con uno o con cuatro arrays—, y así conviene contarlo: el cambio se justifica por la
memoria, no por la velocidad. Tocó la llamada a `tick_ic8()` en `sound_chip.cpp`, no la firma de
`tick_ic8()`, así que los 2.256 vectores de `ic_blocks.txt` siguieron valiendo sin tocarlos.

### 1.5 F6 — `decode_samples()` repetía tres veces el mismo cálculo

`unscramble_addr_wave()` son 17 `BIT()` con sus desplazamientos y se evaluaba tres veces con el
mismo argumento en cada una de las 0x20000 vueltas × 3 ranuras. **Hecho**: una `const u32 addr`
fuera y las tres lecturas la reutilizan.

Lo que **no** se hizo, a propósito: sustituir el `descrambled_i` escrito a mano por un
`bitswap<17>()` de `mame_utils.h`. Diría lo mismo en una línea, pero cualquier reordenación ahí es
audio y el cambio no compra nada; se queda como está.

### 1.6 F11 — copia byte a byte de 32 KB al cambiar de parche

**Hecho**: `RdBoard::selectPatchPage()` usa `memcpy` y `RdBoard::reset()` un `memset` para los 4 KB
de RAM. El compilador probablemente ya reconocía los bucles a `-O2`, pero el cambio de parche ocurre
en el hilo de audio y no es sitio para depender del optimizador. Además dice lo que hace de un
vistazo.

### 1.7 F7 y F8 — el panel

**F7 — `ImageCache::getFromMemory()` dentro del `paint`.** Aparecía en el `paintButton` de cada uno
de los 17 botones, en el `drawRotarySlider` del dial y dos veces en el `paint` del editor.
`ImageCache` toma un cerrojo global, busca en una lista en cada llamada y libera las imágenes que
llevan unos segundos sin usarse, así que de vez en cuando **volvía a decodificar el PNG de
6140×1503 dentro del repintado**.

**Hecho**: las tres hojas de arte se decodifican una sola vez en el constructor del editor
(`backgroundArt`, `interactableArt`, `knobLF.dial`) y se reparten a los botones y al dial. Ningún
`paint()` vuelve a pasar por `ImageCache`.

**F8 — `Lcd::paint()` dibujaba 1.190 rectángulos.** Recorría 34 caracteres × 7 × 5 píxeles llamando
a `fillRect()` en cada píxel, encendido o apagado, cambiando de color antes de cada uno.

**Hecho**: el display se dibuja a una `juce::Image` y el repintado sólo la copia; la imagen se rehace
únicamente cuando cambia el texto, la escala o el tamaño, y a la resolución física del contexto para
que no se vea interpolada en pantalla retina. De paso, `setText()` ya no repinta si el texto no
cambia, y los parámetros del dibujo de fuente se llaman `row`/`col`: antes se llamaban `x` e `y` y
estaban cruzados con los de `fillRect`.

### 1.8 F12 — el 19 % que está en `execute_run()` (abierto, alto riesgo)

`librdpiano/src/mcu.cpp:471`

```cpp
void Mcu::execute_run()
{
    if (!board.commandPort().queue().empty())
        execute_set_input(M6801_TIN_LINE, ASSERT_LINE);

    if (board.soundChip().m_irq_triggered)
        execute_set_input(0, ASSERT_LINE);
    check_irq_lines();

    execute_one();
}
```

Esto corre **100 veces por muestra** (62 en los parches de 32 kHz), o sea 2 millones de veces por
segundo de audio, y el perfil le sigue atribuyendo el 19 % del tiempo total. La cola de comandos
está vacía prácticamente siempre —se llena sólo cuando llega MIDI— y sin embargo se consulta en cada
instrucción, junto con `check_irq_lines()` entera.

**Sigue sin tocarse, y con razón.** El estado de la línea TIN y el momento exacto en que se atiende
una IRQ son parte del comportamiento que el golden congela: mover cuándo se comprueba es mover
cuándo se entra en la interrupción, y eso es audio. Lo que sí es seguro hacer es *medirlo* antes de
decidir: un contador de cuántas de esas 2 M/s de comprobaciones acaban en un `ASSERT_LINE` real. Si
es una de cada 100.000, hay ahí un 10-15 % de rendimiento esperando a alguien que quiera pelearse
con el golden. Si no, se cierra el asunto y se documenta.

En el mismo archivo quedan dos cosas ya resueltas y una que no: `generate_next_sample()` sigue
evaluando el ternario del número de ciclos en la condición del bucle (el compilador lo saca; es
legibilidad), y `RdBoard::read()` sigue con la rama de la ROM de programa la primera, que es lo
correcto porque se lleva la mayoría de los accesos: **no tocar ese orden**.

---

## 2. Memoria

### 2.1 Fugas: no hay

| Objeto | Reserva | Libera | Veredicto |
|---|---|---|---|
| `RdPianoEngine` | `paramPages` (512 KB), 4 búferes, 2 handles de libresample | destructor + `release()` idempotente | Correcto |
| `SoundChip` | `wave_slots` (1,5 MB) | destructor | Correcto |
| `Mcu` / `RdBoard` | nada dinámico | — | Correcto |
| Plugin | `engine` en `unique_ptr`, XML de presets en `unique_ptr` | RAII | Correcto |
| Editor | `LookAndFeel` devuelto a `nullptr` en el destructor | — | Correcto |

`RdPianoEngine::release()` es idempotente de verdad (pone los punteros a `nullptr` después de
liberar) y `prepare()` la llama primero, así que dos `prepare()` seguidos —que es lo que hacen los
hosts al cambiar de tasa de muestreo— no fugan.

**Un caso a documentar, no a arreglar:** `sa_tables()` (`sa_tables.cpp:477`) reserva 320 KB con
`new` dentro de un *magic static* y nunca los libera, a propósito (singleton de proceso, evita el
orden de destrucción de estáticos). La cabecera ya explica qué es y cuánto cuesta; lo que todavía no
dice con esas palabras es que **el `new` no se libera queriendo** y que LeakSanitizer no lo denuncia
porque el puntero sigue siendo alcanzable. Una línea, para que nadie lo "arregle" metiendo un
destructor.

### 2.2 Cuánto ocupa una instancia

| Concepto | Tamaño |
|---|---|
| `SoundChip::wave_slots` (3 ranuras × 512 KB) | 1,5 MB |
| `RdPianoEngine::paramPages` (16 × 32 KB) | 512 KB |
| `SaTables` (compartida por todo el proceso) | 320 KB |
| `SpaceD::eram` | 256 KB |
| `RdBoard::params_rom` | 128 KB |
| Búferes de `prepare()` + libresample | ~1,2 MB |
| Resto (RAM, ROM de programa, 160 `SA_Part`, `iram`) | ~20 KB |

Del orden de **3,75 MB por instancia** más 320 KB por proceso, contra los 4,5 MB de la revisión de
partida. Veinte instancias en una sesión son 75 MB en vez de 90.

Dos partidas más, menores y en código exceptuado (`lsp/`), que se anotan por completitud y **no se
recomienda tocar**: `SpaceD::iram` y `Phaser::iram` declaran `int32_t[0x200]` y sus dos accesores
enmascaran con `& 0x7f`, así que 384 de las 512 posiciones de cada uno son inalcanzables (3 KB
muertos). Es una transcripción del hardware; el tamaño sobrante probablemente venga del chip real.

### 2.3 F10 — `new`/`delete` crudos en las pruebas

Eran más de veinte parejas `X *e = new X(...); ... ; delete e;` en `test_engine.cpp`, más
`test_lsp.cpp` y `e2e.cpp`. No fugaban porque ninguna prueba salía por el medio, pero era frágil por
construcción: en cuanto alguien metiera un `return` temprano tras un `CHECK` fallido —que es
exactamente lo que uno quiere hacer cuando una prueba se rompe— habría fuga, y con ASan encendido en
el ctest de desarrollo eso es un fallo de CI difícil de leer.

**Hecho**: `std::unique_ptr` en las tres (`make_engine()` en `test_engine.cpp` devuelve el motor ya
envuelto), y los cinco búferes de ROM que `standalone.cpp` metía en la **pila** del hilo principal
(≈520 KB) son ahora `static`.

---

## 3. Corrección y comportamiento

### 3.1 F3 — el host creía que el plugin no tiene cola

`getTailLengthSeconds()` devolvía `0.0` mientras el harness medía segundos de release. Al host se le
estaba diciendo lo contrario de lo que pasa: "cuando dejo de recibir notas, dejo de sonar en el
acto". Las consecuencias eran reales al exportar (bounce y *freeze* cortando el final de la última
nota) y en los hosts que dejan de llamar a `processBlock()` con el transporte parado.

**Hecho**: `RdPianoEngine::kTailSeconds = 3.0` y `getTailLengthSeconds()` la devuelve. Son el doble
de la cola real más larga de los 16 parches (1,45 s a −60 dBFS, parche 5), y esa cola real no es una
estimación: la mide `engine_tail_length` contra la declarada, y `plugin_tail_length` comprueba lo
que ve el host.

### 3.2 F4 — miembros sin inicializar en `MksButton`

`paintButton()` usaba `x`, `y`, `w`, `h` y `scaleFactor`, y sólo `position()` los escribía. JUCE
normalmente llama a `resized()` antes del primer `paint()`, pero no es un contrato: un `repaint()`
desde el constructor del editor, o un formato que pinte antes de dimensionar, leía basura y dividía
por `scaleFactor`.

**Hecho**: `int x = 0, y = 0, w = 0, h = 0; float scaleFactor = 1.0f;`. Y en la misma clase,
`KnobLF::drawRotarySlider()` lleva ya `override` —hoy sobrescribía por casualidad, y si JUCE cambia
la firma el compilador lo dirá en vez de dejar el dial con el aspecto por defecto—, y el destructor
es `override = default`.

### 3.3 F13 — un instrumento con bus de entrada estéreo (probado y **descartado**)

El plugin se declara `IS_SYNTH TRUE` / `AU_MAIN_TYPE kAudioUnitType_MusicDevice` y nunca lee la
entrada (`processBlock` la sobrescribe entera). Un bus de entrada activo en un instrumento hace que
algunos hosts lo ofrezcan como efecto, así que lo canónico es no declararlo. La revisión ya avisaba
de que eso se prueba en un DAW real y no sólo con `ctest`. Se probó, y menos mal:

> **Descartado el 2026-09-02.** Se quitó el bus de entrada y **Logic dejó de cargar el plugin**: lo
> inserta, no enseña la interfaz y no suena. No era un fallo del plugin —`auval` lo validaba entero,
> y el editor y el audio funcionaban fuera del DAW—, pero con el bus de vuelta Logic carga.
> Revertido: **el bus de entrada se queda**, y esto es hoy la trampa 12 de CLAUDE.md.

Lo fija la suite `plugin_bus_layout`, que comprueba que hay un bus de entrada y uno de salida, los
dos estéreo, que ninguna otra combinación se acepta, y —para que quede claro por qué la entrada
sobra conceptualmente— que lo que el host traiga en el búfer no se oye.

### 3.4 Cabos sueltos del motor

Lo que se arregló:

- **`plugin_scale()` de `e2e.cpp`** usa `sample / 2` donde el motor usa `sample << 5 >> 6`, y
  difieren en 1 LSB para muestras negativas impares. Sigue igual, pero ahora **está escrito** en el
  sitio: sólo afecta a los WAV de `--wav-dir` y el hash va sobre la muestra cruda.
- **`sound_chip.cpp`** — `u8 SoundChip::read(size_t)` ya no finge que usa el offset; el parámetro va
  sin nombre. Falta la línea que diga si el chip espeja de verdad un único registro en todo el rango
  o si esto es un agujero conocido.

Lo que sigue abierto, todo menor:

- **`resampleBlock()`** — los dos canales comparten `inUsed`: el segundo pisa al primero y el
  retorno del derecho se descarta del todo. Hoy es inocuo porque los dos remuestreadores son
  simétricos y consumen lo mismo, pero es una suposición sin comprobar justo donde, si se rompiera,
  se rompería como **desfase entre canales**. Lo barato: `inUsedL`/`inUsedR` y un `RD_TRACE` si
  difieren.
- **`prepare()`** — `stats.resamplerOpens += 2;` se ejecuta aunque `resample_open()` haya devuelto
  `nullptr`. El contador existe para que `test_engine.cpp` vigile que no se abren remuestreadores
  fuera de `prepare()`, así que conviene que cuente aperturas de verdad. (De paso: la libresample
  empotrada no comprueba ni uno de sus `malloc()`. Es código de terceros y no se reescribe, pero
  está bien saberlo.)
- **`synthesise()`** — `dryL` y `dryR` se calculan con la misma expresión a partir de la misma
  muestra mono. Una variable y dos usos dice lo mismo con menos.
- **`prepare()`** — `powf(10.0f, kMidEqGainDb * 0.05f)` calculado dos veces seguidas, una por canal.
  Da igual el coste; es legibilidad.
- **`patches.h`** — `offsets_in_range()` compara `patchToOffset[i]` contra `WAVE_ROM_SIZE`. Los
  offsets son de la **ROM de parámetros**, no de la de onda; que las dos midan 0x20000 hace que la
  comprobación funcione por casualidad. Debería mirar contra una constante de la params ROM.

### 3.5 Concurrencia

Bien resuelta y no se encontró ninguna carrera real. En concreto: `engine->params` **no** se
comparte entre hilos como podría parecer, porque `syncParamsToEngine()` se llama desde
`processBlock()`, es decir desde el propio hilo de audio, y lee `std::atomic<float>` cacheados. El
resto —parche y afinación— va por el mecanismo de peticiones atómicas que documenta CLAUDE.md.

Dos apuntes menores, los dos todavía como estaban:

- `parameterChanged()` llama a `sendChangeMessage()`, y el host puede invocarlo desde el hilo de
  audio al automatizar un parámetro. `ChangeBroadcaster` está pensado para llamarse desde cualquier
  hilo y en macOS acaba en un `CFRunLoopSourceSignal`, así que en la práctica no bloquea; aun así es
  el único punto `render()`-adyacente que no es estrictamente RT-safe por construcción. Si alguna
  vez aparece un *glitch* al mover automatización, mirar aquí primero.
- `RdEngineStats` son `unsigned long` normales que escribe el hilo de audio y puede leer la UI. Es
  telemetría y da igual perder una cuenta, pero `std::atomic<unsigned long>` con `relaxed` cuesta
  cero y quita el UB formal.

---

## 4. Legibilidad y refactorizaciones

### 4.1 F9 — `render()` hacía seis cosas seguidas

Eran unas 140 líneas con seis fases claramente separadas y cuatro salidas tempranas que repetían el
mismo trío `silence(); midiCount = 0; return;`.

**Hecho**, y con la condición innegociable cumplida: el orden de operaciones dentro de las fases no
se tocó. `render()` es ahora la secuencia legible y las fases son privados de `RdPianoEngine`:

```cpp
void abortBlock(float *l, float *r, int numFrames);              // el trío repetido
int  framesForBlock(int numFrames, double *blockError);          // cuántas muestras del emulador
int  synthesise(int emuFrames);                                  // emulador + efectos + MIDI
void resampleBlock(int emuFrames, int numFrames, double error);  // a la tasa del host
void outputStage(float *l, float *r, int numFrames);             // ganancia, declick, trémolo, EQ
```

`synthesise()` devuelve cuántos eventos MIDI se entregaron para que `render()` vacíe los que caen
más allá del último frame generado, que es lo que antes hacía con la variable viva de la función
larga. La red que lo sostiene es `test_engine.cpp` (`engine_effect_tail`,
`engine_effect_bypass_ramp`, `engine_patch_declick`, `engine_volume_ramp`, `engine_lfo_rate`,
`engine_tremolo`), no el golden, y pasó entera **sin editar ni una prueba**.

### 4.2 F14 — 14 avisos de inicialización de `PAIR`

`PAIR` es una unión de estructuras de cuatro campos, así que `{0, 0}` inicializaba el primer miembro
dejando dos campos fuera, y clang lo decía dos veces por línea. **Hecho**: `PAIR m_ppc = {};` y
compañía inicializan a cero la unión entera, dicen exactamente lo que se quiere y callan los
catorce.

### 4.3 F15 — cabos sueltos

| Dónde | Qué | Estado |
|---|---|---|
| `mcu.cpp` `reset()` | El bloque `m_wai_state`/`m_nmi_state`/`m_nmi_pending = 0` escrito dos veces, separado por cuatro líneas | **Hecho**: queda uno |
| `mcu.cpp` `sendMidiCmd` | Los nombres de la definición iban corridos una posición respecto a la cabecera | **Hecho**: `(u8 cmd, u8 data1, u8 data2)` |
| `mcu.cpp`, `Lcd`, `KnobLF` | Destructores `{}` vacíos | **Hecho**: `= default` |
| `sound_chip.cpp` | Cadena de `if/else if` sobre `field` (0x0 a 0x7) | **Hecho**: `switch`, y el compilador avisa si falta un caso |
| `PluginEditor.cpp` | `static int bgWidth = 6140;` y compañía, globales mutables | **Hecho**: `static constexpr` |
| `lcd/Lcd.cpp` | `uint32_t lcd_col1 = ...` con enlace externo | **Hecho**: `static constexpr` |
| `PluginProcessor.cpp` | `changeProgramName(int index, const juce::String &newName) {}` | **Hecho**: parámetros sin nombre |
| `sound_chip.h` | `SA_Part m_parts[16][16]`: 16 huecos por voz de los que se usan 10 | Correcto y explicado; **no se toca**. Si alguna vez se persigue el 28 % de `SoundChip::update()`, separar "lo que se sintetiza" (10 por voz, contiguo) de "lo que el bus puede escribir" es por donde se empieza |

---

## 5. Pruebas y build

La suite unitaria es rápida (4,1 s) y el e2e también (2,0 s) y bit-exacto, `test_engine.cpp` verifica
cero reservas en `render()` sustituyendo `operator new`, y la regla de no regenerar el golden para
poner algo en verde está escrita donde tiene que estar.

De los tres huecos de la revisión quedan uno y medio:

1. **Los avisos ya se compilan.** `librdpiano/CMakeLists.txt` activa `-Wall -Wextra` y el núcleo sale
   limpio; `-Werror` sigue siendo decisión de la CI y no del árbol local, que es lo razonable. El
   target del plugin no los activa: ahí el ruido sería de JUCE.
2. **Sigue sin haber medida de rendimiento en el árbol.** El perfil de este documento se hace a mano
   con `sample`, y por eso el "2,65 s → 2,0 s" de §1.1 no se puede atribuir a nada. Una bandera
   `rdpiano_e2e --bench` que imprima muestras por segundo daría una cifra comparable entre commits,
   que es justo lo que hace falta antes de tocar F12.
3. **El trémolo ya tiene prueba** (`engine_tremolo`, escrita antes de F2). El `RdBiquad` del EQ medio
   sigue cubierto sólo de refilón por los hashes: es el único bloque de la cadena sin suite propia.

---

## 6. Lo que queda

**Abierto y con riesgo:**
F12 (contabilidad de IRQ por instrucción), y sólo después de que `--bench` funcione. Es el único
hallazgo que puede mover el golden y, si lo mueve, es cambio de audio: alto riesgo tímbrico.

**Abierto y sin riesgo** (todo de §3.4, más §5):
`inUsedL`/`inUsedR` separados, `resamplerOpens` contando aperturas de verdad, `dryL`/`dryR`,
el `powf` duplicado de `prepare()`, `offsets_in_range()` contra la constante correcta,
`RdEngineStats` atómicos, la línea que falta sobre el `new` de `sa_tables()` y sobre
`SoundChip::read()`, la bandera `--bench` y una suite para el EQ.

**Cerrado para siempre:**
F13. Quitar el bus de entrada rompe Logic; está probado, revertido, documentado en la trampa 12 de
CLAUDE.md y fijado por `plugin_bus_layout`. **No volver a intentarlo.**
