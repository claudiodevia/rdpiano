# RdPiano [![RdPiano](https://github.com/giulioz/rdpiano/actions/workflows/main.yml/badge.svg)](https://github.com/giulioz/rdpiano/actions/workflows/main.yml)

Emulador de los pianos digitales de síntesis SA de Roland: **MKS-20**, **RD-1000** y el piano
eléctrico **Rhodes MK-80**.

No es una imitación por muestreo: RdPiano ejecuta el firmware original sobre la placa CPU-B —la
misma que compartían aquellos modelos— con la CPU y los chips de síntesis a medida reimplementados a
partir del análisis del silicio. El chorus BBD y el trémolo sí son aproximaciones, y por tanto menos
exactos que el resto de la cadena.

![UI](docs/ui_screenshot.png)

**Vídeo de demostración:** [https://www.youtube.com/watch?v=7w0uvZ-OZ7U](https://www.youtube.com/watch?v=7w0uvZ-OZ7U)

[![Demo en Youtube](https://img.youtube.com/vi/7w0uvZ-OZ7U/hqdefault.jpg)](https://www.youtube.com/watch?v=7w0uvZ-OZ7U)

## Descargas

Compilaciones de la última versión, sólo macOS (binarios universales arm64 + x86_64):

- [AU](https://github.com/giulioz/rdpiano/releases/download/latest/rdpiano_juce.component.macOS.zip)
- [VST3](https://github.com/giulioz/rdpiano/releases/download/latest/rdpiano_juce.vst3.macOS.zip)
- [Standalone](https://github.com/giulioz/rdpiano/releases/download/latest/rdpiano_juce.app.macOS.zip)

> **macOS bloquea el plugin.** Los binarios no están firmados, así que Gatekeeper los pone en
> cuarentena. Para levantarla, en un terminal:
>
> ```bash
> sudo xattr -rd com.apple.quarantine /Library/Audio/Plug-Ins/Components/rdpiano_juce.component
> ```
>
> (cambia la ruta por la del formato que hayas instalado). Guía detallada:
> [cómo usar VSTs sin firmar en macOS](https://www.osirisguitar.com/2020/04/01/how-to-make-unsigned-vsts-work-in-macos-catalina/).

## Qué hay en el repositorio

| Directorio | Contenido |
|---|---|
| `librdpiano/` | El emulador y la cadena de audio completa, sin dependencias externas. Aquí vive la lógica real; también compila una app standalone de prueba con SDL. |
| `rdpiano_juce/` | El plugin (VST3, AU, AUv3, LV2 y Standalone), construido con JUCE. |
| `roms/` | Los volcados de ROM, empotrados en el binario al compilar. |
| `re_stuff/` | Material de la ingeniería inversa —Verilog, desensamblados—, con fines educativos. No se compila. |
| `scripts/` | Los dos scripts de compilación, POSIX `sh`, que son los mismos que ejecuta la CI. |
| `docs/` | Documentación técnica (ver [Documentación](#documentación)). |

## Compilación

Sólo macOS, con Xcode instalado. Todo —el núcleo, sus pruebas y el plugin— sale del
`CMakeLists.txt` de la raíz:

```bash
git clone <este repo> && cd rdpiano
sh scripts/download-juce.sh   # descarga JUCE 9.0.1 en build/juce; no la repite si ya está
sh scripts/build-osx.sh ALL   # los cinco formatos: VST3, AU, AUv3, LV2 y Standalone
```

En lugar de `ALL` puedes pedir un solo formato (`AU`, `AUv3`, `LV2`, `Standalone`, `VST3`). Los
binarios salen siempre universales (arm64 y x86_64): macOS carga del bundle sólo la rebanada de tu
arquitectura, así que compilar una sola no hace que el plugin vaya más rápido, y el universal es
además el único que carga en un host corriendo bajo Rosetta.

Los plugins quedan en `build/plugin/rdpiano_juce/rdpiano_juce_artefacts/Release/`.

**Los scripts no imprimen la salida de CMake ni de Xcode**: por pantalla va una etiqueta por paso y,
al final, el tiempo, el número de avisos y la ruta de los productos. La salida completa se guarda en
`logs/<script>-<fecha>-<hora>.log` (directorio ignorado por git, se puede borrar entero) y, si un
paso falla, el script vuelca ahí mismo sus últimas líneas.

Ninguno de los dos repite trabajo ya hecho: la descarga se salta si `build/juce` ya es la versión que
toca (`--forzar` la rehace), y la configuración de CMake sólo se repite si su caché dejó de servir.

### Instalar en el sistema

Con `install` el script copia además lo recién
compilado a los directorios del sistema, reemplazando lo que hubiera:

```bash
sh scripts/build-osx.sh AU install    # compila el .component y lo instala
sh scripts/build-osx.sh ALL install   # los cinco formatos, instalados
sh scripts/build-osx.sh AU            # sin la palabra, sólo compila
```

| Formato | Destino |
|---|---|
| AU | `/Library/Audio/Plug-Ins/Components` |
| VST3 | `/Library/Audio/Plug-Ins/VST3` |
| LV2 | `/Library/Audio/Plug-Ins/LV2` |
| Standalone | `/Applications` |

Son directorios del sistema, así que pide la contraseña de administrador una sola vez (`sudo -v`)
antes de copiar nada. **AUv3 no tiene destino propio**: JUCE lo empotra dentro del `.app` del
Standalone, de modo que se registra al instalar la aplicación. Tampoco hay VST2 —JUCE 9 lo quitó—,
así que `/Library/Audio/Plug-Ins/VST` no se toca.

Sin `install` no se escribe nada fuera de `build/`: la instalación es opcional a propósito, porque
pisa lo que ya estuviera instalado.

### Sólo el emulador

Para trabajar en el núcleo no hace falta JUCE:

```bash
cmake -S librdpiano -B build/core && cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

Las pruebas son de dos clases: las **unitarias** (`librdpiano/test/unit/`) cubren placa, ROMs,
protocolo, bloques del chip de sonido, efectos y motor; el **e2e** (`librdpiano/test/e2e.cpp`) toca
los 16 parches sin interfaz y compara el audio con un hash bit a bit contra `test/golden.txt`. Ese
golden no se regenera para poner algo en verde: si el cambio de sonido es intencionado, primero se
escucha.

### Limpieza

Todo lo generado —la descarga de JUCE incluida— vive bajo `build/`: `build/juce` (la descarga),
`build/plugin` (la compilación desde la raíz) y `build/core`, `build/core-asan` (las del núcleo
suelto).

```bash
rm -rf build                      # limpieza completa: el árbol queda como recién clonado
rm -rf build/plugin build/core*   # borra lo compilado, conserva JUCE
```

## Documentación

- [ARQUITECTURA.md](docs/ARQUITECTURA.md) — cómo está construido el sistema, pieza a pieza.
- [FIRMWARE.md](docs/FIRMWARE.md) — qué firmware ejecuta el emulador y por qué sólo ése.
- [AUDITORIA.md](docs/AUDITORIA.md) — revisión de código y defectos encontrados.
- [FIABILIDAD-DIRECTO.md](docs/FIABILIDAD-DIRECTO.md) y
  [RENDIMIENTO-DIRECTO.md](docs/RENDIMIENTO-DIRECTO.md) — tocar en directo sin sustos: fallos,
  clics y latencia.
- [REFACTORIZACION.md](docs/REFACTORIZACION.md) y [PENDIENTE.md](docs/PENDIENTE.md) — diseño y
  trabajo abierto.

## Agradecimientos

- Código de emulación de la CPU 6800 tomado de [MAME](https://github.com/mamedev/mame)
- [InfoSecDJ](https://siliconpr0n.org/archive/doku.php?id=infosecdj:start), [Furrtek](http://furrtek.free.fr/), Jotego, Skutis y otros por la ayuda con la ingeniería inversa del silicio
- [Sean Costello](https://valhalladsp.com/) por la [información](https://gearspace.com/board/showpost.php?p=9200326&postcount=18) sobre cómo implementar el efecto de chorus
- Dominic Mazzoni por la biblioteca de remuestreo de audio
- [probonopd](https://github.com/probonopd) por los pipelines de CI para compilar el plugin JUCE
