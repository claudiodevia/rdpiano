# Revisión de código — lo pendiente

**Alcance:** `librdpiano/` (núcleo, motor y pruebas) y `rdpiano_juce/` (plugin y panel).
**Al día:** 2026-10-14, `develop` @ `919aa06` más las pasadas 1 (pruebas y scripts) y 2 (plugin) de
PLAN-REVISION.md: todo lo que figura aquí sigue abierto en el código.
**Excluidos del juicio de estilo:** `mcu_ops.h`, `mame_utils.h`, `lsp/`, `resample/` y `re_stuff/`
(trampa 5 de CLAUDE.md).

---

## 1. Riesgo alto: contabilidad de IRQ por instrucción

`librdpiano/src/mcu.cpp:470`

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

Corre **100 veces por muestra** (62 en los parches de 32 kHz), unos 2 millones de veces por segundo de
audio, y el perfil le atribuye el **19 %** del tiempo total (`rdpiano_e2e`, `sample` de macOS, Apple
Silicon; el chip de sonido es el 28 % y la CPU emulada el 68 %). La cola de comandos está vacía casi
siempre —sólo se llena con MIDI— y aun así se consulta en cada instrucción, junto con
`check_irq_lines()` entera.

El estado de la línea TIN y el momento exacto en que se atiende una IRQ son parte de lo que el golden
congela: mover cuándo se comprueba es mover cuándo se entra en la interrupción, y eso es audio. Orden:

1. Tener `--bench` (§3.1) para que la mejora sea medible entre commits.
2. Contar cuántas de esas comprobaciones acaban en un `ASSERT_LINE` real. Si es una de cada 100.000,
   hay un 10-15 % de rendimiento esperando; si no, se cierra y se documenta.
3. Si se toca y mueve el golden, es cambio de audio: **alto riesgo tímbrico**.

En el mismo camino caliente, `RdBoard::read()` tiene la rama de la ROM de programa la primera porque se
lleva la mayoría de los accesos: **no tocar ese orden**.

---

## 2. Sin riesgo: el motor

| Dónde | Qué falta |
|---|---|
| `rd_engine.cpp`, `resampleBlock()` | Los dos canales comparten `inUsed`: el segundo pisa al primero y el retorno del derecho se descarta. Hoy es inocuo porque los remuestreadores son simétricos, pero si se rompiera saldría como **desfase entre canales**. Usar `inUsedL`/`inUsedR` y un `RD_TRACE` si difieren. |
| `rd_engine.cpp`, `prepare()` | `stats.resamplerOpens += 2` se suma aunque `resample_open()` devuelva `nullptr`. El contador vigila en `engine_no_alloc_in_render` (`test_engine_host.cpp`) que no se abran remuestreadores fuera de `prepare()`: debe contar aperturas de verdad. |
| `rd_engine.cpp`, `synthesise()` | `dryL` y `dryR` salen de la misma expresión sobre la misma muestra mono: una variable basta. |
| `rd_engine.cpp`, `prepare()` | `powf(10.0f, kMidEqGainDb * 0.05f)` calculado dos veces seguidas, una por canal. |
| `patches.h`, `offsets_in_range()` | Compara `patchToOffset[i]` con `WAVE_ROM_SIZE`, pero los offsets son de la **ROM de parámetros**; funciona porque las dos miden 0x20000. Debe mirar contra una constante de la params ROM. |
| `rd_engine.h`, `RdEngineStats` | `unsigned long` que escribe el hilo de audio y puede leer la UI. `std::atomic<unsigned long>` con `relaxed` cuesta cero y quita el UB formal. |
| `sa_tables.cpp`, `sa_tables()` | Falta decir que el `new` del *magic static* **no se libera a propósito** (singleton de proceso) y que LeakSanitizer no lo denuncia porque sigue alcanzable. Una línea, para que nadie lo "arregle" con un destructor. |
| `sound_chip.cpp`, `SoundChip::read()` | Falta la línea que diga si el chip devuelve de verdad un único registro en todo el rango o si es un agujero conocido. |

---

## 3. Sin riesgo: pruebas

### 3.1 `rdpiano_e2e --bench`

No hay medida de rendimiento en el árbol: el perfil se hace a mano con `sample` y las cifras de dos
sesiones no se pueden comparar. Una bandera que imprima muestras emuladas por segundo daría una cifra
comparable entre commits. Es el requisito previo de §1.

### 3.2 Suite para el EQ medio

