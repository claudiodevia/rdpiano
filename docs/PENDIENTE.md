# Lo que queda pendiente tras la refactorización

**Fecha:** 2026-08-31 · **Rama:** `limpieza` @ `d68938e` · **Alcance:** todo lo que
[REFACTORIZACION.md](REFACTORIZACION.md) dejó abierto al cerrar las fases 0–3.

**Criterio.** Este documento no repite el análisis: recoge, en un solo sitio, los puntos que las
cuatro fases dejaron **explícitamente fuera** —los apartados "Lo que la fase N dejó fuera, y por
qué" y las dos filas *Parciales* de la tabla resumen— y dice de cada uno en qué estado está el
código **hoy**, verificado fichero a fichero, no según el plan. Los defectos funcionales siguen
catalogados en [AUDITORIA.md](AUDITORIA.md) y [FIABILIDAD-DIRECTO.md](FIABILIDAD-DIRECTO.md); aquí
sólo aparecen los que quedaron pendientes *porque un paso del plan decidió no tocarlos*.

**Suelo verificado hoy** (build limpio, sin ASan, Release):
`ctest` verde — `unit` 2,40 s (38 suites, 432 comprobaciones, 0 fallidas) y `e2e` 2,16 s
(16 parches, 0 comprobaciones fallidas, 0 hashes distintos del golden). Cualquier cosa de esta
lista se mide contra eso. Las cinco comprobaciones nuevas respecto a las 427 de la primera
redacción son las del headroom (§2.1), que ya está hecho.

---

## 0. Resumen

Nada de lo pendiente bloquea el proyecto: los cuatro puntos que sí lo hacían —frontera del motor,
protocolo, build único y pruebas— están cerrados. Lo que queda se reparte en cuatro montones muy
distintos, y **conviene no mezclarlos**, porque sólo el primero es gratis.

