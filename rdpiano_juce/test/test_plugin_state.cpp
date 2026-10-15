/**
 * @file test_plugin_state.cpp
 * @brief El plugin visto por el anfitrión: parámetros, presets, programas, MIDI, latencia, cola y buses.
 *
 * Habla con el `AudioProcessor` por su API de JUCE —`getParameters()` por id,
 * `getStateInformation`/`setStateInformation`, `setCurrentProgram`, `processBlock`—; `engine()` y
 * `masterTune()` confirman que el cambio llegó al motor.
 */

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "PluginState.h"
#include "patches.h"
#include "unit_test.h"

namespace
{

    // Los diez parámetros por id. La lista está duplicada a propósito: su
    // función es discrepar si alguien renombra uno, porque un id que cambia
    // rompe todos los presets guardados.
    const char *const kParamIds[] = {"volume",      "chorusEnabled", "chorusRate", "chorusDepth",   "tremoloEnabled",
                                     "tremoloRate", "tremoloDepth",  "efxEnabled", "efxPhaserRate", "efxPhaserDepth"};

    constexpr int kNumParams = (int)(sizeof kParamIds / sizeof kParamIds[0]);

    juce::RangedAudioParameter *findParam(juce::AudioProcessor &p, const char *id)
    {
        for (juce::AudioProcessorParameter *raw : p.getParameters())
        {
            auto *param = dynamic_cast<juce::RangedAudioParameter *>(raw);
            if (param != nullptr && param->paramID == id)
                return param;
        }
        return nullptr;
    }

    // Un preset es un bloque opaco: lo que se compara es el valor normalizado de
    // cada parámetro, que no depende de cómo se serialice.
    struct Snapshot
    {
        float values[kNumParams] = {};
        int patch = 0;
        int tune = 0;
    };

    Snapshot snapshot(RdPiano_juceAudioProcessor &p)
    {
        Snapshot s;
        for (int i = 0; i < kNumParams; i++)
        {
            juce::RangedAudioParameter *param = findParam(p, kParamIds[i]);
            s.values[i] = param ? param->getValue() : -1.0f;
        }
        s.patch = p.getCurrentProgram();
        s.tune = p.masterTune();
        return s;
    }

    // Un estado con la etiqueta correcta pero sin ningún atributo de parámetro:
    // lo que devuelve una sesión guardada por una versión anterior del plugin.
    juce::MemoryBlock legacyState(int patch)
    {
        juce::XmlElement xml("RdPiano");
        xml.setAttribute("currentPatch", patch);

        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary(xml, block);
        return block;
    }

} // namespace

/**
 * @brief Los diez parámetros existen, con ese id exacto, y su valor por defecto es el mismo que el POD del
 *        motor da por defecto. Si las dos listas se separan, el plugin arranca sonando distinto de como suena
 *        el motor en las pruebas del núcleo.
 */
TEST_SUITE(plugin_params_declared)
{
    RdPiano_juceAudioProcessor p;

    for (int i = 0; i < kNumParams; i++)
        checks.add(std::string("existe ") + kParamIds[i], findParam(p, kParamIds[i]) != nullptr,
                   "no está entre getParameters()");

    CHECK_EQ(p.getParameters().size(), kNumParams);

    const RdEngineParams defaults;
    struct Expect
    {
        const char *id;
        float value;
    };
    const Expect expected[] = {
        {"volume", defaults.volume},
        {"chorusEnabled", defaults.chorusEnabled ? 1.0f : 0.0f},
        {"chorusRate", (float)defaults.chorusRate},
        {"chorusDepth", (float)defaults.chorusDepth},
        {"tremoloEnabled", defaults.tremoloEnabled ? 1.0f : 0.0f},
        {"tremoloRate", (float)defaults.tremoloRate},
        {"tremoloDepth", (float)defaults.tremoloDepth},
        {"efxEnabled", defaults.efxEnabled ? 1.0f : 0.0f},
        {"efxPhaserRate", defaults.efxPhaserRate},
        {"efxPhaserDepth", defaults.efxPhaserDepth},
    };

    for (const Expect &e : expected)
    {
        juce::RangedAudioParameter *param = findParam(p, e.id);
        if (param == nullptr)
            continue;

        float got = param->convertFrom0to1(param->getDefaultValue());
        checks.add(std::string("defecto ") + e.id, std::abs(got - e.value) < 1e-4f,
                   check_fmt("plugin %g, motor %g", (double)got, (double)e.value));
    }
}

