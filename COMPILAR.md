# Compilar RdPiano desde el código

> 🧑‍💻 **Esta página es para gente con experiencia.** Si sólo quieres tocar, no necesitas nada de
> esto: descarga el plugin ya compilado siguiendo la [instalación del README](README.md#instalación).

Requiere macOS con Xcode instalado.

```bash
git clone https://github.com/claudiodevia/rdpiano.git && cd rdpiano
sh scripts/download-juce.sh   # descarga JUCE 9.0.1 en build/juce
sh scripts/build-osx.sh ALL   # los cinco formatos: AU, AUv3, LV2, Standalone y VST3
```

En lugar de `ALL` puedes pedir un solo formato (`AU`, `AUv3`, `LV2`, `Standalone`, `VST3`). Los
binarios quedan en `build/plugin/rdpiano_juce/rdpiano_juce_artefacts/Release/`, y son universales
(arm64 y x86_64) siempre.

Añadiendo `install` el script hace además, por ti, el
[paso 2 de la instalación](README.md#paso-2--guárdalo-en-su-carpeta) —copia cada formato a su
carpeta, reemplazando lo que hubiera— y pide la contraseña de administrador una sola vez:

```bash
sh scripts/build-osx.sh AU install    # compila el .component y lo instala
sh scripts/build-osx.sh ALL install   # los cinco formatos, instalados
```

Sin esa palabra no se escribe nada fuera de `build/`. Lo que compilas tú no queda en cuarentena, así
que el [paso 3](README.md#paso-3--dile-al-mac-que-puede-abrirlo) no hace falta.

Los scripts no imprimen la salida de CMake ni de Xcode: por pantalla va una etiqueta por paso y, al
final, el tiempo y la ruta de los productos; lo demás va a `logs/`. Tampoco repiten trabajo ya hecho.

## El repositorio por dentro

| Directorio | Contenido |
|---|---|
| `librdpiano/` | El emulador y la cadena de audio completa, sin dependencias externas. Aquí vive la lógica real. |
| `rdpiano_juce/` | El plugin (VST3, AU, AUv3, LV2 y Standalone), construido con JUCE. |
| `roms/` | Los volcados de ROM, empotrados en el binario al compilar. |
| `re_stuff/` | Material de la ingeniería inversa —Verilog, desensamblados—, con fines educativos. No se compila. |
| `scripts/` | Los dos scripts de compilación, los mismos que ejecuta la CI. |
| `docs/` | Documentación técnica (ver abajo). |

Para trabajar en el núcleo no hace falta JUCE:

```bash
cmake -S librdpiano -B build/core && cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

Todo lo generado —JUCE incluido— vive bajo `build/`, así que `rm -rf build` deja el árbol como recién
clonado.

## Documentación técnica

Los dieciséis sonidos salen de las ROM del **MKS-20** y del **MK-80**, pero el programa que las
interpreta —el que corre sobre la CPU emulada— es el original del **RD-1000** (`roms/RD200_B.bin`),
y sólo ése: el diálogo con el bus depende de direcciones fijas de ese firmware.

Para quien quiera mirar por dentro:

- [ARQUITECTURA.md](docs/ARQUITECTURA.md) — cómo está construido el sistema, pieza a pieza.
- [FIRMWARE.md](docs/FIRMWARE.md) — qué programa interno del piano ejecuta y por qué sólo ése.
