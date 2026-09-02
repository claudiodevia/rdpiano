# Revisión de código — legibilidad, refactorizaciones, rendimiento y memoria

**Alcance:** `librdpiano/` (núcleo, motor y pruebas) y `rdpiano_juce/` (plugin y panel).
**Rama:** `develop` @ `dcf66c9`, árbol limpio.
**Excluidos del juicio de estilo:** `mcu_ops.h`, `mame_utils.h`, `lsp/`, `resample/` y `re_stuff/`
(código de MAME, transcripciones de silicio o de terceros; ver trampa 5 de CLAUDE.md). Sí se
comentan sus **datos** —tamaños de tabla, memoria residente— porque eso no es estilo.

**Complemento:** [ARQUITECTURA.md](ARQUITECTURA.md) describe cómo está construido el sistema; este
documento es lo que se puede mejorar sin cambiarlo de sitio.

---

## 0. Estado de partida

Antes de nada, lo que ya está bien: **el árbol está sano**. Todo lo de abajo son mejoras, no
roturas.

```
rdpiano_tests  48 suite(s), 474 comprobacion(es), 0 fallida(s)
rdpiano_e2e    16 parche(s), 0 comprobacion(es) fallida(s), 0 hash(es) distinto(s) del golden
```

Compilado además con `-Wall -Wextra` (que el proyecto no activa): **15 avisos**, todos triviales y
todos listados abajo (F14, F1). No hay ni un `use-after-free`, ni un puntero colgante, ni una fuga
real en el núcleo ni en el plugin: los cuatro objetos con memoria propia (`RdPianoEngine`,
`SoundChip`, `Mcu`, `RdBoard`) liberan lo que reservan, son no copiables y no movibles a propósito,
y el plugin los guarda en `std::unique_ptr`. El editor devuelve el `LookAndFeel` a `nullptr` antes
de destruirse, que es el error clásico de JUCE en este sitio.

### Prioridades

| # | Hallazgo | Dónde | Tipo | Riesgo de tocarlo |
|---|---|---|---|---|
| **F1** | `render()` limpia un búfer que sobrescribe entero después | `rd_engine.cpp:421` | Rendimiento | Ninguno (bit a bit igual) |
| **F2** | Dos `sin()` de doble precisión por muestra en el trémolo | `rd_engine.cpp:568` | Rendimiento | Bajo |
| **F3** | `getTailLengthSeconds()` devuelve 0 y el piano tiene cola de segundos | `PluginProcessor.cpp:92` | Corrección | Bajo, visible para el usuario |
| **F4** | Miembros sin inicializar en `MksButton` | `PluginEditor.h:60` | Corrección | Ninguno |
| **F5** | Las tablas de onda ocupan 2,25 MB pudiendo ocupar 1,5 MB | `sound_chip.h:55` | Memoria | Medio (**medido**: bit a bit igual) |
| **F6** | `decode_samples()` repite tres veces el mismo cálculo de dirección | `sound_chip.cpp:163` | Rendimiento | Ninguno |
| **F7** | `ImageCache::getFromMemory()` dentro de cada `paint()` | `PluginEditor.h:76` y `.cpp:162` | Rendimiento (UI) | Ninguno |
| **F8** | `Lcd::paint()` dibuja 1.190 rectángulos por repintado | `Lcd.cpp:75` | Rendimiento (UI) | Bajo |
| **F9** | `render()` hace seis cosas en 140 líneas | `rd_engine.cpp:362` | Legibilidad | Medio (hay que conservar el orden de operaciones) |
| **F10** | `new`/`delete` crudos en las pruebas | `test_engine.cpp`, `e2e.cpp` | Memoria (pruebas) | Ninguno |
| **F11** | Copia byte a byte de 32 KB al cambiar de parche | `rd_board.cpp:35` | Rendimiento | Ninguno |
| **F12** | `execute_run()` es el 20 % del tiempo: contabilidad de IRQ por instrucción | `mcu.cpp:475` | Rendimiento | **Alto** (mueve el golden) |
| **F13** | Bus de entrada estéreo declarado en un instrumento | `PluginProcessor.cpp:23` | Corrección | Bajo |
| **F14** | 14 avisos de `PAIR m_x = {0, 0}` | `mcu.h:107-120` | Legibilidad | Ninguno |
| **F15** | Cabos sueltos varios (nombres, `override`, `= default`, duplicados) | varios | Legibilidad | Ninguno |

