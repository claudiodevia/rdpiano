#include "rd_engine.h"

#include "engine/change_scheduler.h"
#include "engine/effects_chain.h"
#include "engine/engine_util.h"
#include "engine/held_notes.h"
#include "engine/midi.h"
#include "engine/output_stage.h"
#include "engine/patch_bank.h"
#include "engine/rate_converter.h"
#include "engine/retrigger_meter.h"
#include "mcu.h"

/**
 * @file rd_engine.cpp
 * @brief La orquestación del motor: ciclo de vida, cambios de parche y afinación, y las fases de render().
 */

namespace
{

    /// El escalado seco: (sample << 5 >> 6) / 65536 * 0.5. Los desplazamientos
    /// van en EffectsChain; el 0,5, con la ganancia de parche.
    constexpr float kEmuToFloat = 1.0f / 65536.0f;
    constexpr float kOutputScaling = 0.5f;

} // namespace

/** @brief El estado del motor y lo que hace con él; RdPianoEngine solo le reenvía. */
class RdPianoEngine::Impl
{
  public:
    Impl(const RdRomSet *romSets, const u8 *programRom, const RdEngineParams &params, RdEngineStats &stats);

    void prepare(double hostSampleRate, int maxBlockSize);
    void release() { rate.close(); }

    void setPatch(int patch);
    void requestPatch(int patch);
    void setMasterTune(int16_t tune);
    void requestMasterTune(int16_t tune) { changes.requestTune(tune); }
    void allNotesOff();
    void pushMidi(int frame, u8 status, u8 data1, u8 data2);
    void render(float *left, float *right, int numFrames);

    const RdEngineParams &params;
    RdEngineStats &stats;

    Mcu mcu;
    PatchBank bank;
    MidiQueue midi;
    HeldNotes held;
    RetriggerMeter meter;
    ChangeScheduler changes;
    EffectsChain effects;
    RateConverter rate;
    OutputStage output;

    double hostRate = 0;
    int maxBlock = 0;
    int sourceRate = MIN_PATCH_RATE;
    int currentPatch = 0;
    int16_t currentMasterTune = 0;

    /// El volumen se interpola dentro del bloque, a la tasa del emulador: sin
    /// esto, mover el mando es un escalón por bloque (zíper).
    float volumeSmoothed = 1.0f;

  private:
    /** @brief Pone el parche en el emulador y ajusta a su tasa todo lo que corre a ella. */
    void applyPatch(int patch);

    /** @brief Afina corriendo el emulador; apaga las voces y suelta el pedal del firmware. */
    void applyMasterTune(int16_t tune);

    /** @brief Tras un cambio que apagó el firmware: devuelve lo pulsado al nivel alcanzado y elige la subida. */
    void finishChange();

    /** @brief Lo que render() atiende entre bloques: los cambios pedidos desde otro hilo, con su declick. */
    void serviceRequests(int numFrames);

    /** @brief Entrega un mensaje al firmware y lo apunta en el espejo; el único camino de salida del MIDI. */
    void sendTracked(u8 status, u8 data1, u8 data2);

    /** @brief Una tecla o el pedal al espejo, al medidor y al firmware. */
    void sendKey(const KeyEvent &key);

    /**
     * @brief Bloque que no se puede rendir: silencio, y su MIDI al firmware igual.
     *
     * Dejar la salida intacta devolvería al host la entrada o el bloque anterior. Perder la cola dejaría una
     * nota pulsada en firmware y espejo para siempre, y el siguiente cambio la resucitaría.
     */
    void abortBlock(float *left, float *right, int numFrames);

    /** @brief Entrega al firmware los eventos de la cola desde `first` y la vacía. */
    void flushMidi(int first);

    /**
     * @brief El bucle de síntesis a `sourceRate`: emulador, medidor, efectos, volumen y reparto del MIDI.
     * @return Cuántos eventos de la cola se entregaron; los demás caen más allá del último frame.
     */
    int synthesise(int emuFrames);

    /** @brief La ganancia de salida del parche que suena. */
    float patchGainTarget() const { return kOutputScaling * patchOutputGain[currentPatch]; }

    /** @brief La tasa del emulador ha cambiado con el parche. */
    void onSourceRateChanged(int newRate);
};

// ---------------------------------------------------------------- ciclo de vida

RdPianoEngine::Impl::Impl(const RdRomSet *romSets, const u8 *programRom, const RdEngineParams &p, RdEngineStats &s)
    : params(p), stats(s), mcu(romSets[patchToRomSetId[0]].ic5, romSets[patchToRomSetId[0]].ic6,
                               romSets[patchToRomSetId[0]].ic7, programRom, romSets[patchToRomSetId[0]].ic18),
      bank(romSets, mcu)
{
    mcu.setSampleRate(patchSampleRates[0]);
    onSourceRateChanged(patchSampleRates[0]);
    effects.reset();
}

