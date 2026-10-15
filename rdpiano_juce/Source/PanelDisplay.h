#pragma once

#include <JuceHeader.h>

#include "PluginParams.h"
#include "lcd/Lcd.h"

/**
 * @file PanelDisplay.h
 * @brief Qué enseña el display y dónde queda el dial alfa, como funciones puras del estado del plugin.
 *
 * El editor solo lee el estado, llama aquí y pinta; las pruebas comprueban el texto sin abrir una ventana.
 */

namespace panel
{
    /** @brief Qué muestra el display y, con él, qué edita el dial alfa. Solo uno está activo a la vez. */
    enum DisplayMode
    {
        kModePatch = 0, ///< El nombre del parche; el dial elige parche.
        kModeTune,      ///< La afinación en Hz; el dial afina.
        kModeChorusRate,
        kModeChorusDepth,
        kModeTremoloRate,
        kModeTremoloDepth,
        kModePhaserRate,
        kModePhaserDepth,

        kNumDisplayModes
    };

    /** @brief Un modo del display: qué rótulo enseña y qué parámetro edita el dial. */
    struct ModeSpec
    {
        /// Los 15 caracteres de la primera fila. Parche y afinación tienen su propia línea y no lo usan.
        const char *label = nullptr;

        /// El parámetro que edita el dial; kNumRdParams en parche y afinación, que no son parámetros.
        RdParamId param = kNumRdParams;

        /// Con un modo "alternativo" activo, los botones de banco y de parche apagan su testigo. Los dos del
        /// phaser no lo son.
        bool countsAsAlternative = false;
    };

    /// Los ocho modos, en el orden de DisplayMode. Las etiquetas ocupan 15 columnas exactas: la fila de arriba
    /// es etiqueta más el número del paso alineado a la derecha en dos columnas.
    inline constexpr ModeSpec modeSpecs[kNumDisplayModes] = {
        {.countsAsAlternative = false}, // kModePatch
        {.countsAsAlternative = true},  // kModeTune
        {"CHORUS RATE    ", kChorusRate, true},
        {"CHORUS DEPTH   ", kChorusDepth, true},
        {"TREMOLO RATE   ", kTremoloRate, true},
        {"TREMOLO DEPTH  ", kTremoloDepth, true},
        {"PHASER RATE    ", kEfxPhaserRate, false},
        {"PHASER DEPTH   ", kEfxPhaserDepth, false},
    };

    /// Los 15 pasos que enseña el display y recorre el dial. Los parámetros enteros van de 0 a 14 y los del
    /// phaser son 0..1 continuos: el paso se calcula sobre el valor normalizado y vale para los seis.
    inline constexpr int kParamSteps = 15;

    /** @brief Lo que enseña el panel en un instante: las dos filas del display y la posición del dial. */
    struct DisplayFrame
    {
        uint8_t chars[Lcd::kChars]; ///< En el juego de caracteres del display; 0xff es el marcador de paso.
        double dial;                ///< Posición del dial alfa, -1..1.
    };

    /**
     * @brief Compone el display de un modo.
     * @param patch Parche que enseña kModePatch, 0..NUM_PATCHES-1.
     * @param tune Afinación que enseña kModeTune, en el rango de un int16.
     * @param step Paso del parámetro que enseñan los otros seis modos, 0..kParamSteps-1.
     */
    DisplayFrame displayFrame(DisplayMode mode, int patch, int tune, int step);

    /** @brief El paso en el que cae un parámetro, dado su valor normalizado 0..1. */
    int stepFromNormalised(float normalised);

    /** @brief El valor normalizado 0..1 de un paso. */
    float normalisedFromStep(int step);

    /** @brief El paso que elige el dial en un modo de parámetro: -1..1 repartido en kParamSteps tramos iguales. */
    int stepFromDial(double dial);

    /** @brief El parche que elige el dial: -1..1 repartido en NUM_PATCHES tramos iguales. */
    int patchFromDial(double dial);

    /** @brief La afinación que elige el dial: -1..1 sobre todo el rango de un int16. */
    int tuneFromDial(double dial);
} // namespace panel
