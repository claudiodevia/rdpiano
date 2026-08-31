# Fiabilidad en directo — RdPiano

**Fecha:** 2026-08-30 · **Rama:** `limpieza` @ `a03dbe0` · **Alcance:** `librdpiano/`, `rdpiano_juce/Source/`, CI y build.

**Criterio de este documento.** No es una auditoría general de calidad de código: es una revisión con
un único objetivo, *tocar en directo con este plugin sin sustos*. Un fallo entra aquí solo si puede
producir, sobre un escenario, alguna de estas cinco cosas:

1. **Crash** del host.
2. **Cuelgue** o pérdida de plazos del hilo de audio (dropouts).
3. **Silencio** inesperado.
4. **Nota colgada** o zumbido que no se puede cortar.
5. **Ruido audible** (clics, distorsión) durante una interpretación normal.

Todo lo marcado **[medido]** se comprobó ejecutando el emulador real con las ROM del repositorio, o
reproduciendo la aritmética de `processBlock()` contra la `libresample` real del proyecto. El
apartado [§20](#20-cómo-reproducir-las-medidas) explica cómo rehacer cada medida.

Este documento **complementa** a [AUDITORIA.md](AUDITORIA.md), no lo sustituye. Cuando un hallazgo
ya estaba allí lo digo explícitamente y añado la medida nueva; los hallazgos **N** son nuevos.

---

## 0. Resumen ejecutivo

| # | Sev. | Problema | Efecto en directo | Ubicación |
|---|---|---|---|---|
| **N1** | **CRÍTICO** | Un **Program Change** MIDI deja el plugin **mudo de forma permanente** | Silencio total a mitad de tema | [mcu.cpp:592-595](../librdpiano/src/mcu.cpp#L592-L595) |
| **N2** | **CRÍTICO** | **No existe panic**: CC 120/123/121 se ignoran por completo | Nota/zumbido colgado sin forma de cortarlo | [mcu.cpp:588-615](../librdpiano/src/mcu.cpp#L588-L615) |
| A4 | **CRÍTICO** | Desbordamiento de montículo si el host entrega un bloque mayor que el declarado | **Crash** | [PluginProcessor.cpp:318](../rdpiano_juce/Source/PluginProcessor.cpp#L318) |
| A1 | **CRÍTICO** | Mudo total por debajo de 32 kHz (factor de remuestreo invertido) | Silencio en dispositivos/modos a 22,05 o 16 kHz | [PluginProcessor.cpp:314](../rdpiano_juce/Source/PluginProcessor.cpp#L314) |
| **N3** | **ALTO** | **Sin headroom**: 8 de 16 parches superan 0 dBFS con un acorde grande, *antes* del chorus y del EQ +8 dB | Distorsión dura en los fortes | [PluginProcessor.cpp:518](../rdpiano_juce/Source/PluginProcessor.cpp#L518) |
| **N4** | **ALTO** | Cambiar de parche genera **dos** clics: uno inmediato (~1 ms de hueco) y otro ~2 s después | Clic audible en cada cambio de sonido | [PluginProcessor.cpp:487-513](../rdpiano_juce/Source/PluginProcessor.cpp#L487-L513) |
| A2+A3 | **ALTO** | `resample_open()` ×2 (3,8 ms) + `loadSounds()` (1,6 ms) en un solo bloque | Dropout garantizado al cambiar de parche | [PluginProcessor.cpp:496](../rdpiano_juce/Source/PluginProcessor.cpp#L496), [221](../rdpiano_juce/Source/PluginProcessor.cpp#L221) |
| **N5** | **ALTO** | El **dial alfa** llama a `setCurrentProgram()` en *cada evento de arrastre* → un `loadSounds()` por movimiento de ratón | Ráfaga de dropouts al girar el dial | [PluginEditor.cpp:284](../rdpiano_juce/Source/PluginEditor.cpp#L284) |
| A8 | **ALTO** | Retorno temprano de `processBlock` sin limpiar el buffer | Basura o realimentación en vez de silencio | [PluginProcessor.cpp:399-411](../rdpiano_juce/Source/PluginProcessor.cpp#L399-L411) |
| **N6** | **ALTO** | La CI **nunca ejecuta** el harness e2e; publica release en cada push a `master` | Se distribuyen builds sin verificar | [.github/workflows/main.yml](../.github/workflows/main.yml) |
| A5 | **ALTO** | Toda la temporización MIDI se colapsa al inicio del bloque | Todo cuantizado a ~10 ms; sin resolución intra-bloque | [PluginProcessor.cpp:436-453](../rdpiano_juce/Source/PluginProcessor.cpp#L436-L453) |
| A6 | **ALTO** | `SA_Part` sin inicializar + índice de wave ROM sin enmascarar | UB latente; ruido arbitrario si la memoria no viene a cero | [sound_chip.h:38](../librdpiano/include/sound_chip.h#L38), [sound_chip.cpp:284](../librdpiano/src/sound_chip.cpp#L284) |
| A7 | **ALTO** | `printf()` + `fflush()` en el hilo de audio | Bloqueo indefinido del hilo RT | [mcu.cpp:501](../librdpiano/src/mcu.cpp#L501), [PluginProcessor.cpp:401](../rdpiano_juce/Source/PluginProcessor.cpp#L401) |
| **N7** | **MEDIO** | Modo **omni**: responde a los 16 canales MIDI, sin filtro | Imposible hacer splits/capas en un rig multicanal | [mcu.cpp:590](../librdpiano/src/mcu.cpp#L590) |
| **N8** | **MEDIO** | **Pitch bend, modulación y expresión** se descartan | Sin recursos expresivos habituales | [mcu.cpp:588-615](../librdpiano/src/mcu.cpp#L588-L615) |
| **N9** | **MEDIO** | Sin guarda de NaN/Inf ni limitador a la salida | Un NaN llega íntegro a los altavoces | [PluginProcessor.cpp:519-536](../rdpiano_juce/Source/PluginProcessor.cpp#L519-L536) |
| **N10** | **MEDIO** | `commands_queue` es un `std::queue` **sin techo** con `push` desde el hilo de audio | Reserva de memoria en RT | [mcu.h:36](../librdpiano/include/mcu.h#L36) |
| A9 | **MEDIO** | Fuga de ~1,2 MB por instancia (resamplers nunca cerrados) | Consumo creciente al duplicar pistas | [PluginProcessor.cpp:177-186](../rdpiano_juce/Source/PluginProcessor.cpp#L177-L186) |
| A15 | **MEDIO** | `Mcu::reset()` no vacía `commands_queue` **[medido: 9 bytes sobreviven]** | Arranque en estado indefinido | [mcu.cpp:262](../librdpiano/src/mcu.cpp#L262) |
| **N11** | **MEDIO** | La velocidad de chorus y tremolo depende del sample rate del parche | El chorus va 1,6× más rápido en parches de 32 kHz | [PluginProcessor.cpp:428](../rdpiano_juce/Source/PluginProcessor.cpp#L428) |
| **N12** | **MEDIO** | Sintetizador que declara un **bus de entrada estéreo** | Riesgo de validación AU / enrutado erróneo | [PluginProcessor.cpp:83](../rdpiano_juce/Source/PluginProcessor.cpp#L83) |
| **N13** | **MEDIO** | Binarios **sin firmar ni notarizar**; el README pide `xattr -rd` | Gatekeeper bloquea el plugin tras una actualización de macOS | [README.md](../README.md) |
| **N14** | **BAJO** | `getTailLengthSeconds()` devuelve 0 con colas de varios segundos | Colas cortadas en bounce/parada | [PluginProcessor.cpp:210](../rdpiano_juce/Source/PluginProcessor.cpp#L210) |
| **N15** | **BAJO** | El estado guardado no lleva número de versión | Sesiones antiguas/nuevas incompatibles en silencio | [PluginProcessor.cpp:586](../rdpiano_juce/Source/PluginProcessor.cpp#L586) |

Los identificadores `A#` remiten a la sección homónima de [AUDITORIA.md](AUDITORIA.md).

---

## 1. Lo que **sí** está bien (verificado)

Conviene empezar por aquí, porque acota el problema: **el núcleo del emulador no es el riesgo**.

| Comprobación | Resultado |
|---|---|
| Coste de CPU con 16 voces sonando | **3,77 % de un núcleo** (×27 tiempo real) **[medido]** |
| Coste en reposo | 1,69 % de un núcleo |
| Estabilidad en ejecución larga (12 min emulados, prueba cada minuto) | RMS de nota **idéntico** minuto a minuto, cero deriva, cero voces colgadas **[medido]** |
| ¿Puede el bucle del emulador colgarse? | **No.** `generate_next_sample()` ejecuta un número **fijo** de instrucciones (100 o 62); no hay bucle `while` dependiente de datos. Un firmware descarriado no cuelga el hilo de audio. |
| Cola de comandos bajo ráfaga (10→80 notas/s durante 3 s) | Nunca pasa de **6 bytes**; drena en 0,7 ms **[medido]** |
| Cambio de parche con notas sonando | No deja voces colgadas; la cola se extingue **[medido]** |
| Compensación de deriva del remuestreador en régimen estable | **Funciona**: una corrección cada ~2,1 s que drena el retraso interno del remuestreador **sin producir hueco** **[medido]** |
| Harness e2e con las 16 ROM | 16/16 parches en verde, hashes idénticos al golden |

Esto significa que casi todos los riesgos de directo están en **la capa del plugin y en el manejo de
MIDI**, no en la emulación. Es una buena noticia: es la parte más fácil de arreglar y la que menos
riesgo tímbrico tiene.

---

## 2. N1 · CRÍTICO — Un Program Change MIDI deja el plugin mudo para siempre  **[medido]**

Este es, con diferencia, el peor fallo para uso en directo.

[`Mcu::sendMidiCmd`, mcu.cpp:592-595](../librdpiano/src/mcu.cpp#L592-L595) reenvía el Program Change
directamente al firmware:

```cpp
if (command == 0xC) {
  commands_queue.push(0x30 | (data2 & 0xF));
}
```

Pero `loadSounds()` **solo deja cargado un parche a la vez**: reubica una página de 32 K de la params
ROM en `0x8000` y parchea los bytes `0x00–0x02` para que el firmware apunte ahí
([mcu.cpp:617-638](../librdpiano/src/mcu.cpp#L617-L638)). Desde el punto de vista del firmware, el
único programa válido es el **0**. Cualquier otro número lee una zona de la params ROM rellena con
`0xFF`.

Medido sobre el parche 0 cargado (MKS-20 Piano 1), enviando `0xC0 n` y luego una nota:

```
   PC 1 -> rms       0.0   MUDO
   PC 2 -> rms       0.0   MUDO
   PC 3 -> rms       0.0   MUDO
   PC 4 -> rms       0.0   MUDO
   PC 5 -> rms       0.0   MUDO
   PC 6 -> rms       0.0   MUDO
   PC 7 -> rms       0.0   MUDO
   (referencia con el parche cargado: rms 13462.8)
```

Silencio absoluto, no un sonido raro. Y **el plugin no se entera**: `currentPatch`,
`sourceSampleRate` y la UI siguen indicando el parche anterior, así que no hay ninguna señal visual
de lo ocurrido. Secuencia de recuperación medida:

```
estado inicial            rms 13462.8
tras PC 3                 rms     0.0
tras PC 0 (volver)        rms 13462.8      <- solo un PC 0 lo devuelve
tras PC 3 otra vez        rms     0.0
tras setCurrentProgram(0) rms 13462.8      <- o tocar un botón del panel
```

**Por qué importa en directo.** Un Program Change es exactamente la forma normal de cambiar de sonido
desde un teclado maestro, desde MainStage o desde un DAW al arrancar transporte. Muchos controladores
lo envían automáticamente al seleccionar una escena. El resultado es un silencio total que solo se
arregla acercándose al ordenador y haciendo clic en un botón del panel.

**Corrección.** El plugin —no el firmware— debe ser el dueño del cambio de parche:

1. En `processBlock`, **interceptar** los mensajes `0xC0` antes de pasarlos al MCU.
2. Traducir `program % NUM_PATCHES` a una petición de cambio de parche, encolada de forma no
   bloqueante hacia el hilo de mensajes (ver [§5](#5-n5--el-dial-de-parches-recarga-las-rom-en-cada-evento-de-arrastre)).
3. **No** reenviar nunca el `0xC0` crudo a `commands_queue`.

Con eso, un PC pasa a hacer lo obvio: seleccionar uno de los 16 parches, con sus ROM y su sample
rate. Es además la funcionalidad que un músico espera.

**Mientras tanto**, en el rig: desactivar el envío de Program Change hacia la pista de RdPiano.

---

## 3. N2 · CRÍTICO — No existe panic: CC 120/123/121 se ignoran  **[medido]**

`sendMidiCmd` solo entiende cuatro cosas: note on, note off, program change y **CC 64** (sostenido).
Todo lo demás se descarta en silencio, incluidos los tres mensajes que existen precisamente para
salvar un directo:

| Mensaje | Función | Bytes encolados **[medido]** |
|---|---|---|
| CC 123 | All Notes Off | **0** |
| CC 120 | All Sound Off | **0** |
| CC 121 | Reset All Controllers | **0** |

Escenario realista: pedal de sostenido pisado, acorde, se sueltan las teclas y **se pierde el
pedal-up** (cable, hub USB, cambio de escena del host, el propio host que deja de reenviar CC).
Medido:

```
  acorde con pedal              rms   31008.9
  teclas sueltas (pedal abajo)  rms   10481.2
  +5 s                          rms    2689.9
  tras CC123 All Notes Off      rms     853.2   <- no hace nada
  tras CC120 All Sound Off      rms     468.4   <- no hace nada
  tras CC121 Reset Controllers  rms     312.8   <- no hace nada
  tras CC64=0 (pedal arriba)    rms      32.0   <- unico remedio
```

El zumbido decae por sí solo muy despacio (sigue audible 11 s después) y **lo único que lo corta es
que llegue el CC 64 = 0 que se perdió**. Tampoco hay botón de pánico en el panel ni forma de
reinicializar el MCU desde la UI.

**Corrección.** Tres piezas, ninguna toca `sound_chip.cpp`:

1. En `sendMidiCmd`, tratar CC 120 y CC 123 como «note-off de las 128 notas» (encolar la secuencia
   `0xB0, nota, 0x00` para cada nota activa) y CC 121 como «pedal arriba» (`0x50`). Basta con llevar
   un bitmap de 128 bits de notas encendidas dentro del `Mcu`.
2. Enviar además `0x50` (pedal arriba) ante CC 120/123, o el sostenido volverá a atrapar las notas.
3. Un **botón de pánico en la UI** que haga lo mismo, más `mcuReset()` si se mantiene pulsado. Es la
   red de seguridad que un músico necesita a mitad de tema.

Y como norma general: cualquier CC no reconocido debe descartarse explícitamente, no por caída al
final de una cadena de `else if`.

---

## 4. N3 · ALTO — El plugin no tiene headroom: 8 de 16 parches saturan  **[medido]**

La cadena de escalado es:

```
sample  --(<<5, chorus, >>6)-->  sample/2  --(/65536 * volume)-->  --(* 0.5)-->  salida
```

es decir, **`sample / 262144`** con el volumen al máximo y el chorus apagado. Midiendo el pico real
del emulador con un acorde de 16 notas a velocity 127 en cada parche:

| Parche | Pico | En unidades de plugin | dBFS |
|---|---|---|---|
| MKS-20: E-Piano 1 | 601 958 | **2,296** | **+7,2** |
| MK-80: Special | 404 965 | **1,545** | **+3,8** |
| MKS-20: E-Piano 2 | 368 061 | **1,404** | **+2,9** |
| MKS-20: Piano 1 | 359 737 | **1,372** | **+2,7** |
| MK-80: A. Piano 1 | 355 641 | **1,357** | **+2,6** |
| MK-80: Blend | 355 336 | **1,355** | **+2,6** |
| MK-80: Vibraphone | 295 181 | **1,126** | **+1,0** |
| MKS-20: Clavi | 281 199 | **1,073** | **+0,6** |
| MKS-20: Piano 3 | 269 276 | **1,027** | **+0,2** |
| *(los otros 7)* | | 0,617 – 0,875 | −4,2 … −1,2 |

**Y esto es la señal seca.** Después vienen dos etapas que suman ganancia:

- el **chorus** (`spaceD`), que mezcla húmedo con seco;
- el **EQ de medios**, +8 dB a 350 Hz con **Q = 0,2** —una campana extremadamente ancha, que sube
  buena parte del espectro— aplicado en
  [PluginProcessor.cpp:539-547](../rdpiano_juce/Source/PluginProcessor.cpp#L539-L547).

Con el E-Piano 1 se parte de +7,2 dBFS antes del EQ. En float el buffer no se recorta dentro del
plugin, pero cualquier conversor, bus de hardware o pista con fader a 0 dB recorta. En directo eso es
distorsión dura justo en los fortes: precisamente donde más se nota.

**Corrección.** No es cuestión de bajar el volumen a ciegas (rompería sesiones existentes y el
balance entre parches). Lo razonable:

1. Un **factor de compensación por parche** en `patches.h`, medido con el harness, que normalice los
   16 a un pico común (p. ej. −3 dBFS con acorde de 16 notas).
2. Un **limitador suave de seguridad** al final de la cadena (soft clip / `tanh`, o un
   `juce::dsp::Limiter` con techo en −0,3 dBFS). En un instrumento de directo esto no es un lujo.
3. Revisar el Q = 0,2 del EQ: para un realce de carácter suele quererse Q ≈ 0,7; con 0,2 es casi una
   ganancia global de +8 dB.

---

## 5. N4 · ALTO — Cada cambio de parche produce dos clics  **[medido]**

Reproduciendo la aritmética de `processBlock()` con la `libresample` real, a 48 kHz, bloques de 512,
y cambiando el parche de uno de 20 kHz a uno de 32 kHz en el bloque 300:

```
bloque | emuSR | frames | inUsed | salida | huecos | nota
     0 | 20000 |    214 |    214 |    447 |     65 |
     1 | 20000 |    214 |    214 |    512 |      0 |
   192 | 20000 |     86 |     86 |    512 |      0 | correccion de deriva (sin hueco: correcta)
   300 | 32000 |    342 |    342 |    471 |     41 | <-- cambio de parche
   384 | 32000 |    214 |    214 |    405 |    107 | correccion de deriva  <-- SEGUNDO clic
   400 | 20000 |    214 |    214 |    447 |     65 | <-- vuelta al parche de 20 kHz
```

Dos defectos distintos:

**(a) Hueco inmediato.** Al cambiar `sourceSampleRate`, `processBlock` cierra y reabre los dos
manejadores del remuestreador ([PluginProcessor.cpp:487-498](../rdpiano_juce/Source/PluginProcessor.cpp#L487-L498)).
El nuevo manejador arranca sin historia, así que el primer bloque produce **41–65 muestras menos de
las pedidas**. Como el buffer se limpió a cero antes, esas muestras salen **como silencio al final
del bloque**: un salto de amplitud, es decir, un clic. Lo mismo ocurre en el primer bloque tras
cargar el plugin (65 muestras, 1,35 ms).

**(b) Segundo clic ~1,8 s después.** `samplesError` es un acumulador que lleva la cuenta del retraso
interno del remuestreador. Al reabrir el manejador ese retraso pasa a **cero**, pero `samplesError`
**no se reinicia**. Unos 190 bloques más tarde el acumulador cruza el umbral y dispara una corrección
para drenar un retraso que ya no existe: se renderizan 86 frames en lugar de 214 y salen **107
muestras a cero** (2,2 ms de hueco). Un clic aislado, unos dos segundos después de haber cambiado de
sonido — el tipo de fallo que es imposible de diagnosticar de oído porque no coincide con la acción
que lo causó.

**Corrección.**

- Para (b), la mínima: `samplesError = 0;` en el mismo bloque en que se reabre el remuestreador.
- Para (a), la buena: **abrir los dos manejadores una sola vez en `prepareToPlay()`** con
  `minFactor = hostSR/32000` y `maxFactor = hostSR/20000`, y no volver a tocarlos.
  `resample_process()` acepta un `factor` variable dentro de ese rango, así que el cambio de parche
  pasa a ser solo un cambio de número. Esto elimina de paso el problema A2 (3,8 ms de `resample_open`
  en el hilo de audio) y la fuga A9.
- Y en cualquier caso, **rellenar el hueco**: si `resample_process` devuelve menos muestras de las
  pedidas, repetir la última muestra o aplicar un fundido corto en vez de dejar ceros.

---

## 6. N5 · ALTO — El dial de parches recarga las ROM en cada evento de arrastre

[PluginEditor.cpp:284](../rdpiano_juce/Source/PluginEditor.cpp#L284):

```cpp
} else {
  audioProcessor.setCurrentProgram((alphaDial.getValue() + 1) * 8);
}
```

`sliderValueChanged` se dispara en **cada movimiento del ratón** durante el arrastre. Y
`setCurrentProgram` ([PluginProcessor.cpp:216-235](../rdpiano_juce/Source/PluginProcessor.cpp#L216-L235))
llama incondicionalmente a `loadSounds()` — la comprobación de «¿ha cambiado el ROM set?» está
comentada — que son **1,55 ms de media, 1,58 ms el peor caso [medido]** de desmezclado de ROM, todo
ello **dentro del `mcuLock`**.

Girar el dial de un extremo a otro son fácilmente 30–60 eventos. Eso son 30–60 recargas de ROM, cada
una bloqueando al hilo de audio, más 30–60 `resample_open` si de paso se cruza la frontera 20/32 kHz.
El mismo patrón afecta a `setMasterTune` desde
[PluginEditor.cpp:258](../rdpiano_juce/Source/PluginEditor.cpp#L258), que además ejecuta 200 muestras
de emulación bajo el cerrojo.

**Precisión sobre el cerrojo.** `juce::SpinLock::enter()` gira 20 veces y después cede con
`Thread::yield()` (`juce_core/threads/juce_Thread.cpp:346`).
No es, por tanto, un bloqueo duro que congele la máquina: es un hilo de tiempo real cediendo el
procesador repetidamente durante 1,5 ms o más. Con el presupuesto de un bloque de 128 frames a 48 kHz
—**2,67 ms**— eso es un plazo perdido. Y un cambio de parche completo suma
`loadSounds` (1,55 ms) + `resample_open` ×2 (3,76 ms) ≈ **5,3 ms**: el doble del presupuesto.

**Corrección.**

1. `setCurrentProgram` debe **salir inmediatamente si `index == currentPatch`** (elimina la inmensa
   mayoría de las recargas del arrastre).
2. Preparar el nuevo estado **fuera del cerrojo** y publicarlo con un intercambio de puntero atómico
   (doble búfer de `Mcu`, ~1,39 MB por copia **[medido]** — perfectamente asumible), o hacer el
   trabajo pesado en un hilo de fondo y entregarlo por una FIFO sin bloqueo.
3. Si se conserva un cerrojo, el lado de audio debe usar `tryEnter()` con camino de repliegue
   (reproducir silencio o el bloque anterior), nunca esperar.
4. En la UI, aplicar el cambio de parche solo al **soltar** el dial (`Slider::DragEndCallback`), no
   durante el arrastre.

---

## 7. Crash: bloques mayores de lo declarado  (confirma A4)  **[medido]**

`prepareToPlay` dimensiona los búferes de salida con `samplesPerBlock`
([PluginProcessor.cpp:318-319](../rdpiano_juce/Source/PluginProcessor.cpp#L318-L319)), pero
`processBlock` escribe `buffer.getNumSamples()` posiciones
([PluginProcessor.cpp:418-422](../rdpiano_juce/Source/PluginProcessor.cpp#L418-L422)) sin ninguna
guarda. La guarda existente solo mira el búfer de *entrada* del remuestreador.

Simulación con la aritmética real, host a 48 kHz:

```
  blk declarado  512  blk real 1024 -> desbordamiento de buffer en 50/50 bloques
  blk declarado  128  blk real  512 -> 'too many samples' en 50/50 bloques (silencio, sin desbordar)
```

Es decir: si el bloque crece **moderadamente**, se escriben hasta 512 floats (2 KB) fuera del
montículo — corrupción y probable crash. Si crece **mucho**, salta la otra guarda y el resultado es
silencio con el buffer sin limpiar (A8).

`maximumExpectedSamplesPerBlock` es un valor *esperado*, no un máximo contractual. Cambios de
dispositivo de audio, freeze de pista, render offline y algunos hosts AUv3 lo superan.

**Corrección.** Guardar `preparedBlockSize` y, si `buffer.getNumSamples()` lo supera, **reasignar**
(o al menos limitar y limpiar) en lugar de escribir fuera. Y dimensionar por `getNumSamples()`, no
por el valor de `prepareToPlay`.

---

## 8. Silencio por debajo de 32 kHz  (confirma A1)  **[medido]**

```
  host 32000 Hz -> ok
  host 22050 Hz -> 'too many samples to render' en 50/50 bloques, salida 0
  host 16000 Hz -> 'too many samples to render' en 50/50 bloques, salida 0
  host  8000 Hz -> 'too many samples to render' en 50/50 bloques, salida 0
```

El factor está invertido en [PluginProcessor.cpp:314](../rdpiano_juce/Source/PluginProcessor.cpp#L314)
(`sampleRate / 32000` en vez de `32000 / sampleRate`). Por encima de 32 kHz el error queda oculto
porque sobredimensiona; por debajo, el plugin es **completamente mudo** y además escupe un `printf` +
`fflush` por bloque desde el hilo de audio.

En directo esto importa menos que en estudio (casi nadie toca a 22 kHz), pero el mismo error de
dimensionado es el que deja el búfer justo en los casos límite, y el `printf` por bloque es un
bloqueo del hilo RT.

---

## 9. N7 · MEDIO — Modo omni: sin filtro de canal MIDI  **[medido]**

`sendMidiCmd` decodifica `data1 >> 4`, descartando el nibble de canal:

```
   canal  1 -> rms 14067.4 SUENA
   canal  6 -> rms 14067.4 SUENA
   canal 11 -> rms 14067.4 SUENA
   canal 16 -> rms 14067.4 SUENA
```

En un DAW con una pista por plugin da igual. En un rig de directo —MainStage, un teclado maestro con
zonas, un módulo compartido— significa que **no se puede hacer un split ni una capa**: RdPiano suena
con todo lo que pase por el puerto. Añadir un parámetro «MIDI channel» (Omni / 1–16) es trivial y
elimina toda una clase de sorpresas en escenario.

## 10. N8 · MEDIO — Pitch bend, modulación y expresión se descartan  **[medido]**

Un pitch bend (`0xE0`) encola **0 bytes**. Igual la rueda de modulación, el pedal de expresión y
cualquier CC distinto del 64. Es una limitación funcional, no un fallo de estabilidad, pero conviene
que esté documentada: quien monte un rig contando con el bend se lleva la sorpresa en el escenario, no
en el ensayo.

## 11. N9 · MEDIO — Sin guarda de NaN ni limitador a la salida

El último bucle de `processBlock` copia del búfer remuestreado a la salida y aplica el trémolo
([PluginProcessor.cpp:519-536](../rdpiano_juce/Source/PluginProcessor.cpp#L519-L536)) sin ninguna
comprobación. `juce::ScopedNoDenormals` está presente (bien), pero no hay nada que detenga un NaN o
un Inf procedente del remuestreador, del IIR o de un estado corrupto. Un NaN en el bus de audio es,
en la práctica, un ruido a plena escala o el silencio del resto de la sesión hasta que se reinicie el
canal.

Una guarda de tres líneas al final del bloque (`if (!std::isfinite(x)) x = 0;` más el limitador de
[§4](#4-n3--alto--el-plugin-no-tiene-headroom-8-de-16-parches-saturan)) convierte un fallo
catastrófico en un artefacto inaudible.

## 12. N10 · MEDIO — `commands_queue` reserva memoria en el hilo de audio

`std::queue<u8>` sobre `std::deque` ([mcu.h:36](../librdpiano/include/mcu.h#L36)): cada `push` puede
reservar memoria, y `sendMidiCmd` se llama desde `processBlock`. Además no tiene techo.

**Buena noticia [medido]:** en la práctica la cola no crece. Con ráfagas de 10 a 80 notas por segundo
durante 3 s nunca pasa de **6 bytes** y drena en 0,7 ms. Así que el riesgo real no es el desbordamiento
de memoria, sino la **reserva** en el hilo RT, que es un fallo de forma, no de fondo.

Vale la pena anotar el coste de latencia que sí es real y no es un defecto sino física del handshake
original:

```
    1 nota  ->  3 bytes,  1 muestra  =  0,05 ms
    4 notas -> 12 bytes,  24 muestras =  1,20 ms
   10 notas -> 30 bytes,  75 muestras =  3,75 ms
   16 notas -> 48 bytes, 127 muestras =  6,35 ms
   24 notas -> 72 bytes, 275 muestras = 13,75 ms
```

Un acorde de 16 notas tarda **6,35 ms** en entrar entero, y 24 notas (acorde con pedal más notas
nuevas) **13,75 ms**. Es una desincronización perceptible en pasajes densos, e inherente a cómo el
firmware consume la cola byte a byte. No es un defecto del plugin, pero conviene conocerla: sumada al
colapso de la temporización MIDI de A5, es la razón de que los acordes suenen «arpegiados» de forma
sutil.

**Corrección de forma:** sustituir por un ring buffer de tamaño fijo (p. ej. 1024 bytes) sin
reservas, descartando por el extremo antiguo si se llena.

## 13. N11 · MEDIO — El chorus y el trémolo cambian de velocidad con el parche

`spaceD->process()` se llama **una vez por muestra del emulador**, dentro del bucle de render
([PluginProcessor.cpp:461](../rdpiano_juce/Source/PluginProcessor.cpp#L461)), y su LFO avanza por
muestra. Como el emulador corre a 20 000 o a 32 000 Hz según el parche, **el mismo ajuste de "Chorus
Rate" produce una velocidad 1,6× mayor en los parches de 32 kHz** (Harpsichord, Clavi, E-Piano 2,
Contemporary, MK-80 Clavi). En el hardware original el BBD corre a frecuencia propia, independiente.

Se corrige escalando `spaceD->rate` por `sourceSampleRate / 20000.0`. Es un cambio tímbrico
consciente: cambiará el sonido de esos cinco parches con chorus, así que conviene compararlo de oído
antes de darlo por bueno.

El trémolo tiene un problema análogo pero distinto: su fase se calcula como
`sin(rate * π * tremoloPhase / destSampleRate)` con `tremoloPhase` monótona creciente
([PluginProcessor.cpp:524-534](../rdpiano_juce/Source/PluginProcessor.cpp#L524-L534)). Al cambiar
`tremoloRate` la fase **salta**, produciendo un clic. Debería integrarse la fase de forma incremental
(`phase += 2π·f/fs`) en lugar de multiplicar por un contador absoluto.

## 14. N12 · MEDIO — Un sintetizador que declara bus de entrada

El `.jucer` marca `pluginIsSynth,pluginWantsMidiIn`, pero el constructor declara un bus de entrada
estéreo activo por defecto ([PluginProcessor.cpp:83](../rdpiano_juce/Source/PluginProcessor.cpp#L83)).
Consecuencias:

- El bucle de limpieza de [PluginProcessor.cpp:376-377](../rdpiano_juce/Source/PluginProcessor.cpp#L376-L377)
  (`for i = numInputs; i < numOutputs`) **no limpia ningún canal**, porque hay tantas entradas como
  salidas. Combinado con el retorno temprano sin `clear()` (A8), lo que sale por los altavoces es lo
  que el host dejó en el búfer.
- En AU/AUv3, un instrumento con bus de entrada puede comportarse de forma inesperada al validar o al
  enrutar. **Acción concreta antes de un directo:** ejecutar `auval -v aumu RDPN GlZs` y comprobar
  que pasa limpio.

**Corrección.** Eliminar el bus de entrada (`BusesProperties().withOutput(...)` a secas) y añadir
`buffer.clear()` incondicional al principio de `processBlock`.

## 15. N13 · MEDIO — Binarios sin firmar ni notarizar

El README instruye al usuario a ejecutar `sudo xattr -rd com.apple.quarantine ...`. Eso funciona
hasta que macOS se actualiza, hasta que el plugin se reinstala, o hasta que Gatekeeper endurece la
política. Para un instrumento del que se depende en un escenario, **firmar con un Developer ID y
notarizar** es la diferencia entre «abre» y «el DAW no lo encuentra la noche del concierto».

Recomendación operativa mínima si no se quiere pagar la cuenta de desarrollador: **congelar una copia
del `.component`/`.vst3` que funcione**, guardarla fuera del sistema y no actualizarla antes de un
directo. Ver el checklist de [§18](#18-checklist-operativo-para-un-directo).

## 16. N6 · ALTO — La CI publica releases que nadie ha verificado

[.github/workflows/main.yml](../.github/workflows/main.yml) compila macOS en cada push y, en `master`,
publica una release rodante con tag `latest` — la misma que enlaza el README para descargar. **En
ningún punto se ejecuta el harness e2e.** Existe (`librdpiano/test/e2e.cpp`), es headless, no necesita
SDL ni JUCE, tarda **2,8 s en verificar los 16 parches** con hash bit-exacto, y no está conectado.

Para un proyecto sin equipo de mantenimiento, esta es probablemente la mejora de fiabilidad con mejor
relación coste/beneficio de todo el documento: un job de CI de tres pasos convierte «espero que este
build esté bien» en «el build está verificado o no se publica».

```yaml
  test-core:
    runs-on: macos-15
    steps:
      - uses: actions/checkout@v4
      - name: Build & run e2e
        run: |
          cmake -S librdpiano -B build -DRDPIANO_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release
          cmake --build build --target rdpiano_e2e
          ./build/rdpiano_e2e --roms roms --golden librdpiano/test/golden.txt
```

y añadir `needs: [build-osx, test-core]` al job `release`.

Aparte, lo ya señalado en A17 y que sigue vigente: `marvinpinto/action-automatic-releases@latest` es
una acción **archivada por su autor**, referenciada por una etiqueta **mutable**, ejecutada con
`GITHUB_TOKEN` con permiso de escritura. Quien controle esa etiqueta puede publicar artefactos
arbitrarios en la release que descargan los usuarios. Sustituirla por `softprops/action-gh-release`
fijada por SHA, y declarar `permissions: contents: write` en el job.

---

## 17. Cómo verificar todo esto sin oídos: ampliar el harness

El harness e2e cubre bien el **núcleo**. Pero casi todos los fallos de este documento están en la
**capa del plugin**, que hoy no se prueba de ninguna forma automática. Tres añadidos, en orden de
valor:

### 17.1 Un simulador de host (el que más fallos habría cazado)

Un ejecutable que reproduzca la aritmética de `processBlock()` contra la `libresample` real —
exactamente lo que hice para medir [§5](#5-n4--alto--cada-cambio-de-parche-produce-dos-clics),
[§7](#7-crash-bloques-mayores-de-lo-declarado--confirma-a4--medido) y
[§8](#8-silencio-por-debajo-de-32-khz--confirma-a1--medido)— y compruebe:

- que **todos** los bloques producen exactamente `numSamples` muestras (ni huecos ni ceros al final);
- que ningún escenario escribe fuera de los búferes (compilar el simulador con ASan);
- que ningún camino sale de `processBlock` sin dejar el búfer definido;
- barriendo: 8/16/22,05/32/44,1/48/88,2/96/192 kHz × bloques 16/32/64/128/512/1024 × bloques de
  tamaño variable × cambios de parche 20↔32 kHz en mitad del stream.

Esto es viable sin JUCE si se factoriza la aritmética de buffers de `processBlock` a una función
libre y comprobable. Es, además, un buen refactor por sí mismo.

### 17.2 Pruebas de robustez MIDI en el harness e2e

Casos que hoy nadie comprueba y que son exactamente los fallos N1, N2, N7 y N8:

- Program Change 0–15 → cada uno debe producir el parche correspondiente y **sonar**.
- CC 123 / CC 120 → el RMS debe caer a silencio en menos de N ms.
- Pedal pisado + note-off + CC 123 → silencio.
- Mensajes cortos (1 y 2 bytes: reloj `0xF8`, active sensing `0xFE`, SysEx `F0 F7`) → no deben
  encolar nada ni leer fuera de los datos del mensaje.
- Nota on sin off × 128 → sin desbordes ni voces infinitas.

### 17.3 Vigilancia de rango en el golden

El harness ya registra el pico por parche. Añadir un umbral explícito: **fallar si el pico convertido
a unidades de plugin supera 1,0** cierra el bucle sobre [§4](#4-n3--alto--el-plugin-no-tiene-headroom-8-de-16-parches-saturan)
y evita que una futura compensación de ganancia se rompa sin que nadie se entere.

### 17.4 Sanitizadores en CI

El `CMakeLists.txt` ya fuerza ASan por defecto. Un segundo job que compile el e2e con
`-fsanitize=address,undefined` y lo ejecute detectaría A6 (índice de wave ROM sin enmascarar) y A10
(desbordamiento de `m_icount` a los ~5,9 min) automáticamente. Tarda unos segundos.

---

## 18. Checklist operativo para un directo

Independiente del código, y aplicable ya:

- [ ] **Desactivar el envío de Program Change** hacia la pista de RdPiano hasta que N1 esté
      corregido. Es el fallo con peor consecuencia y activación más probable.
- [ ] Fijar el **tamaño de bloque** del interfaz y no cambiarlo con la sesión abierta (evita A4).
- [ ] Fijar la **frecuencia de muestreo** en 44,1 o 48 kHz (evita A1).
- [ ] **Elegir el parche antes de empezar** y no tocar el dial alfa durante la interpretación; si hay
      que cambiar de sonido, usar los botones y hacerlo entre temas (N4, N5).
- [ ] Dejar **6 dB de margen** en el fader del canal, o un limitador después del plugin (N3).
- [ ] Tener una **segunda instancia ya cargada** con el sonido de repuesto en otra pista silenciada:
      hoy no hay panic ni recarga rápida, y una instancia limpia es el único «reset» fiable.
- [ ] Guardar una **copia congelada** del `.component`/`.vst3` que funciona, fuera de `~/Library`
      (N13).
- [ ] Ejecutar `auval -v aumu RDPN GlZs` tras cada actualización de macOS (N12).
- [ ] Hacer una **prueba de resistencia** de la duración del bolo real (≥ 2 h) con el rig completo
      antes del directo. La estabilidad del núcleo está medida hasta 12 minutos emulados; la del
      conjunto plugin + host + interfaz no.

---

## 19. Plan de trabajo priorizado

Ordenado por *riesgo en escenario ÷ coste de implementación*, no por severidad abstracta.

### Bloque 1 — Antes de volver a tocar en directo

Todo esto es **fuera de `sound_chip.cpp`**: no cambia el hash del golden, así que se verifica con el
harness actual sin arriesgar el timbre.

| Orden | Tarea | Ref. | Esfuerzo |
|---|---|---|---|
| 1 | Botón de pánico + CC 120/123/121 | [§3](#3-n2--crítico--no-existe-panic-cc-120123121-se-ignoran--medido) | bajo |
| 2 | Interceptar Program Change en el plugin y mapearlo a los 16 parches | [§2](#2-n1--crítico--un-program-change-midi-deja-el-plugin-mudo-para-siempre--medido) | medio |
| 3 | `buffer.clear()` en todos los retornos tempranos; quitar el bus de entrada | A8, [§14](#14-n12--medio--un-sintetizador-que-declara-bus-de-entrada) | trivial |
| 4 | Guarda de tamaño de bloque + reasignación (fin del crash) | [§7](#7-crash-bloques-mayores-de-lo-declarado--confirma-a4--medido) | bajo |
| 5 | `setCurrentProgram` con salida temprana si el índice no cambia; dial que aplica al soltar | [§6](#6-n5--alto--el-dial-de-parches-recarga-las-rom-en-cada-evento-de-arrastre) | bajo |
| 6 | Abrir los resamplers en `prepareToPlay` con rango min/max; `samplesError = 0` al reabrir | [§5](#5-n4--alto--cada-cambio-de-parche-produce-dos-clics), A2, A9 | medio |
| 7 | Quitar todos los `printf`/`fflush` de los caminos de audio (contador atómico o solo en DEBUG) | A7 | bajo |
| 8 | Job de CI que ejecute el harness e2e y bloquee la release | [§16](#16-n6--alto--la-ci-publica-releases-que-nadie-ha-verificado) | bajo |

### Bloque 2 — Robustez y calidad de sonido

| Orden | Tarea | Ref. |
|---|---|---|
| 9 | Compensación de ganancia por parche + limitador de seguridad + guarda de NaN | [§4](#4-n3--alto--el-plugin-no-tiene-headroom-8-de-16-parches-saturan), [§11](#11-n9--medio--sin-guarda-de-nan-ni-limitador-a-la-salida) |
| 10 | Corregir el factor `32000 / sampleRate` | [§8](#8-silencio-por-debajo-de-32-khz--confirma-a1--medido) |
| 11 | Temporización MIDI: comparación `<=`, conversión de unidades host→emulador, sin `std::vector` en RT | A5 |
| 12 | Doble búfer / FIFO en lugar del spinlock alrededor de `loadSounds` y `setMasterTune` | A3, [§6](#6-n5--alto--el-dial-de-parches-recarga-las-rom-en-cada-evento-de-arrastre) |
| 13 | Inicializar todos los miembros de `SA_Part`; `waverom_addr &= 0x1FFFF` | A6 |
| 14 | `Mcu::reset()` que vacíe la cola y ponga chip/RAM/latch/timer en estado conocido | A15 |
| 15 | Ring buffer de tamaño fijo para `commands_queue` | [§12](#12-n10--medio--commands_queue-reserva-memoria-en-el-hilo-de-audio) |
| 16 | Filtro de canal MIDI (Omni / 1–16) | [§9](#9-n7--medio-modo-omni-sin-filtro-de-canal-midi--medido) |
| 17 | Simulador de host + pruebas MIDI en el harness | [§17](#17-cómo-verificar-todo-esto-sin-oídos-ampliar-el-harness) |
| 18 | Fijar la acción de release por SHA; `permissions:` mínimo | A17 |

### Bloque 3 — Cuando se toque esa zona

Cambios que **sí** alteran el sonido y requieren verificación auditiva, no solo el hash:

- Escalar la velocidad del chorus por el sample rate del parche ([§13](#13-n11--medio--el-chorus-y-el-trémolo-cambian-de-velocidad-con-el-parche)).
- Fase incremental en el trémolo (elimina el clic al cambiar de rate).
- Revisar el Q = 0,2 del EQ de medios.
- Pitch bend y CC expresivos ([§10](#10-n8--medio--pitch-bend-modulación-y-expresión-se-descartan--medido)).
- Modelo de ciclos de CPU (A10): cambiaría la temporización efectiva del firmware. **No abordar como
  una simple corrección de UB.**

> **Advertencia que conviene repetir.** El harness e2e detecta cualquier cambio de audio mediante un
> hash bit-exacto, pero **no dice si el cambio suena bien**. Todo lo del Bloque 3 mueve el hash a
> propósito: hay que renderizar los WAV (`--wav-dir`), escucharlos y solo entonces regenerar el
> golden. Nunca al revés.

---

## 20. Cómo reproducir las medidas

Todas las sondas se construyen contra el núcleo sin dependencias externas, desde la raíz del
repositorio. Los ficheros de sonda no se han añadido al repositorio; el código de cada una está
descrito aquí lo bastante como para rehacerla en unos minutos.

**Base común** — arrancar un `Mcu` con el mismo handshake que `mcuReset()`:

```cpp
Mcu *m = new Mcu(ic5, ic6, ic7, prog /*RD200_B.bin*/, ic18);
m->loadSounds(ic5, ic6, ic7, ic18, patchToOffset[patch]);
m->reset();
m->commands_queue.push(0x30); m->commands_queue.push(0xE0);
m->commands_queue.push(0x00); m->commands_queue.push(0x00);
for (int i = 0; i < 1024; i++) m->generate_next_sample(rate32);
m->commands_queue.push(0x31); m->commands_queue.push(0x30);
```

```bash
clang++ -std=c++17 -O2 -I librdpiano/include -Wno-constant-logical-operand \
        -o sonda sonda.cpp librdpiano/src/mcu.cpp librdpiano/src/sound_chip.cpp
```

| § | Medida | Método |
|---|---|---|
| [§1](#1-lo-que-sí-está-bien-verificado) | Coste de CPU | Cronometrar 3 s de audio emulado (60 000 muestras) con 0/1/4/16 notas activas, `std::chrono::high_resolution_clock`. |
| [§1](#1-lo-que-sí-está-bien-verificado) | Estabilidad larga | 12 ciclos de: 1 min de silencio emulado + nota de prueba + medida de la cola tras note-off. |
| [§2](#2-n1--crítico--un-program-change-midi-deja-el-plugin-mudo-para-siempre--medido) | Program Change | `sendMidiCmd(0xC0, n, 0)`, esperar 0,5 s, tocar C4 y medir RMS de 0,4 s. |
| [§3](#3-n2--crítico--no-existe-panic-cc-120123121-se-ignoran--medido) | Panic | CC 64=127, acorde de 7 notas, note-offs, y luego CC 123 / 120 / 121 / 64=0 midiendo RMS entre cada uno. Contar además `commands_queue.size()` antes y después de cada CC. |
| [§4](#4-n3--alto--el-plugin-no-tiene-headroom-8-de-16-parches-saturan) | Headroom | Por parche: 16 note-on a velocity 127, acumular el pico sobre 1,2 s, dividir por 262 144 (`sample<<5>>6 / 65536 * 0.5`). |
| [§5](#5-n4--alto--cada-cambio-de-parche-produce-dos-clics), [§7](#7-crash-bloques-mayores-de-lo-declarado--confirma-a4--medido), [§8](#8-silencio-por-debajo-de-32-khz--confirma-a1--medido) | Simulador de host | **Hecho en la fase 2**, y sin copiar nada: `librdpiano/test/unit/test_engine.cpp` instancia `RdPianoEngine`, que ya contiene esas líneas. Cuenta salida por bloque, muestras a cero y desbordes, con ASan. |
| [§6](#6-n5--alto--el-dial-de-parches-recarga-las-rom-en-cada-evento-de-arrastre) | `loadSounds` / `resample_open` | Cronometrar 20 llamadas en bucle y quedarse con media y peor caso. |
| [§9](#9-n7--medio-modo-omni-sin-filtro-de-canal-midi--medido) | Canales | `sendMidiCmd(0x90 \| ch, 60, 100)` para ch = 0, 5, 10, 15; medir RMS. |
| [§12](#12-n10--medio--commands_queue-reserva-memoria-en-el-hilo-de-audio) | Cola y latencia | Encolar N note-on de golpe y contar las muestras hasta `commands_queue.empty()`. Para la ráfaga: inyectar a 10/20/40/80 notas por segundo durante 3 s registrando el máximo de `size()`. |

Máquina de referencia de las medidas de tiempo: macOS 15 (Darwin 25.6), Apple Silicon, build
`-O2` sin sanitizadores. Los tiempos absolutos varían con la máquina; las relaciones
(coste de un bloque vs. presupuesto) no.
