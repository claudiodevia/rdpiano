#pragma once

#include <JuceHeader.h>

#include "PluginParams.h"
#include "rd_engine.h"

/**
 * @file PluginProcessor.h
 * @brief El plugin: parámetros, presets y el puente con JUCE.
 */

/**
 * @brief El procesador: parámetros, presets y el puente con JUCE.
 *
 * La cadena de audio vive en RdPianoEngine y los parámetros son un
 * `AudioProcessorValueTreeState` construido desde la tabla de PluginParams.h.
 */
class RdPiano_juceAudioProcessor : public juce::AudioProcessor,
                                   public juce::ChangeBroadcaster,
                                   public juce::Timer,
                                   public juce::AudioProcessorValueTreeState::Listener
{
  public:
    //==============================================================================
    RdPiano_juceAudioProcessor();
    ~RdPiano_juceAudioProcessor() override;

    //==============================================================================
    void parameterChanged(const juce::String &parameterID, float newValue) override;

    /**
     * @brief Avisa al anfitrión y al panel, desde el hilo de mensajes, de lo que cambió fuera de él.
     *
     * Un program change MIDI cambia el parche dentro del motor y la automatización mueve parámetros en el hilo
     * de audio: ninguno de los dos puede tocar la interfaz, así que aquí se recogen a 10 Hz.
     */
    void timerCallback() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout &layouts) const override;

    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

    //==============================================================================
    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String &newName) override;

    //==============================================================================
    /**
     * @brief Guarda el preset: el árbol del APVTS con parche y afinación como propiedades (PluginState.h).
     *
     * La etiqueta raíz `RdPiano` no se renombra: las sesiones guardadas se reconocen por ella.
     */
    void getStateInformation(juce::MemoryBlock &destData) override;

    /** @brief Carga un preset: lo que no traiga vuelve a fábrica; un bloque que no sea un preset se ignora. */
    void setStateInformation(const void *data, int sizeInBytes) override;

    //==============================================================================

    juce::AudioProcessorValueTreeState apvts; ///< Los parámetros, tal como los serializa el preset.

    /** @brief Un parámetro por su índice de tabla, que es como lo pide el editor. */
    juce::RangedAudioParameter &param(RdParamId id) const;

    /**
     * @brief Valor actual de un parámetro.
     * @return El valor sin normalizar.
     */
    float paramValue(RdParamId id) const;

    /**
     * @brief Cambia un parámetro como un gesto completo del usuario: begin, valor y end.
     *
     * Sin el gesto, la automatización en modo touch o latch no lo graba. Si el valor no cambia, no avisa.
     *
     * @param value Valor sin normalizar.
     */
    void setParamValue(RdParamId id, float value);

    /** @brief La afinación pedida, aunque el motor aún no la haya atendido. */
    int16_t masterTune() const { return rdEngine.masterTune(); }

    /** @brief El motor, de solo lectura: para que las pruebas confirmen que un cambio llegó. */
    const RdPianoEngine &engine() const { return rdEngine; }

    /**
     * @brief Pide al motor una afinación; la atiende render() con su declick.
     * @param tune Desviación con signo: ±32767 son ±3,85 Hz sobre 442 Hz.
     */
    void setMasterTune(int16_t tune);

  private:
    RdPianoEngine rdEngine;

    /**
     * @brief Vuelca los parámetros al POD que lee render(), una vez por bloque.
     *
     * Lee los `std::atomic<float>` de paramValues y no el APVTS: buscar por id es una búsqueda de cadena en el
     * hilo de audio.
     */
    void syncParamsToEngine();

    /// Valores de los parámetros, resueltos una vez en el constructor.
    std::atomic<float> *paramValues[kNumRdParams] = {};

    /// Algún parámetro cambió desde el último aviso al panel: parameterChanged() puede correr en el hilo de
    /// audio, así que marca y timerCallback() avisa.
    std::atomic<bool> paramsDirty{false};

    int lastNotifiedPatch = 0; ///< El parche del último aviso al anfitrión y al panel; solo lo toca el temporizador.

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RdPiano_juceAudioProcessor)
};
