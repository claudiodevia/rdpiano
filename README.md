# RdPiano

[![CI](https://github.com/claudiodevia/rdpiano/actions/workflows/main.yml/badge.svg)](https://github.com/claudiodevia/rdpiano/actions/workflows/main.yml)
[![JUCE 9.0.1](https://img.shields.io/badge/JUCE-9.0.1-8DC63F)](https://juce.com/)
[![macOS](https://img.shields.io/badge/macOS-11%2B%20universal-000000?logo=apple&logoColor=white)](#instalación)
[![Formatos](https://img.shields.io/badge/formatos-VST3%20%7C%20AU%20%7C%20AUv3%20%7C%20LV2%20%7C%20Standalone-orange)](#instalación)
[![Licencia GPLv3](https://img.shields.io/badge/licencia-GPLv3-blue)](LICENSE)

Emulador de los pianos digitales de síntesis SA de Roland: **MKS-20**, **RD-1000** y el piano
eléctrico **Rhodes MK-80**.

No es una imitación por muestreo: RdPiano ejecuta el firmware original sobre la placa CPU-B —la
misma que compartían aquellos modelos— con la CPU y los chips de síntesis a medida reimplementados a
partir del análisis del silicio. El chorus BBD y el trémolo sí son aproximaciones, y por tanto menos
exactos que el resto de la cadena.

![UI](docs/ui_screenshot.png)

**Vídeo de demostración:** [https://www.youtube.com/watch?v=7w0uvZ-OZ7U](https://www.youtube.com/watch?v=7w0uvZ-OZ7U)

[![Demo en Youtube](https://img.youtube.com/vi/7w0uvZ-OZ7U/hqdefault.jpg)](https://www.youtube.com/watch?v=7w0uvZ-OZ7U)

## Qué cambia respecto al proyecto original

Este repositorio es una bifurcación de [rdpiano](https://github.com/giulioz/rdpiano), que emula la
máquina con mucha fidelidad pero está pensado como investigación. Aquí el objetivo es otro: **que se
pueda enchufar y tocar un concierto con él**. La emulación —el firmware y los chips de síntesis— es
la misma; lo que cambia es todo lo que la rodea.

- **Volúmenes parejos.** Los dieciséis sonidos están medidos y normalizados al mismo nivel. Antes
  unos se quedaban cortos y otros saturaban al pasar de uno a otro en mitad de un tema.
- **Sin ruidos ni clics.** El chorus y el phaser ya no sueltan de golpe el audio viejo al
  encenderlos; encender o apagar un efecto es una transición suave, cambiar de sonido lleva su
  propio silenciado, y mover el volumen ya no produce zumbido.
- **Memoria bajo control.** Todo lo caro —las tablas de onda descifradas, unos 2,75 MB— se prepara
  una sola vez al arrancar, no en cada cambio de sonido. No hay fugas: el plugin ocupa lo mismo
  después de tres horas que al abrirlo.
- **Rendimiento para el escenario.** Cambiar de sonido cuesta microsegundos en vez de rehacer el
  trabajo entero. El motor no reserva memoria ni se bloquea mientras genera audio, así que el
  programa anfitrión no pierde bloques: no hay cortes ni chasquidos al manipular el panel tocando.
- **Detalles de directo.** El dial enseña el nombre mientras se gira y aplica el sonido al soltarlo,
  sin pasar por los quince de en medio; el plugin declara su latencia al anfitrión; los ajustes
  guardados recuperan también el sonido y la afinación.
- **Empaquetado y probado.** Cinco formatos en un binario universal, compilación con una orden y
  pruebas automáticas que verifican en cada cambio que el sonido sigue siendo bit a bit el mismo.

El detalle de cada versión está en el [CHANGELOG](CHANGELOG.md).

## Instalación

Necesitas un Mac con macOS 11 o posterior. Da igual que sea Apple o Intel: el archivo es el mismo.

### 1. Elige y descarga

- **[Aplicación](https://github.com/claudiodevia/rdpiano/releases/download/latest/rdpiano_juce.app.macOS.zip)** — para tocar sin nada más. Si tienes dudas, empieza por aquí.
- **[AU](https://github.com/claudiodevia/rdpiano/releases/download/latest/rdpiano_juce.component.macOS.zip)** — si usas Logic Pro, GarageBand o MainStage.
- **[VST3](https://github.com/claudiodevia/rdpiano/releases/download/latest/rdpiano_juce.vst3.macOS.zip)** — si usas Ableton Live, Cubase, Reaper o Studio One.
- **[LV2](https://github.com/claudiodevia/rdpiano/releases/download/latest/rdpiano_juce.lv2.macOS.zip)** — si usas Ardour o Mixbus.

Haz doble clic en el archivo descargado para descomprimirlo. Aparecerá el RdPiano al lado.

### 2. Guárdalo en su carpeta

Si descargaste la **aplicación**, arrástrala a tu carpeta de Aplicaciones y ya está: pasa al paso 3.

Para los demás, la carpeta no se ve a simple vista. Para llegar a ella:

1. Abre el **Finder**.
2. En el menú de arriba, elige **Ir › Ir a la carpeta…**
3. Copia y pega la línea que te toque, y pulsa Intro:

   - AU: `/Library/Audio/Plug-Ins/Components`
   - VST3: `/Library/Audio/Plug-Ins/VST3`
   - LV2: `/Library/Audio/Plug-Ins/LV2`

4. Arrastra RdPiano dentro de esa carpeta. El Mac te pedirá tu contraseña; es normal.

Si ya tenías una versión anterior, arrástrala a la papelera antes de poner la nueva.

### 3. Dile al Mac que puede abrirlo

Este paso hay que hacerlo, o tu programa de música dirá que RdPiano está dañado. No lo está: es que
el Mac desconfía de todo lo que no viene de la App Store.

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

### 4. A tocar

Abre tu programa de música y busca **RdPiano** entre los instrumentos. Si el programa ya estaba
abierto, ciérralo y vuelve a abrirlo. Logic Pro, la primera vez, se toma unos segundos en revisarlo.

## Compilar desde el código

Sólo hace falta si quieres tocar el código o ir por delante de la última versión publicada. Requiere
macOS con Xcode instalado.

```bash
git clone https://github.com/claudiodevia/rdpiano.git && cd rdpiano
sh scripts/download-juce.sh   # descarga JUCE 9.0.1 en build/juce
sh scripts/build-osx.sh ALL   # los cinco formatos: AU, AUv3, LV2, Standalone y VST3
```

En lugar de `ALL` puedes pedir un solo formato (`AU`, `AUv3`, `LV2`, `Standalone`, `VST3`). Los
binarios quedan en `build/plugin/rdpiano_juce/rdpiano_juce_artefacts/Release/`, y son universales
(arm64 y x86_64) siempre.

Añadiendo `install` el script hace además el paso 2 por ti —copia cada formato a su carpeta,
reemplazando lo que hubiera— y pide la contraseña de administrador una sola vez:

```bash
sh scripts/build-osx.sh AU install    # compila el .component y lo instala
sh scripts/build-osx.sh ALL install   # los cinco formatos, instalados
```

Sin esa palabra no se escribe nada fuera de `build/`. Lo que compilas tú no queda en cuarentena, así
que el paso 3 no hace falta.

Los scripts no imprimen la salida de CMake ni de Xcode: por pantalla va una etiqueta por paso y, al
final, el tiempo y la ruta de los productos; lo demás va a `logs/`. Tampoco repiten trabajo ya hecho.

### El repositorio por dentro

| Directorio | Contenido |
|---|---|
| `librdpiano/` | El emulador y la cadena de audio completa, sin dependencias externas. Aquí vive la lógica real. |
| `rdpiano_juce/` | El plugin (VST3, AU, AUv3, LV2 y Standalone), construido con JUCE. |
| `roms/` | Los volcados de ROM, empotrados en el binario al compilar. |
| `re_stuff/` | Material de la ingeniería inversa —Verilog, desensamblados—, con fines educativos. No se compila. |
| `scripts/` | Los dos scripts de compilación, los mismos que ejecuta la CI. |
| `docs/` | Documentación técnica (ver [Documentación](#documentación)). |

Para trabajar en el núcleo no hace falta JUCE:

```bash
cmake -S librdpiano -B build/core && cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

Todo lo generado —JUCE incluido— vive bajo `build/`, así que `rm -rf build` deja el árbol como recién
clonado.

## Documentación

- [ARQUITECTURA.md](docs/ARQUITECTURA.md) — cómo está construido el sistema, pieza a pieza.
- [FIRMWARE.md](docs/FIRMWARE.md) — qué firmware ejecuta el emulador y por qué sólo ése.

## Agradecimientos

- Código de emulación de la CPU 6800 tomado de [MAME](https://github.com/mamedev/mame)
- [InfoSecDJ](https://siliconpr0n.org/archive/doku.php?id=infosecdj:start), [Furrtek](http://furrtek.free.fr/), Jotego, Skutis y otros por la ayuda con la ingeniería inversa del silicio
- [Sean Costello](https://valhalladsp.com/) por la [información](https://gearspace.com/board/showpost.php?p=9200326&postcount=18) sobre cómo implementar el efecto de chorus
- Dominic Mazzoni por la biblioteca de remuestreo de audio
- [probonopd](https://github.com/probonopd) por los pipelines de CI para compilar el plugin JUCE
- [giulioz](https://github.com/giulioz) por el proyecto original [rdpiano](https://github.com/giulioz/rdpiano), del que éste es una bifurcación