| # | Tema | Clase | ¿Mueve el audio? | Esfuerzo | Origen |
|---|---|---|---|---|---|
| 1 | Fila única por parche; `displayPatchNames` en el editor | Limpieza | No | Bajo | [REF §11](REFACTORIZACION.md#11-tablas-de-datos-duplicadas) |
| 2 | `lsp_common.h`: tabla, utilidades e IRAM duplicadas | Limpieza | No (lo fija `test_lsp.cpp`) | Bajo | [REF §12](REFACTORIZACION.md#12-lsp-dos-transcripciones-con-la-misma-infraestructura) |
| 3 | Escalado seco duplicado en el harness | Limpieza | No | Muy bajo | [REF §11](REFACTORIZACION.md#11-tablas-de-datos-duplicadas) |
| 4 | LUT de IC10/IC11: opciones 2 y 3 (blob, signos en el bit 15) | Rendimiento | No (lo fija `test_sa_tables.cpp`) | Medio | [REF §4](REFACTORIZACION.md#4-320-kb-de-lut-deterministas-recalculadas-por-instancia) |
| 5 | ~~Headroom: pico 2,82 (≈ +9 dB sobre fondo de escala)~~ **hecho** | **Cambio de audio** | **Sí, aplicado** | — | [FIAB §4](FIABILIDAD-DIRECTO.md) |
| 6 | 23 huecos de silencio por troceado (búfer circular de salida) | **Cambio de audio** | **Sí** | Alto | [REF §17.6](REFACTORIZACION.md#176-lo-que-solo-se-puede-probar-con-un-motor-de-verdad) |
| 7 | Ritmo del margen de arranque: motor 20 kHz, harness el del parche | **Cambio de audio** | **Sí** (5 parches) | Bajo | trampa 7 de [CLAUDE.md](../CLAUDE.md) |
| 8 | MIDI que se descarta: program change, CC 120/123, canal, pitch bend | Comportamiento | Sí, donde hoy no suena nada | Medio | [FIAB §§2, 3, 9, 10](FIABILIDAD-DIRECTO.md) |
| 9 | Cambio de parche: recarga de ROM bajo `mcuLock`, dos clics | Comportamiento | Sí (transitorio) | Medio | [AUD §3](AUDITORIA.md), [FIAB §§5, 6](FIABILIDAD-DIRECTO.md) |
| 10 | `masterTune` no automatizable; aplicarlo corre el emulador | Comportamiento | No | Medio | [REF §9](REFACTORIZACION.md#9-parámetros-hacerlo-a-mano-cuesta-120-líneas-y-se-desincroniza) |
| 11 | `SoundChip::read(offset)` ignora el offset; `offset % 8` pliega | Aritmética | **Sí, si se decide mal** | Bajo | [AUD §14](AUDITORIA.md), [REF §5](REFACTORIZACION.md#5-soundchipupdate-tres-bloques-que-piden-ser-tres-funciones) |
| 12 | `waverom_addr` sin máscara; `SA_Part` con 7 campos sin inicializar | Aritmética | **Sí, si se toca** | Bajo | [AUD §6](AUDITORIA.md), fase 1 |
| 13 | Bus de entrada estéreo en un plugin `pluginIsSynth` | Compatibilidad | No | Bajo | [FIAB §14](FIABILIDAD-DIRECTO.md) |
| 14 | La UI no la prueba nadie | Prueba | — | Medio | fase 3 |
| 15 | JUCE clavado en 8.0.1; acción de release sin fijar; sin firmar | Build | No | Bajo–Medio | fase 3, [AUD §17](AUDITORIA.md), [FIAB §15](FIABILIDAD-DIRECTO.md) |

Los puntos 1–4 se pueden hacer hoy, en cualquier orden, y el golden lo prueba. El 5 ya está hecho
(§2.1); los 6–9 son decisiones de producto que hay que **escuchar** antes de dar por buenas. Los 11 y 12 son los únicos
que pueden mover la aritmética del emulador, y por eso van con el procedimiento de
[REF §17.1](REFACTORIZACION.md#171-la-regla-caracterizar-antes-de-mover) o no van.

---

## 1. Deuda de refactor pura (no cambia el audio)

### 1.1 La fila única por parche — [REF §11](REFACTORIZACION.md#11-tablas-de-datos-duplicadas)

Es lo único que la tabla resumen de REFACTORIZACION deja marcado *Parcial* junto con `lsp/`. La
fase 0 subió a [patches.h](../librdpiano/include/patches.h) la identidad de las ROM y puso
`inline constexpr` y `static_assert` en todo, pero **siguen siendo cinco arrays paralelos**
alineados a mano: `patchNames` (:25), `patchToRomSetId` (:34), `patchToOffset` (:41),
`patchSampleRates` (:63) y `romSetFiles` (:82).

Y sigue habiendo una segunda tabla de nombres: `displayPatchNames` en
[PluginEditor.cpp:172](../rdpiano_juce/Source/PluginEditor.cpp#L172), en formato de 2×17 columnas
para el LCD, que es lo que consume [PluginEditor.cpp:437](../rdpiano_juce/Source/PluginEditor.cpp#L437).
El comentario de :170 ya deja escrito que duplica `patchNames`.

Lo que autoriza el cambio ya existe: `test_patches.cpp` y los diez `static_assert`. Un
`struct Patch { name; displayName; romSet; offset; sampleRate; }` con `kPatches[NUM_PATCHES]`
elimina los cinco arrays y la tabla del editor de una vez, y las comprobaciones actuales siguen
valiendo sin editarlas.

### 1.2 `lsp_common.h` — [REF §12](REFACTORIZACION.md#12-lsp-dos-transcripciones-con-la-misma-infraestructura)

Pendiente desde la fase 0 y anotado otra vez en la 2 y en la 3. Verificado hoy: siguen duplicados

- `spaceDRateTable` ([spaced.h:15](../librdpiano/include/lsp/spaced.h#L15)) y `phaserRateTable`
  ([phaser.h:15](../librdpiano/include/lsp/phaser.h#L15)) — 128 valores idénticos, byte a byte;
- `writeMemOffs`/`readMemOffs` sobre `iram[0x200]`
  ([spaced.h:87](../librdpiano/include/lsp/spaced.h#L87),
  [phaser.h:76](../librdpiano/include/lsp/phaser.h#L76));
- `DATA_BITS`, `MIN_VAL`, `MAX_VAL`, `clamp_24`, `sign_extend_24`.

La red que faltaba la puso la fase 2: `test_lsp.cpp` congela por hash la respuesta a impulso y a
barrido de los dos efectos, con seis hashes, y comprueba que las dos tablas de rate son la misma.
Con eso, unificar el andamiaje es un refactor verificable — **sin tocar una línea de los cuerpos
transcritos** ([REF §20](REFACTORIZACION.md#20-qué-no-tocar)).

### 1.3 El escalado seco, todavía en dos sitios

La fase 2 lo dejó **una vez** en el motor (`kEmuInputShift`, `kEmuOutputShift`, `kOutputScaling` en
[rd_engine.cpp](../librdpiano/src/rd_engine.cpp)), pero el harness conserva su copia:
`plugin_scale()` en [e2e.cpp](../librdpiano/test/e2e.cpp), con el comentario "El plugin escala así
la señal seca". Son dos líneas; el riesgo es que diverjan y los WAV de `--wav-dir` dejen de sonar
como el plugin sin que nada lo diga.

El headroom (§2.1) subió la apuesta: la compensación por parche también tuvo que aplicarse a mano
en `plugin_scale()` para que los WAV siguieran saliendo al nivel del producto. Es exactamente la
divergencia que este punto anuncia, ya cobrada una vez.

### 1.4 Las LUT: opciones 2 y 3 — [REF §4](REFACTORIZACION.md#4-320-kb-de-lut-deterministas-recalculadas-por-instancia)

La fase 1 aplicó la opción 1 (compartirlas): `sizeof(Mcu)` −320 KB y la segunda instancia de 21 ms
a 4,9 ms. Quedan las otras dos —blob precalculado en vez de generador, y signos empaquetados en el
bit 15— con los dos `TODO: I want to believe there is a better way`
([sa_tables.cpp:14](../librdpiano/src/sa_tables.cpp#L14) y :68) intactos. No son urgentes: lo que
dolía era el coste por instancia, y eso ya está. `test_sa_tables.cpp` las autoriza cuando se
quieran.

---

## 2. Decisiones de comportamiento aplazadas (hay que escucharlas)

### 2.1 Headroom — [FIABILIDAD §4](FIABILIDAD-DIRECTO.md) · **hecho**

Estaba así: `engine_headroom` medía **pico 2,82** con 16 voces a velocity 127 y la cadena seca, casi
9 dB por encima de fondo de escala. Medido después con el motor entero —EQ incluido, que es lo que
de verdad sale del plugin— era peor de lo que decía FIABILIDAD: **los 16 parches** pasaban de fondo
de escala, de +1,9 dBFS (Harpsichord) a **+13,7** (E-Piano 1), y entre el más flojo y el más
caliente había casi **12 dB**. Cambiar de sonido cambiaba de nivel.

Lo aplicado es el punto 1 de los tres que proponía FIABILIDAD: una **compensación por parche**,
`patchOutputGain[]` en [patches.h](../librdpiano/include/patches.h), que normaliza los 16 al mismo
pico. El motor la aplica en la salida
([rd_engine.cpp](../librdpiano/src/rd_engine.cpp), junto a `kOutputScaling`), **después** del
emulador y de `lsp/`: los dos son aritmética entera transcrita del hardware, así que el golden del
e2e y los seis hashes de `test_lsp.cpp` siguen intactos. Verificado: `ctest` verde sin regenerar
nada.

El objetivo es **−6 dBFS**, no los −3 que sugería FIABILIDAD, y la razón es el punto 2 que **no** se
ha aplicado: no hay limitador detrás. Medido, el chorus a la profundidad de fábrica añade hasta
**+4,9 dB** sobre la señal seca —el phaser, al contrario, atenúa, así que el peor caso no es tener
todos los efectos puestos sino sólo el chorus, que además es el ajuste de fábrica—; con la seca a
−6 dBFS el peor caso de toda la cadena se queda en **−1,15 dBFS** y nada recorta. El punto 3
(revisar el Q = 0,2 del EQ) tampoco se ha tocado: eso es timbre, no headroom.

La medida vive en el harness, no en un script suelto: **`rdpiano_e2e --headroom`** mide el pico de
cada parche con el motor entero y escribe la tabla corregida. Es idempotente —aplica la ganancia
que ya está puesta y propone `actual × objetivo / medido`—, así que una segunda pasada devuelve los
mismos números, y por eso la tabla se puede regenerar sin arrastrar el error de la anterior.

`engine_headroom` ([test_engine.cpp](../librdpiano/test/unit/test_engine.cpp)) está dado la vuelta,
que era justo lo que la comprobación anterior pedía: ya no fija "recorta hoy" sino que no recorta
(parches 0 y 6, el que era más caliente, los dos al objetivo) y que el peor caso con chorus —el
parche 5— se queda por debajo de fondo de escala.

**Lo que hay que escuchar.** Es un cambio de audio y bastante grande: el nivel de salida baja entre
8 y 17 dB según el parche, así que **las sesiones guardadas suenan más flojas** y el `volume` del
plugin llega sólo a 1,0, sin margen para recuperarlo desde dentro. Los WAV de `--wav-dir` ya salen
con la compensación puesta, para que lo que se escucha sea el nivel del producto.

### 2.2 Los 23 huecos por troceado — [REF §17.6](REFACTORIZACION.md#176-lo-que-solo-se-puede-probar-con-un-motor-de-verdad)

La invariancia de bloque bit a bit **no es una propiedad de esta arquitectura**: `renderBufferFrames`
se redondea hacia arriba y el corrector de deriva le resta hasta `numFrames/4`
([rd_engine.cpp:267-283](../librdpiano/src/rd_engine.cpp#L267-L283)), así que cuántas muestras
genera el emulador depende de cómo trocee el host. Medido: 4.096 muestras de una vez contra
`7+13+1+512+256+3+1024+64` coinciden al 0,01 % de nivel, pero **23 muestras salen en silencio
exacto**. Es el `printf("click")` que el plugin arrastraba desde siempre, ahora medible;
`engine_block_invariance` ([test_engine.cpp:305](../librdpiano/test/unit/test_engine.cpp#L305)) fija
el número con margen (`<= 32`) para que el día que se arregle baje y no suba.

El arreglo es un **búfer circular de salida**: el emulador produce a su ritmo, `render()` consume lo
que le piden y guarda el resto. Es el cambio de diseño más grande que queda, y cambia el audio.

### 2.3 El ritmo del margen de arranque — trampa 7

`Mcu::boot()` pide el ritmo sin valor por defecto, a propósito, porque motor y harness no coinciden:
el motor pasa 20 kHz siempre ([rd_engine.cpp:177](../librdpiano/src/rd_engine.cpp#L177), con su
razón escrita en :171) y el harness pasa el del parche
([e2e.cpp:208](../librdpiano/test/e2e.cpp#L208)). En los cinco parches de 32 kHz eso son dos
arranques distintos. Alinearlos movería el golden de esos cinco: es un cambio de audio a escuchar,
no un ajuste de coherencia.

### 2.4 El MIDI que se descarta

[`sendMidiCmd`](../librdpiano/src/mcu.cpp#L559) atiende exactamente cuatro cosas: program change,
note off, note on y sustain (CC 64). Verificado hoy, queda fuera:

| Qué | Estado | Documentado en |
|---|---|---|
| **Program change entrante** | Se pasa al firmware (`programChange(data2 & 0xF)`) sin remapear ROM ni página de params: deja el plugin mudo | [FIAB §2](FIABILIDAD-DIRECTO.md) (N1, CRÍTICO) |
| **CC 120/123 (panic)** | Se ignoran. `allNotesOff()` existe y está probado desde la fase 1 ([command_port.h:136](../librdpiano/include/command_port.h#L136)), pero nada lo conecta al MIDI entrante — el propio comentario de :133 lo dice | [FIAB §3](FIABILIDAD-DIRECTO.md) |
| **Filtro de canal** | No hay: omni permanente, ni en `pushMidi` ni en `sendMidiCmd` | [FIAB §9](FIABILIDAD-DIRECTO.md) |
| **Pitch bend, modulación, expresión** | Se descartan | [FIAB §10](FIABILIDAD-DIRECTO.md) |

Lo importante para el refactor: **ya existe el sitio donde arreglarlo** —era lo que §3 perseguía—,
así que cada uno de los cuatro es hoy una línea en `sendMidiCmd` más su prueba en
`test_command_port.cpp`. El primero es el más grave y el menos obvio: intercepta un mensaje que el
usuario espera que cambie de parche.

### 2.5 El cambio de parche sigue bloqueando el hilo de audio

[PluginProcessor.cpp:115-118](../rdpiano_juce/Source/PluginProcessor.cpp#L115-L118) toma `mcuLock`
desde el hilo de UI y llama a `engine->setPatch()`, que puede recargar las ROM de onda
([rd_engine.cpp:204-205](../librdpiano/src/rd_engine.cpp#L204-L205)). La fase 1 abarató el caso
común —dentro del mismo ROM set son 0,8 ms en vez de 2,86— pero el diseño es el mismo: el hilo de
audio espera en un spinlock mientras la UI trabaja ([AUD §3](AUDITORIA.md)). Y arrastrar el dial de
parches sigue disparando una recarga por evento ([FIAB §6](FIABILIDAD-DIRECTO.md)) y dos clics por
cambio ([FIAB §5](FIABILIDAD-DIRECTO.md)). El arreglo natural —diferir el cambio al principio de
`render()`— es el mismo que pide `masterTune`.

### 2.6 `masterTune` automatizable — [REF §9](REFACTORIZACION.md#9-parámetros-hacerlo-a-mano-cuesta-120-líneas-y-se-desincroniza)

Sigue fuera del `APVTS`, viajando en el preset como propiedad del árbol, y por buen motivo:
`setMasterTune()` corre ~200 muestras de emulador y termina con un `programChange(0)`
([mcu.cpp:606-620](../librdpiano/src/mcu.cpp#L606-L620), trampa 4). Hacerlo automatizable exige
antes diferir la aplicación al principio de `render()`, bajo el lock que ya existe — el mismo
mecanismo que 2.5, y por eso los dos deberían ir juntos.

### 2.7 Los dos puntos que pueden mover la aritmética

- **`SoundChip::read(offset)` ignora su parámetro** ([sound_chip.cpp:20-22](../librdpiano/src/sound_chip.cpp#L20-L22)):
  devuelve siempre `m_irq_id`. Y `write()` decodifica el campo con `offset % 8`
  ([:29](../librdpiano/src/sound_chip.cpp#L29)), que pliega los bytes altos
  ([AUD §14](AUDITORIA.md)). Decidir cuál de las dos lecturas es la correcta es un cambio de
  comportamiento, no un movimiento de código: la fase 1 lo dejó fuera por eso.
- **`waverom_addr` sin máscara** ([sa_blocks.h:154](../librdpiano/include/sa_blocks.h#L154)):
  con `wave_addr_high >= 0x40` indexa fuera de `samples_exp[0x20000]`. El firmware no parece
  producir ese estado —se descubrió alimentando el chip a mano al construir los vectores de borde—
  pero nada lo impide. Y `SA_Part` ([sa_blocks.h:30-37](../librdpiano/include/sa_blocks.h#L30-L37))
  sigue con siete campos sin inicializar (`pitch_lut_i`, `wave_addr_loop`, `wave_addr_high`,
  `env_dest`, `env_speed`, `flags_*`, `env_offset`), que es [AUD §6](AUDITORIA.md).

Poner una máscara **es** un cambio de aritmética ([REF §20](REFACTORIZACION.md#20-qué-no-tocar)):
si se toca, se toca con los 2.256 vectores de `ic_blocks.txt` delante y comparando el golden.

### 2.8 El bus de entrada estéreo

Un plugin marcado `pluginIsSynth` que declara entrada estéreo. El daño que causaba —devolver la
entrada como salida en los retornos tempranos— desapareció en la fase 2 al escribir siempre los
`numFrames`. Quitar el bus cambia el layout que ven los hosts y puede romper sesiones guardadas:
las fases 2 y 3 lo dejaron fuera por eso, y sigue siendo la decisión correcta salvo que se decida
romper compatibilidad ([FIAB §14](FIABILIDAD-DIRECTO.md)).

---

## 3. Lo que ninguna prueba cubre

Sigue vigente [REF §17.8](REFACTORIZACION.md#178-lo-que-estas-pruebas-no-cubren), con un matiz que
la fase 3 añadió:

- **La UI.** `rdpiano_plugin_tests` llega al `AudioProcessor`, no al `AudioProcessorEditor`. Las dos
  tablas de la fase 3 (`ButtonSpec`, `ModeSpec`) hacen el panel mucho más fácil de probar —son
  datos— pero nadie comprueba todavía que cubran todos los parámetros y no repitan índices, que es
  lo que [REF §17.7](REFACTORIZACION.md#177-datos-y-plugin) proponía y es barato.
- **`setMasterTune()`**, que no lo toca ninguna suite de extremo a extremo.
- **El timbre.** El golden dice si cambió; no dice si mejoró. Un cambio en `sound_chip.cpp` que pase
  el harness con el hash movido sigue siendo de alto riesgo tímbrico.
- **El host real**: automatización, suspensión, cambios de tasa en caliente, escaneo.

---

## 4. Build e infraestructura

| Qué | Estado hoy | Nota |
|---|---|---|
| **JUCE 8.0.1** | Clavado en [download-juce.sh:16](../scripts/download-juce.sh#L16) | Actualizar quitaría el `CGWindowListCreateImage` que obliga a fijar el objetivo de despliegue en 10.13, pero mueve DSP y UI: hay que escucharlo y mirarlo |
| **Acción de release sin fijar** | `marvinpinto/action-automatic-releases@latest` en [main.yml:101](../.github/workflows/main.yml#L101) | Es la última cadena de suministro sin pinear del workflow: las de `actions/*` sí llevan versión. [AUD §17](AUDITORIA.md) |
| **Binarios sin firmar ni notarizar** | No hay `codesign` en ningún sitio | [FIAB §15](FIABILIDAD-DIRECTO.md). Gatekeeper los bloquea en una máquina que no sea la del que compila |
| **Deuda de formato** | `sound_chip.cpp` y `patches.h` a 4 espacios; zonas heredadas de MAME con tabs | Se salda al tocarlos, nunca en bloque ([CLAUDE.md](../CLAUDE.md)) |

---

## 5. Orden sugerido, si hay una fase 4

Mismo criterio que las anteriores: la prueba (**T**) va antes del cambio, y ningún paso empieza con
su **T** en rojo por otra razón que la que arregla.

| # | Paso | Por qué en este orden |
|---|---|---|
| 23 | Fila única por parche + `displayPatchNames` fuera del editor | No toca audio, y `test_patches.cpp` ya lo protege. Cierra §11 |
| 24 | `lsp_common.h` | No toca audio, y `test_lsp.cpp` ya lo protege. Cierra §12 y con él la tabla resumen de REFACTORIZACION |
| 25 | `plugin_scale()` del harness → constante del motor | Dos líneas; evita que los WAV de `--wav-dir` diverjan del producto |
| 26 | **T** + tabla del editor cubierta (índices y cobertura de parámetros) | Barato, y es lo único de la UI que se puede probar sin abrir ventana |
| 27 | **T** + MIDI: program change, CC 120/123, filtro de canal, pitch bend | Cada uno una línea en `sendMidiCmd`; N1 primero, que es el crítico |
| 28 | **T** + cambio de parche y `masterTune` diferidos al principio de `render()` | Un solo mecanismo resuelve 2.5 y 2.6; habilita automatizar `masterTune` |
| ~~29~~ | ~~Headroom: compensación por parche~~ **hecho** (§2.1). Queda el limitador, si se quiere | Se hizo fuera de orden: no toca el emulador, así que el golden no lo tapa |
| 30 | Búfer circular de salida | El más grande y el que más audio cambia; con el golden y los WAV delante |

Los puntos 11 y 12 del resumen (§2.7) **no aparecen en la lista a propósito**: no son pasos de
refactor, son decisiones sobre hardware emulado que hay que resolver mirando el silicio o los
vectores, no reordenando código.

---

## 6. Cómo verificar cualquiera de estos pasos

Lo mismo de siempre, sin novedad ([REF §21](REFACTORIZACION.md#21-cómo-reproducir-las-medidas)):

```bash
cmake -S librdpiano -B build/core -DRDPIANO_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --target rdpiano_tests rdpiano_e2e
ctest --test-dir build/core --output-on-failure          # unit + e2e
```

Y con el plugin dentro, desde la raíz:

```bash
cmake -B build/plugin -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build/plugin --config Release --target rdpiano_tests rdpiano_e2e rdpiano_plugin_tests
ctest --test-dir build/plugin -C Release --output-on-failure
```

Para los pasos 29 y 30, además: `rdpiano_e2e --wav-dir DIR`, escuchar los 16 WAV, y sólo entonces
`--write-golden`. La regla no cambia: **nunca se regenera el golden para poner algo en verde.**