`RdBiquad` es el único bloque de la cadena sin suite propia; sólo lo cubren de refilón los hashes.

### 3.3 La trampa 3 no la fijan los vectores

`sound_chip_blocks_vectors` reimplementa el silenciado `investigate` (`part.env_value != 0 ? sample : 0`) en
la prueba en vez de ejercitar `SoundChip::update()`, y ningún vector cubre el early-out
`env_value == 0 && env_dest == 0`: los dos hacks solo los vigila el golden de e2e. El arreglo es un
`part_contribution()` compartido por `update()` y la suite; toca el bucle caliente, así que va con el lote 6
(alto riesgo) de la pasada 4.

### 3.4 Restos de la pasada 1

| Dónde | Qué falta |
|---|---|
| `test_lsp.cpp`, `lsp_spaced` | El impulso "del plugin" copia a mano el periodo de 450 ms de `chorusRateToMsPeriod[5]` y la fórmula de `rd_engine.cpp`: si cambian, esta suite no se entera (`engine_golden` sí). Se cierra cuando la pasada 3 saque la tabla de `rd_engine.cpp`. |
| `test_engine_patch.cpp`, `engine_patch_prepare` | Compara `prepareRomSetFor()`, que es un no-op, con nada. Se reescribe o se borra al quitar esa API (pasada 3, lote 11); lo que decía fijar lo fija ya `board_select_patch_page`. |
| `mcu.h`, `commandPort() const` | Acceso de solo lectura a la cola, añadido para `test_mcu.cpp`. Si la pasada 4 crea `RdMachine`, el acceso nace ahí. |
| `rdpiano_standalone` | Arranca ya con `boot()` y lee las ROM de `patches.h`, pero solo sabe el parche 0 a 20 kHz y nadie lo ha escuchado tras el cambio. La CI lo compila desde que instala SDL2 y portmidi; un `--patch N` (con la tasa de `patchSampleRates[]`) lo haría útil para juzgar los 16 timbres. |
| `core-asan` | Sin `-fsanitize=undefined -fno-sanitize-recover`: puede saltar en el código de MAME y pedir supresiones. |
| `engine_golden` | Hashes de los bits `float` capturados en Apple Silicon, iguales en Release y en RelWithDebInfo con ASan. La CI solo corre arm64; en x86_64 (Rosetta) no se han comprobado. |

---

## 4. Restos de la pasada 2: el plugin

| Dónde | Qué falta |
|---|---|
| Logic, gestos | Fader, conmutadores y dial en modo parámetro abren y cierran gesto (`plugin_param_gesture` lo fija para `setParamValue()`), pero nadie ha grabado automatización en modo touch o latch con el plugin instalado. Hasta probarlo en Logic (y a ser posible en Live) el arreglo no está confirmado. |
| Logic, el resto | Instalar (`build-osx.sh AU install`, pide contraseña) y probar carga, presets, dial, display y automatización: el panel solo se ha comparado con capturas fuera de la ventana. |
| Dial en modo parámetro | Los 15 pasos se reparten ahora el recorrido por igual (`panel::stepFromDial`); antes el último solo se alcanzaba en el tope y la ida y vuelta podía caer un paso abajo por redondeo. Cambia el tacto: confirmarlo a mano. |
| Fader con automatización | `SliderParameterAttachment` atiende los cambios del hilo de audio con `triggerAsyncUpdate()`, que puede tomar un cerrojo al publicar el mensaje: es el camino estándar de JUCE (lo hacen todos sus attachments), pero es la misma clase de llamada que se quitó de `parameterChanged()`. Si molesta, el fader puede volver al temporizador a costa de moverse a 10 Hz. |
| Dibujo del panel | Sin prueba: en la pasada se comparó píxel a píxel con un arnés temporal (`createComponentSnapshot` del editor en ocho estados, con `ScopedJuceInitialiser_GUI`) que no quedó en el árbol porque no está probado en la CI sin pantalla. Único cambio visible: el sprite del dial se mueve 1 px en algunas posiciones (ángulo `1,5·π` en vez de `3.14/2 + 3.14`). |
| `patches.h`, `patchNames` | El Doxygen dice "tal como lo enseña el display": es el nombre de programa del host; el display lo parte por los dos puntos (`PanelDisplay.cpp`). Una línea del núcleo, fuera del alcance de la pasada 2. |
| `lcd/` | `Lcd.h`, `Lcd.cpp` y `lcd_font.h` siguen con finales de línea CRLF, a diferencia del resto del árbol: normalizarlos en commit aislado. |