---

## 1. Rendimiento

### 1.1 De dónde sale el tiempo, medido

Perfil de `rdpiano_e2e` (Release, sin ASan, `sample` de macOS, 2.180 muestras de pila, Apple
Silicon). Los 16 parches emulan ~110 s de audio en **2,65 s** de CPU: unas **41×** tiempo real, o
sea que una instancia del plugin come del orden del **2,4 % de un núcleo** a 20 kHz.

| Función (cima de pila) | Muestras | % |
|---|---:|---:|
| `SoundChip::update()` | 614 | 28,2 % |
| `Mcu::read_byte()` → `RdBoard::read()` | 503 | 23,1 % |
| `Mcu::execute_run()` | 441 | 20,2 % |
| Opcodes sueltos (`cpx_di`, `cli`, `lda_ix`, `beq`, `bcs`…) | ~450 | 20,6 % |
| `Mcu::generate_next_sample()` | 100 | 4,6 % |
| `SoundChip::decode_samples()` (construcción, no bucle) | 44 | 2,0 % |

La lectura importante: **el chip de sonido es el 28 % y la CPU emulada el 68 %**. Cualquier trabajo
de optimización que no toque uno de esos dos sitios no se va a notar. Y los dos son justamente los
que el golden congela, así que lo barato está fuera de ellos y lo caro es de alto riesgo tímbrico.

### 1.2 F1 — `render()` limpia un búfer que va a sobrescribir entero

`librdpiano/src/rd_engine.cpp:421`

```cpp
for (int i = 0; i < emuCapacity; i++)
{
    emuL[i] = 0.0f;
    emuR[i] = 0.0f;
}
```

Es **trabajo muerto, entero**. Dentro del bucle de síntesis, `emuL[i]` y `emuR[i]` se **asignan**
(no se acumulan) para todo `i < renderBufferFrames`, y `resample_process()` sólo lee esas
`renderBufferFrames` posiciones. Ni una sola de las que se ponen a cero se llega a leer con ese
cero.

Además el bucle recorre `emuCapacity`, que está dimensionado para el peor caso (32 kHz más el
margen de deriva) y no para el bloque actual: a 48 kHz con bloques de 512, `emuCapacity` ≈ 474
mientras que `renderBufferFrames` ≈ 213. Se limpian 474 posiciones por canal para usar 213.

**Qué hacer:** borrar el bucle. La limpieza de `outL`/`outR` de las líneas siguientes **sí** hace
falta —`resample_process()` puede devolver menos de `numFrames` y la cola se lee igual— y ésa ya
recorre `numFrames`, no la capacidad.

**Verificación:** `engine_*` de `test_engine.cpp` (la salida tiene que ser idéntica bit a bit).

### 1.3 F2 — dos `sin()` de doble precisión por muestra en el trémolo

`librdpiano/src/rd_engine.cpp:565-573`

```cpp
tremoloPhase = (tremoloPhase + 1) & 0xffffffff;
if (params.tremoloEnabled)
{
    float tremoloL = (float)(0.5 + 0.5 * sin(tremRate * 3.14159265359 * tremoloPhase / destSampleRate));
    float tremoloR =
        (float)(0.5 + 0.5 * sin(3.1415926535 + tremRate * 3.14159265359 * tremoloPhase / destSampleRate));
```

Tres cosas:

1. **El canal derecho es el izquierdo cambiado de signo.** `sin(π + x) = −sin(x)`, así que
   `0.5 + 0.5·sin(π + x)` es exactamente `1.0 − (0.5 + 0.5·sin(x))`. Se puede quitar el segundo
   `sin()` y con él la mitad del coste. (Ojo: el código escribe π con distinta precisión en cada
   sitio —`3.14159265359` y `3.1415926535`—, así que la equivalencia es exacta *matemáticamente* y
   difiere en el último bit *numéricamente*. Es un efecto de trémolo, no el emulador: no hay golden
   que mover, pero conviene decirlo en el commit.)
2. **La fase crece sin acotar.** `tremoloPhase` es `unsigned long` (64 bits en macOS) enmascarado a
   32 bits, así que el argumento de `sin()` llega a valores de cientos de miles de radianes y la
   fase da un salto discontinuo cada ~25 h de audio a 48 kHz. Lo natural es acumular la fase módulo
   `2π` (o módulo el periodo en muestras) e incrementarla, en vez de multiplicar el contador
   absoluto en cada muestra.
