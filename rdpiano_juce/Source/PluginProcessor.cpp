/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../../librdpiano/include/patches.h"

// Las ROMs empotradas, en el orden de RomSetId. Los nombres canónicos están en
// patches.h y test_patches.cpp comprueba que el .jucer empotra exactamente
// esos ficheros.
static const RdRomSet romSets[ROMSET_COUNT] = {
    // ROMSET_MKS20_A
    {(const uint8_t *)BinaryData::mks20_15179738_BIN,
     (const uint8_t *)BinaryData::mks20_15179737_BIN,
     (const uint8_t *)BinaryData::mks20_15179736_BIN,
     (const uint8_t *)BinaryData::mks20_15179757_BIN},
    // ROMSET_MKS20_B
    {(const uint8_t *)BinaryData::mks20_15179741_BIN,
     (const uint8_t *)BinaryData::mks20_15179740_BIN,
     (const uint8_t *)BinaryData::mks20_15179739_BIN,
     (const uint8_t *)BinaryData::mks20_15179757_BIN},
    // ROMSET_MK80
    {(const uint8_t *)BinaryData::MK80_IC5_bin,
     (const uint8_t *)BinaryData::MK80_IC6_bin,
     (const uint8_t *)BinaryData::MK80_IC7_bin,
     (const uint8_t *)BinaryData::MK80_IC18_bin},
};

//==============================================================================
RdPiano_juceAudioProcessor::RdPiano_juceAudioProcessor()
    : AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
  engine = std::make_unique<RdPianoEngine>(
      romSets, (const uint8_t *)BinaryData::RD200_B_bin);

  // DAW parameters
  addParameter(volume = new juce::AudioParameterFloat(
                   juce::ParameterID{"volume", 1}, // parameterID
                   "Volume",                       // parameter name
                   0.0f,                           // minimum value
                   1.0f,                           // maximum value
                   1.0));                          // default value
  addParameter(chorusEnabled = new juce::AudioParameterBool(
                   juce::ParameterID{"chorusEnabled", 1}, // parameterID
                   "Chorus Enabled",                      // parameter name
                   true));                                // default value
  addParameter(chorusRate = new juce::AudioParameterInt(
                   juce::ParameterID{"chorusRate", 1}, // parameterID
                   "Chorus Rate",                      // parameter name
                   0,                                  // minimum value
                   14,                                 // maximum value
                   5));                                // default value
  addParameter(chorusDepth = new juce::AudioParameterInt(
                   juce::ParameterID{"chorusDepth", 1}, // parameterID
                   "Chorus Depth",                      // parameter name
                   0,                                   // minimum value
                   14,                                  // maximum value
                   14));                                // default value
  addParameter(tremoloEnabled = new juce::AudioParameterBool(
                   juce::ParameterID{"tremoloEnabled", 1}, // parameterID
                   "Tremolo Enabled",                      // parameter name
                   false));                                // default value
  addParameter(tremoloRate = new juce::AudioParameterInt(
                   juce::ParameterID{"tremoloRate", 1}, // parameterID
                   "Tremolo Rate",                      // parameter name
                   0,                                   // minimum value
                   14,                                  // maximum value
                   6));                                 // default value
  addParameter(tremoloDepth = new juce::AudioParameterInt(
                   juce::ParameterID{"tremoloDepth", 1}, // parameterID
                   "Tremolo Depth",                      // parameter name
                   0,                                    // minimum value
                   14,                                   // maximum value
                   6));                                  // default value
  addParameter(efxEnabled = new juce::AudioParameterBool(
                   juce::ParameterID{"efxEnabled", 1}, "EFX Enabled", false));
  // addParameter(
  //     efxPhaserOnOff = new juce::AudioParameterBool(
  //         juce::ParameterID{"efxPhaserOnOff", 1}, "Phaser Enabled", false));
  addParameter(
      efxPhaserRate = new juce::AudioParameterFloat(
          juce::ParameterID{"efxPhaserRate", 1}, "Phaser Rate", 0, 1, 0.4));
  addParameter(
      efxPhaserDepth = new juce::AudioParameterFloat(
          juce::ParameterID{"efxPhaserDepth", 1}, "Phaser Depth", 0, 1, 0.8));

  volume->addListener(this);
  chorusEnabled->addListener(this);
  chorusRate->addListener(this);
  chorusDepth->addListener(this);
  tremoloEnabled->addListener(this);
  tremoloRate->addListener(this);
  tremoloDepth->addListener(this);
  efxEnabled->addListener(this);
  // efxPhaserOnOff->addListener(this);
  efxPhaserRate->addListener(this);
  efxPhaserDepth->addListener(this);
}

