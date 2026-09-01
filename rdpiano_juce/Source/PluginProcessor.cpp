#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "patches.h"

// Las ROMs empotradas, en el orden de RomSetId. Los nombres canónicos están en
// patches.h y test_patches.cpp comprueba que rdpiano_juce/CMakeLists.txt empotra
// exactamente esos ficheros.
static const RdRomSet romSets[ROMSET_COUNT] = {
    // ROMSET_MKS20_A
    {(const uint8_t *)BinaryData::mks20_15179738_BIN, (const uint8_t *)BinaryData::mks20_15179737_BIN,
     (const uint8_t *)BinaryData::mks20_15179736_BIN, (const uint8_t *)BinaryData::mks20_15179757_BIN},
    // ROMSET_MKS20_B
    {(const uint8_t *)BinaryData::mks20_15179741_BIN, (const uint8_t *)BinaryData::mks20_15179740_BIN,
     (const uint8_t *)BinaryData::mks20_15179739_BIN, (const uint8_t *)BinaryData::mks20_15179757_BIN},
    // ROMSET_MK80
    {(const uint8_t *)BinaryData::MK80_IC5_bin, (const uint8_t *)BinaryData::MK80_IC6_bin,
     (const uint8_t *)BinaryData::MK80_IC7_bin, (const uint8_t *)BinaryData::MK80_IC18_bin},
};

//==============================================================================
RdPiano_juceAudioProcessor::RdPiano_juceAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "RdPiano", createRdParameterLayout())
{
    engine = std::make_unique<RdPianoEngine>(romSets, (const uint8_t *)BinaryData::RD200_B_bin);

    // Los diez parámetros salen de la tabla de PluginParams.h: aquí se cachean
    // sus valores crudos y se escucha su cambio para repintar el panel.
    for (int i = 0; i < kNumRdParams; i++)
    {
        paramValues[i] = apvts.getRawParameterValue(rdParamSpecs[i].id);
        jassert(paramValues[i] != nullptr);
        apvts.addParameterListener(rdParamSpecs[i].id, this);
    }
}

RdPiano_juceAudioProcessor::~RdPiano_juceAudioProcessor()
{
    for (int i = 0; i < kNumRdParams; i++)
        apvts.removeParameterListener(rdParamSpecs[i].id, this);
}

//==============================================================================
void RdPiano_juceAudioProcessor::parameterChanged(const juce::String &, float) { sendChangeMessage(); }

//==============================================================================
// Acceso por índice de tabla: es lo que deja al editor recorrer descriptores en
// vez de repetir un bloque por control.
juce::RangedAudioParameter &RdPiano_juceAudioProcessor::param(RdParamId id) const
{
    juce::RangedAudioParameter *p = apvts.getParameter(rdParamSpecs[id].id);
    jassert(p != nullptr);
    return *p;
}

float RdPiano_juceAudioProcessor::paramValue(RdParamId id) const { return paramValues[id]->load(); }

void RdPiano_juceAudioProcessor::setParamValue(RdParamId id, float value)
{
    juce::RangedAudioParameter &p = param(id);
    p.setValueNotifyingHost(p.convertTo0to1(value));
}

//==============================================================================
const juce::String RdPiano_juceAudioProcessor::getName() const { return JucePlugin_Name; }

bool RdPiano_juceAudioProcessor::acceptsMidi() const { return true; }

bool RdPiano_juceAudioProcessor::producesMidi() const { return false; }

bool RdPiano_juceAudioProcessor::isMidiEffect() const { return false; }

double RdPiano_juceAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int RdPiano_juceAudioProcessor::getNumPrograms() { return NUM_PATCHES; }

int RdPiano_juceAudioProcessor::getCurrentProgram() { return currentPatch; }

void RdPiano_juceAudioProcessor::setCurrentProgram(int index)
{
    // El dial del panel dispara esto en cada evento de arrastre y la mayoría
    // repiten el parche que ya está puesto: sin la salida temprana, cada píxel
    // pagaba una recarga.
    if (index < 0 || index >= getNumPrograms() || index == currentPatch)
        return;

    // Los ~2,9 ms de descifrar las ROM de onda del juego nuevo van FUERA del
    // cerrojo: escriben en el juego de reserva del chip, que render() no lee.
    // Bajo cerrojo sólo queda publicarlo y remapear la página (~0,03 ms), que
    // es lo que el hilo de audio puede llegar a esperar.
    engine->prepareRomSetFor(index);

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

void RdPiano_juceAudioProcessor::changeProgramName(int index, const juce::String &newName) {}

void RdPiano_juceAudioProcessor::setMasterTune(int16_t tune)
{
    // Igual que el parche: el dial repite valores mientras se arrastra y cada
    // repetición correría el emulador ~200 muestras con el cerrojo tomado.
    if (tune == masterTune)
        return;

    masterTune = tune;

    // El switcharoo y la codificación viven en el núcleo. Del plugin es
    // serializarlo: esto corre el emulador desde el hilo de UI (trampa 4 de
    // CLAUDE.md).
    mcuLock.enter();
    engine->setMasterTune(tune);
    mcuLock.exit();

    sendChangeMessage();
}

// Vuelca los parámetros de JUCE al POD que lee render(), una vez por bloque. Se
// lee de los `std::atomic<float>` cacheados en el constructor y no del APVTS:
// buscar por id es una búsqueda de cadena en el hilo de audio.
void RdPiano_juceAudioProcessor::syncParamsToEngine()
{
    RdEngineParams &p = engine->params;
    p.volume = paramValue(kVolume);
    p.chorusEnabled = paramValue(kChorusEnabled) >= 0.5f;
    p.chorusRate = (int)paramValue(kChorusRate);
    p.chorusDepth = (int)paramValue(kChorusDepth);
    p.tremoloEnabled = paramValue(kTremoloEnabled) >= 0.5f;
    p.tremoloRate = (int)paramValue(kTremoloRate);
    p.tremoloDepth = (int)paramValue(kTremoloDepth);
    p.efxEnabled = paramValue(kEfxEnabled) >= 0.5f;
    p.efxPhaserRate = paramValue(kEfxPhaserRate);
    p.efxPhaserDepth = paramValue(kEfxPhaserDepth);
}

//==============================================================================
void RdPiano_juceAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Aquí se reserva todo lo que render() necesita y arranca el firmware. No
    // hay que volver a seleccionar el parche: boot() reinicia el firmware, no el
    // mapeo de la página de params, y re-aplicarlo cambiaría el audio (trampa 8
    // de CLAUDE.md).
    mcuLock.enter();
    engine->prepare(sampleRate, samplesPerBlock);
    mcuLock.exit();

    // Lo que el hilo de audio está dispuesto a esperar por el cerrojo: un cuarto
    // del bloque. Cubre de sobra al único que puede tenerlo tomado un rato
    // —setMasterTune, ~0,36 ms— sin llegar nunca a comerse el bloque entero.
    const double blockSeconds = sampleRate > 0.0 ? (double)samplesPerBlock / sampleRate : 0.0;
    mcuLockTimeoutTicks.store((juce::int64)(juce::Time::getHighResolutionTicksPerSecond() * blockSeconds * 0.25),
                              std::memory_order_relaxed);
}