void RdPianoEngine::Impl::prepare(double newHostRate, int newMaxBlock)
{
    hostRate = newHostRate;
    maxBlock = newMaxBlock < 0 ? 0 : newMaxBlock;
    stats.resamplerOpens.fetch_add((unsigned long)rate.open(hostRate, maxBlock), std::memory_order_relaxed);

    // Trampa 8 de CLAUDE.md: lo pedido y sin atender se aplica antes de
    // arrancar. No hace falta finishChange(): boot() reinicia el firmware.
    const ChangeScheduler::Due pending = changes.takePending();
    if (pending.patch >= 0 && pending.patch != currentPatch)
        applyPatch(pending.patch);
    if (pending.tune != ChangeScheduler::kNoTune)
    {
        currentMasterTune = (int16_t)pending.tune;
        changes.publishTune(currentMasterTune);
    }

    // Trampa 7 de CLAUDE.md: el firmware calienta siempre a 20 kHz.
    mcu.boot(currentMasterTune, false);

    changes.prepare(hostRate);
    held.clear();
    meter.reset();
    midi.clear();

    effects.reset();
    effects.snapTo(params);
    volumeSmoothed = params.volume;
    output.prepare(hostRate, patchGainTarget());
}

// ---------------------------------------------------------------- control

void RdPianoEngine::Impl::onSourceRateChanged(int newRate)
{
    sourceRate = newRate;
    effects.setSourceRate(newRate);
    meter.setSourceRate(newRate);
}

void RdPianoEngine::Impl::applyPatch(int patch)
{
    bank.activate(mcu, patch);
    currentPatch = patch;
    changes.publishPatch(patch);
    onSourceRateChanged(patchSampleRates[patch]);
}

void RdPianoEngine::Impl::applyMasterTune(int16_t tune)
{
    currentMasterTune = tune;
    changes.publishTune(tune);
    mcu.setMasterTune(tune);
}

void RdPianoEngine::Impl::finishChange()
{
    const int retriggered = held.restore(mcu, meter.velocityDrop());
    changes.finish(retriggered > 0);
}

void RdPianoEngine::Impl::setPatch(int patch)
{
    if (patch < 0 || patch >= NUM_PATCHES)
        return;

    changes.cancelPatch();
    applyPatch(patch);
    finishChange();
}

void RdPianoEngine::Impl::requestPatch(int patch)
{
    if (patch < 0 || patch >= NUM_PATCHES)
        return;

    changes.request(patch);
}

void RdPianoEngine::Impl::setMasterTune(int16_t tune)
{
    changes.cancelTune();
    applyMasterTune(tune);
    finishChange();
}

void RdPianoEngine::Impl::serviceRequests(int numFrames)
{
    ChangeScheduler::Due due;
    if (!changes.service(numFrames, currentPatch, currentMasterTune, due))
        return;

    // Afinar primero: sus program change apagarían las voces que acaba de
    // devolver el cambio de parche, y así lo pulsado se restaura una vez.
    if (due.tune != ChangeScheduler::kNoTune)
        applyMasterTune((int16_t)due.tune);
    if (due.patch >= 0)
        applyPatch(due.patch);

    finishChange();
}

void RdPianoEngine::Impl::allNotesOff()
{
    mcu.allNotesOff();
    held.clear();
}

// ---------------------------------------------------------------- MIDI