3. **Se calcula la división por `destSampleRate` en cada muestra.** Con fase incremental
   desaparece.

El trémolo está apagado de fábrica, así que esto sólo se paga cuando el usuario lo enciende: ~96.000
`sin()` de doble por segundo a 48 kHz, del orden del 0,2-0,5 % de un núcleo. No es urgente; es que
es gratis arreglarlo.

**Verificación:** no hay prueba de trémolo hoy. Merece una del mismo estilo que `engine_lfo_rate`
(periodo por autocorrelación), que es justamente el hueco que deja este cambio.

### 1.4 F5 — las tablas de onda: 2,25 MB que pueden ser 1,5 MB (**medido**)

`librdpiano/include/sound_chip.h:55-61`

```cpp
struct WaveTables
{
    uint16_t exp[0x20000];
    bool exp_sign[0x20000];
    uint16_t delta[0x20000];
    bool delta_sign[0x20000];
};
```

768 KB por ranura × 3 ranuras = **2,25 MB por instancia del plugin**, y cada muestra sintetizada
hace 160 lecturas (16 voces × 10 partes) en **cuatro arrays distintos** con el mismo índice: cuatro
líneas de caché potenciales por parte donde podría haber una.

Los signos son un bit cada uno y viven en un `bool` de un byte; `exp` usa 14 bits y `delta` 9, así
que el bit 15 de cada `uint16_t` está libre. Empaquetando:

```cpp
struct WaveEntry
{
    uint16_t exp;   // bit 15 = signo
    uint16_t delta; // bit 15 = signo
};
WaveEntry e[0x20000];   // 512 KB por ranura
```

**Se probó de verdad**, no sobre el papel: se aplicó el cambio en una copia del árbol y se
recompiló.

| | Base | Empaquetado |
|---|---|---|
| `rdpiano_e2e` (mejor de 3) | 2,65 s | 2,63 s |
| Hashes distintos del golden | 0 | **0** |
| `rdpiano_tests` | 474/474 | **474/474** |
| Residente en tablas de onda | 2,25 MB | **1,5 MB** |

Es decir: **bit a bit idéntico, 768 KB menos, y el tiempo no se mueve** (la diferencia está dentro
del ruido de medida). La hipótesis de que la localidad de caché daría un empujón resultó ser falsa
—el acceso a la wave ROM es suficientemente disperso como para fallar igual con uno o con cuatro
arrays—, y así conviene contarlo: el cambio se justifica por la memoria, no por la velocidad.

**Coste:** toca la firma de la llamada a `tick_ic8()` en `sound_chip.cpp:111`, no la de `tick_ic8()`
en sí, así que los 2.256 vectores de `ic_blocks.txt` siguen valiendo sin tocarlos.

### 1.5 F6 — `decode_samples()` repite tres veces el mismo cálculo

`librdpiano/src/sound_chip.cpp:163-165`

```cpp
const u8 b5 = unscramble_data_wave(temp_ic5[unscramble_addr_wave((u32)descrambled_i)]);
const u8 b6 = unscramble_data_wave(temp_ic6[unscramble_addr_wave((u32)descrambled_i)]);
const u8 b7 = unscramble_data_wave(temp_ic7[unscramble_addr_wave((u32)descrambled_i)]);
```

`unscramble_addr_wave()` son 17 `BIT()` con sus desplazamientos, y se evalúa tres veces con el mismo
argumento en cada una de las 0x20000 vueltas × 3 ranuras. Sacar `const u32 addr = unscramble_addr_wave(...)`
fuera es una línea y ahorra dos tercios de esa parte. `decode_samples()` es el 2 % del perfil de
e2e y ~9 ms del arranque del motor, así que el efecto es pequeño pero real, y la línea queda más
clara que como está.

Lo mismo, más suave, en el `descrambled_i` de arriba: es una permutación de bits escrita a mano que
`bitswap<17>()` de `mame_utils.h` expresa en una línea, igual que ya hacen `unscramble_addr_params()`
y compañía en `rom_loader.h`. Aquí sí conviene ser conservador: cualquier reordenación es audio.

### 1.6 F11 — copia byte a byte de 32 KB al cambiar de parche

`librdpiano/src/rd_board.cpp:33-36`

