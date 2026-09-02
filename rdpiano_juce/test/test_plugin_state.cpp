// Presets y programas: la ida y vuelta del estado del plugin.
//
// Habla con el `AudioProcessor` sólo por su API pública —`getParameters()` por
// id, `getStateInformation`/`setStateInformation`, `setCurrentProgram`— y nunca
// por los punteros concretos: así sigue valiendo, sin editarla, si cambia cómo
// se guarda el estado por dentro.

#include <JuceHeader.h>

#include "PluginProcessor.h"
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
        s.tune = p.masterTune;
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

// Los diez parámetros existen, con ese id exacto, y su valor por defecto es el
// mismo que el POD del motor da por defecto. Si las dos listas se separan, el
// plugin arranca sonando distinto de como suena el motor en las pruebas del
// núcleo.
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

// Ida y vuelta completa: mover todo, guardar, restaurar en otra instancia.
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
    CHECK_EQ(restored.engine->patch(), 11);
    CHECK_EQ(restored.engine->masterTune(), 4321);
}

// Una sesión guardada por una versión anterior no trae todos los atributos: lo
// que falte tiene que volver al valor de fábrica, el mismo con el que arranca
// el plugin.
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

// Un preset corrupto no debe dejar el plugin en un estado imposible.
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
    CHECK_EQ(p.engine->patch(), p.getCurrentProgram());
}

// Los programas son los parches de `patches.h`, con sus nombres.
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
        checks.add(check_fmt("el motor sigue al programa %d", i), p.engine->patch() == i,
                   check_fmt("motor %d, plugin %d", p.engine->patch(), p.getCurrentProgram()));
    }

    // Fuera de rango: se ignora, no se recorta a un parche cualquiera.
    p.setCurrentProgram(5);
    p.setCurrentProgram(-1);
    CHECK_EQ(p.getCurrentProgram(), 5);
    p.setCurrentProgram(NUM_PATCHES);
    CHECK_EQ(p.getCurrentProgram(), 5);
}

// El retardo de grupo del remuestreador, declarado al anfitrión: sin esto todo
// lo que toque el plugin queda 1,4 ms tarde respecto de lo que el DAW cree.
TEST_SUITE(plugin_latency)
{
    RdPiano_juceAudioProcessor p;

    p.prepareToPlay(48000.0, 512);

    CHECK(p.getLatencySamples() > 0);
    CHECK_EQ(p.getLatencySamples(), p.engine->latencySamples());

    // Y no se renegocia al cambiar de sonido: declarar latencia nueva en mitad
    // de una sesión es justo lo que a los anfitriones no les gusta.
    const int declared = p.getLatencySamples();
    p.setCurrentProgram(11);
    CHECK_EQ(p.getLatencySamples(), declared);

    p.releaseResources();
}

// La cola declarada: el anfitrión decide con ella cuánto sigue pidiendo bloques
// después del último evento. Cuánto dura de verdad lo mide engine_tail_length en
// la suite del núcleo; aquí sólo se comprueba que el plugin declara la del motor
// y no un cero.
TEST_SUITE(plugin_tail_length)
{
    RdPiano_juceAudioProcessor p;

    CHECK(p.getTailLengthSeconds() > 0.0);
    CHECK_EQ(p.getTailLengthSeconds(), p.engine->tailLengthSeconds());

    // Y sigue siendo la misma con el plugin preparado y con otro sonido puesto:
    // renegociarla en mitad de una sesión es justo lo que no se puede hacer.
    p.prepareToPlay(48000.0, 512);
    p.setCurrentProgram(5);
    CHECK_EQ(p.getTailLengthSeconds(), RdPianoEngine::kTailSeconds);

    p.releaseResources();
}
