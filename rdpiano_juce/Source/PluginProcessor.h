/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include "../../librdpiano/include/rd_engine.h"
#include <JuceHeader.h>
#include <memory>

//==============================================================================
/**
 * Desde la fase 2 este archivo es sólo el plugin: parámetros, presets y el
 * puente con JUCE. La cadena de audio entera —emulador, chorus, phaser,
 * trémolo, EQ, resampling y reparto del MIDI— vive en `RdPianoEngine`, que no
 * conoce JUCE y se prueba headless (REFACTORIZACION §1).
 */
class RdPiano_juceAudioProcessor
    : public juce::AudioProcessor,
      public juce::ChangeBroadcaster,
      public juce::AudioProcessorParameter::Listener
{
public:
  //==============================================================================
  RdPiano_juceAudioProcessor();
  ~RdPiano_juceAudioProcessor() override;

  //==============================================================================
  void parameterValueChanged(int parameterIndex, float newValue) override;
  void parameterGestureChanged(int parameterIndex,
                               bool gestureIsStarting) override;

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

  juce::AudioParameterFloat *volume;
  juce::AudioParameterBool *chorusEnabled;
  juce::AudioParameterInt *chorusRate;
  juce::AudioParameterInt *chorusDepth;
  juce::AudioParameterBool *tremoloEnabled;
  juce::AudioParameterInt *tremoloRate;
  juce::AudioParameterInt *tremoloDepth;
  juce::AudioParameterBool *efxEnabled;
  juce::AudioParameterFloat *efxPhaserRate;
  juce::AudioParameterFloat *efxPhaserDepth;

  // Espejos de lo que el motor ya sabe, para el editor y para los presets.
  int currentPatch = 0;
  int masterTune = 0;

  std::unique_ptr<RdPianoEngine> engine;

  void setMasterTune(int16_t tune);

  // Serializa el hilo de UI con el de audio: `setPatch` y `setMasterTune`
  // corren el emulador (trampa 4 de CLAUDE.md).
  juce::SpinLock mcuLock;

private:
  void syncParamsToEngine();

  //==============================================================================
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RdPiano_juceAudioProcessor)
};
