#pragma once

#include <JuceHeader.h>

#include "PluginParams.h"
#include "PluginProcessor.h"
#include "lcd/Lcd.h"

//==============================================================================
/**
 * El panel, guiado por tablas: un `ButtonSpec` por botón y un `ModeSpec` por
 * modo del display. El constructor, `resized()`, `buttonClicked()` y
 * `updateValues()` son bucles sobre esas dos tablas.
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

    void updateValues();

    //==============================================================================
    // Qué muestra el display y, con él, qué edita el dial alfa. Sólo uno puede
    // estar activo a la vez.
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

    class MksButton : public juce::Button
    {
      public:
        int x = 0, y = 0, w = 0, h = 0;
        float scaleFactor = 1.0f;
        bool enabled = false;
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
            g.drawImage(juce::ImageCache::getFromMemory(BinaryData::interactable_png, BinaryData::interactable_pngSize),
                        x / scaleFactor - topLeft.x + downShift, y / scaleFactor - topLeft.y - downShift,
                        w / scaleFactor, h / scaleFactor, x, y, w, h);

            if (enabled)
            {
                g.setColour(juce::Colours::red);
                g.fillRect(49 / scaleFactor + downShift, 36 / scaleFactor - downShift, 79 / scaleFactor,
                           28 / scaleFactor);
            }
        }
    };

    //==============================================================================
    // Un botón del panel: dónde responde al ratón, qué trozo de arte dibuja y qué
    // hace al pulsarlo. La misma tabla decide cuándo se enciende el testigo rojo.
    struct ButtonSpec
    {
        enum Action
        {
            kSelectBank,  // `value` es el primer parche del banco
            kSelectPatch, // `value` es el botón 1..8, dentro del banco actual
            kToggleParam, // invierte `param`
            kCycleModes,  // ninguno -> `first` -> `second` -> ninguno
        };

        // En coordenadas del fondo (6140x1503). `bounds` es el área sensible al
        // ratón y `art` el recorte de interactable.png: no coinciden —el arte se
        // desborda unos píxeles— y venían así del panel original.
        int bounds[4];
        int art[4];

        Action action;
        int value;
        RdParamId param;
        DisplayMode first;
        DisplayMode second;
    };

    // Un modo del display. `label` son los 15 caracteres de la primera fila; los
    // dos modos que no editan un parámetro (parche y afinación) tienen su propia
    // línea y no lo usan.
    struct ModeSpec
    {
        const char *label;
        RdParamId param;

        // Con un modo "alternativo" activo, los botones de banco y de parche
        // apagan su testigo. Los dos del phaser no cuentan (nunca contaron).
        bool countsAsAlternative;
    };

    static const int kNumButtons = 17;
    static const ButtonSpec buttonSpecs[kNumButtons];
    static const ModeSpec modeSpecs[kNumDisplayModes];

    MksButton buttons[kNumButtons];
    DisplayMode mode = kModePatch;

    // El dial de parches sólo cambia de sonido al SOLTARLO: mientras se arrastra
    // enseña el nombre y nada más. Un gesto de extremo a extremo eran quince
    // cambios de parche encadenados, con sus quince cortes de audio.
    bool dialDragging = false;
    int dialPatchPreview = -1;

    // Última posición del fader ya pintada: el fondo sólo se repinta cuando se
    // mueve, y sólo la franja del fader.
    float paintedVolume = -1.0f;

    // Los 15 pasos que enseña el display y recorre el dial. Los parámetros
    // enteros van de 0 a 14 y los del phaser son 0..1 continuos: el paso se
    // calcula sobre el valor normalizado y vale para los seis.
    static const int kParamSteps = 15;

    int paramStep(DisplayMode m) const;
    void renderParamLine(uint8_t (&line)[Lcd::kChars], const ModeSpec &spec, int step) const;

    Lcd lcd;

    class KnobLF : public juce::LookAndFeel_V3
    {
      public:
        KnobLF() = default;
        ~KnobLF() override = default;

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

            g.drawImage(juce::ImageCache::getFromMemory(BinaryData::alphadial_png, BinaryData::alphadial_pngSize),
                        px - 244 / scale / 2, py - 244 / scale / 2, 244 / scale, 244 / scale, 0, 0, 244, 244);
        }

      private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KnobLF)
    };
    KnobLF knobLF;
    juce::Slider alphaDial;

    juce::Slider volumeSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RdPiano_juceAudioProcessorEditor)
};