```cpp
for (size_t i = 0; i < PARAMS_PAGE_BYTES; i++)
    params_rom[i + 0x8000] = page[i];
```

`memcpy()`. Igual en `RdBoard::reset()` (`rd_board.cpp:45`), que pone a cero 4 KB de RAM en un bucle
de bytes. El compilador probablemente ya los reconoce a `-O2`, pero el cambio de parche ocurre en el
hilo de audio y es de los sitios donde no conviene depender del optimizador. Además `memcpy` dice lo
que hace de un vistazo.

### 1.7 F7 y F8 — el panel

**F7 — `ImageCache::getFromMemory()` dentro del `paint`.** Aparece en tres sitios
(`PluginEditor.h:76`, `PluginEditor.h:172`, `PluginEditor.cpp:162` y `:167`), es decir en el
`paintButton` de cada uno de los 17 botones, en el `drawRotarySlider` del dial y dos veces en el
`paint` del editor. `ImageCache` toma un cerrojo global y busca en una lista en cada llamada, y
además libera las imágenes que llevan unos segundos sin usarse, así que de vez en cuando **vuelve a
decodificar el PNG de 6140×1503 dentro del repintado**. Lo correcto es cargar las tres imágenes una
vez como miembros `juce::Image` del editor y pasárselas a los botones.

**F8 — `Lcd::paint()` dibuja 1.190 rectángulos.** `Lcd.cpp:60-77` recorre 34 caracteres × 7 × 5
píxeles y llama a `g.fillRect()` **en cada píxel, encendido o apagado**, cambiando de color antes de
cada uno. Dos mejoras sin cambiar el aspecto: pintar el fondo apagado de una vez con un `fillAll()`
y dibujar sólo los píxeles encendidos (–60 % de llamadas), o —mejor— renderizar el display a una
`juce::Image` una sola vez por cambio de texto. Como `setText()` ya evita repintar cuando el texto no
cambia, el segundo camino es el natural.

Detalle de lectura en el mismo sitio: `LCD_FontRenderStandard(int32_t x, int32_t y, ...)` recibe la
fila en `x` y la columna en `y`, y luego pinta `fillRect(yy, xx, ...)`. Funciona, pero los nombres
mienten; `row`/`col` costaría lo mismo.

### 1.8 F12 — el 20 % que está en `execute_run()` (alto riesgo)

`librdpiano/src/mcu.cpp:475-489`

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
segundo de audio, y el perfil le atribuye el 20 % del tiempo total. La cola de comandos está vacía
prácticamente siempre —se llena sólo cuando llega MIDI— y sin embargo se consulta en cada
instrucción, junto con `check_irq_lines()` entera.

**No se toca sin medir antes.** El estado de la línea TIN y el momento exacto en que se atiende una
IRQ son parte del comportamiento que el golden congela: mover cuándo se comprueba es mover cuándo se
entra en la interrupción, y eso es audio. Lo que sí es seguro hacer es *medirlo* antes de decidir:
poner un contador de cuántas de esas 2 M/s de comprobaciones acaban en un `ASSERT_LINE` real. Si es
una de cada 100.000, hay ahí un 10-15 % de rendimiento esperando a alguien que quiera pelearse con
el golden. Si no, se cierra el asunto y se documenta.

En el mismo archivo, dos cosas gratis:

- `generate_next_sample()` (`mcu.cpp:553`) evalúa el ternario en la **condición** del bucle:
  `for (size_t cycle = 0; cycle < (sampleRate32 ? 62 : 100); cycle++)`. El compilador lo saca, pero
  escrito como `const size_t cycles = sampleRate32 ? 62 : 100;` se lee mejor y no depende de nadie.
- `RdBoard::read()` (`rd_board.h:126`) ya tiene la rama de la ROM de programa la primera, que es lo
  correcto: es la que se lleva la mayoría de los accesos (búsqueda de opcode). No tocar el orden.

---

## 2. Memoria

### 2.1 Fugas: no hay

Repasado uno a uno:

| Objeto | Reserva | Libera | Veredicto |
|---|---|---|---|
| `RdPianoEngine` | `paramPages` (512 KB), 4 búferes, 2 handles de libresample | destructor + `release()` idempotente | Correcto |
| `SoundChip` | `wave_slots` (2,25 MB) | destructor | Correcto |
| `Mcu` / `RdBoard` | nada dinámico | — | Correcto |
| Plugin | `engine` en `unique_ptr`, XML de presets en `unique_ptr` | RAII | Correcto |
| Editor | `LookAndFeel` devuelto a `nullptr` en el destructor | — | Correcto |