/**
 * @brief Ida y vuelta completa: mover todo, guardar, restaurar en otra instancia.
 */
TEST_SUITE(plugin_state_roundtrip)
{
    RdPiano_juceAudioProcessor source;

    // Valores deliberadamente distintos de los de fábrica, y ninguno en el
    // extremo del rango: un fallo de normalización que confunda 0 con el mínimo
    // no pasaría desapercibido.
    const float wanted[kNumParams] = {0.37f, 0.0f, 11.0f, 2.0f, 1.0f, 9.0f, 3.0f, 1.0f, 0.65f, 0.15f};

    for (int i = 0; i < kNumParams; i++)
    {
        juce::RangedAudioParameter *param = findParam(source, kParamIds[i]);
        if (param != nullptr)
            param->setValueNotifyingHost(param->convertTo0to1(wanted[i]));
    }

    source.setCurrentProgram(11);
    source.setMasterTune(4321);

    const Snapshot before = snapshot(source);

    juce::MemoryBlock state;
    source.getStateInformation(state);
    checks.add("el preset no está vacío", state.getSize() > 0, check_fmt("%d bytes", (int)state.getSize()));

    RdPiano_juceAudioProcessor restored;
    restored.setStateInformation(state.getData(), (int)state.getSize());

    const Snapshot after = snapshot(restored);

    for (int i = 0; i < kNumParams; i++)
        checks.add(std::string("vuelve ") + kParamIds[i], std::abs(after.values[i] - before.values[i]) < 1e-5f,
                   check_fmt("guardado %g, restaurado %g", (double)before.values[i], (double)after.values[i]));

    CHECK_EQ(after.patch, before.patch);
    CHECK_EQ(after.tune, before.tune);

    // El parche restaurado tiene que haber llegado al motor, no sólo al espejo
    // que lee el editor.
    CHECK_EQ(restored.engine().patch(), 11);
    CHECK_EQ(restored.engine().masterTune(), 4321);
}

/**
 * @brief Una sesión guardada por una versión anterior no trae todos los atributos: lo que falte tiene que
 *        volver al valor de fábrica, el mismo con el que arranca el plugin.
 */
TEST_SUITE(plugin_state_missing_attributes)
{
    RdPiano_juceAudioProcessor fresh;
    const Snapshot factory = snapshot(fresh);

    RdPiano_juceAudioProcessor p;

    // Se mueve todo primero: si setStateInformation no toca un parámetro, el
    // valor que queda es este y no el de fábrica, y la comprobación lo ve.
    for (int i = 0; i < kNumParams; i++)
    {
        juce::RangedAudioParameter *param = findParam(p, kParamIds[i]);
        if (param != nullptr)
            param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.0f : 1.0f);
    }

    juce::MemoryBlock state = legacyState(3);
    p.setStateInformation(state.getData(), (int)state.getSize());

    const Snapshot after = snapshot(p);

    for (int i = 0; i < kNumParams; i++)
        checks.add(std::string("de fábrica ") + kParamIds[i], std::abs(after.values[i] - factory.values[i]) < 1e-5f,
                   check_fmt("fábrica %g, restaurado %g", (double)factory.values[i], (double)after.values[i]));

    CHECK_EQ(after.patch, 3);
}

/**
 * @brief Un preset corrupto no debe dejar el plugin en un estado imposible.
 */
TEST_SUITE(plugin_state_garbage)
{
    RdPiano_juceAudioProcessor p;
    const Snapshot before = snapshot(p);

    const char junk[] = "no soy un preset";
    p.setStateInformation(junk, (int)sizeof junk);

    const Snapshot after = snapshot(p);

    for (int i = 0; i < kNumParams; i++)
        checks.add(std::string("intacto ") + kParamIds[i], std::abs(after.values[i] - before.values[i]) < 1e-5f,
                   check_fmt("antes %g, después %g", (double)before.values[i], (double)after.values[i]));

    CHECK(after.patch >= 0 && after.patch < NUM_PATCHES);

    // Y un preset con la etiqueta correcta pero un parche fuera de rango.
    juce::MemoryBlock state = legacyState(99);
    p.setStateInformation(state.getData(), (int)state.getSize());
    CHECK(p.getCurrentProgram() >= 0 && p.getCurrentProgram() < NUM_PATCHES);
    CHECK_EQ(p.engine().patch(), p.getCurrentProgram());

    // Una afinación fuera del rango de un int16 se recorta al extremo; sin recortar, 40000 daba la vuelta a
    // una afinación negativa.
    const int tunes[][2] = {{40000, 32767}, {-40000, -32768}, {32767, 32767}, {-32768, -32768}};
    for (const auto &t : tunes)
    {
        juce::XmlElement xml("RdPiano");
        xml.setAttribute("masterTune", t[0]);
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary(xml, block);
        p.setStateInformation(block.getData(), (int)block.getSize());
        checks.add(check_fmt("afinación %d en el preset", t[0]), p.engine().masterTune() == t[1],
                   check_fmt("motor %d, esperado %d", (int)p.engine().masterTune(), t[1]));
    }
}

