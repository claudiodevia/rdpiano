# Rendimiento en directo — clics y latencia al cambiar de parche y de efectos

**Fecha:** 2026-09-01 · **Rama:** `limpieza` @ `7f61a9f` · **Alcance:** la cadena de audio entera
(`librdpiano/src/rd_engine.cpp`, `lsp/`, `resample/`, el emulador) y la capa del plugin
(`rdpiano_juce/Source/`).

**Pregunta que responde este documento.** *Al cambiar de parche o al activar/desactivar un efecto se
oye un ruido y se nota un retardo.* ¿De dónde salen exactamente, y qué se puede hacer sin mover el
timbre?

> **Estado (1 sep 2026): los pasos 1 a 9 del plan de [§11](#11-plan-de-corrección-por-relación-beneficioriesgo)
> están implementados**; P1–P10 corregidos. Falta sólo el paso 10 (P11, la velocidad del LFO), que
> **cambia el audio de cinco parches** y necesita verificación auditiva antes: es decisión de
> producto, no corrección silenciosa. Todas las medidas de abajo son del estado **anterior** a la
> corrección y se dejan como están: son el "antes" contra el que se comparó. Golden y hashes de
> `test_lsp.cpp` no se movieron.

**Criterio.** Todo lo marcado **[medido]** se obtuvo ejecutando el motor real (`RdPianoEngine`) con
las ROM del repositorio, a 48 kHz y bloques de 512, sin sanitizadores y con `-O2`. Cuando algo es
deducción del código y no medida, va marcado **[análisis]**. El apartado
[§12](#12-cómo-reproducir-las-medidas) explica cómo rehacer cada número.

Este documento **no repite** [AUDITORIA.md](AUDITORIA.md) ni
[FIABILIDAD-DIRECTO.md](FIABILIDAD-DIRECTO.md): aquélla catalogó defectos de corrección y ésta
riesgos de fiabilidad, y buena parte ya está corregida. Aquí sólo hay **transitorios y latencia**, y
se dice en cada caso si el hallazgo es nuevo o si es uno viejo que sigue vivo.

---

## 0. Resumen ejecutivo

La conclusión corta: **el problema no es de CPU**. El motor consume entre el 3,1 % y el 4,9 % de un
núcleo en el peor caso medido, no reserva memoria en `render()`, no pierde bloques en régimen
estable y el corrector de deriva del remuestreador no produce un solo hueco en 30 segundos. Lo que
se oye son **transitorios de conmutación**: estado de efectos que se congela y se suelta de golpe,
un cambio de parche que corta en seco lo que esté sonando, y un cerrojo que a veces devuelve un
bloque entero de silencio.

| # | Sev. | Problema | Qué se oye | Ubicación |
|---|---|---|---|---|
| **P1** | **CRÍTICO** | Al desactivar un efecto se **congela su línea de retardo**; al reactivarlo suelta lo que quedó dentro | **Estallido de −11 dBFS** al encender el chorus, aunque no se esté tocando nada | [rd_engine.cpp:334-357](../librdpiano/src/rd_engine.cpp#L334-L357) |
| **P2** | **CRÍTICO** | Un **program change MIDI** deja el plugin mudo hasta que se toca el panel | Silencio total a mitad de tema | [mcu.cpp:566-570](../librdpiano/src/mcu.cpp#L566-L570) |
| **P3** | **ALTO** | El cambio de parche **corta en seco** todo lo que suena, con un pico de +3,5 a +6,5 dB sobre la propia nota | Clic seco y notas truncadas en cada cambio de sonido | [rd_engine.cpp:187-206](../librdpiano/src/rd_engine.cpp#L187-L206) |
| **P4** | **ALTO** | El bypass de chorus y phaser es un **salto duro**, sin rampa | Clic al pulsar CHORUS o EFX con una nota sonando | [rd_engine.cpp:334-357](../librdpiano/src/rd_engine.cpp#L334-L357) |
| **P5** | **MEDIO** | El bloque de silencio con que responde `processBlock` cuando no consigue el cerrojo **es en sí mismo un clic** | Chasquido al cambiar de parche o afinar con búferes pequeños | [PluginProcessor.cpp:239-243](../rdpiano_juce/Source/PluginProcessor.cpp#L239-L243) |
| **P6** | **MEDIO** | `setMasterTune()` (0,215 ms p95) **no cabe** en el plazo del cerrojo con bloques de 32 muestras | Bloques perdidos mientras se gira TUNE | [PluginProcessor.cpp:161-166](../rdpiano_juce/Source/PluginProcessor.cpp#L161-L166) |
| **P7** | **MEDIO** | `volume` y `patchOutputGain` se aplican **sin rampa** | Zíper al mover el volumen; escalón de nivel al cambiar de parche | [rd_engine.cpp:380-385](../librdpiano/src/rd_engine.cpp#L380-L385) |
| **P8** | **MEDIO** | Los **1,4 ms** de retardo de grupo del remuestreador no se declaran al host | Desfase sin compensar al grabar | falta `setLatencySamples()` |
| **P9** | **MEDIO** | `prepareRomSetFor()` guarda **un solo** juego preparado: alternar dos bancos cuesta 1,1 ms de hilo de UI por cambio | Interfaz que se atasca al barrer el dial | [rd_engine.cpp:173-185](../librdpiano/src/rd_engine.cpp#L173-L185) |
| **P10** | **MEDIO** | El dial de parches dispara **un cambio por evento de arrastre** | 15 cortes de audio en un solo gesto | [PluginEditor.cpp:258-261](../rdpiano_juce/Source/PluginEditor.cpp#L258-L261) |
| **P11** | **BAJO** | La velocidad de chorus y phaser **depende de la tasa del parche** | El chorus va 1,6× más rápido en los parches de 32 kHz | [rd_engine.cpp:310-314](../librdpiano/src/rd_engine.cpp#L310-L314) |
| **P12** | **BAJO** | Micro-desperdicio: limpieza de `emuCapacity` entero por bloque, dos `sin()` de doble precisión por muestra | Nada audible ni medible | [rd_engine.cpp:297-301](../librdpiano/src/rd_engine.cpp#L297-L301), [:387-395](../librdpiano/src/rd_engine.cpp#L387-L395) |

**P2** es el hallazgo `N1` de [FIABILIDAD-DIRECTO §2](FIABILIDAD-DIRECTO.md), verificado hoy y
**todavía abierto**. **P11** es `N11` del mismo documento. Los diez restantes son nuevos.

Lo importante: **P1 y P4 se corrigen con doce líneas y cuestan 0,03 ms por bloque** —el 0,3 % del
presupuesto—, y la corrección está medida más abajo ([§4.3](#43-la-corrección-medida)). Es la mejor
relación beneficio/riesgo de toda la lista.

---

## 1. El suelo: lo que ya está bien

Conviene fijarlo primero, porque descarta las tres sospechas habituales.

| Comprobación | Resultado | |
|---|---|---|
| Coste de `render()`, 8 voces, parche de 20 kHz | 0,326 ms por bloque de 512 = **3,1 % de un núcleo** | [medido] |
| Coste de `render()`, 8 voces, parche de 32 kHz | 0,425 ms = **4,0 %** | [medido] |
| Peor caso: 16 voces + chorus + phaser + trémolo | 0,487–0,519 ms = **4,6–4,9 %** | [medido] |
| Huecos del remuestreador, 30 s en régimen estable | **0 bloques** de 2840 con la cola a cero | [medido] |
| Huecos tras un cambio de tasa 20 → 32 kHz, 30 s después | **0 bloques**; `clicks=0`, `tooFew=0`, `tooMany=0` | [medido] |
| Reservas de memoria en `render()` | 0 (lo vigila `test_engine.cpp`) | |
| Cola de comandos hacia el firmware | nunca desborda; `midiDropped=0` | [medido] |
| `allNotesOff()` | extingue en ~0,5 s por la envolvente natural, **sin clic** | [medido] |

Los dos clics que [FIABILIDAD-DIRECTO §5](FIABILIDAD-DIRECTO.md) midió en el cambio de parche
—`resample_open()` en el hilo de audio y la corrección de deriva espuria dos segundos después— **ya
no existen**: abrir los dos manejadores una sola vez en `prepare()`
([rd_engine.cpp:147-153](../librdpiano/src/rd_engine.cpp#L147-L153)) los cerró. Verificado hoy: cero
colas a cero en 30 s a ambos lados de un cambio de tasa.

Es decir: **el remuestreador ya no es el culpable, y la CPU nunca lo fue.**

---

## 2. Dónde se va cada milisegundo de latencia

La latencia percibida al tocar es la suma de cuatro cosas, sólo una de las cuales es del plugin:

```
  tecla ──► driver/host ──► processBlock ──► emulador ──► remuestreador ──► salida
             (búfer)         (0–1 bloque)     (2–5 ms)       (1,4 ms)
```

| Tramo | Coste | |
|---|---|---|
| Búfer del host | 0,67 ms (32) … 10,67 ms (512) a 48 kHz | |
| Reparto del MIDI dentro del bloque | **0** — `render()` entrega cada evento en su muestra ([rd_engine.cpp:322-328](../librdpiano/src/rd_engine.cpp#L322-L328)) | [medido] |
| Handshake del firmware + ataque de la envolvente | **2,5–3,8 ms** hasta el 0,1 % del pico | [medido] |
| Hasta el 10 % del pico (lo que se percibe como el ataque) | **3,3 ms** (MK-80) … **6,8 ms** (MKS-20 Piano 1) | [medido] |
| Retardo de grupo del remuestreador | **1,4 ms** a 20 kHz, 0,875 ms a 32 kHz (`Xoff` = 28 muestras) | [medido] |

Latencias de ataque medidas, host a 48 kHz, umbral del 10 % del pico:

```
   parche  0 (20000 Hz): 6,79 ms      parche  8 (20000 Hz): 3,40 ms
   parche  3 (32000 Hz): 5,48 ms      parche 15 (20000 Hz): 3,33 ms
```

Dos lecturas de esto:

1. **La mayor parte no se puede quitar**: son el handshake del firmware por el puerto 1 y el ataque
   real del instrumento. Un MKS-20 de verdad tampoco responde en cero.
2. **P8 — los 1,4 ms del remuestreador sí se pueden declarar.** El plugin no llama nunca a
   `setLatencySamples()`, así que el host asume compensación cero. En directo se nota poco; al
   **grabar**, todo lo que toque el plugin queda 1,4 ms tarde respecto de lo que el DAW cree.
   `setLatencySamples(round(28.0 * hostRate / 20000.0))` —67 muestras a 48 kHz— y no volver a
   tocarlo (declarar el peor caso constante evita renegociar la latencia en cada cambio de parche,
   que es algo que a los hosts no les gusta).

**La latencia que el usuario nota al cambiar de parche no es ésta.** Medido: una nota disparada en
el mismo instante del cambio llega al 10 % de su pico en 4,46 ms, y 100 ms después del cambio, en
3,73 ms. El cambio de parche **no** añade latencia de ataque. Lo que se percibe como retardo es otra
cosa, y son P3, P5 y P9: el corte del sonido anterior, el bloque de silencio, y el hilo de interfaz
ocupado 1,1 ms por cada movimiento del dial.

---

## 3. P1 · CRÍTICO — Reactivar un efecto suelta la cola congelada del retardo  **[medido]**

Este es el ruido que el usuario describe al activar y desactivar efectos, y es con diferencia el más
llamativo de todo el documento.

### 3.1 Qué pasa

[rd_engine.cpp:334-357](../librdpiano/src/rd_engine.cpp#L334-L357), dentro del bucle por muestra:

```cpp
if (params.chorusEnabled)
    spaceD.process();
else
{
    spaceD.audioOutL = spaceD.audioInL;   // bypass: process() NO se llama
    spaceD.audioOutR = spaceD.audioInR;
}
```

Cuando el efecto está apagado **no se llama a `process()`**. Y `process()` es lo único que escribe en
`eram[0x10000]` —los 256 KB de línea de retardo de `SpaceD`
([spaced.h:84](../librdpiano/include/lsp/spaced.h#L84))— y lo único que avanza `eramPos` y la fase
del LFO. Es decir: **la línea de retardo se queda congelada con el último audio que pasó por ella**,
indefinidamente. Lo mismo con el `iram[0x200]` de la cadena de todo-paso del phaser
([phaser.h:72](../librdpiano/include/lsp/phaser.h#L72)).

Al volver a encender, la primera llamada a `process()` lee un tap que apunta a memoria escrita hace
—literalmente— todo el tiempo que el efecto haya estado apagado, y lo escupe entero.

### 3.2 La medida

Procedimiento: acorde de seis notas a velocity 127 con el chorus encendido, 1,5 s; se apaga el
chorus, se sueltan las notas y se dejan pasar **8 segundos** hasta silencio absoluto (pico
`0,00000000`); entonces se vuelve a encender el chorus **sin tocar ninguna tecla**.

```
   pico del acorde original: 0,5318   pico en el silencio previo: 0,00000000

   pico por ventana de 10 ms tras encender el chorus, en silencio absoluto:
     0,1885  0,2685  0,0008  0,0000  0,0000  0,0000  0,0000  0,0000 ...
```

**Un estallido de 0,2685 de pico (−11,4 dBFS) y ~25 ms de duración, salido de la nada.** No hay nota
sonando, no hay entrada; es audio de hace ocho segundos que estaba atrapado en el `eram`.

Con el phaser, lo mismo pero más flojo: pico 0,0482 (−26,3 dBFS) durante ~10 ms.

En términos musicales: **encender el chorus entre dos frases produce un chasquido tan fuerte como
una nota tocada a media dinámica.** Y ocurre siempre, no ocasionalmente.

### 3.3 Por qué es peor de lo que parece

El estallido no depende de cuánto tiempo lleve apagado el efecto: el contenido congelado no se
degrada. Un chorus apagado al principio del concierto y encendido en la tercera canción suelta el
acorde de la primera.

---

## 4. P4 · ALTO — El bypass es un salto duro, sin rampa

El mismo bloque de código tiene un segundo defecto, independiente del anterior: **el bypass conmuta
entre dos señales distintas de una muestra a la siguiente**, sin cruce.

### 4.1 La medida

Con una nota sonando, conmutando entre bloques y midiendo el mayor salto muestra a muestra
(`max |x[n] − x[n−1]|`) en los 100 ms anteriores y los 100 ms posteriores:

| Conmutación | Salto típico antes | Salto en el cambio | |
|---|---|---|---|
| chorus OFF → ON | 0,00542 | 0,01021 | **×1,9** |
| chorus ON → OFF | 0,00635 | 0,00841 | ×1,3 |
| phaser OFF → ON | 0,00542 | 0,00873 | ×1,6 |
| phaser ON → OFF | 0,00203 | 0,00408 | **×2,0** |

Duplicar el salto máximo entre muestras es un clic audible, aunque de los suaves. Sumado a P1 —que
en el caso OFF → ON lo precede— da el "ruido al activar el efecto" tal cual se describe.

### 4.2 La corrección

Las dos cosas se arreglan con el mismo cambio, y es pequeño: **llamar siempre a `process()`, y hacer
el bypass mezclando** con un coeficiente que sube y baja en rampa.

```cpp
const s32 dryL = spaceD.audioInL, dryR = spaceD.audioInR;
spaceD.process();                       // SIEMPRE: la linea de retardo nunca se congela

const float target = params.chorusEnabled ? 1.0f : 0.0f;
const float step   = 1.0f / (0.010f * (float)sourceRate);   // rampa de 10 ms
chorusMix += juce_like_clamp(target - chorusMix, -step, step);

spaceD.audioOutL = (s32)(dryL + (spaceD.audioOutL - dryL) * chorusMix);
spaceD.audioOutR = (s32)(dryR + (spaceD.audioOutR - dryR) * chorusMix);
```

`chorusMix` y `efxMix` son dos `float` de estado del motor, puestos a cero en `prepare()`. Con
`chorusMix == 0` la salida es **exactamente** la de hoy con el efecto apagado (`dry`), y con
`chorusMix == 1` es exactamente la de hoy con el efecto encendido: el cambio sólo afecta a la
transición y a lo que la línea de retardo contiene mientras está en bypass.

### 4.3 La corrección, medida

Aplicada a una copia del motor y pasada por las mismas sondas:

| | Hoy | Con la corrección |
|---|---|---|
| Estallido al encender el chorus en silencio | **0,458** | **0,000** |
| Estallido al encender el phaser en silencio | 0,093 | 0,0007 (−63 dBFS) |
| Salto máximo, chorus ON → OFF | ×1,3 | **×1,0** (indistinguible del audio normal) |
| Salto máximo, phaser OFF → ON | ×1,6 | **×0,8** |
| Salto máximo, phaser ON → OFF | ×2,0 | ×1,4 |
| Salto máximo, chorus OFF → ON | ×1,9 | ×1,8 |

El único que apenas mejora es chorus OFF → ON, y por un motivo legítimo: el chorus **es** una señal
distinta del seco, así que aparecer en 10 ms es un cambio real de contenido, no un artefacto. Con
una rampa de 30–50 ms baja también; es cuestión de gusto y se ajusta con una constante.

Coste, medido sobre 200 bloques de 512 con 8 voces:

```
                          hoy      con la correccion
   seco                 0,320 ms   0,349 ms
   chorus               0,325 ms   0,351 ms
   phaser               0,338 ms   0,349 ms
   chorus+phaser        0,345 ms   0,349 ms
   +tremolo             0,350 ms   0,351 ms
```

**+0,029 ms por bloque en el peor caso: el 0,3 % del presupuesto de 10,67 ms.** Y hay una ventaja
extra que no se ve en la tabla: el coste pasa a ser **constante** con independencia de qué efectos
estén encendidos, así que activar un efecto ya no produce un escalón de carga en el hilo de audio.

**Riesgo tímbrico: ninguno con los efectos encendidos.** `test_lsp.cpp` congela la respuesta a
impulso de `SpaceD::process()` y `Phaser::process()`, que no se tocan; el golden del harness mide el
emulador desnudo, que tampoco. Lo que cambia es la transición, que es justo lo que se quiere
cambiar.

---

## 5. P3 · ALTO — El cambio de parche corta en seco lo que esté sonando  **[medido]**

### 5.1 La medida

Nota `C4` a velocity 110 sonando con un pico estable de 0,0740, chorus apagado para aislar. Se
cambia de parche y se mide el pico en ventanas de 5 ms:

```
   0 -> 1  (mismo juego de ROM)   0,1197  0,1213  0,0004  0,0000  0,0000 ...
   0 -> 8  (otro juego de ROM)    0,1101  0,1114  0,0004  0,0000  0,0000 ...
   0 -> 3  (20 kHz -> 32 kHz)     0,1562  0,0801  0,0002  0,0000  0,0000 ...
```

Dos cosas a la vez, ambas audibles:

1. **Un pico de 0,110 a 0,156 durante ~10 ms**, es decir **+3,5 a +6,5 dB por encima de la propia
   nota**. Eso es el clic.
2. **Silencio absoluto a partir del milisegundo 10.** La nota no se desvanece: se corta. Medido a lo
   largo de 2 s después del cambio, el RMS es exactamente `0,00000`.

### 5.2 Qué causa cada una

Se pueden separar. `RdPianoEngine::setPatch()` hace tres cosas
([rd_engine.cpp:187-206](../librdpiano/src/rd_engine.cpp#L187-L206)): publica el juego de ROM de
onda si cambió, remapea la página de parámetros (`selectPatch`) y pide al firmware que la relea
(`reloadPatch`, que son un `0x31` y un `0x30` por la cola de comandos,
[command_port.h:85-89](../librdpiano/include/command_port.h#L85-L89)).

Aislando: **`setPatch()` al *mismo* parche** —mismo juego de ROM, mismo offset, con lo que sólo
queda `reloadPatch()`—, con la misma nota sonando:

```
   antes:     pico 0,0740   rms 0,04089
   0-10 ms:   pico 0,0701   10-50 ms: pico 0,0003   0,5-1,0 s: rms 0,00000
```

El veredicto es limpio:

- **El corte de las notas lo produce `reloadPatch()`**, es decir el firmware al recibir el program
  change. Ocurre igual sin tocar una sola ROM.
- **El pico de +3,5…+6,5 dB lo produce el intercambio de las tablas de onda y de la página de
  parámetros bajo unas voces que todavía están sonando**: durante esos ~10 ms el `SoundChip` está
  leyendo muestras del parche nuevo con las envolventes y las fases del viejo. En el caso aislado,
  donde no se cambia ninguna ROM, ese pico **no aparece** (0,0701 frente a 0,0740 previos).

### 5.3 Qué se puede hacer

El corte de notas es discutible: la máquina real también cambia de sonido cortando, y hay quien lo
querrá tal cual. **El pico no lo es**: es un artefacto de emulación puro, voces del parche viejo
leyendo la ROM del nuevo.

La corrección que arregla los dos y no obliga a decidir nada sobre el firmware es un **declick en el
motor**: convertir el cambio de parche en una petición que `render()` atiende, con una envolvente
alrededor.

```
  bloque N   : el hilo de UI publica {patch pedido}. render() ve la peticion,
               aplica una rampa descendente de ~3 ms sobre la salida.
  bloque N+1 : con la salida ya en cero, setPatch() de verdad (0,063 ms).
               rampa ascendente de ~15 ms.
```

Coste: dos bloques de retardo en el cambio —21 ms a 512, 1,3 ms a 32— y una rampa. A cambio,
desaparecen el pico y el escalón, y el corte pasa a ser un fundido corto en vez de un salto a cero.
Y como efecto secundario **desaparece el cerrojo**, que es P5 y P6.

---

## 6. P5 y P6 · MEDIO — El cerrojo y el bloque de silencio

### 6.1 El bloque mudo es un clic

Cuando `processBlock` no consigue `mcuLock` dentro del plazo, devuelve el bloque a cero
([PluginProcessor.cpp:239-243](../rdpiano_juce/Source/PluginProcessor.cpp#L239-L243)). Es la
decisión correcta frente a la alternativa —esperar sin límite en el hilo de tiempo real—, y el MIDI
no se pierde porque ya está en la cola del motor. Pero **un bloque de silencio en mitad de una nota
es exactamente un clic**, y de los feos, porque tiene dos flancos.

Medido, insertando un bloque a cero en mitad de una nota sostenida:

```
   salto tipico entre muestras: 0,00913
   al entrar en el bloque mudo: 0,05610   (x6,1)
   al salir del bloque mudo:    0,08632   (x9,5)
```

Un salto **casi diez veces** el de la señal normal, dos veces seguidas con 10,7 ms de silencio en
medio. Si `blocksPreempted` no es cero, se oye.

### 6.2 El plazo no siempre cabe

`prepareToPlay` fija el plazo en **un cuarto del bloque**
([PluginProcessor.cpp:161-166](../rdpiano_juce/Source/PluginProcessor.cpp#L161-L166)). Contra los
costes reales, medidos sobre 40 repeticiones:

| Operación | Mediana | p95 | Peor |
|---|---|---|---|
| `prepareRomSetFor()` *(fuera del cerrojo)* | 1,107 ms | 1,524 ms | 1,530 ms |
| `setPatch()`, otro juego de ROM *(bajo cerrojo)* | 0,063 ms | 0,085 ms | 0,085 ms |
| `setPatch()`, mismo juego de ROM *(bajo cerrojo)* | 0,063 ms | 0,087 ms | 0,107 ms |
| `setMasterTune()` *(bajo cerrojo)* | 0,158 ms | **0,215 ms** | 0,219 ms |
| `render()` de 512 muestras *(hilo de audio)* | 0,232 ms | 0,315 ms | 0,316 ms |

Y el plazo por tamaño de bloque a 48 kHz:

| Bloque | Periodo | Plazo (¼) | `setPatch` 0,085 ms | `setMasterTune` 0,215 ms |
|---|---|---|---|---|
| 32 | 0,667 ms | 0,167 ms | cabe (×2,0) | **NO CABE** |
| 64 | 1,333 ms | 0,333 ms | cabe (×3,9) | cabe (×1,5) |
| 128 | 2,667 ms | 0,667 ms | cabe | cabe (×3,1) |
| 256 | 5,333 ms | 1,333 ms | cabe | cabe |
| 512 | 10,667 ms | 2,667 ms | cabe | cabe |

Con **32 muestras de búfer** —que es lo que pone medio mundo para tocar en directo— girar el dial de
TUNE garantiza bloques perdidos, y por tanto clics. Con 64 el margen es de 1,5×, que no sobrevive a
que el planificador desaloje al hilo de interfaz en el peor momento. Y ojo: la tabla compara con el
coste **nominal**; el riesgo real no es que la operación sea larga, es que el hilo de UI que tiene
el cerrojo tomado sea desalojado.

La nota de CLAUDE.md —*"nada nuevo bajo `mcuLock` que dure más que `setMasterTune`"*— está bien
puesta, pero el número que cita (≈0,36 ms) es más pesimista que el medido hoy (0,215 ms p95) y aun
así no cabe en 32 muestras.

### 6.3 La corrección: que el cerrojo no exista

El cerrojo está para serializar tres operaciones que corren el emulador desde el hilo de interfaz.
Si en vez de correrlas allí se **publican como petición** y las ejecuta `render()`, no hay nada que
serializar:

```cpp
// hilo de UI                                  // hilo de audio, al principio de render()
engine->prepareRomSetFor(n);   // 1,1 ms       int p = pendingPatch.exchange(-1, acquire);
pendingPatch.store(n, release);                if (p >= 0) { setPatch(p); declick(); }
```

- `setPatch()` cuesta 0,063 ms de mediana. Sobre el bloque más pequeño (32 muestras = 0,667 ms) es
  el 9 % del presupuesto, **una sola vez**, y `render()` ya consume 0,03 ms de ese bloque. Cabe.
- `setMasterTune()` cuesta 0,158 ms: el 24 % de un bloque de 32. Cabe, pero justo; conviene
  atenderlo **como mucho una vez por bloque** (el dial genera decenas de eventos por segundo y el
  `exchange` colapsa los repetidos por construcción, que es exactamente lo que se quiere).
- Lo caro —`prepareRomSetFor()`, 1,1 ms— se queda donde está, en el hilo de interfaz, que es para lo
  que se partió en dos.

Se van con ello: `acquireEngineLock()`, `mcuLockTimeoutTicks`, `blocksPreempted` y el clic de §6.1.
Y el declick de §5.3 encaja de forma natural, porque el cambio ya se aplica dentro de `render()`,
que es el único sitio donde se puede hacer una rampa.

---

## 7. P2 · CRÍTICO — El program change MIDI sigue dejando el plugin mudo  **[medido]**

Es `N1` de [FIABILIDAD-DIRECTO §2](FIABILIDAD-DIRECTO.md), verificado hoy contra el código actual y
**sin corregir**.

[mcu.cpp:566-570](../librdpiano/src/mcu.cpp#L566-L570) reenvía el program change al firmware tal
cual:

```cpp
if (command == 0xC)
    board.commandPort().programChange(data2 & 0xF);
```

Pero el firmware espera encontrar los parámetros de ese parche en la página que `selectPatch()` haya
mapeado, y esa página **sigue siendo la del parche anterior**: `programChange()` a secas cambia el
número, no el mapa de memoria.

Medido, con una nota sonando y un `0xC0 05`:

```
   antes del PC:                     rms 0,04089
   tras el PC:  rms(0-0,25 s) 0,00608   rms(0,5-1,0 s) 0,00000
   nota nueva despues del PC:        pico 0,00000   <-- MUDO
   tras un setPatch() desde el panel: pico 0,35750   <-- recuperado
```

El plugin queda **completamente mudo** y no se recupera solo: hay que tocar el panel. La única
excepción es `0xC0 00` (parche 0), que es la página que siempre está mapeada, y ése sí suena.

En un rig de directo con un controlador que mande program change para seleccionar sonidos, esto es
silencio a mitad de tema sin ninguna forma de diagnosticarlo desde el escenario.

**Corrección.** Interceptar el program change en `RdPianoEngine::pushMidi()`/`render()` y tratarlo
como lo que es —un cambio de parche completo, con su `selectPatch()`— en vez de reenviarlo al
firmware. Con la petición de §6.3 ya en su sitio, el caso "mismo juego de ROM" se atiende en el acto
(0,063 ms) y el caso "otro juego de ROM" necesita el descifrado de 1,1 ms, que no puede ir en el
hilo de audio: o se hace en un hilo de fondo y se aplica cuando esté listo, o se elimina el problema
con P9 (abajo). Mientras tanto, **ignorar el program change es estrictamente mejor que el
comportamiento actual**.

---

## 8. P9 y P10 · MEDIO — El hilo de interfaz y el barrido del dial

### 8.1 Sólo hay un juego de ROM preparado

`SoundChip` guarda **dos** juegos de tablas de onda: el activo y uno de reserva
([sound_chip.h:58-61](../librdpiano/include/sound_chip.h#L58-L61)), 768 KB cada uno. Con eso,
`prepareRomSetFor()` puede dejar descifrado *un* juego fuera del cerrojo. Pero hay **tres** juegos de
ROM, y un set list que alterne entre MKS-20 y MK-80 paga el descifrado —1,1 ms de mediana, 1,5 ms el
peor caso— **en cada cambio**, sobre el hilo de interfaz.

**Corrección: tener los tres juegos descifrados a la vez.** El coste es memoria: 3 × 768 KB = 2,25 MB
por instancia, frente a los 1,5 MB de hoy. Setecientos cincuenta kilobytes más para que **ningún
cambio de parche vuelva a costar un milisegundo**, y `prepareRomSetFor()` desaparece del API.

Con eso, un cambio de parche pasa a ser: intercambio de puntero (O(1)) + `selectPatch()`. Y
`selectPatch()`, que hoy descifra 32 KB por llamada, se puede convertir en un `memcpy` cacheando las
16 páginas ya descifradas (16 × 32 KB = 512 KB). El cambio de parche completo bajaría de 0,063 ms a
unos pocos microsegundos, y P2 y P7 se resolverían sin ningún hilo de fondo.

### 8.2 El dial dispara un cambio por evento de arrastre

[PluginEditor.cpp:258-261](../rdpiano_juce/Source/PluginEditor.cpp#L258-L261):

```cpp
if (mode == kModePatch)
{
    audioProcessor.setCurrentProgram((int)((value + 1) * 8));
    return;
}
```

`setCurrentProgram` ya sale temprano si el índice repite
([PluginProcessor.cpp:86-87](../rdpiano_juce/Source/PluginProcessor.cpp#L86-L87)), que era la
corrección de `N5`, y eso elimina la mayoría de las recargas de un arrastre. Pero **cada índice
nuevo por el que pasa el dial es un cambio de parche de verdad**, con todo lo del §5: pico, corte, y
en su caso 1,1 ms de descifrado.

Medido, encadenando los 15 cambios de 0 a 15, uno por bloque:

```
   15 cambios encadenados: 7,07 ms de hilo de UI en total (0,472 ms por cambio)
   pico durante el barrido: 0,2022  (+3,1 dB sobre la nota que estaba sonando)
```

Un gesto de dial de un extremo a otro son **quince cortes de audio y quince picos** en 160 ms. Suena
a cremallera.

**Corrección.** Aplicar el cambio al **soltar** el dial (`Slider::onDragEnd`), no durante el
arrastre, y limitar a mostrar el nombre del parche en el LCD mientras se gira. Es una línea en el
editor y no toca el motor.

**De propina** —no es de rendimiento, pero se ve desde aquí—: `(value + 1) * 8` con `value` en
`[-1, 1]` da `16` en el tope del dial, que `setCurrentProgram` rechaza por fuera de rango. El
extremo superior del dial no selecciona nada.

### 8.3 El repintado  **[análisis]**

`updateValues()` termina en `this->repaint()`
([PluginEditor.cpp:347](../rdpiano_juce/Source/PluginEditor.cpp#L347)) y `paint()` redibuja el fondo
entero —un PNG de 6140×1503 reescalado a la ventana— en cada llamada
([PluginEditor.cpp:158-165](../rdpiano_juce/Source/PluginEditor.cpp#L158-L165)). Y `updateValues()`
se llama en cada evento de arrastre del dial y en cada `changeListenerCallback`.

No está medido —haría falta el plugin corriendo en un host— pero es el sospechoso natural de la
sensación de "la interfaz va pesada" durante el arrastre, y se acota con un `repaint(bounds)` de la
zona que cambia o marcando los componentes estáticos como `setBufferedToImage(true)`.

---

## 9. P7 · MEDIO — Parámetros sin rampa

`volume` se aplica muestra a muestra dentro del bucle del emulador
([rd_engine.cpp:357-358](../librdpiano/src/rd_engine.cpp#L357-L358)) y `patchOutputGain` en la
salida ([rd_engine.cpp:380-385](../librdpiano/src/rd_engine.cpp#L380-L385)), pero **ambos se leen
una vez por bloque** y saltan de un valor al siguiente sin interpolación.

- **`volume`**: medido, un salto de 1,0 a 0,2 entre dos bloques no produce un pico mayor que el
  audio normal (0,01024 frente a 0,00913 de salto típico) — porque el salto ocurre en un punto
  cualquiera de la onda. Pero un mando movido rápido son decenas de saltos por segundo, y eso es
  zíper. Es el defecto menos grave de la lista y el más fácil: interpolar linealmente de `volume`
  anterior a actual a lo largo del bloque.
- **`patchOutputGain`**: aquí sí importa, porque los factores van de 0,408 (E-Piano 1) a 1,618
  (Harpsichord), **12 dB entre extremos** ([patches.h:79-99](../librdpiano/include/patches.h#L79-L99)).
  Un cambio de parche que cruce esa distancia mete un escalón de ganancia de 12 dB en la cola del
  parche anterior — salvo que el declick de §5.3 ya la haya llevado a cero, que es otra razón para
  hacerlo.

Los mandos de chorus (`rate`, `depth`) sí se pueden cambiar de golpe: medido, `depth` de 0 a 14 entre
bloques da un salto máximo de 0,01154 frente a 0,01236 antes — **no produce clic**, porque el efecto
integra el cambio en su propia línea de retardo.

---

## 10. P11 y P12 · BAJO

### 10.1 La velocidad del chorus depende del parche  **[análisis]**

`spaceD.rate` sale de la tabla sin escalar por la tasa de fuente
([rd_engine.cpp:310-311](../librdpiano/src/rd_engine.cpp#L310-L311)), y `SpaceD::process()` avanza
su fase **una vez por muestra del emulador**. Como el emulador corre a 20 kHz o a 32 kHz según el
parche ([patches.h:59-62](../librdpiano/include/patches.h#L59-L62)), **el LFO del chorus va 1,6×
más rápido en los cinco parches de 32 kHz**. Lo mismo con el phaser.

El trémolo, en cambio, se calcula a la tasa del host con `destSampleRate` en el denominador
([rd_engine.cpp:390-392](../librdpiano/src/rd_engine.cpp#L390-L392)) y **no** tiene el problema.

Es `N11` de [FIABILIDAD-DIRECTO](FIABILIDAD-DIRECTO.md). En directo se nota como que el chorus
"cambia de velocidad" al pasar de Piano 1 a Harpsichord con el mismo ajuste en el panel. La
corrección —escalar `rate` por `sourceRate/20000`— **cambia el audio** de los cinco parches de
32 kHz con chorus, así que es una decisión de producto, no una corrección silenciosa.

### 10.2 Micro-desperdicio

Ninguno de los dos es medible, y se listan sólo para que no se busquen dos veces:

- `render()` limpia `emuL`/`emuR` sobre **`emuCapacity` entero**
  ([rd_engine.cpp:297-301](../librdpiano/src/rd_engine.cpp#L297-L301)) cuando sólo va a escribir
  `renderBufferFrames`. A 48 kHz y bloques de 512, `emuCapacity` son 469 posiciones y
  `renderBufferFrames` 214: se limpia el doble de lo necesario. Son 1,8 KB por bloque.
- El trémolo llama a **`sin()` de doble precisión dos veces por muestra de salida**
  ([rd_engine.cpp:390-392](../librdpiano/src/rd_engine.cpp#L390-L392)) y sólo usa el resultado si
  está activado. Medido: 0,005 ms por bloque, el 0,05 % del presupuesto. Un oscilador recursivo o
  una LUT lo quitarían, pero no hay nada que ganar.

---

## 11. Plan de corrección, por relación beneficio/riesgo

Ordenado por lo que conviene hacer primero, no por gravedad.

| Orden | Qué | Arregla | Esfuerzo | ¿Mueve el golden? | Red de seguridad | Estado |
|---|---|---|---|---|---|---|
| **1** | `process()` siempre + mezcla en rampa para el bypass | **P1, P4** | ~12 líneas | **No** | `test_lsp.cpp` (intacto), `engine_effect_tail`, `engine_effect_bypass_ramp` | **hecho** |
| **2** | Ignorar o traducir el program change MIDI | **P2** | ~10 líneas | No | `engine_program_change` | **hecho** (traducido) |
| **3** | Cambio de parche y afinación como petición atendida por `render()`; fuera `mcuLock` | **P5, P6** | Medio | No | `test_engine.cpp` (cambio de parche en caliente) | **hecho** |
| **4** | Declick de ~3 ms / ~15 ms alrededor del cambio de parche | **P3, P7** (parcial) | Bajo, sobre el 3 | No | `engine_patch_declick` | **hecho** |
| **5** | Descifrar los tres juegos de ROM en `prepare()`; cachear las 16 páginas de parámetros | **P9**, cierra **P2** | Medio, +1,25 MB | No | `test_engine.cpp` (`engine_patch_prepare`) | **hecho** (al construir, no en `prepare()`: `setPatch()` puede ir antes) |
| **6** | Cambio de parche al soltar el dial, no al arrastrar | **P10** | 1 línea | No | — | **hecho** (con el `jlimit` de la propina) |
| **7** | `setLatencySamples(67)` en `prepareToPlay` | **P8** | 1 línea | No | `rdpiano_plugin_tests`, `engine_latency` | **hecho** |
| **8** | Rampa de `volume` y `patchOutputGain` dentro del bloque | **P7** | Bajo | **No** (fuera del emulador) | `engine_volume_ramp` | **hecho** |
| **9** | Acotar el repintado del editor | §8.3 | Bajo | No | — | **hecho** (el LCD se repinta él; el fondo, sólo si se movió el fader) |
| **10** | Escalar el LFO de chorus/phaser por `sourceRate` | **P11** | 2 líneas | **SÍ**, 5 parches | Hay que **escuchar** antes | **pendiente** |

Los pasos 1 a 8 **no mueven el golden ni los hashes de `test_lsp.cpp`**: la aritmética entera del
emulador y de `lsp/` no se toca en ninguno. El paso 10 sí cambia el audio y entra en la misma
categoría que los puntos 6–9 de [PENDIENTE.md](PENDIENTE.md): decisión de producto, verificación
auditiva primero.

El paso 1 por sí solo elimina el estallido que motivó este documento, cuesta el 0,3 % del
presupuesto de CPU y no puede romper nada que las pruebas actuales no detecten. Es por donde
empezar.

**Cómo quedó** (1 sep 2026). Los nueve primeros están hechos y las pruebas nuevas reproducen los
defectos: quitando la corrección del paso 1, `engine_effect_tail` mide un pico de 0,225 al encender
el chorus en silencio absoluto (el estallido de §3.2); quitando la del paso 2,
`engine_program_change` mide `rms 0,000000` (el plugin mudo de §7). Dos decisiones se apartan de lo
escrito arriba, y las dos hacia el lado seguro:

- el descifrado de los tres juegos de ROM y de las 16 páginas se hace **al construir el motor**, no
  en `prepare()`: `setPatch()` se puede llamar antes de preparar —lo hacen el harness y las
  pruebas— y `prepare()` se puede llamar más de una vez. Son ~9 ms y 2,75 MB por instancia;
- `prepareRomSetFor()` no desaparece, se queda como **no-op**. Así `engine_patch_prepare` sigue
  fijando, sin editarla, que la ruta en dos fases y la directa dan el mismo audio muestra a muestra.

`setPatch()` y `setMasterTune()` siguen siendo inmediatos —el harness los necesita así—; lo que el
plugin usa son `requestPatch()` y `requestMasterTune()`.

---

## 12. Cómo reproducir las medidas

Todas las sondas se construyen contra el núcleo, sin JUCE ni dependencias externas. Los ficheros de
sonda no están en el repositorio; con lo de abajo se rehacen en unos minutos.

```bash
cmake -S librdpiano -B build/core -DRDPIANO_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --target librdpiano
clang++ -std=c++23 -O2 -I librdpiano/include -Wno-constant-logical-operand \
        -o sonda sonda.cpp build/core/liblibrdpiano.a
./sonda --roms roms
```

**Andamiaje común** — un simulador de host de treinta líneas alrededor de `RdPianoEngine`:

```cpp
RdPianoEngine e(romSets, progRom);
e.prepareRomSetFor(patch); e.setPatch(patch); e.prepare(48000.0, 512);
// por bloque: e.pushMidi(frame, status, d1, d2); e.render(L, R, 512);
// y se acumula L/R en un vector para analizarlo despues
```

| § | Medida | Método |
|---|---|---|
| [§1](#1-el-suelo-lo-que-ya-está-bien) | Coste de `render()` | Cronometrar 200–300 bloques con 8 o 16 voces activas, por configuración de efectos y por tasa del parche. |
| [§1](#1-el-suelo-lo-que-ya-está-bien) | Huecos del remuestreador | Contar, por bloque, las muestras finales exactamente iguales a `0.0f`; 30 s de audio, con y sin cambio de tasa por medio. |
| [§2](#2-dónde-se-va-cada-milisegundo-de-latencia) | Latencia de ataque | Marcar la posición antes del `pushMidi` de note-on y buscar la primera muestra que supere el 0,1 % y el 10 % del pico posterior. |
| [§2](#2-dónde-se-va-cada-milisegundo-de-latencia) | Retardo del remuestreador | `resample_get_filter_width()` sobre un manejador abierto igual que en `prepare()`; da `Xoff` en muestras de **entrada**. |
| [§3](#3-p1--crítico--reactivar-un-efecto-suelta-la-cola-congelada-del-retardo--medido) | Cola fantasma | Acorde con el efecto encendido → apagarlo → note-offs → 8 s de silencio (verificar pico `0`) → encenderlo sin tocar nada → pico por ventanas de 10 ms. |
| [§4](#4-p4--alto--el-bypass-es-un-salto-duro-sin-rampa), [§6.1](#61-el-bloque-mudo-es-un-clic) | Clics | `max \|x[n] − x[n−1]\|` en los 100 ms anteriores y posteriores al evento; el cociente entre ambos es la métrica. |
| [§4.3](#43-la-corrección-medida) | La corrección | Copiar `rd_engine.cpp`, aplicar el parche de §4.2 y enlazar la sonda contra los objetos de `build/core/CMakeFiles/librdpiano.dir/` **excluyendo** `rd_engine.cpp.o`. |
| [§5](#5-p3--alto--el-cambio-de-parche-corta-en-seco-lo-que-esté-sonando--medido) | Cambio de parche | Nota sostenida, `prepareRomSetFor` + `setPatch`, pico por ventanas de 5 ms durante 200 ms. Para aislar `reloadPatch()`: `setPatch()` al **mismo** parche. |
| [§6.2](#62-el-plazo-no-siempre-cabe) | Coste de las operaciones de control | 40 repeticiones alternando juego de ROM, mediana / p95 / peor con `high_resolution_clock`. |
| [§7](#7-p2--crítico--el-program-change-midi-sigue-dejando-el-plugin-mudo--medido) | Program change | `pushMidi(0, 0xC0, n, 0)`, 1 s, note-on, medir pico; luego `setPatch()` desde el "panel" y volver a medir. |
| [§8.2](#82-el-dial-dispara-un-cambio-por-evento-de-arrastre) | Barrido del dial | 15 `setPatch` encadenados, uno por bloque, cronometrando el total y midiendo el pico durante el barrido. |

Máquina de referencia: macOS 15 (Darwin 25.6), Apple Silicon, `-O2` sin sanitizadores, host
simulado a 48 kHz con bloques de 512. Los tiempos absolutos varían con la máquina; las relaciones
—coste frente a presupuesto, pico frente a señal— no.

---

## 13. Lo que este documento no cubre

- **El timbre.** Todo lo de aquí son transitorios y tiempos. Ninguna medida juzga si el instrumento
  suena bien; para eso, `test/standalone.cpp` o el plugin en un DAW.
- **La interfaz medida de verdad.** §8.3 es análisis del código, no una medida con el plugin
  corriendo. Sigue sin haber ninguna prueba sobre el editor
  ([PENDIENTE.md](PENDIENTE.md) §14).
- **`setMasterTune()` como función.** Se ha medido su coste, no su corrección; sigue sin cobertura.
- **Comportamiento bajo carga real del sistema.** Todas las medidas son de un proceso solo en una
  máquina ociosa. El riesgo de P5/P6 es precisamente lo que pasa cuando no es así, y eso sólo se ve
  en un DAW con la sesión llena.