`RdPianoEngine::release()` es idempotente de verdad (pone los punteros a `nullptr` después de
liberar) y `prepare()` la llama primero, así que dos `prepare()` seguidos —que es lo que hacen los
hosts al cambiar de tasa de muestreo— no fugan. Está bien resuelto.

**Un caso a documentar, no a arreglar:** `sa_tables()` (`sa_tables.cpp:477`) reserva 320 KB con
`new` y nunca los libera, a propósito (singleton de proceso, evita el orden de destrucción de
estáticos). No lo denuncia LeakSanitizer porque el puntero vive en almacenamiento estático y sigue
siendo alcanzable, pero conviene que el comentario lo diga con esas palabras para que nadie lo
"arregle" metiendo un destructor.

### 2.2 Cuánto ocupa una instancia

| Concepto | Tamaño |
|---|---|
| `SoundChip::wave_slots` (3 ranuras) | 2,25 MB → **1,5 MB** con F5 |
| `RdPianoEngine::paramPages` (16 × 32 KB) | 512 KB |
| `SaTables` (compartida por todo el proceso) | 320 KB |
| `SpaceD::eram` | 256 KB |
| `RdBoard::params_rom` | 128 KB |
| Búferes de `prepare()` + libresample | ~1,2 MB |
| Resto (RAM, ROM de programa, 160 `SA_Part`, `iram`) | ~20 KB |

Del orden de **4,5 MB por instancia** más 320 KB por proceso. Para un plugin que se abre veinte
veces en una sesión son 90 MB: no es alarmante, pero F5 se lleva 15 MB de esos veinte gratis y sin
mover un bit del audio.

Dos partidas más, menores y en código exceptuado (`lsp/`), que se anotan por completitud y **no se
recomienda tocar**: `SpaceD::iram` y `Phaser::iram` declaran `int32_t[0x200]` y sus dos accesores
enmascaran con `& 0x7f`, así que 384 de las 512 posiciones de cada uno son inalcanzables (3 KB
muertos). Es una transcripción del hardware; el tamaño sobrante probablemente venga del chip real.

### 2.3 F10 — `new`/`delete` crudos en las pruebas

`librdpiano/test/unit/test_engine.cpp` (más de veinte parejas), `test_lsp.cpp:17`, `e2e.cpp:196`.

El patrón es siempre `X *e = new X(...); ... ; delete e;`. Hoy no fuga porque ninguna prueba sale
por el medio, pero es frágil por construcción: en cuanto alguien meta un `return` temprano tras un
`CHECK` que falle —que es exactamente lo que uno quiere hacer cuando una prueba se rompe— se fuga.
`std::unique_ptr` cuesta lo mismo de escribir y quita el problema para siempre. Con ASan encendido
en el ctest de desarrollo, además, la fuga sería un fallo de CI difícil de leer.

Caso aparte, `librdpiano/test/standalone.cpp:212-216`: `main()` mete cinco búferes de ROM en la
**pila** (0x20000 × 4 + 0x2000 ≈ 520 KB). Cabe en los 8 MB del hilo principal de macOS, pero es
mucho para la pila y no hace falta: `static` o `std::vector` y listo. En ese mismo `main()`, los
`return 2` de error se van sin liberar `mcu` (irrelevante porque el proceso muere, pero es el mismo
patrón).

---

## 3. Corrección y comportamiento

### 3.1 F3 — el host cree que el plugin no tiene cola

`rdpiano_juce/Source/PluginProcessor.cpp:92`

```cpp
double RdPiano_juceAudioProcessor::getTailLengthSeconds() const { return 0.0; }
```

El propio harness e2e mide **2,5 s de release** después del note-off, y las pruebas de cola
comprueban justamente que el sonido tarda en extinguirse. Devolver 0 le dice al host lo contrario:
"cuando dejo de recibir notas, dejo de sonar en el acto". Las consecuencias son reales y se ven al
exportar:

- render offline / bounce: el host puede cortar el final de la última nota;
- *freeze* de pista: igual;
- algunos hosts dejan de llamar a `processBlock()` cuando el transporte para y no hay eventos.

