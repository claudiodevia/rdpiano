#pragma once

#include <JuceHeader.h>

/**
 * @file PanelButton.h
 * @brief Un botón del panel: su recorte de la hoja de arte y su testigo rojo.
 */

/** @brief Un botón del panel, dibujado con su recorte de interactable.png; pulsado se hunde y el testigo se mueve con él. */
class PanelButton : public juce::Button
{
  public:
    PanelButton() : juce::Button({}) {}

    /**
     * @brief La hoja de arte, que el editor decodifica una vez.
     *
     * `juce::Image` es un asa contada: copiarla no copia píxeles.
     */
    void setSheet(const juce::Image &sheet) { art = sheet; }

    /**
     * @brief Qué recorte de la hoja dibuja.
     * @param area Recorte, en píxeles del fondo; puede desbordar los límites del botón.
     * @param scale Píxeles del fondo por píxel de la ventana.
     */
    void setArt(juce::Rectangle<int> area, float scale);

    /** @brief Enciende o apaga el testigo; repinta solo si cambia. */
    void setLit(bool shouldBeLit);

    bool isLit() const { return lit; }

    void paintButton(juce::Graphics &g, bool isMouseOverButton, bool isButtonDown) override;

  private:
    static constexpr int kPressShift = 8; ///< Cuánto se hunde al pulsarlo, en píxeles del fondo.

    /// El testigo, en píxeles del fondo y relativo a la esquina del botón.
    inline static const juce::Rectangle<int> kLampArea{49, 36, 79, 28};

    juce::Image art;
    juce::Rectangle<int> artArea;
    float scale = 1.0f;
    bool lit = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelButton)
};
