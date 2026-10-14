# Revisión de código — lo pendiente

**Alcance:** `librdpiano/` (núcleo, motor y pruebas) y `rdpiano_juce/` (plugin y panel).
**Al día:** 2026-10-14, `develop` @ `55545df`: todo lo que figura aquí sigue abierto en el código.
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
| `rd_engine.cpp`, `prepare()` | `stats.resamplerOpens += 2` se suma aunque `resample_open()` devuelva `nullptr`. El contador vigila en `test_engine.cpp` que no se abran remuestreadores fuera de `prepare()`: debe contar aperturas de verdad. |
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