Devolver del orden de `3.0` (el release más largo medido) lo arregla. Es una línea y no toca audio.

### 3.2 F4 — miembros sin inicializar en `MksButton`

`rdpiano_juce/Source/PluginEditor.h:60-61`

```cpp
int x, y, w, h;
float scaleFactor;
```

`paintButton()` los usa los cinco y sólo `position()` —que llama `resized()`— los escribe. JUCE
normalmente llama a `resized()` antes del primer `paint()`, pero no es un contrato: un
`repaint()` disparado desde el constructor del editor, o un formato que pinte antes de dimensionar,
lee basura y divide por `scaleFactor`. Inicializarlos en la declaración (`int x = 0, y = 0, w = 0,
h = 0; float scaleFactor = 1.0f;`) cierra el caso sin más discusión.

En la misma clase, `KnobLF::drawRotarySlider()` (`PluginEditor.h:161`) **no lleva `override`**. Hoy
sí sobrescribe —el `const` de los parámetros no cuenta para la firma—, pero si JUCE cambia la firma
en una versión futura el método dejará de llamarse en silencio y el dial se pintará con el aspecto
por defecto. `override` convierte eso en un error de compilación.

### 3.3 F13 — un instrumento con bus de entrada estéreo

`rdpiano_juce/Source/PluginProcessor.cpp:22-24`

```cpp
: AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
```

El plugin se declara `IS_SYNTH TRUE` / `AU_MAIN_TYPE kAudioUnitType_MusicDevice` y nunca lee la
entrada (`processBlock` la borra y la sobrescribe). Un bus de entrada activo en un instrumento hace
que algunos hosts lo ofrezcan como efecto, y `auval` se pone quisquilloso con la combinación. Lo
normal es no declararlo. **Cuidado:** cambiar la configuración de buses puede alterar cómo el host
reconoce sesiones ya guardadas, así que va con prueba en un DAW real, no sólo con `ctest`.

### 3.4 Cabos sueltos del motor

- **`rd_engine.cpp:530-531`** — el aviso de `-Wunused-variable` que sale al compilar:

  ```cpp
  int out = resample_process(resampleL, ratio, emuL, renderBufferFrames, 0, &inUsed, outL, numFrames);
  resample_process(resampleR, ratio, emuR, renderBufferFrames, 0, &inUsed, outR, numFrames);
  ```

  `out` sólo se usa dentro de `RD_TRACE(...)`, que en release no compila a nada. Además los dos
  canales comparten `inUsed` —el segundo pisa al primero— y el retorno del derecho se descarta del
  todo. Hoy es inocuo porque los dos remuestreadores son simétricos y consumen lo mismo, pero es una
  suposición sin comprobar en el sitio donde, si se rompiera, se rompería como **desfase entre
  canales**. Lo barato: `inUsedL`/`inUsedR` y un `RD_TRACE` si difieren.

- **`rd_engine.cpp:199`** — `stats.resamplerOpens += 2;` se ejecuta aunque `resample_open()` haya
  devuelto `nullptr`. El contador existe precisamente para que `test_engine.cpp` vigile que no se
  abren remuestreadores fuera de `prepare()`, así que conviene que cuente aperturas de verdad. (De
  paso: la libresample empotrada no comprueba ni uno de sus `malloc()` —`resample.c:101-158`—. Es
  código de terceros y no se reescribe, pero está bien saberlo.)

- **`rd_engine.cpp:480-481`** — `dryL` y `dryR` se calculan con la misma expresión a partir de la
  misma muestra mono. Una variable y dos usos dice lo mismo con menos.

- **`rd_engine.cpp:243-244`** — `powf(10.0f, kMidEqGainDb * 0.05f)` calculado dos veces seguidas,
  una por canal. Es en `prepare()`, así que da igual el coste; es legibilidad.

- **`e2e.cpp:103`** — `plugin_scale()` usa `sample / 2` donde el motor usa `sample << 5 >> 6`. La
  división entera trunca hacia cero y el desplazamiento aritmético trunca hacia −∞: difieren en 1
  LSB para muestras **negativas impares**. Sólo afecta a los WAV de `--wav-dir` (el hash va sobre la
  muestra cruda), pero como esos WAV existen para escuchar lo que sale del producto, conviene que
  sean lo que sale del producto.