/**
 * @brief Parche y afinación en el árbol del preset, sin procesador: nombres de los atributos, ida y vuelta y
 *        saneado de lo que traiga un preset corrupto o antiguo.
 */
TEST_SUITE(plugin_state_extras)
{
    const juce::ValueTree written = writeRdState(juce::ValueTree("RdPiano"), {13, -1234});
    CHECK_EQ((int)written.getProperty("currentPatch", -1), 13);
    CHECK_EQ((int)written.getProperty("masterTune", -1), -1234);

    const RdPresetExtras back = readRdState(written);
    CHECK_EQ(back.patch, 13);
    CHECK_EQ(back.tune, -1234);

    const RdPresetExtras empty = readRdState(juce::ValueTree("RdPiano"));
    CHECK_EQ(empty.patch, 0);
    CHECK_EQ(empty.tune, 0);

    juce::ValueTree bad("RdPiano");
    bad.setProperty("currentPatch", NUM_PATCHES, nullptr);
    bad.setProperty("masterTune", 70000, nullptr);
    CHECK_EQ(readRdState(bad).patch, 0);
    CHECK_EQ(readRdState(bad).tune, 32767);

    bad.setProperty("currentPatch", -3, nullptr);
    bad.setProperty("masterTune", "abc", nullptr);
    CHECK_EQ(readRdState(bad).patch, 0);
    CHECK_EQ(readRdState(bad).tune, 0);
}

/**
 * @brief Los programas son los parches de `patches.h`, con sus nombres.
 */
TEST_SUITE(plugin_programs)
{
    RdPiano_juceAudioProcessor p;

    CHECK_EQ(p.getNumPrograms(), NUM_PATCHES);

    for (int i = 0; i < NUM_PATCHES; i++)
        checks.add(check_fmt("nombre del programa %d", i), p.getProgramName(i) == juce::String(patchNames[i]),
                   check_fmt("\"%s\", esperado \"%s\"", p.getProgramName(i).toRawUTF8(), patchNames[i]));

    for (int i = 0; i < NUM_PATCHES; i++)
    {
        p.setCurrentProgram(i);
        checks.add(check_fmt("el motor sigue al programa %d", i), p.engine().patch() == i,
                   check_fmt("motor %d, plugin %d", p.engine().patch(), p.getCurrentProgram()));
    }

    // Fuera de rango: se ignora, no se recorta a un parche cualquiera.
    p.setCurrentProgram(5);
    p.setCurrentProgram(-1);
    CHECK_EQ(p.getCurrentProgram(), 5);
    p.setCurrentProgram(NUM_PATCHES);
    CHECK_EQ(p.getCurrentProgram(), 5);

    // Y el nombre de un programa que no existe es vacío, sin leer fuera de la tabla.
    CHECK(p.getProgramName(-1).isEmpty());
    CHECK(p.getProgramName(NUM_PATCHES).isEmpty());
}

/**
 * @brief El retardo de grupo del remuestreador, declarado al anfitrión: sin esto todo lo que toque el plugin
 *        queda 1,4 ms tarde respecto de lo que el DAW cree.
 */
TEST_SUITE(plugin_latency)
{
    RdPiano_juceAudioProcessor p;

    p.prepareToPlay(48000.0, 512);

    CHECK(p.getLatencySamples() > 0);
    CHECK_EQ(p.getLatencySamples(), p.engine().latencySamples());

    // Y no se renegocia al cambiar de sonido: declarar latencia nueva en mitad
    // de una sesión es justo lo que a los anfitriones no les gusta.
    const int declared = p.getLatencySamples();
    p.setCurrentProgram(11);
    CHECK_EQ(p.getLatencySamples(), declared);

    p.releaseResources();
}