RdPiano_juceAudioProcessor::~RdPiano_juceAudioProcessor() {}

//==============================================================================
void RdPiano_juceAudioProcessor::parameterValueChanged(int parameterIndex,
                                                       float newValue)
{
  sendChangeMessage();
}

void RdPiano_juceAudioProcessor::parameterGestureChanged(
    int parameterIndex, bool gestureIsStarting) {}

//==============================================================================
const juce::String RdPiano_juceAudioProcessor::getName() const
{
  return JucePlugin_Name;
}

bool RdPiano_juceAudioProcessor::acceptsMidi() const { return true; }

bool RdPiano_juceAudioProcessor::producesMidi() const { return false; }

bool RdPiano_juceAudioProcessor::isMidiEffect() const { return false; }

double RdPiano_juceAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int RdPiano_juceAudioProcessor::getNumPrograms() { return NUM_PATCHES; }

int RdPiano_juceAudioProcessor::getCurrentProgram() { return currentPatch; }

void RdPiano_juceAudioProcessor::setCurrentProgram(int index)
{
  if (index < 0 || index >= getNumPrograms())
    return;

  // El motor decide si hay que recargar el juego de ROM o basta con remapear
  // una página (REFACTORIZACION §6). Lo que sigue siendo del plugin es
  // serializarlo con el hilo de audio: setPatch() corre el emulador.
  mcuLock.enter();
  engine->setPatch(index);
  mcuLock.exit();

  currentPatch = index;
  sendChangeMessage();
}

const juce::String RdPiano_juceAudioProcessor::getProgramName(int index)
{
  if (index >= getNumPrograms())
    return juce::String();

  return juce::String(patchNames[index]);
}

void RdPiano_juceAudioProcessor::changeProgramName(
    int index, const juce::String &newName) {}

void RdPiano_juceAudioProcessor::setMasterTune(int16_t tune)
{
  masterTune = tune;

  // El switcharoo y la codificación viven en el núcleo (REFACTORIZACION §3).
  // Lo que sigue siendo del plugin es serializarlo con el hilo de audio: esto
  // corre el emulador desde el hilo de UI (trampa 4 de CLAUDE.md).
  mcuLock.enter();
  engine->setMasterTune(tune);
  mcuLock.exit();

  sendChangeMessage();
}

// Vuelca los parámetros de JUCE al POD que lee render(). Se llama una vez por
// bloque, antes de renderizar: el motor no conoce juce::AudioParameter.
void RdPiano_juceAudioProcessor::syncParamsToEngine()
{
  RdEngineParams &p = engine->params;
  p.volume = *volume;
  p.chorusEnabled = *chorusEnabled;
  p.chorusRate = *chorusRate;
  p.chorusDepth = *chorusDepth;
  p.tremoloEnabled = *tremoloEnabled;
  p.tremoloRate = *tremoloRate;
  p.tremoloDepth = *tremoloDepth;
  p.efxEnabled = *efxEnabled;
  p.efxPhaserRate = *efxPhaserRate;
  p.efxPhaserDepth = *efxPhaserDepth;
}

//==============================================================================
void RdPiano_juceAudioProcessor::prepareToPlay(double sampleRate,
                                               int samplesPerBlock)
{
  // Todo lo que render() va a necesitar —búferes, resamplers, coeficientes del
  // EQ— se reserva aquí (AUDITORIA §§1, 2, 11, 12), y arranca el firmware.
  //
  // No hace falta volver a seleccionar el parche: lo que boot() reinicia es el
  // firmware, no el mapeo de la página de params que hizo selectPatch(), así
  // que el parche sobrevive al arranque. Volver a aplicarlo cambiaría el audio
  // (medido: hash distinto ya en el parche 0, por el trabajo de firmware que
  // añaden el 0x31/0x30 de más).
  mcuLock.enter();
  engine->prepare(sampleRate, samplesPerBlock);
  mcuLock.exit();
}

void RdPiano_juceAudioProcessor::releaseResources()
{
  mcuLock.enter();
  engine->release();
  mcuLock.exit();
}

bool RdPiano_juceAudioProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const
{
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;

  return true;
}