- **`patches.h:137`** — `offsets_in_range()` compara `patchToOffset[i]` contra `WAVE_ROM_SIZE`. Los
  offsets son de la **ROM de parámetros**, no de la de onda; que las dos midan 0x20000 hace que la
  comprobación funcione por casualidad. Debería mirar contra una constante de la params ROM.

- **`sound_chip.cpp:23`** — `u8 SoundChip::read(size_t offset) { return m_irq_id; }` ignora el
  offset por completo. Si es el comportamiento del chip (un único registro espejado en todo el
  rango), merece un comentario de una línea; si no, es un agujero.

### 3.5 Concurrencia

Bien resuelta y no encontré ninguna carrera real. En concreto: `engine->params` **no** se comparte
entre hilos como podría parecer, porque `syncParamsToEngine()` se llama desde `processBlock()`, es
decir desde el propio hilo de audio, y lee `std::atomic<float>` cacheados. El resto —parche y
afinación— va por el mecanismo de peticiones atómicas que documenta CLAUDE.md.

Dos apuntes menores:

- `parameterChanged()` (`PluginProcessor.cpp:50`) llama a `sendChangeMessage()`, y el host puede
  invocarlo desde el hilo de audio al automatizar un parámetro. `ChangeBroadcaster` está pensado
  para llamarse desde cualquier hilo y en macOS acaba en un `CFRunLoopSourceSignal`, así que en la
  práctica no bloquea; aun así es el único punto de `render()`-adyacente que no es estrictamente
  RT-safe por construcción. Si alguna vez aparece un *glitch* al mover automatización, mirar aquí
  primero.
- `RdEngineStats` son `unsigned long` normales que escribe el hilo de audio y puede leer la UI. Es
  telemetría y da igual perder una cuenta, pero `std::atomic<unsigned long>` con `relaxed` cuesta
  cero y quita el UB formal.

---

## 4. Legibilidad y refactorizaciones

### 4.1 F9 — `render()` hace seis cosas seguidas

`librdpiano/src/rd_engine.cpp:362-590`, unas 140 líneas de cuerpo con seis fases claramente
separadas y ya comentadas: (1) atender peticiones y validar el bloque, (2) calcular cuántas muestras
del emulador hacen falta con su corrección de deriva, (3) el bucle de síntesis con los dos efectos y
sus rampas, (4) remuestreo, (5) ganancia + declick + trémolo, (6) EQ y vaciado de la cola MIDI.

Los comentarios son buenos —explican el *por qué*, que es lo difícil— pero la función es larga y
tiene cuatro salidas tempranas que repiten el mismo trío `silence(); midiCount = 0; return;`.
Partirla en privados que reciban lo que necesitan:

```cpp
int  framesForBlock(int numFrames, double *currentError);  // fase 2
void synthesise(int emuFrames, int numFrames);             // fase 3
void outputStage(float *l, float *r, int numFrames);       // fase 5-6
void abortBlock(float *l, float *r, int numFrames);        // el trío repetido
```

**Condición innegociable:** el orden de operaciones dentro de las fases no se toca. La red que lo
sostiene es `test_engine.cpp` (`engine_effect_tail`, `engine_effect_bypass_ramp`,
`engine_patch_declick`, `engine_volume_ramp`, `engine_lfo_rate`), no el golden, y la regla del
proyecto aplica entera: la prueba primero, el refactor después, y sin editar la prueba.

### 4.2 F14 — 14 avisos de inicialización de `PAIR`

`librdpiano/include/mcu.h:107-112` y `:120`

```cpp
PAIR m_ppc = {0, 0};
```

`PAIR` es una unión de estructuras de cuatro campos, así que `{0, 0}` inicializa el primer miembro
de la unión dejando dos campos fuera, y clang lo dice dos veces por línea
(`-Wmissing-field-initializers` y `-Wmissing-braces`). Son los 14 avisos de los 15 que salen al
compilar con `-Wall -Wextra`. `PAIR m_ppc = {};` inicializa a cero la unión entera, dice exactamente
lo que se quiere y calla los catorce.

### 4.3 F15 — cabos sueltos

