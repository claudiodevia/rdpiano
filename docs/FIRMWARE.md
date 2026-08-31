# Firmware: variantes, handshake y trazas retiradas

**Alcance:** qué firmware ejecuta el emulador, por qué solo ese, y qué se sabe de los otros.

Esta nota recoge conocimiento que hasta ahora vivía en forma de código comentado dentro de
[mcu.cpp](../librdpiano/src/mcu.cpp) y del [.jucer](../rdpiano_juce/rdpiano_juce.jucer). El código
se retiró en la fase 0 del refactor ([REFACTORIZACION §13](REFACTORIZACION.md#13-código-muerto-y-campos-vestigiales));
lo que sabía, está aquí.

---

## 1. Qué firmware se ejecuta

El emulador carga **`roms/RD200_B.bin`** (8 KB) como ROM de programa de la CPU-B, y solo ese,
aunque en `roms/` haya dumps de otras máquinas de la misma familia:

| Fichero | Máquina | Estado |
|---|---|---|
| `RD200_B.bin` | RD-1000 / RD-200, CPU-B | **El que se ejecuta** |
| `RD200_A.bin` | RD-1000 / RD-200, CPU-A | No se emula: la CPU-A se sustituye por `sendMidiCmd()` |
| `mks20_cpub_1.0.bin` | MKS-20, CPU-B v1.0 | Alternativa conocida; ver §2 |
| `mks20_cpua_1.1.BIN` | MKS-20, CPU-A v1.1 | Idem CPU-A |
| `MK80_B.bin`, `MKS20_A.BIN`, `MKS20_B.BIN` | Rhodes MK-80 / MKS-20 | Dumps, no usados por el emulador |

Los dumps de **onda** (IC5/IC6/IC7) y de **parámetros** (IC18) sí se usan los tres juegos: son los
que distinguen un parche MKS-20 de uno MK-80 ([patches.h](../librdpiano/include/patches.h)). Lo que
está atado a una sola variante es el **firmware**, por lo que explica el apartado siguiente.

## 2. Por qué el firmware no es intercambiable: el handshake por PC

`Mcu::read_byte` no implementa el bus de datos entre CPU-A y CPU-B: lo *simula* comparando el
contador de programa con direcciones concretas de la rutina de recepción del firmware. Cuando la
CPU está ejecutando una de esas instrucciones, el puerto 1 devuelve el siguiente byte de
`commands_queue`; el resto del tiempo devuelve `0xff`.

| Firmware | Puerto 1 (datos) | Puerto 2 (control) |
|---|---|---|
| `RD200_B.bin` (**en uso**) | `0xE12B`, `0xE15E`, `0xE168` | `0xE15A` |
| `mks20_cpub_1.0.bin` | `0xE0E4`, `0xE111`, `0xE11B` | `0xE10D` |

Cambiar de firmware **exige recalcular estas cuatro direcciones** desasistiendo la rutina de
recepción del dump nuevo; no basta con sustituir el fichero. Las de MKS-20 de la tabla se
verificaron en su momento y funcionaban, junto con el recurso `mks20_cpub_1.0.bin` empotrado en el
plugin. Ese recurso se retiró del `.jucer` (ocupaba 8 KB en cada binario y nada lo referenciaba);
el fichero sigue en `roms/` para quien quiera retomarlo.

Ver también [ARQUITECTURA §4.2](ARQUITECTURA.md#42-el-handshake-cpu-a--cpu-b) y la trampa 1 de
[CLAUDE.md](../CLAUDE.md).

## 3. El bit de sample rate del puerto 2

El firmware escribe en el puerto 2 (`0x0003`) un bit —`(data >> 2) & 1`— que en la máquina real
selecciona la tasa de muestreo. El emulador lo leía en `Mcu::current_sample_rate`, pero **nunca
llegó a funcionar**: el valor no se corresponde con la tasa real del parche. La tasa que se usa
sale de `patchSampleRates[]` en [patches.h](../librdpiano/include/patches.h), tanto en el plugin
como en el harness, y se le pasa a `generate_next_sample(bool sampleRate32)`.

El campo se retiró por eso. Si alguien quiere retomarlo, el punto de partida es la escritura del
puerto 2 en `Mcu::write_byte` y comparar el bit con la columna de `patchSampleRates[]`.

## 4. El bucle de ejecución y su "failsafe"

`Mcu::execute_run()` ejecuta **una** instrucción por llamada, y `generate_next_sample()` la llama
100 veces (62 a 32 kHz) por muestra. Hubo una versión con bucle por presupuesto de ciclos, del
estilo del `execute_run` de MAME:

```cpp
do {
  if (m_icount > 10000)  // failsafe: el presupuesto nunca debería dispararse
    m_icount = 0;
  if (m_wai_state & (M6800_WAI | M6800_SLP))
    eat_cycles();
  else
    execute_one();
} while (m_icount > 0);
```

Se abandonó porque el reloj maestro del emulador es el audio, no la CPU: quien decide cuánto corre
la CPU es `generate_next_sample()`. Consecuencia que conviene tener presente: `m_icount` sigue
existiendo y `increment_counter()` lo va restando, pero **nadie lo recarga**, así que se vuelve
negativo en la primera instrucción y `eat_cycles()` deja de hacer nada. Los estados `WAI`/`SLP` se
atraviesan ejecutando instrucciones, no esperando.
