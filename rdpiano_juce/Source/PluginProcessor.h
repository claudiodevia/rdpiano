#pragma once

#include <JuceHeader.h>
#include <memory>

#include "PluginParams.h"
#include "rd_engine.h"

//==============================================================================
/**
 * El plugin: parámetros, presets y el puente con JUCE. La cadena de audio vive
 * en `RdPianoEngine` y los parámetros son un `AudioProcessorValueTreeState`
 * construido desde la tabla de `PluginParams.h`.
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

    // El motor puede cambiar de parche sin pasar por aquí: un program change
    // MIDI lo hace. El temporizador corre en el hilo de mensajes y sólo lee un
    // atómico; el hilo de audio no toca la interfaz.
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
    void getStateInformation(juce::MemoryBlock &destData) override;
    void setStateInformation(const void *data, int sizeInBytes) override;

    //==============================================================================

    // Los parámetros, con el acceso por índice de tabla que usa el editor.
    juce::AudioProcessorValueTreeState apvts;

    juce::RangedAudioParameter &param(RdParamId id) const;
    float paramValue(RdParamId id) const;
    void setParamValue(RdParamId id, float value);

    // Espejos de lo que el motor ya sabe, para el editor y para los presets.
    int currentPatch = 0;
    int masterTune = 0;

    std::unique_ptr<RdPianoEngine> engine;

    void setMasterTune(int16_t tune);

  private:
    void syncParamsToEngine();

    // Valores de los parámetros, resueltos una vez en el constructor: buscar por
    // id desde el hilo de audio sería una búsqueda de cadena por bloque.
    std::atomic<float> *paramValues[kNumRdParams] = {};

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RdPiano_juceAudioProcessor)
};
