/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <memory>

#include "PluginParams.h"
#include "rd_engine.h"

//==============================================================================
/**
 * Desde la fase 2 este archivo es sólo el plugin: parámetros, presets y el
 * puente con JUCE. La cadena de audio entera —emulador, chorus, phaser,
 * trémolo, EQ, resampling y reparto del MIDI— vive en `RdPianoEngine`, que no
 * conoce JUCE y se prueba headless (REFACTORIZACION §1).
 *
 * Desde la fase 3 los parámetros son un `AudioProcessorValueTreeState`
 * construido desde la tabla de `PluginParams.h` (§9): declararlos, guardarlos
 * y validarlos era antes tres listas paralelas escritas a mano, con dos juegos
 * de valores por defecto que no coincidían. `rdpiano_plugin_tests` fija la ida
 * y vuelta.
 */
class RdPiano_juceAudioProcessor
    : public juce::AudioProcessor,
      public juce::ChangeBroadcaster,
      public juce::AudioProcessorValueTreeState::Listener
{
public:
  //==============================================================================
  RdPiano_juceAudioProcessor();
  ~RdPiano_juceAudioProcessor() override;

  //==============================================================================
  void parameterChanged(const juce::String &parameterID,
                        float newValue) override;

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

  // Los parámetros, y el acceso por índice de tabla que usa el editor: es lo
  // que permite que `PluginEditor` recorra descriptores en vez de repetir un
  // bloque por control (§10).
  juce::AudioProcessorValueTreeState apvts;

  juce::RangedAudioParameter &param(RdParamId id) const;
  float paramValue(RdParamId id) const;
  void setParamValue(RdParamId id, float value);

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

  // Punteros crudos a los valores de los parámetros, resueltos una vez en el
  // constructor. `render()` los lee una vez por bloque: buscar por id desde el
  // hilo de audio sería una búsqueda de cadena por bloque.
  std::atomic<float> *paramValues[kNumRdParams] = {};

  //==============================================================================
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RdPiano_juceAudioProcessor)
};