void RdPiano_juceAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                              juce::MidiBuffer &midiMessages)
{
  juce::ScopedNoDenormals noDenormals;

  const int numSamples = buffer.getNumSamples();
  for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels();
       ++ch)
    buffer.clear(ch, 0, numSamples);

  if (buffer.getNumChannels() < 2 || numSamples <= 0)
    return;

  syncParamsToEngine();

  // Un solo recorrido de midiMessages, que ya viene ordenado por
  // samplePosition. `numBytes` se mira antes de leer data[1] y data[2]: un
  // SysEx corto vive en el montículo y leer de más sí sale del búfer
  // (AUDITORIA §5).
  for (const auto metadata : midiMessages)
  {
    const juce::uint8 *raw = metadata.data;
    const int n = metadata.numBytes;
    if (n < 1)
      continue;

    engine->pushMidi(metadata.samplePosition, raw[0],
                     n > 1 ? raw[1] : (juce::uint8)0,
                     n > 2 ? raw[2] : (juce::uint8)0);
  }

  juce::SpinLock::ScopedLockType lock(mcuLock);
  engine->render(buffer.getWritePointer(0), buffer.getWritePointer(1),
                 numSamples);
}

//==============================================================================
bool RdPiano_juceAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor *RdPiano_juceAudioProcessor::createEditor()
{
  return new RdPiano_juceAudioProcessorEditor(*this);
}

//==============================================================================
void RdPiano_juceAudioProcessor::getStateInformation(
    juce::MemoryBlock &destData)
{
  std::unique_ptr<juce::XmlElement> xml(new juce::XmlElement("RdPiano"));
  xml->setAttribute("masterTune", masterTune);
  xml->setAttribute("currentPatch", currentPatch);
  xml->setAttribute("volume", (double)*volume);
  xml->setAttribute("chorusEnabled", (bool)*chorusEnabled);
  xml->setAttribute("chorusRate", (int)*chorusRate);
  xml->setAttribute("chorusDepth", (int)*chorusDepth);
  xml->setAttribute("tremoloEnabled", (bool)*tremoloEnabled);
  xml->setAttribute("tremoloRate", (int)*tremoloRate);
  xml->setAttribute("tremoloDepth", (int)*tremoloDepth);
  xml->setAttribute("efxEnabled", (bool)*efxEnabled);
  xml->setAttribute("efxPhaserRate", (float)*efxPhaserRate);
  xml->setAttribute("efxPhaserDepth", (float)*efxPhaserDepth);
  copyXmlToBinary(*xml, destData);
}

void RdPiano_juceAudioProcessor::setStateInformation(const void *data,
                                                     int sizeInBytes)
{
  std::unique_ptr<juce::XmlElement> xmlState(
      getXmlFromBinary(data, sizeInBytes));

  if (xmlState.get() != nullptr)
  {
    if (xmlState->hasTagName("RdPiano"))
    {
      masterTune = xmlState->getIntAttribute("masterTune", 0);
      currentPatch = xmlState->getIntAttribute("currentPatch", 0);
      *volume = (float)xmlState->getDoubleAttribute("volume", 1.0);
      *chorusEnabled = (bool)xmlState->getBoolAttribute("chorusEnabled", true);
      *chorusRate = (int)xmlState->getIntAttribute("chorusRate", 1);
      *chorusDepth = (int)xmlState->getIntAttribute("chorusDepth", 3);
      *tremoloEnabled =
          (bool)xmlState->getBoolAttribute("tremoloEnabled", false);
      *tremoloRate = (int)xmlState->getIntAttribute("tremoloRate", 6);
      *tremoloDepth = (int)xmlState->getIntAttribute("tremoloDepth", 6);
      *efxEnabled = (bool)xmlState->getBoolAttribute("efxEnabled", false);
      *efxPhaserRate =
          (float)xmlState->getDoubleAttribute("efxPhaserRate", 0.4);
      *efxPhaserDepth =
          (float)xmlState->getDoubleAttribute("efxPhaserDepth", 0.8);
    }
  }

  if (currentPatch < 0 || currentPatch >= getNumPrograms())
    currentPatch = 0;
  if (*volume < 0 || *volume > 1)
    *volume = 1;
  if (*chorusRate > 14)
    *chorusRate = 1;
  if (*chorusDepth > 14)
    *chorusDepth = 3;
  if (*tremoloRate > 14)
    *tremoloRate = 6;
  if (*tremoloDepth > 14)
    *tremoloDepth = 6;
  if (*efxPhaserRate > 1)
    *efxPhaserRate = 0.4;
  if (*efxPhaserDepth > 1)
    *efxPhaserDepth = 0.8;

  setMasterTune(masterTune);

  mcuLock.enter();
  engine->setPatch(currentPatch);
  mcuLock.exit();
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
  return new RdPiano_juceAudioProcessor();
}
