#pragma once

#include <JuceHeader.h>

#include "PluginParams.h"
#include "PluginProcessor.h"
#include "lcd/Lcd.h"

/**
 * @file PluginEditor.h
 * @brief El panel del plugin: botones, dial, fader y display.
 */

/**
 * @brief El panel, guiado por tablas: un ButtonSpec por botón y un ModeSpec por modo del display.
 *
 * El constructor, resized(), buttonClicked() y updateValues() son bucles sobre
 * esas dos tablas.
 */
class RdPiano_juceAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         public juce::Button::Listener,
                                         public juce::Slider::Listener,
                                         public juce::ChangeListener
{
  public:
    RdPiano_juceAudioProcessorEditor(RdPiano_juceAudioProcessor &);
    ~RdPiano_juceAudioProcessorEditor() override;

    //==============================================================================
    void resized() override;
    void paint(juce::Graphics &g) override;
    void visibilityChanged() override;

    void buttonClicked(juce::Button *) override;
    void sliderValueChanged(juce::Slider *) override;
    void sliderDragStarted(juce::Slider *) override;
    void sliderDragEnded(juce::Slider *) override;
    void changeListenerCallback(juce::ChangeBroadcaster *) override;

    /** @brief Repinta lo que haya cambiado: display, testigos y, si se movió, la franja del fader. */
    void updateValues();

    /**
     * @brief Qué muestra el display y, con él, qué edita el dial alfa.
     *
     * Sólo uno puede estar activo a la vez.
     */
    enum DisplayMode
    {
        kModePatch = 0, // el nombre del parche; el dial elige parche
        kModeTune,
        kModeChorusRate,
        kModeChorusDepth,
        kModeTremoloRate,
        kModeTremoloDepth,
        kModePhaserRate,
        kModePhaserDepth,

        kNumDisplayModes
    };

  private:
    RdPiano_juceAudioProcessor &audioProcessor;

    /** @brief Un botón del panel: dibuja su recorte de la hoja de arte y su testigo rojo. */
    class MksButton : public juce::Button
    {
      public:
        int x = 0, y = 0, w = 0, h = 0;
        float scaleFactor = 1.0f;
        bool enabled = false;

        /// La hoja de arte, que carga el editor una sola vez. `juce::Image` es un
        /// asa contada, así que copiarla aquí no copia píxeles; lo que no puede
        /// haber es un `ImageCache::getFromMemory()` dentro del paint, porque
        /// toma un cerrojo global y de vez en cuando vuelve a decodificar el PNG.
        juce::Image art;

        MksButton() : juce::Button("") {}
        void position(int x, int y, int w, int h, float scaleFactor)
        {
            this->x = x;
            this->y = y;
            this->w = w;
            this->h = h;
            this->scaleFactor = scaleFactor;
        }
        void paintButton(juce::Graphics &g, bool isMouseOverButton, bool isButtonDown) override
        {
            auto topLeft = getBoundsInParent().getTopLeft();
            float downShift = isButtonDown ? 8 / scaleFactor : 0;
            g.drawImage(art, x / scaleFactor - topLeft.x + downShift, y / scaleFactor - topLeft.y - downShift,
                        w / scaleFactor, h / scaleFactor, x, y, w, h);

            if (enabled)
            {
                g.setColour(juce::Colours::red);
                g.fillRect(49 / scaleFactor + downShift, 36 / scaleFactor - downShift, 79 / scaleFactor,
                           28 / scaleFactor);
            }
        }
    };

    /**
     * @brief Un botón del panel: dónde responde al ratón, qué trozo de arte dibuja y qué hace al pulsarlo.
     *
     * La misma tabla decide cuándo se enciende el testigo rojo.
     */
    struct ButtonSpec
    {
        /** @brief Qué hace el botón al pulsarlo. */
        enum Action
        {
            kSelectBank,  ///< `value` es el primer parche del banco.
            kSelectPatch, ///< `value` es el botón 1..8, dentro del banco actual.
            kToggleParam, ///< Invierte `param`.
            kCycleModes,  ///< Ninguno -> `first` -> `second` -> ninguno.
        };

        /// En coordenadas del fondo (6140x1503). `bounds` es el área sensible al
        /// ratón y `art` el recorte de interactable.png: no coinciden —el arte se
        /// desborda unos píxeles— y venían así del panel original.
        int bounds[4];
        int art[4];

        Action action;
        int value;
        RdParamId param;
        DisplayMode first;
        DisplayMode second;
    };

    /** @brief Un modo del display: qué rótulo enseña y qué parámetro edita el dial. */
    struct ModeSpec
    {
        /// Los 15 caracteres de la primera fila. Los dos modos que no editan un
        /// parámetro (parche y afinación) tienen su propia línea y no lo usan.
        const char *label;
        RdParamId param;

        /// Con un modo "alternativo" activo, los botones de banco y de parche
        /// apagan su testigo. Los dos del phaser no cuentan (nunca contaron).
        bool countsAsAlternative;
    };

    static const int kNumButtons = 17;
    static const ButtonSpec buttonSpecs[kNumButtons];
    static const ModeSpec modeSpecs[kNumDisplayModes];

    /// Las hojas de arte, decodificadas una vez en el constructor y repartidas a
    /// los botones y al dial: ningún paint() vuelve a pasar por ImageCache.
    juce::Image backgroundArt;
    juce::Image interactableArt;

    MksButton buttons[kNumButtons];
    DisplayMode mode = kModePatch;

    /// Los diales de parche y de afinación sólo cambian el sonido al SOLTARLOS:
    /// mientras se arrastran enseñan el valor y nada más. Un gesto de extremo a
    /// extremo eran quince cambios de parche encadenados con sus quince cortes de
    /// audio, y afinar apaga las voces del firmware igual que cambiar de parche.
    bool dialDragging = false;
    static const int kNoDialPreview = 0x7fffffff;
    int dialPreview = kNoDialPreview;

    /// El valor que el dial enseña sin haber aplicado todavía, o el que ya está
    /// puesto. Sólo cuenta en el modo que lo dejó: el dial es uno y el valor
    /// crudo de un modo no significa nada en otro.
    int dialValueOr(DisplayMode forMode, int applied) const
    {
        return mode == forMode && dialPreview != kNoDialPreview ? dialPreview : applied;
    }

    /// Última posición del fader ya pintada: el fondo sólo se repinta cuando se
    /// mueve, y sólo la franja del fader.
    float paintedVolume = -1.0f;

    /// Los 15 pasos que enseña el display y recorre el dial. Los parámetros
    /// enteros van de 0 a 14 y los del phaser son 0..1 continuos: el paso se
    /// calcula sobre el valor normalizado y vale para los seis.
    static const int kParamSteps = 15;

    /**
     * @brief En qué paso está el parámetro que edita un modo.
     * @param m Modo del display.
     * @return El paso, 0..kParamSteps-1.
     */
    int paramStep(DisplayMode m) const;

    /**
     * @brief Escribe la línea del display para un modo que edita un parámetro.
     * @param line Fila del display, en el juego de caracteres del original.
     * @param spec Modo a dibujar.
     * @param step Paso en el que está el parámetro.
     */
    void renderParamLine(uint8_t (&line)[Lcd::kChars], const ModeSpec &spec, int step) const;

    Lcd lcd;

    /** @brief El aspecto del dial alfa: dibuja la hoja de arte girada en vez del rotatorio de JUCE. */
    class KnobLF : public juce::LookAndFeel_V3
    {
      public:
        KnobLF() = default;
        ~KnobLF() override = default;

        juce::Image dial; ///< El dial, cargado por el editor: mismo motivo que en MksButton.

        void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height, float sliderPos,
                              const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider &slider) override
        {
            auto scale = (770.55 / width);
            auto centerX = x + width * 0.5f;
            auto centerY = y + height * 0.5f;
            auto radius = juce::jmin(width * 0.5f, height * 0.5f) - (244.0f / scale) / 2 - 30.0f / scale;
            auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle) + 3.14 / 2 + 3.14;
            auto px = centerX + cos(angle) * radius;
            auto py = centerY + sin(angle) * radius;

            g.drawImage(dial, px - 244 / scale / 2, py - 244 / scale / 2, 244 / scale, 244 / scale, 0, 0, 244, 244);
        }

      private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KnobLF)
    };
    KnobLF knobLF;
    juce::Slider alphaDial;

    juce::Slider volumeSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RdPiano_juceAudioProcessorEditor)
};