| Dónde | Qué |
|---|---|
| `mcu.cpp:359-361` y `:368-370` | El mismo bloque `m_wai_state`/`m_nmi_state`/`m_nmi_pending = 0` escrito **dos veces** en `reset()`, separado por cuatro líneas. Sobra el segundo. |
| `mcu.cpp:561` | `void Mcu::sendMidiCmd(u8 data1, u8 data2, u8 data3)` cuando la declaración dice `(u8 cmd, u8 data1, u8 data2)`. Los nombres van corridos una posición respecto a la cabecera: quien lea la definición cree que `data1` es el primer byte de datos y es el estado. |
| `mcu.cpp:377` | `Mcu::~Mcu() {}` → `= default`. Igual `Lcd::~Lcd() {}` y `KnobLF::KnobLF()/~KnobLF()`. |
| `sound_chip.cpp:52-90` | La cadena de `if/else if` sobre `field` (0x0 a 0x7) es un `switch` de manual, y como los ocho casos están cubiertos, el `switch` deja además que el compilador avise si algún día falta uno. |
| `PluginEditor.cpp:8-11` | `static int bgWidth = 6140;` y compañía son globales **mutables** de la unidad de traducción. Son constantes del arte: `constexpr`. |
| `Lcd.cpp:50-51` | `uint32_t lcd_col1 = 0xFF233336;` sin `static` ni `const`: enlace externo, mutable, dos símbolos globales sueltos con nombre genérico dentro del binario del plugin. `static constexpr` dentro de la clase o del anónimo. |
| `PluginProcessor.cpp:124` | `changeProgramName(int index, const juce::String &newName) {}` — parámetros con nombre en un cuerpo vacío; quitar los nombres deja claro que es intencionadamente un no-op. |
| `sound_chip.h:69` | `SA_Part m_parts[16][16]`: 16 huecos por voz de los que sólo se usan 10 (`PARTS_PER_VOICE`), porque el firmware direcciona 16 bytes por parte. Es correcto y está explicado, pero significa que el bucle de `update()` recorre la matriz a saltos. Si alguna vez se persigue el 28 % de `SoundChip::update()`, separar "lo que se sintetiza" (10 por voz, contiguo) de "lo que el bus puede escribir" es por donde se empieza. |

---

## 5. Pruebas y build

Lo que ya está bien: la suite unitaria es rápida (3,2 s), el e2e es rápido (2,7 s) y bit-exacto,
`test_engine.cpp` verifica cero reservas en `render()` sustituyendo `operator new`, y la regla de no
regenerar el golden para poner algo en verde está escrita donde tiene que estar. Es una red mejor
que la de la mayoría de los proyectos de este tamaño.

Tres huecos:

1. **Los avisos no se compilan.** `librdpiano/CMakeLists.txt` no activa `-Wall -Wextra`. Los 15
   avisos que salen al activarlos son triviales (F14 y F1), así que el coste de dejarlos activados
   —y de plantearse `-Werror` en CI, no en local— es hoy prácticamente cero. Cuanto más se tarde,
   más caro.
2. **No hay medida de rendimiento en el árbol.** El perfil de este documento se hizo a mano con
   `sample`. Una bandera `rdpiano_e2e --bench` que imprima muestras por segundo daría una cifra
   comparable entre commits, que es justo lo que hace falta antes de tocar F12.
3. **El trémolo y el EQ no tienen prueba.** El chorus sí (`engine_lfo_rate`), el phaser está en
   `test_lsp.cpp`, pero el trémolo —que es lo que toca F2— y el `RdBiquad` sólo están cubiertos de
   refilón por los hashes. Antes de tocar el trémolo hay que escribir la suya.

---

## 6. Orden sugerido

**Ahora, sin riesgo y sin discusión** (todo bit a bit igual o fuera del camino del audio):
F1 (búfer muerto), F4 (miembros sin inicializar), F14 (avisos de `PAIR`), F15 (cabos sueltos),
F6 (dirección repetida), F11 (`memcpy`), y activar `-Wall -Wextra`.

**Después, con una prueba delante:**
F3 (cola declarada, medirla primero), F2 (trémolo, escribir su prueba antes), F7 y F8 (panel),
F10 (`unique_ptr` en pruebas).

**Con más cuidado:**
F5 (empaquetado de tablas: ya está medido y verificado, pero toca `sound_chip.cpp` y merece pasar
golden y vectores explícitamente), F9 (partir `render()`), F13 (buses, probar en un DAW).

**Sólo con la bandera `--bench` funcionando y sabiendo lo que se hace:**
F12 (contabilidad de IRQ por instrucción). Es el único hallazgo que puede mover el golden y, si lo
mueve, es cambio de audio: alto riesgo tímbrico.
