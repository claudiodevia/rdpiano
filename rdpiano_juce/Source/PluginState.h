#pragma once

#include <JuceHeader.h>

/**
 * @file PluginState.h
 * @brief Lo que el preset lleva además de los parámetros: parche y afinación, como propiedades del árbol.
 */

/**
 * @brief Parche y afinación tal como viajan en el preset.
 *
 * No son parámetros (trampa 10): el parche es el programa del anfitrión y la afinación no es un mando de mezcla.
 */
struct RdPresetExtras
{
    int patch = 0;    ///< 0..NUM_PATCHES-1.
    int16_t tune = 0; ///< Desviación con signo, la de RdPianoEngine::masterTune().
};

/**
 * @brief Añade parche y afinación al árbol de parámetros que se guarda.
 *
 * Los atributos se llaman `currentPatch` y `masterTune` y no se renombran: los leen las sesiones guardadas.
 *
 * @param params Copia del árbol del APVTS; su tipo (`RdPiano`) es la etiqueta raíz del preset.
 * @return El árbol con las dos propiedades.
 */
juce::ValueTree writeRdState(juce::ValueTree params, const RdPresetExtras &extras);

/**
 * @brief Lee parche y afinación de un preset, saneados.
 * @return Lo que falte vale 0; un parche fuera de rango, 0; una afinación fuera del int16, recortada al extremo.
 */
RdPresetExtras readRdState(const juce::ValueTree &state);
