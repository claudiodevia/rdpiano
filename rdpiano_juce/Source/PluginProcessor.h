/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include "../../librdpiano/include/mcu.h"
#include "lsp/spaced.h"
#include "lsp/phaser.h"
#include "resample/libresample.h"
#include <JuceHeader.h>
#include <memory>
#include <vector>

// Los handles del resampler son punteros de C que hay que cerrar; con esto los
// cierra el destructor (AUDITORIA §9).
struct ResampleHandleDeleter
{
  void operator()(void *handle) const
  {
    if (handle)
      resample_close(handle);
  }
};
typedef std::unique_ptr<void, ResampleHandleDeleter> ResampleHandle;

//==============================================================================
/**
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
  // juce::AudioParameterBool *efxPhaserOnOff;
  juce::AudioParameterFloat *efxPhaserRate;
  juce::AudioParameterFloat *efxPhaserDepth;

  int currentPatch = 0;
  int masterTune = 0;

  std::unique_ptr<Mcu> mcu;

  ResampleHandle resampleL;
  ResampleHandle resampleR;
  int savedDestSampleRate = 0;
  int sourceSampleRate = 0;
  int savedSourceSampleRate = 0;
  double samplesError = 0;

  std::vector<float> emu_sample_bufferL;
  std::vector<float> emu_sample_bufferR;
  std::vector<float> emu_resampled_sample_bufferL;
  std::vector<float> emu_resampled_sample_bufferR;
  size_t emu_sample_buffer_size = 0;

  unsigned long tremoloPhase = 0;

  void setMasterTune(int16_t tune);
  void mcuReset();

  SpaceD spaceD;
  Phaser phaser;

  juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                 juce::dsp::IIR::Coefficients<float>>
      midEQ;

  juce::SpinLock mcuLock;

private:
  //==============================================================================
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RdPiano_juceAudioProcessor)
};
