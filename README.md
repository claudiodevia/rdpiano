# RdPiano [![RdPiano](https://github.com/giulioz/rdpiano/actions/workflows/main.yml/badge.svg)](https://github.com/giulioz/rdpiano/actions/workflows/main.yml)

RdPiano emula con precisión los pianos digitales de síntesis SA, como el Roland MKS-20, el RD1000 y el piano eléctrico Rhodes MK-80.
Simula la placa CPU-B reutilizada en distintos modelos, emulando la CPU y los chips a medida. La emulación de los chips a medida deriva del análisis del silicio.
También aproxima el chorus BBD y el efecto de trémolo, aunque con menos exactitud que la emulación digital.

![UI](docs/ui_screenshot.png)

**Vídeo de demostración:** [https://www.youtube.com/watch?v=7w0uvZ-OZ7U](https://www.youtube.com/watch?v=7w0uvZ-OZ7U)

[![Demo en Youtube](https://img.youtube.com/vi/7w0uvZ-OZ7U/hqdefault.jpg)](https://www.youtube.com/watch?v=7w0uvZ-OZ7U)

## Descargas del plugin

- [AU para macOS](https://github.com/giulioz/rdpiano/releases/download/latest/rdpiano_juce.component.macOS.zip)
- [VSTi para macOS](https://github.com/giulioz/rdpiano/releases/download/latest/rdpiano_juce.vst3.macOS.zip)
- [Standalone para macOS](https://github.com/giulioz/rdpiano/releases/download/latest/rdpiano_juce.app.macOS.zip)

**NOTA (macOS)**: si tienes problemas en macOS, es posible que el sistema operativo esté bloqueando el plugin por venir de un desarrollador no registrado. Puedes autorizarlo ejecutando este comando en un terminal:

```sudo xattr -rd com.apple.quarantine /Users/<tuusuario>/Library/Audio/Plug-Ins/Components/RRV10.component```

Más información en esta guía: https://www.osirisguitar.com/2020/04/01/how-to-make-unsigned-vsts-work-in-macos-catalina/

## Contenido

- **rdpiano_juce**: versión del emulador como plugin (VSTi/AU), para usar con DAWs
- **librdpiano**: versión del emulador sin dependencias, para usar como biblioteca en otro software; también compila una app standalone de prueba con SDL
- **re_stuff**: herramientas usadas durante el proceso de ingeniería inversa, sobre todo con fines educativos
- **scripts**: los dos scripts de compilación — `download-juce.sh` (descarga el árbol de JUCE) y `build-osx.sh` (configura y compila un formato de plugin, o `ALL` para los cinco); la CI ejecuta estos mismos dos

## Compilación

Todo — el núcleo, sus pruebas y el plugin — se compila desde el `CMakeLists.txt` de la raíz.
Solo macOS, requiere Xcode.

```bash
git clone <este repo> && cd rdpiano
bash scripts/download-juce.sh   # descarga JUCE 9.0.1 en build/juce
bash scripts/build-osx.sh ALL # VST3, AU, AUv3, LV2 y Standalone
                              # o un solo formato: AU, AUv3, LV2, Standalone, VST3
```

Los plugins quedan en `build/plugin/rdpiano_juce/rdpiano_juce_artefacts/Release/`.

Todo lo generado — la descarga de JUCE incluida — vive bajo `build/`: `build/juce` (la
descarga), `build/plugin` (la compilación desde la raíz) y `build/core`, `build/core-asan` (las del
núcleo suelto). `rm -rf build` es una limpieza completa; `rm -rf build/plugin build/core*` conserva el árbol de JUCE.

Para trabajar solo en el emulador (sin necesidad de JUCE):

```bash
cmake -S librdpiano -B build/core && cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

## Agradecimientos

- Código de emulación de la CPU 6800 tomado de [MAME](https://github.com/mamedev/mame)
- [InfoSecDJ](https://siliconpr0n.org/archive/doku.php?id=infosecdj:start), [Furrtek](http://furrtek.free.fr/), Jotego, Skutis y otros por la ayuda con la ingeniería inversa del silicio
- [Sean Costello](https://valhalladsp.com/) por la [información](https://gearspace.com/board/showpost.php?p=9200326&postcount=18) sobre cómo implementar el efecto de chorus
- Dominic Mazzoni por la biblioteca de remuestreo de audio
- [probonopd](https://github.com/probonopd) por los pipelines de CI para compilar el plugin JUCE
