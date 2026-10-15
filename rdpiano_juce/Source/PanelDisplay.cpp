/**
 * @file PanelDisplay.cpp
 * @brief Texto del display y mapeos del dial alfa.
 */

#include "PanelDisplay.h"
#include "patches.h"

#include <cstdio>
#include <cstring>

namespace panel
{
    namespace
    {
        /** @brief Copia un texto en el display a partir de una columna, sin salirse de las 34. */
        void lcdPut(uint8_t (&line)[Lcd::kChars], int at, const char *text)
        {
            for (int i = 0; text[i] != '\0' && at + i < Lcd::kChars; i++)
                line[at + i] = (uint8_t)text[i];
        }

        /** @brief Modelo arriba y nombre abajo, partiendo `patchNames` ("MKS-20: Piano 1") por los dos puntos. */
        void patchLine(uint8_t (&line)[Lcd::kChars], int patch)
        {
            const char *name = patchNames[patch];
            const char *colon = std::strchr(name, ':');
            const int modelLength = colon != nullptr ? (int)(colon - name) : 0;

            for (int i = 0; i < modelLength && i < Lcd::kColumns; i++)
                line[i] = (uint8_t)name[i];

            lcdPut(line, Lcd::kColumns, colon != nullptr ? colon + 2 : name);
        }

        /** @brief "TUNING" arriba y los Hz abajo: 442 Hz de referencia y ±3,85 Hz, lo que abarca el firmware. */
        void tuneLine(uint8_t (&line)[Lcd::kChars], int tune)
        {
            const juce::String hz(442.0 + tune / 32767.0 * 3.85, 1);

            lcdPut(line, 0, "TUNING");
            lcdPut(line, Lcd::kColumns, hz.toRawUTF8());
            lcdPut(line, Lcd::kColumns + hz.length(), "Hz");
        }

        /** @brief "ETIQUETA        7" arriba y la barra de pasos abajo, con el marcador en el paso. */
        void paramLine(uint8_t (&line)[Lcd::kChars], const ModeSpec &spec, int step)
        {
            lcdPut(line, 0, spec.label);

            char number[4];
            std::snprintf(number, sizeof number, "%2d", step + 1);
            lcdPut(line, Lcd::kColumns - 2, number);

            for (int i = 0; i < kParamSteps; i++)
                line[Lcd::kColumns + 1 + i] = '_';

            // El marcador es el carácter 0xff del juego del display: por eso la línea es
            // un búfer de bytes y no una `juce::String`, que lo codificaría en UTF-8.
            line[Lcd::kColumns + 1 + step] = 0xff;
        }
    } // namespace

    DisplayFrame displayFrame(DisplayMode mode, int patch, int tune, int step)
    {
        DisplayFrame frame;
        std::memset(frame.chars, ' ', Lcd::kChars);

        if (mode == kModePatch)
        {
            if (patch >= 0 && patch < NUM_PATCHES)
                patchLine(frame.chars, patch);
            frame.dial = patch / (double)NUM_PATCHES * 2.0 - 1.0;
        }
        else if (mode == kModeTune)
        {
            tuneLine(frame.chars, tune);
            frame.dial = tune / 32767.0;
        }
        else
        {
            paramLine(frame.chars, modeSpecs[mode], step);
            frame.dial = step / (double)(kParamSteps - 1) * 2.0 - 1.0;
        }

        return frame;
    }

    int stepFromNormalised(float normalised)
    {
        return juce::jlimit(0, kParamSteps - 1, juce::roundToInt(normalised * (kParamSteps - 1)));
    }

    float normalisedFromStep(int step) { return (float)step / (kParamSteps - 1); }

    int stepFromDial(double dial) { return juce::jlimit(0, kParamSteps - 1, (int)((dial + 1) / 2 * kParamSteps)); }

    int patchFromDial(double dial) { return juce::jlimit(0, NUM_PATCHES - 1, (int)((dial + 1) / 2 * NUM_PATCHES)); }

    int tuneFromDial(double dial) { return (int)(dial * 32767.0); }
} // namespace panel