/**
 * @brief La cola declarada: el anfitrión decide con ella cuánto sigue pidiendo bloques después del último
 *        evento. Cuánto dura de verdad lo mide engine_tail_length en la suite del núcleo; aquí sólo se
 *        comprueba que el plugin declara la del motor y no un cero.
 */
TEST_SUITE(plugin_tail_length)
{
    RdPiano_juceAudioProcessor p;

    CHECK(p.getTailLengthSeconds() > 0.0);
    CHECK_EQ(p.getTailLengthSeconds(), p.engine().tailLengthSeconds());

    // Y sigue siendo la misma con el plugin preparado y con otro sonido puesto:
    // renegociarla en mitad de una sesión es justo lo que no se puede hacer.
    p.prepareToPlay(48000.0, 512);
    p.setCurrentProgram(5);
    CHECK_EQ(p.getTailLengthSeconds(), RdPianoEngine::kTailSeconds);

    p.releaseResources();
}

/**
 * @brief Los buses. Lo canónico en un instrumento sería no declarar entrada, y se probó: Logic deja de cargar
 *        la AU —ventana vacía y sin sonido— aunque `auval` la valide. Así que el bus de entrada estéreo se
 *        queda, y esta suite lo fija para que nadie lo vuelva a quitar sin leer la trampa 12 de CLAUDE.md.
 */
TEST_SUITE(plugin_bus_layout)
{
    RdPiano_juceAudioProcessor p;

    CHECK_EQ(p.getBusCount(true), 1);
    CHECK_EQ(p.getBusCount(false), 1);
    CHECK_EQ(p.getMainBusNumInputChannels(), 2);
    CHECK_EQ(p.getMainBusNumOutputChannels(), 2);

    // La salida estéreo se acepta; cualquier otra cosa, no.
    juce::AudioProcessor::BusesLayout stereo;
    stereo.inputBuses.add(juce::AudioChannelSet::stereo());
    stereo.outputBuses.add(juce::AudioChannelSet::stereo());
    CHECK(p.checkBusesLayoutSupported(stereo));

    juce::AudioProcessor::BusesLayout mono;
    mono.inputBuses.add(juce::AudioChannelSet::stereo());
    mono.outputBuses.add(juce::AudioChannelSet::mono());
    CHECK(!p.checkBusesLayoutSupported(mono));

    // Y aunque haya entrada, no se lee: lo que el anfitrión traiga en el búfer
    // no se oye, porque el plugin lo sobrescribe entero.
    p.prepareToPlay(48000.0, 256);

    juce::AudioBuffer<float> buffer(2, 256);
    for (int ch = 0; ch < 2; ch++)
        for (int i = 0; i < 256; i++)
            buffer.getWritePointer(ch)[i] = 0.5f;

    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);

    float peak = 0.0f;
    for (int ch = 0; ch < 2; ch++)
        for (int i = 0; i < 256; i++)
            peak = juce::jmax(peak, std::abs(buffer.getReadPointer(ch)[i]));

    checks.add("sin notas, salida en silencio", peak < 1e-4f, check_fmt("pico %g", (double)peak));

    p.releaseResources();
}

/**
 * @brief MIDI por processBlock, como lo manda el anfitrión: un note-on suena y su posición dentro del bloque se
 *        respeta.
 */
TEST_SUITE(plugin_midi_sounds)
{
    RdPiano_juceAudioProcessor p;
    const int block = 512;
    p.prepareToPlay(48000.0, block);

    juce::AudioBuffer<float> buffer(2, block);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (int b = 0; b < 40; b++)
    {
        midi.clear();
        if (b == 1)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
        buffer.clear();
        p.processBlock(buffer, midi);
        if (b == 0)
            checks.add("silencio antes de la nota", buffer.getMagnitude(0, block) < 1e-4f,
                       check_fmt("pico %g", (double)buffer.getMagnitude(0, block)));
        peak = juce::jmax(peak, buffer.getMagnitude(0, block));
    }
    checks.add("el note-on suena", peak > 0.01f, check_fmt("pico %g", (double)peak));

    p.releaseResources();
}

/**
 * @brief Un program change MIDI cambia el parche en el motor, y el temporizador del procesador lo recoge para
 *        que getCurrentProgram() (y con él el anfitrión y el panel) lo vean.
 */
