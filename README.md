<div align="center">

# RdPiano

**El sonido de los pianos digitales de Roland de los ochenta, en tu Mac.**

[![CI](https://github.com/claudiodevia/rdpiano/actions/workflows/main.yml/badge.svg)](https://github.com/claudiodevia/rdpiano/actions/workflows/main.yml)
[![JUCE 9.0.1](https://img.shields.io/badge/JUCE-9.0.1-8DC63F)](https://juce.com/)
[![macOS](https://img.shields.io/badge/macOS-11%2B%20universal-000000?logo=apple&logoColor=white)](#instalación)
[![Formatos](https://img.shields.io/badge/formatos-VST3%20%7C%20AU%20%7C%20AUv3%20%7C%20LV2%20%7C%20Standalone-orange)](#instalación)
[![Licencia GPLv3](https://img.shields.io/badge/licencia-GPLv3-blue)](LICENSE)

[Instalación](#instalación) · [Características](#características) · [Vídeo](#así-se-ve-y-así-suena) · [Compilar](COMPILAR.md)

</div>

---

## Qué es RdPiano

Los dieciséis sonidos de dos pianos digitales de Roland de los años ochenta —el **MKS-20** y el
piano eléctrico **Rhodes MK-80**—, en un instrumento que puedes abrir en tu programa de música o
tocar por su cuenta.

> ℹ️ **No son grabaciones.** RdPiano funciona por dentro como la máquina de verdad: se reconstruyó
> pieza a pieza lo que hacía el aparato al pulsar una tecla, y cada nota se calcula igual que la
> calculaba él. El chorus y el trémolo son la única excepción: ahí la imitación es aproximada.

---

## Instalación

> 💻 Necesitas un Mac con macOS 11 o posterior. Da igual que sea Apple o Intel: el archivo es el mismo.

### Paso 1 · Descarga el que uses

Si no tienes claro cuál, empieza por la **aplicación**: se abre sola, sin nada más.

| | Descarga esto | Si usas |
|---|---|---|
| 🎹 | **[Aplicación](https://github.com/claudiodevia/rdpiano/releases/download/latest/rdpiano_juce.app.macOS.zip)** | Nada en particular: tocar y ya está |
| 🍎 | **[AU](https://github.com/claudiodevia/rdpiano/releases/download/latest/rdpiano_juce.component.macOS.zip)** | Logic Pro, GarageBand, MainStage |
| 🎛️ | **[VST3](https://github.com/claudiodevia/rdpiano/releases/download/latest/rdpiano_juce.vst3.macOS.zip)** | Ableton Live, Cubase, Reaper, Studio One |
| 🐧 | **[LV2](https://github.com/claudiodevia/rdpiano/releases/download/latest/rdpiano_juce.lv2.macOS.zip)** | Ardour, Mixbus |

Haz doble clic en el archivo descargado. Aparecerá RdPiano al lado.

### Paso 2 · Guárdalo en su carpeta

Si descargaste la **aplicación**, arrástrala a tu carpeta de Aplicaciones y pasa al paso 3.

Para los demás, la carpeta no se ve a simple vista:

1. Abre el **Finder**.
2. En el menú de arriba, elige **Ir › Ir a la carpeta…**
3. Copia y pega la línea que te toque, y pulsa Intro:

   | Formato | Carpeta |
   |---|---|
   | AU | `/Library/Audio/Plug-Ins/Components` |
   | VST3 | `/Library/Audio/Plug-Ins/VST3` |
   | LV2 | `/Library/Audio/Plug-Ins/LV2` |

4. Arrastra RdPiano dentro de esa carpeta. El Mac te pedirá tu contraseña; es normal.

> 💡 Si ya tenías una versión anterior, arrástrala a la papelera antes de poner la nueva.

### Paso 3 · Dile al Mac que puede abrirlo

> ⚠️ Este paso hay que hacerlo, o tu programa de música dirá que RdPiano está dañado. No lo está: es
> que el Mac desconfía de todo lo que no viene de la App Store.

1. Abre la aplicación **Terminal** (está en Aplicaciones › Utilidades, o búscala con la lupa).
2. Copia y pega **una** de estas líneas, la del formato que instalaste, y pulsa Intro:

   ```bash
   sudo xattr -rd com.apple.quarantine /Applications/rdpiano_juce.app
   sudo xattr -rd com.apple.quarantine /Library/Audio/Plug-Ins/Components/rdpiano_juce.component
   sudo xattr -rd com.apple.quarantine /Library/Audio/Plug-Ins/VST3/rdpiano_juce.vst3
   sudo xattr -rd com.apple.quarantine /Library/Audio/Plug-Ins/LV2/rdpiano_juce.lv2
   ```

3. Te pedirá la contraseña de tu Mac. Escríbela aunque no veas nada en pantalla —no se muestra a
   propósito— y pulsa Intro.

Si no dice nada, ha funcionado. Ya puedes cerrar el Terminal.

### Paso 4 · A tocar

Abre tu programa de música y busca **RdPiano** entre los instrumentos. Si ya estaba abierto, ciérralo
y vuelve a abrirlo. Logic Pro, la primera vez, tarda unos segundos en revisarlo.

---

## Características

**En directo**

- Cambias de sonido mientras tocas y la música no se corta: las notas y el pedal siguen sonando.
- Afinas sobre la marcha sin que el instrumento se quede mudo.
- Los diales enseñan el valor mientras los giras y lo aplican al soltarlos.

**Sonido**

- Los dieciséis sonidos suenan al mismo nivel: ninguno se queda corto ni satura.
- Ni clics ni ruidos al encender un efecto o mover el volumen.

**Rendimiento**

- Cambiar de sonido es cosa de un instante: puedes manejar el panel mientras suena.
- Gasta poca memoria y no la va perdiendo: ocupa lo mismo tras tres horas que al abrirlo.

**Con tu programa de música**

- Cinco formatos, y un solo archivo para Mac con procesador Apple y para Mac con Intel.
- Los ajustes se guardan enteros, con el sonido elegido y la afinación.
- Lo que toques queda a tiempo en la grabación, y al exportar no se corta el final de las notas.

> 🔀 Esto es una bifurcación de [rdpiano](https://github.com/giulioz/rdpiano), que imita la máquina con
> mucha fidelidad pero está pensado como investigación. Aquí el sonido es el mismo, nota por nota;
> lo que cambia es todo lo de arriba, para que se pueda enchufar y tocar un concierto con él. El
> detalle de cada versión está en el [CHANGELOG](CHANGELOG.md).

---

## Así se ve, y así suena

![UI](docs/ui_screenshot.png)

[![Demo en Youtube](https://img.youtube.com/vi/7w0uvZ-OZ7U/hqdefault.jpg)](https://www.youtube.com/watch?v=7w0uvZ-OZ7U)

**Vídeo de demostración:** [https://www.youtube.com/watch?v=7w0uvZ-OZ7U](https://www.youtube.com/watch?v=7w0uvZ-OZ7U)

---

## Compilar desde el código

> 🧑‍💻 **Para gente con experiencia.** Si sólo quieres tocar RdPiano, no hace falta: con la
> [instalación](#instalación) de arriba es suficiente.

Quien quiera tocar el código o ir por delante de la última versión publicada tiene los pasos, los
formatos y el mapa del repositorio en **[COMPILAR.md](COMPILAR.md)**, junto con la documentación
técnica del proyecto.

---

## Agradecimientos

- [MAME](https://github.com/mamedev/mame), de donde viene el código que imita el microprocesador del piano
- [InfoSecDJ](https://siliconpr0n.org/archive/doku.php?id=infosecdj:start), [Furrtek](http://furrtek.free.fr/), Jotego, Skutis y otros por la ayuda al desentrañar los chips originales
- [Sean Costello](https://valhalladsp.com/) por la [información](https://gearspace.com/board/showpost.php?p=9200326&postcount=18) sobre cómo hacer el efecto de chorus
- Dominic Mazzoni por su biblioteca de audio
- [probonopd](https://github.com/probonopd) por la automatización que compila el plugin
- [giulioz](https://github.com/giulioz) por el proyecto original [rdpiano](https://github.com/giulioz/rdpiano), del que éste es una bifurcación
