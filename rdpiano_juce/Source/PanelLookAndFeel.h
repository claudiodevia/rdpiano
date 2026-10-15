#pragma once

#include <JuceHeader.h>

/**
 * @file PanelLookAndFeel.h
 * @brief El aspecto de los dos controles continuos del panel: el dial alfa y el fader de volumen.
 *
 * Los dos dibujan arte de las hojas del panel en vez de los controles de JUCE, y sacan la escala del ancho del
 * propio control.
 */

/** @brief El dial alfa: el sprite de alphadial.png girando sobre el círculo del dial del fondo. */
class AlphaDialLookAndFeel : public juce::LookAndFeel_V3
{
  public:
    /** @brief El sprite, que el editor decodifica una vez (asa contada: no copia píxeles). */
    void setSprite(const juce::Image &image) { sprite = image; }

    void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height, float sliderPos,
                          const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider &slider) override;

  private:
    static constexpr int kSpriteSize = 244; ///< Lado de alphadial.png, en píxeles del fondo.
    static constexpr int kSpriteInset = 30; ///< Lo que el sprite se separa del borde del dial.

    juce::Image sprite;
};

/**
 * @brief El fader de volumen: el tirador de interactable.png sobre el recorrido del fondo.
 *
 * Hereda de LookAndFeel_V4, el de JUCE por defecto, para conservar el radio del tirador y con él la relación
 * entre ratón y valor.
 */
class FaderLookAndFeel : public juce::LookAndFeel_V4
{
  public:
    /** @brief La hoja de arte, que el editor decodifica una vez. */
    void setSheet(const juce::Image &image) { sheet = image; }

    void drawLinearSlider(juce::Graphics &g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                          float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider &slider) override;

  private:
    juce::Image sheet;
};