void RdPianoEngine::Impl::pushMidi(int frame, u8 status, u8 data1, u8 data2)
{
    if ((status >> 4) == kMidiProgramChange)
    {
        if (data1 < NUM_PATCHES)
            requestPatch(data1);
        else
            stats.programIgnored.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (!midi.push(frame, status, data1, data2))
        stats.midiDropped.fetch_add(1, std::memory_order_relaxed);
}

void RdPianoEngine::Impl::sendTracked(u8 status, u8 data1, u8 data2)
{
    const KeyEvent key = KeyEvent::decode(status, data1, data2);

    // Trampa 14 de CLAUDE.md; lo fija `engine_repeated_note`.
    if (key.kind == KeyEvent::NoteOn && held.isHeld(key.note))
        sendKey(KeyEvent::release(key.note));

    sendKey(key);
}

void RdPianoEngine::Impl::sendKey(const KeyEvent &key)
{
    if (held.track(key))
        meter.startOnset();

    switch (key.kind)
    {
    case KeyEvent::NoteOn:
        mcu.noteOn(key.note, key.velocity);
        break;
    case KeyEvent::NoteOff:
        mcu.noteOff(key.note);
        break;
    case KeyEvent::Sustain:
        mcu.sustain(key.pedalDown);
        break;
    case KeyEvent::Other:
        break;
    }
}

// ---------------------------------------------------------------- render

void RdPianoEngine::Impl::flushMidi(int first)
{
    for (int i = first; i < midi.size(); i++)
        sendTracked(midi[i].status, midi[i].data1, midi[i].data2);
    midi.clear();
}

void RdPianoEngine::Impl::abortBlock(float *left, float *right, int numFrames)
{
    silence(left, right, numFrames);
    flushMidi(0);
}

int RdPianoEngine::Impl::synthesise(int emuFrames)
{
    effects.beginBlock(params);

    // `frame` va en muestras del host e `i` en las del emulador. La cola llega
    // ordenada; un evento fuera de orden se entrega en el vaciado del final.
    const double hostToEmu = (double)sourceRate / hostRate;
    int nextEvent = 0;

    // Al final se asigna el destino en vez de acumularlo: con el mando quieto
    // el paso es exactamente 0 y no hay deriva.
    const float volumeTarget = params.volume;
    const float volumeStep = (volumeTarget - volumeSmoothed) / (float)emuFrames;

    float *emuL = rate.emuLeft();
    float *emuR = rate.emuRight();

    for (int i = 0; i < emuFrames; i++)
    {
        while (nextEvent < midi.size() && (int)(midi[nextEvent].frame * hostToEmu) <= i)
        {
            sendTracked(midi[nextEvent].status, midi[nextEvent].data1, midi[nextEvent].data2);
            nextEvent++;
        }

        const s32 sample = mcu.nextSample();
        meter.feed(sample);

        s32 outL;
        s32 outR;
        effects.process(sample, outL, outR);

        emuL[i] = outL * kEmuToFloat * volumeSmoothed;
        emuR[i] = outR * kEmuToFloat * volumeSmoothed;
        volumeSmoothed += volumeStep;
    }
    volumeSmoothed = volumeTarget;

    return nextEvent;
}

void RdPianoEngine::Impl::render(float *left, float *right, int numFrames)
{
    serviceRequests(numFrames);

    if (numFrames <= 0)
    {
        abortBlock(left, right, numFrames);
        return;
    }

    if (numFrames > rate.outCapacity())
    {
        stats.blockTooLarge.fetch_add(1, std::memory_order_relaxed);
        abortBlock(left, right, numFrames);
        return;
    }

    const int emuFrames = rate.plan(numFrames, sourceRate, stats);
    if (emuFrames == 0)
    {
        abortBlock(left, right, numFrames);
        return;
    }

    const int eventsSent = synthesise(emuFrames);
    rate.process(emuFrames, numFrames, sourceRate, stats);
    output.process(rate.outLeft(), rate.outRight(), left, right, numFrames, params, patchGainTarget(), changes);

    // Eventos que el host situó al final del bloque y que la conversión de
    // tasas deja más allá del último frame generado.
    flushMidi(eventsSent);
}

// ---------------------------------------------------------------- reenvío

RdPianoEngine::RdPianoEngine(const RdRomSet *romSets, const u8 *programRom)
    : impl(new Impl(romSets, programRom, params, stats))
{
}

RdPianoEngine::~RdPianoEngine() { delete impl; }

void RdPianoEngine::prepare(double hostSampleRate, int maxBlockSize) { impl->prepare(hostSampleRate, maxBlockSize); }
void RdPianoEngine::release() { impl->release(); }
void RdPianoEngine::setPatch(int patch) { impl->setPatch(patch); }
int RdPianoEngine::patch() const { return impl->changes.latestPatch(); }
int RdPianoEngine::activePatch() const { return impl->currentPatch; }
void RdPianoEngine::requestPatch(int patch) { impl->requestPatch(patch); }
void RdPianoEngine::requestMasterTune(int16_t tune) { impl->requestMasterTune(tune); }
void RdPianoEngine::setMasterTune(int16_t tune) { impl->setMasterTune(tune); }
int16_t RdPianoEngine::masterTune() const { return impl->changes.latestTune(); }
int16_t RdPianoEngine::activeMasterTune() const { return impl->currentMasterTune; }
int RdPianoEngine::latencySamples() const { return impl->rate.latency(); }
void RdPianoEngine::allNotesOff() { impl->allNotesOff(); }
void RdPianoEngine::pushMidi(int frame, u8 status, u8 data1, u8 data2) { impl->pushMidi(frame, status, data1, data2); }
void RdPianoEngine::render(float *left, float *right, int numFrames) { impl->render(left, right, numFrames); }
double RdPianoEngine::hostSampleRate() const { return impl->hostRate; }
int RdPianoEngine::sourceSampleRate() const { return impl->sourceRate; }
int RdPianoEngine::preparedBlockSize() const { return impl->maxBlock; }