// El hilo de UI puede tener el cerrojo y ser desalojado por el planificador:
// esperarlo sin límite desde el hilo de audio es la inversión de prioridad de
// libro. Aquí la espera está acotada y el que no llega a tiempo es el bloque,
// no el callback entero.
bool RdPiano_juceAudioProcessor::acquireEngineLock()
{
    if (mcuLock.tryEnter())
        return true;

    const juce::int64 deadline =
        juce::Time::getHighResolutionTicks() + mcuLockTimeoutTicks.load(std::memory_order_relaxed);
    do
    {
        juce::Thread::yield();
        if (mcuLock.tryEnter())
            return true;
    } while (juce::Time::getHighResolutionTicks() < deadline);

    return false;
}

void RdPiano_juceAudioProcessor::releaseResources()
{
    mcuLock.enter();
    engine->release();
    mcuLock.exit();
}

bool RdPiano_juceAudioProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void RdPiano_juceAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);

    // Limpiando: el bus es estéreo, pero si el host entrega otra cosa hay que
    // devolver silencio, no lo que trajera el búfer.
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
    {
        buffer.clear();
        return;
    }

    syncParamsToEngine();

    // Un solo recorrido de midiMessages, que ya viene ordenado por
    // samplePosition. `numBytes` se mira antes de leer data[1] y data[2]: un
    // mensaje corto vive en el montículo y leer de más sale del búfer.
    for (const auto metadata : midiMessages)
    {
        const juce::uint8 *raw = metadata.data;
        const int n = metadata.numBytes;
        if (n < 1)
            continue;

        engine->pushMidi(metadata.samplePosition, raw[0], n > 1 ? raw[1] : (juce::uint8)0,
                         n > 2 ? raw[2] : (juce::uint8)0);
    }

    // Los eventos ya están en la cola del motor: si este bloque se pierde, el
    // siguiente render() los entrega igualmente en vez de tragárselos.
    if (!acquireEngineLock())
    {
        buffer.clear();
        blocksPreempted.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    engine->render(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples);
    mcuLock.exit();
}

//==============================================================================
bool RdPiano_juceAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor *RdPiano_juceAudioProcessor::createEditor()
{
    return new RdPiano_juceAudioProcessorEditor(*this);
}

//==============================================================================
// El preset es el árbol del APVTS más las dos cosas que no son parámetros: el
// parche (que es el programa) y la afinación maestra. La etiqueta raíz
// <RdPiano> y los nombres de esos dos atributos no cambian: una sesión de una
// versión anterior se sigue abriendo y recupera parche y afinación (sus diez
// parámetros, que iban como atributos de la raíz, vuelven a fábrica).
void RdPiano_juceAudioProcessor::getStateInformation(juce::MemoryBlock &destData)
{
    juce::ValueTree state = apvts.copyState();
    state.setProperty("currentPatch", currentPatch, nullptr);
    state.setProperty("masterTune", masterTune, nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void RdPiano_juceAudioProcessor::setStateInformation(const void *data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));

    // Un bloque que no es un preset nuestro se ignora entero: el plugin se queda
    // como estaba, no a medio cargar.
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType()))
        return;

    // Lo que el preset no traiga vuelve a fábrica, no al valor que el usuario
    // tuviera puesto. `replaceState` sólo toca los parámetros que encuentra en
    // el árbol, así que el punto de partida tiene que ser el valor por defecto.
    for (int i = 0; i < kNumRdParams; i++)
    {
        juce::RangedAudioParameter &p = param((RdParamId)i);
        p.setValueNotifyingHost(p.getDefaultValue());
    }

    juce::ValueTree state = juce::ValueTree::fromXml(*xml);
    apvts.replaceState(state);

    setMasterTune((int16_t)(int)state.getProperty("masterTune", 0));

    int patch = state.getProperty("currentPatch", 0);
    if (patch < 0 || patch >= getNumPrograms())
        patch = 0;
    setCurrentProgram(patch);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new RdPiano_juceAudioProcessor(); }
