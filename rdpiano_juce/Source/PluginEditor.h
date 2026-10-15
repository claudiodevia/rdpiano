#pragma once

#include <JuceHeader.h>
#include <optional>

#include "PanelButton.h"
#include "PanelDisplay.h"
#include "PanelLayout.h"
#include "PanelLookAndFeel.h"
#include "PluginParams.h"
#include "PluginProcessor.h"
#include "lcd/Lcd.h"
#include "patches.h"

/**
 * @file PluginEditor.h
 * @brief El panel del plugin: botones, dial, fader y display.
 */

/**
 * @brief El panel, guiado por la tabla de botones (ButtonSpec) y la de modos del display (panel::modeSpecs).
 *
 * El constructor, resized(), pressButton() y updateValues() son bucles sobre esas tablas; lo que enseña el
 * display sale de PanelDisplay.h y dónde va cada pieza, de PanelLayout.h.
 */
class RdPiano_juceAudioProcessorEditor : public juce::AudioProcessorEditor,
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

    void sliderValueChanged(juce::Slider *) override;
    void sliderDragStarted(juce::Slider *) override;
    void sliderDragEnded(juce::Slider *) override;
    void changeListenerCallback(juce::ChangeBroadcaster *) override;

  private:
    RdPiano_juceAudioProcessor &audioProcessor;

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

        /// En coordenadas del fondo. `bounds` es el área sensible al ratón y `art` el recorte de
        /// interactable.png: no coinciden, el arte se desborda unos píxeles.
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> art;

        Action action;
        int value = 0;                                 ///< Con kSelectBank y kSelectPatch.
        RdParamId param = kNumRdParams;                ///< Con kToggleParam.
        panel::DisplayMode first = panel::kModePatch;  ///< Con kCycleModes.
        panel::DisplayMode second = panel::kModePatch; ///< Con kCycleModes; igual a `first` si solo hay uno.
    };

    static const int kNumButtons = 17;
    static constexpr int kPatchesPerBank = NUM_PATCHES / 2; ///< MKS-20 y MK-80: dos bancos de ocho.
    static const ButtonSpec buttonSpecs[kNumButtons];

    /// Las hojas de arte, decodificadas una vez en el constructor y repartidas a botones, dial y fader: ningún
    /// paint() pasa por ImageCache, que toma un cerrojo global y a veces vuelve a decodificar el PNG.
    juce::Image backgroundArt;
    juce::Image interactableArt;

    PanelButton buttons[kNumButtons];
    panel::DisplayMode mode = panel::kModePatch;

    /// En modo parche y afinación el dial cambia el sonido al soltarlo: mientras se arrastra solo enseña el
    /// valor en dialPreview. Cada cambio apaga las voces del firmware y el motor las devuelve con su declick.
    bool dialDragging = false;
    std::optional<int> dialPreview;

    /// El valor que el dial enseña sin haber aplicado todavía, o el que ya está
    /// puesto. Sólo cuenta en el modo que lo dejó: el dial es uno y el valor
    /// crudo de un modo no significa nada en otro.
    int dialValueOr(panel::DisplayMode forMode, int applied) const
    {
        return mode == forMode && dialPreview ? *dialPreview : applied;
    }

    /**
     * @brief En qué paso está el parámetro que edita un modo.
     * @return El paso, 0..panel::kParamSteps-1.
     */
    int paramStep(panel::DisplayMode m) const;

    /** @brief Píxeles del fondo por píxel de la ventana. */
    float artScale() const;

    /** @brief Lo que hace el botón `index` de buttonSpecs al pulsarlo. */
    void pressButton(int index);

    /**
     * @brief Pone un parámetro en un paso del display.
     *
     * Dentro del gesto que abrió el dial al empezar a arrastrar, si es de ese parámetro; si no, como gesto
     * completo.
     */
    void setParamStep(RdParamId id, int step);

    /** @brief Pone display, testigos y dial al día; cada uno se repinta solo si cambia. */
    void updateValues();

    Lcd lcd;

    AlphaDialLookAndFeel dialLookAndFeel;
    FaderLookAndFeel faderLookAndFeel;
    juce::Slider alphaDial;

    juce::Slider volumeSlider;
    juce::SliderParameterAttachment volumeAttachment;

    /// El parámetro que edita el dial en el gesto abierto al empezar a arrastrarlo, si es un modo de
    /// parámetro.
    std::optional<RdParamId> dialGesture;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RdPiano_juceAudioProcessorEditor)
};
