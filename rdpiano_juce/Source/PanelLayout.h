#pragma once

#include <JuceHeader.h>

/**
 * @file PanelLayout.h
 * @brief Dónde está cada pieza del panel, en píxeles de la hoja de fondo.
 *
 * Todo el arte (background.png, interactable.png, alphadial.png) está dibujado a la escala del fondo,
 * 6140×1503; la ventana lo enseña a 1/kUiScale. Las áreas van en esas coordenadas y toUi() las pasa a la
 * ventana.
 */

namespace panel
{
    inline constexpr int kArtWidth = 6140;  ///< Ancho de background.png.
    inline constexpr int kArtHeight = 1503; ///< Alto de background.png.
    inline constexpr int kUiScale = 5;      ///< Píxeles del fondo por píxel de la ventana.
    inline constexpr int kUiWidth = kArtWidth / kUiScale;
    inline constexpr int kUiHeight = kArtHeight / kUiScale;

    /// El display: el hueco del fondo menos un margen de 60 px a cada lado y 40 arriba y abajo.
    inline const juce::Rectangle<int> kLcdArea{2044, 322, 1278, 233};

    /// El dial se midió sobre un SVG del panel de 311,92 unidades de ancho.
    inline constexpr double kSvgToArt = kArtWidth / 311.92;

    /// El dial alfa: un cuadrado de 770,55 px, la medida de alphadial.png girando.
    inline const juce::Rectangle<double> kDialArea{204.564 * kSvgToArt, 24.061 * kSvgToArt, 39.145 * kSvgToArt,
                                                   39.145 * kSvgToArt};

    inline const juce::Rectangle<int> kFaderTrack{1188, 660, 100, 656};   ///< Recorrido del fader de volumen.
    inline const juce::Rectangle<int> kFaderCapArt{1188, 1179, 100, 131}; ///< El tirador, en interactable.png.

    /**
     * @brief Pasa un área del fondo a la ventana.
     *
     * Trunca cada coordenada por separado, como `setBounds()` con valores `float`: así se ha colocado siempre
     * el panel, y redondear lo movería un píxel.
     *
     * @param scale Píxeles del fondo por píxel de la ventana.
     */
    template <typename T> juce::Rectangle<int> toUi(const juce::Rectangle<T> &area, float scale)
    {
        return {(int)(area.getX() / scale), (int)(area.getY() / scale), (int)(area.getWidth() / scale),
                (int)(area.getHeight() / scale)};
    }
} // namespace panel