TEST_SUITE(plugin_midi_program_change)
{
    RdPiano_juceAudioProcessor p;
    const int block = 512;
    p.prepareToPlay(48000.0, block);

    juce::AudioBuffer<float> buffer(2, block);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::programChange(1, 9), 0);
    p.processBlock(buffer, midi);

    midi.clear();
    for (int b = 0; b < 20; b++)
        p.processBlock(buffer, midi);

    p.timerCallback();
    CHECK_EQ(p.getCurrentProgram(), 9);

    p.releaseResources();
}

/**
 * @brief Volver al parche anterior justo después de un program change MIDI, antes de que pase el temporizador:
 *        la petición llega al motor y no se descarta por creer que ese parche ya está puesto.
 */
TEST_SUITE(plugin_program_after_midi_change)
{
    RdPiano_juceAudioProcessor p;
    const int block = 512;
    p.prepareToPlay(48000.0, block);

    juce::AudioBuffer<float> buffer(2, block);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::programChange(1, 9), 0);
    p.processBlock(buffer, midi);
    CHECK_EQ(p.engine().patch(), 9);

    // Sin timerCallback() de por medio: es la ventana en la que el panel aún enseña el parche 0.
    p.setCurrentProgram(0);
    CHECK_EQ(p.engine().patch(), 0);
    CHECK_EQ(p.getCurrentProgram(), 0);

    p.releaseResources();
}

/**
 * @brief Trampa 10: el parche y la afinación viajan en el preset como propiedades, no como parámetros
 *        automatizables.
 */
TEST_SUITE(plugin_patch_and_tune_not_parameters)
{
    RdPiano_juceAudioProcessor p;

    for (juce::AudioProcessorParameter *raw : p.getParameters())
    {
        const juce::String name = raw->getName(64).toLowerCase();
        auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(raw);
        const juce::String id = ranged != nullptr ? ranged->paramID.toLowerCase() : juce::String();
        checks.add(check_fmt("\"%s\" no es parche ni afinación", name.toRawUTF8()),
                   !id.contains("patch") && !id.contains("tune") && !name.contains("patch") && !name.contains("tune"),
                   "el parche o la afinación se han vuelto automatizables");
    }

    // Y siguen en el preset, como atributos.
    p.setCurrentProgram(7);
    p.setMasterTune(1000);
    juce::MemoryBlock state;
    p.getStateInformation(state);
    std::unique_ptr<juce::XmlElement> xml =
        juce::AudioProcessor::getXmlFromBinary(state.getData(), (int)state.getSize());
    CHECK(xml != nullptr);
    if (xml != nullptr)
    {
        CHECK_EQ(xml->getIntAttribute("currentPatch", -1), 7);
        CHECK_EQ(xml->getIntAttribute("masterTune", -1), 1000);
    }
}

namespace
{
    // Lo que ve el anfitrión de un parámetro: gestos abiertos, cerrados y cambios de valor.
    struct GestureLog : juce::AudioProcessorListener
    {
        int begins = 0, ends = 0, changes = 0;
        bool changedOutsideGesture = false;
        int open = 0;

        void audioProcessorParameterChanged(juce::AudioProcessor *, int, float) override
        {
            changes++;
            changedOutsideGesture |= open == 0;
        }
        void audioProcessorChanged(juce::AudioProcessor *, const ChangeDetails &) override {}
        void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor *, int) override
        {
            begins++;
            open++;
        }
        void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor *, int) override
        {
            ends++;
            open--;
        }
    };
} // namespace

/**
 * @brief Un cambio desde el panel llega al anfitrión dentro de un gesto completo: sin begin/end, la
 *        automatización en modo touch o latch no lo graba. Repetir el valor no le manda nada.
 */
TEST_SUITE(plugin_param_gesture)
{
    RdPiano_juceAudioProcessor p;
    GestureLog log;
    p.addListener(&log);

    p.setParamValue(kChorusEnabled, p.paramValue(kChorusEnabled) >= 0.5f ? 0.0f : 1.0f);
    CHECK_EQ(log.begins, 1);
    CHECK_EQ(log.ends, 1);
    CHECK_EQ(log.changes, 1);
    CHECK(!log.changedOutsideGesture);

    p.setParamValue(kChorusEnabled, p.paramValue(kChorusEnabled));
    CHECK_EQ(log.begins, 1);
    CHECK_EQ(log.changes, 1);

    p.removeListener(&log);
}
