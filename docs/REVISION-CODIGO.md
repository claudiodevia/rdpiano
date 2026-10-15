# Revisión de código — lo pendiente

**Alcance:** `librdpiano/` (núcleo, motor y pruebas) y `rdpiano_juce/` (plugin y panel).
**Al día:** 2026-10-14, `develop` @ `1870b79` más la pasada 3 (motor) de PLAN-REVISION.md, después de las
pasadas 1 (pruebas y scripts) y 2 (plugin): todo lo que figura aquí sigue abierto en el código.
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

## 2. Sin riesgo: comentarios del emulador

| Dónde | Qué falta |
|---|---|
| `sa_tables.cpp`, `sa_tables()` | Falta decir que el `new` del *magic static* **no se libera a propósito** (singleton de proceso) y que LeakSanitizer no lo denuncia porque sigue alcanzable. Una línea, para que nadie lo "arregle" con un destructor. |
| `sound_chip.cpp`, `SoundChip::read()` | Falta la línea que diga si el chip devuelve de verdad un único registro en todo el rango o si es un agujero conocido. |

---

## 3. Sin riesgo: pruebas

### 3.1 `rdpiano_e2e --bench`

No hay medida de rendimiento en el árbol: el perfil se hace a mano con `sample` y las cifras de dos
sesiones no se pueden comparar. Una bandera que imprima muestras emuladas por segundo daría una cifra
comparable entre commits. Es el requisito previo de §1.

### 3.2 La trampa 3 no la fijan los vectores

`sound_chip_blocks_vectors` reimplementa el silenciado `investigate` (`part.env_value != 0 ? sample : 0`) en
la prueba en vez de ejercitar `SoundChip::update()`, y ningún vector cubre el early-out
`env_value == 0 && env_dest == 0`: los dos hacks solo los vigila el golden de e2e. El arreglo es un
`part_contribution()` compartido por `update()` y la suite; toca el bucle caliente, así que va con el lote 6
(alto riesgo) de la pasada 4.

### 3.3 Restos de la pasada 1

| Dónde | Qué falta |
|---|---|
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
| `lcd/` | `Lcd.h`, `Lcd.cpp` y `lcd_font.h` siguen con finales de línea CRLF, a diferencia del resto del árbol: normalizarlos en commit aislado. |

---

## 5. Restos de la pasada 3: el motor

| Dónde | Qué falta |
|---|---|
| Volumen frente a ganancia de parche | `volume` se aplica a la tasa del emulador (`synthesise()`) y `patchOutputGain` tras el remuestreador (`OutputStage`). Unificarlos no es bit a bit igual: mueve `engine_golden` y solo se haría escuchando. Recomendación: dejarlo. |
| `EffectsChain::kChorusDepthDivisor` | La profundidad del chorus se divide por 15 y la del trémolo por 14: con el dial a tope el chorus se queda en 14/15. Puede ser un error de transcripción, pero cambiarlo es audio: solo escuchando y moviendo `engine_golden` a propósito. |
| `Mcu`, constructor | Descifra la ranura 0 dos veces: `SoundChip` en su constructor y `RdBoard::loadRomSet()` otra vez (~1,2 ms cada una). `PatchBank` ya no añade la tercera. Es de la pasada 4 (el camino del harness). |
| Cifras de coste | Los Doxygen de `Mcu::loadRomSet()` y `RdBoard::decodeRomSet()` dicen ~2,9 ms por juego de onda; medido ahora (Apple Silicon, Release), ~1,2 ms, y construir el motor entero ~6,2 ms. Pasada 4. |
| `Mcu::sendMidiCmd()` | Queda solo para e2e y el standalone SDL; el motor decodifica una vez (`KeyEvent::decode()`) y llama a `noteOn`/`noteOff`/`sustain`. Las dos decodificaciones deben coincidir hasta que la pasada 4 decida si `sendMidiCmd()` se apoya en ellas. |
| `RdBoard::selectRomSet()` | El motor ya no la usa (`Mcu::selectWaveSet()` cambia solo la onda), así que `params_rom_src` se queda con la IC18 del parche 0. Inocuo: el motor mapea páginas ya descifradas y nunca llama a `selectPatch()`. |
