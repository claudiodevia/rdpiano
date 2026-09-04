#include "rd_engine.h"

#include <math.h>

#include "mcu.h"
#include "rd_trace.h"
#include "resample/libresample.h"
#include "rom_loader.h"

/**
 * @file rd_engine.cpp
 * @brief La cadena de audio: constantes medidas, ciclo de vida y las fases de render().
 */

/// El periodo del chorus por posición del dial, en milisegundos. Es parte de la
/// cadena, no de la UI.
static const int chorusRateToMsPeriod[15] = {
    2700, // 1
    1380, // 2
    910,  // 3
    680,  // 4
    540,  // 5
    450,  // 6
    385,  // 7
    335,  // 8
    300,  // 9
    265,  // 10
    245,  // 11
    220,  // 12
    205,  // 13
    190,  // 14
    175,  // 15
};

/// El EQ medio, afinado de oído contra un MKS-20. Los coeficientes se calculan
/// una vez en prepare().
static const float kMidEqFreq = 350.0f;
static const float kMidEqQ = 0.2f;
static const float kMidEqGainDb = 8.0f;

/// El escalado seco: (sample << 5 >> 6) / 65536 * 0.5.
static const int kEmuInputShift = 5;
static const int kEmuOutputShift = 6;
static const float kEmuToFloat = 1.0f / 65536.0f;
static const float kOutputScaling = 0.5f;

static const float kPi = 3.14159265358979323846f;

/// La fase del trémolo se lleva en doble: es un acumulador, no un coeficiente.
static const double kTwoPi = 2.0 * 3.14159265358979323846;

/// Rampas de conmutación, en milisegundos. La de los efectos es la que evita el
/// salto duro del bypass; las otras dos son el declick de los cambios que apagan
/// el firmware —parche y afinación—: bajar rápido, subir despacio, que es como
/// se oye menos.
static const float kEffectMixRampMs = 10.0f;
static const float kChangeFadeOutMs = 6.0f;
static const float kChangeFadeInMs = 15.0f;

/// Subida cuando el cambio ha tenido que volver a disparar lo que se estaba
/// tocando: más larga que la normal a propósito, porque es la que esconde el
/// golpe de martillo de las notas que reentran (los primeros milisegundos del
/// ataque).
static const float kChangeFadeInHeldMs = 80.0f;

/// Cuánto baja el nivel del ataque por unidad de velocidad. Medido sobre los 16
/// parches: entre v16 y v120 la curva es recta a 0,228 dB por unidad (por debajo
/// de v16 se aplana, de ahí el suelo de la corrección). Es lo que convierte
/// "esta nota ya había decaído N dB" en la velocidad con la que hay que volver a
/// dispararla.
static const float kDbPerVelocityUnit = 0.228f;

/// Tope de la corrección: más allá, la velocidad resultante se sale de la parte
/// recta de la curva y el timbre deja de parecerse al que sonaba.
static const float kMaxRetriggerAttenDb = 30.0f;

/// El seguidor de nivel es un RMS de constante `kLevelTauMs`, no un detector de
/// pico: el pico del ataque de un acorde suma en fase y el del sustain no, así
/// que medir picos daba varios dB de decaimiento que no existían. La ventana de
/// ataque es más larga que la constante para que al RMS le dé tiempo a llenarse.
static const float kLevelTauMs = 25.0f;
static const float kOnsetWindowMs = 80.0f;

/// El retardo de grupo se declara al anfitrión en el peor caso —el parche más
/// lento— para no renegociar la latencia en cada cambio de sonido.
static const double kWorstSourceRate = 20000.0;

static inline int clamp_index(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/**
 * @brief Curva de la rampa de declick: plana al arrancar, suave al llegar.
 *
 * Smoothstep, derivada cero en 0 y en 1. El arranque plano es lo que esconde el
 * transitorio de la nota que reentra; que además se aplane al final es lo que
 * evita el bombeo de la subida al cuadrado, que se pasaba media rampa 12 dB por
 * debajo. En 0 y en 1 no cambia nada.
 */
static inline float declick_shape(float g) { return g * g * (3.0f - 2.0f * g); }

static inline float clamp_step(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/**
 * @brief Una rampa por muestra a partir de su duración.
 * @param ms Duración de la rampa.
 * @param rate Tasa a la que corre; puede ser 0 antes de prepare(), de ahí el suelo.
 * @return El paso por muestra, acotado a 1.
 */
static inline float ramp_step(float ms, double rate)
{
    const double samples = ms * 0.001 * rate;
    return samples > 1.0 ? (float)(1.0 / samples) : 1.0f;
}

// ---------------------------------------------------------------- biquad

// juce::dsp::ArrayCoefficients<float>::makePeakFilter, en float, con el mismo
// orden de operaciones; `assignImpl` divide después todo por a0.
void RdBiquad::setPeak(double sampleRate, float frequency, float q, float gainFactor)
{
    const float A = sqrtf(gainFactor);
    const float omega = (2.0f * kPi * (frequency > 2.0f ? frequency : 2.0f)) / (float)sampleRate;
    const float alpha = sinf(omega) / (q * 2.0f);
    const float c2 = -2.0f * cosf(omega);
    const float alphaTimesA = alpha * A;
    const float alphaOverA = alpha / A;

    const float rb0 = 1.0f + alphaTimesA;
    const float rb1 = c2;
    const float rb2 = 1.0f - alphaTimesA;
    const float ra0 = 1.0f + alphaOverA;
    const float ra1 = c2;
    const float ra2 = 1.0f - alphaOverA;

    const float inv = 1.0f / ra0;
    b0 = rb0 * inv;
    b1 = rb1 * inv;
    b2 = rb2 * inv;
    a1 = ra1 * inv;
    a2 = ra2 * inv;
}

// ---------------------------------------------------------------- ciclo de vida

RdPianoEngine::RdPianoEngine(const RdRomSet *sets, const u8 *prog) : romSets(sets), programRom(prog)
{
    const RdRomSet &set = romSets[patchToRomSetId[0]];
    mcu = new Mcu(set.ic5, set.ic6, set.ic7, programRom, set.ic18);

    // Todo lo caro de cambiar de parche, hecho aquí y una sola vez: los tres
    // juegos de ROM de onda descifrados en su ranura del chip (~2,9 ms cada uno,
    // 768 KB cada uno) y las 16 páginas de parámetros (32 KB cada una). A partir
    // de ahí un cambio de parche es activar una ranura y copiar una página, que
    // es lo que lo hace posible desde el hilo de audio.
    for (int s = 0; s < ROMSET_COUNT; s++)
        mcu->decodeRomSet((unsigned)s, romSets[s].ic5, romSets[s].ic6, romSets[s].ic7);

    paramPages = new u8[(size_t)NUM_PATCHES * PARAMS_PAGE_BYTES];
    for (int p = 0; p < NUM_PATCHES; p++)
        decode_params_window(paramPages + (size_t)p * PARAMS_PAGE_BYTES, romSets[patchToRomSetId[p]].ic18,
                             patchToOffset[p]);

    currentPatch = 0;
    latestPatch.store(0, std::memory_order_relaxed);
    sourceRate = patchSampleRates[0];
    mcu->selectRomSet((unsigned)patchToRomSetId[0], set.ic18);

    spaceD.reset();
    phaser.reset();
    eqL.reset();
    eqR.reset();
}

RdPianoEngine::~RdPianoEngine()
{
    release();
    delete[] paramPages;
    delete mcu;
}

const u8 *RdPianoEngine::paramPage(int patch) const { return paramPages + (size_t)patch * PARAMS_PAGE_BYTES; }

void RdPianoEngine::release()
{
    delete[] emuL;
    delete[] emuR;
    delete[] outL;
    delete[] outR;
    emuL = emuR = outL = outR = nullptr;
    emuCapacity = 0;
    outCapacity = 0;

    if (resampleL)
        resample_close(resampleL);
    if (resampleR)
        resample_close(resampleR);
    resampleL = resampleR = nullptr;
}

void RdPianoEngine::prepare(double newHostRate, int newMaxBlock)
{
    release();

    hostRate = newHostRate;
    maxBlock = newMaxBlock < 0 ? 0 : newMaxBlock;

    // El búfer intermedio va a la tasa del EMULADOR: el factor es
    // sourceRate/hostRate, no su inverso. Se dimensiona para el peor caso
    // —32 kHz, porque el parche cambia sin volver a preparar— más el margen de
    // numFrames/4 que puede añadir la corrección de deriva de render().
    const double worstRatio = 32000.0 / hostRate;
    emuCapacity = (int)ceil(maxBlock * worstRatio) + maxBlock / 4 + 4;
    if (emuCapacity < 4)
        emuCapacity = 4;
    outCapacity = maxBlock < 1 ? 1 : maxBlock;

    emuL = new float[emuCapacity]();
    emuR = new float[emuCapacity]();
    outL = new float[outCapacity]();
    outR = new float[outCapacity]();

    samplesError = 0;

    // Los dos resamplers se abren aquí y no se vuelven a tocar: cada
    // resample_open(highQuality=1) cuesta ~600 KB y 2,5 ms. El rango cubre todos
    // los parches a esta tasa de host y resample_process() acepta un factor
    // variable dentro de él; el filtro no depende del rango.
    const double minFactor = hostRate / 32000.0;
    const double maxFactor = hostRate / 20000.0;
    resampleL = resample_open(1, minFactor, maxFactor);
    resampleR = resample_open(1, minFactor, maxFactor);
    stats.resamplerOpens += 2;

    // El retardo de grupo del remuestreador, en muestras del host: `Xoff` va en
    // muestras de ENTRADA, así que se convierte con la tasa del emulador.
    const int filterWidth = resampleL ? resample_get_filter_width(resampleL) : 0;
    latencyFrames = (int)(filterWidth * hostRate / kWorstSourceRate + 0.5);

    // Una petición sin atender se aplica ANTES de arrancar: el orden bueno es
    // seleccionar el parche y luego preparar (trampa 8 de CLAUDE.md), y quien
    // pidió el cambio con el motor parado espera eso.
    const int requested = patchRequest.exchange(-1, std::memory_order_acquire);
    if (requested >= 0 && requested < NUM_PATCHES && requested != currentPatch)
        applyPatch(requested);

    const int tune = tuneRequest.exchange(kNoTuneRequest, std::memory_order_acquire);
    if (tune != kNoTuneRequest)
    {
        currentMasterTune = (int16_t)tune;
        latestTune.store(tune, std::memory_order_relaxed);
    }

    // El firmware arranca siempre a 20 kHz, también en los parches de 32 kHz. El
    // harness e2e calienta al ritmo del parche destino: la divergencia (trampa 7
    // de CLAUDE.md) sigue viva porque cerrarla movería el golden.
    mcu->boot(currentMasterTune, false);

    // Las rampas, en muestras. Las de los efectos van al ritmo del emulador
    // (dentro del bucle de síntesis) y el declick al del host (en la salida).
    effectMixStep = ramp_step(kEffectMixRampMs, sourceRate);
    declickDownStep = ramp_step(kChangeFadeOutMs, hostRate);
    declickUpStep = ramp_step(kChangeFadeInMs, hostRate);
    declickGain = 1.0f;
    declickPatch = -1;
    declickTune = kNoTuneRequest;

    // `boot()` reinicia el firmware entero, así que el espejo de lo pulsado
    // arranca vacío con él.
    for (int note = 0; note < 128; note++)
        heldVelocity[note] = 0;
    sustainDown = false;

    levelSmooth = 1.0f - expf(-1.0f / (kLevelTauMs * 0.001f * (float)sourceRate));
    levelSq = 0.0f;
    onsetSq = 0.0f;
    onsetHold = 0;

    // Arrancar en la posición que ya tienen los mandos: la rampa es para las
    // conmutaciones, no para el primer bloque.
    chorusMix = params.chorusEnabled ? 1.0f : 0.0f;
    efxMix = params.efxEnabled ? 1.0f : 0.0f;
    volumeSmoothed = params.volume;
    outputGainSmoothed = kOutputScaling * patchOutputGain[currentPatch];

    spaceD.reset();
    phaser.reset();

    eqL.setPeak(hostRate, kMidEqFreq, kMidEqQ, powf(10.0f, kMidEqGainDb * 0.05f));
    eqR.setPeak(hostRate, kMidEqFreq, kMidEqQ, powf(10.0f, kMidEqGainDb * 0.05f));
    eqL.reset();
    eqR.reset();

    tremoloPhase = 0;
    midiCount = 0;
}

// ---------------------------------------------------------------- control

void RdPianoEngine::prepareRomSetFor(int) {}

void RdPianoEngine::applyPatch(int patch)
{
    const int romSet = patchToRomSetId[patch];
    if (romSet != patchToRomSetId[currentPatch])
        mcu->selectRomSet((unsigned)romSet, romSets[romSet].ic18);

    mcu->selectPatchPage(paramPage(patch), patchToOffset[patch]);
    currentPatch = patch;
    latestPatch.store(patch, std::memory_order_relaxed);
    mcu->reloadPatch();

    sourceRate = patchSampleRates[patch];
    effectMixStep = ramp_step(kEffectMixRampMs, sourceRate);

    // La constante del seguidor de nivel va al ritmo del emulador, que acaba de
    // cambiar con el parche.
    levelSmooth = 1.0f - expf(-1.0f / (kLevelTauMs * 0.001f * (float)sourceRate));
}

/**
 * @brief Cierra un cambio que ha apagado el firmware: devuelve lo pulsado y elige la subida.
 *
 * Tanto el program change de `reloadPatch()` como el switcharoo de la afinación
 * apagan las voces y sueltan el pedal dentro del firmware, así que los dos
 * terminan aquí. Si ha reentrado algo, la salida sube despacio: es lo que
 * convierte el golpe de tecla en una entrada suave.
 */
void RdPianoEngine::finishChange()
{
    const int retriggered = restoreHeldNotes();
    declickUpStep = ramp_step(retriggered > 0 ? kChangeFadeInHeldMs : kChangeFadeInMs, hostRate);
}

void RdPianoEngine::trackMidi(u8 status, u8 data1, u8 data2)
{
    const u8 command = (u8)(status >> 4);
    const u8 note = (u8)(data1 & 0x7f);

    if (command == 0x9 && data2 > 0)
    {
        heldVelocity[note] = (u8)(data2 & 0x7f);

        // Empieza la ventana en la que se mide de qué nivel arranca la nota.
        onsetHold = (int)(kOnsetWindowMs * 0.001f * (float)sourceRate);
        onsetSq = 0.0f;
    }
    else if (command == 0x8 || (command == 0x9 && data2 == 0))
        heldVelocity[note] = 0;
    else if (command == 0xb && data1 == 64)
        sustainDown = data2 >= 64;
}

void RdPianoEngine::sendTracked(u8 status, u8 data1, u8 data2)
{
    trackMidi(status, data1, data2);
    mcu->sendMidiCmd(status, data1, data2);
}

/**
 * @brief Le devuelve al firmware el pedal y las teclas que siguen pulsadas.
 *
 * El pedal va primero: al revés, las notas reenviadas nacerían sin sostenido.
 * Son bytes a la cola de comandos y nada más, así que vale desde el hilo de
 * audio. La velocidad no es la original sino la que deja la nota en el nivel al
 * que había llegado decayendo: reentrar con el ataque entero es exactamente el
 * golpe de tecla que no debe oírse al cambiar de sonido.
 *
 * @return Cuántas notas han reentrado.
 */
int RdPianoEngine::restoreHeldNotes()
{
    if (sustainDown)
        mcu->sendMidiCmd(0xb0, 64, 127);

    int deltaVelocity = 0;
    if (onsetSq > 0.0f && levelSq > 0.0f && levelSq < onsetSq)
    {
        // Los dos seguidores son potencias, de ahí el 10 y no el 20.
        float attenDb = -10.0f * log10f(levelSq / onsetSq);
        if (attenDb > kMaxRetriggerAttenDb)
            attenDb = kMaxRetriggerAttenDb;
        deltaVelocity = (int)(attenDb / kDbPerVelocityUnit + 0.5f);
    }

    int retriggered = 0;
    for (int note = 0; note < 128; note++)
    {
        if (heldVelocity[note] == 0)
            continue;

        int velocity = (int)heldVelocity[note] - deltaVelocity;
        if (velocity < 1)
            velocity = 1;

        mcu->sendMidiCmd(0x90, (u8)note, (u8)velocity);
        retriggered++;
    }
    return retriggered;
}

void RdPianoEngine::setPatch(int patch)
{
    if (patch < 0 || patch >= NUM_PATCHES)
        return;

    // Cambio inmediato: manda sobre cualquier petición a medio atender.
    patchRequest.store(-1, std::memory_order_relaxed);
    declickPatch = -1;
    applyPatch(patch);
    finishChange();
}

void RdPianoEngine::requestPatch(int patch)
{
    if (patch < 0 || patch >= NUM_PATCHES)
        return;

    latestPatch.store(patch, std::memory_order_relaxed);
    patchRequest.store(patch, std::memory_order_release);
}

void RdPianoEngine::requestMasterTune(int16_t tune)
{
    latestTune.store((int)tune, std::memory_order_relaxed);
    tuneRequest.store((int)tune, std::memory_order_release);
}

/** @brief Lo que render() atiende entre bloques: todo lo que antes corría el hilo de UI con el cerrojo tomado. */
void RdPianoEngine::serviceRequests()
{
    const int requested = patchRequest.exchange(-1, std::memory_order_acquire);
    if (requested >= 0 && requested < NUM_PATCHES)
        declickPatch = requested == currentPatch ? -1 : requested;

    // Afinar apaga el firmware igual que cambiar de parche (el switcharoo de
    // `Mcu::setMasterTune()`), así que va por el mismo declick: sin él, mover el
    // dial de TUNE cortaba el sonido en seco.
    const int tune = tuneRequest.exchange(kNoTuneRequest, std::memory_order_acquire);
    if (tune != kNoTuneRequest)
        declickTune = (int16_t)tune != currentMasterTune ? tune : kNoTuneRequest;

    if (declickPatch < 0 && declickTune == kNoTuneRequest)
        return;

    // El cambio se aplica con la salida ya en cero y siempre entre bloques: la
    // tasa del emulador cambia con el parche y el bloque entero depende de ella.
    if (declickGain > 0.0f)
        return;

    // La afinación primero: sus program change apagarían las voces que acaba de
    // devolver el cambio de parche, y así lo pulsado se restaura una sola vez.
    if (declickTune != kNoTuneRequest)
    {
        applyMasterTune((int16_t)declickTune);
        declickTune = kNoTuneRequest;
    }

    if (declickPatch >= 0)
    {
        applyPatch(declickPatch);
        declickPatch = -1;
    }

    finishChange();
}

void RdPianoEngine::applyMasterTune(int16_t tune)
{
    currentMasterTune = tune;
    latestTune.store((int)tune, std::memory_order_relaxed);
    mcu->setMasterTune(tune);
}

void RdPianoEngine::setMasterTune(int16_t tune)
{
    // Cambio inmediato: manda sobre cualquier petición a medio atender.
    tuneRequest.store(kNoTuneRequest, std::memory_order_relaxed);
    declickTune = kNoTuneRequest;
    applyMasterTune(tune);
    finishChange();
}

void RdPianoEngine::allNotesOff()
{
    mcu->allNotesOff();

    // El espejo también: si no, el siguiente cambio de parche resucitaría las
    // notas que el pánico acaba de apagar.
    for (int note = 0; note < 128; note++)
        heldVelocity[note] = 0;
    sustainDown = false;
}

void RdPianoEngine::pushMidi(int frame, u8 status, u8 data1, u8 data2)
{
    // El program change es un cambio de parche completo, con su página de
    // parámetros: reenviarlo al firmware tal cual dejaba el motor mudo, porque
    // la página mapeada seguía siendo la del parche anterior.
    if ((status & 0xf0) == 0xc0)
    {
        requestPatch(data1 & 0x0f);
        return;
    }

    if (midiCount >= kMidiQueueSize)
    {
        stats.midiDropped++;
        return;
    }

    RdMidiEvent &e = midiQueue[midiCount++];
    e.frame = frame;
    e.status = status;
    e.data1 = data1;
    e.data2 = data2;
}

// ---------------------------------------------------------------- render

static inline void silence(float *left, float *right, int numFrames)
{
    for (int i = 0; i < numFrames; i++)
    {
        left[i] = 0.0f;
        right[i] = 0.0f;
    }
}

/**
 * @brief Bloque que no se puede rendir.
 *
 * La salida se limpia —dejarla intacta devolvería al host la entrada o el bloque
 * anterior en vez de silencio— y la cola MIDI se vacía, porque sus eventos ya no
 * tienen dónde ir.
 *
 * @param left Canal izquierdo.
 * @param right Canal derecho.
 * @param numFrames Muestras del bloque.
 */
void RdPianoEngine::abortBlock(float *left, float *right, int numFrames)
{
    silence(left, right, numFrames);
    midiCount = 0;
}

/**
 * @brief Cuántas muestras del emulador pide este bloque, con la corrección de deriva.
 * @param numFrames Muestras del host que pide el anfitrión.
 * @param blockError Sale con lo que hay que acumular en `samplesError` si el bloque se rinde.
 * @return Muestras del emulador a generar, o 0 si el bloque no se puede rendir.
 */
int RdPianoEngine::framesForBlock(int numFrames, double *blockError)
{
    const double renderBufferFramesFloat = (double)numFrames / hostRate * sourceRate;
    int renderBufferFrames = (int)ceil(renderBufferFramesFloat);
    double currentError = renderBufferFrames - renderBufferFramesFloat;

    const int limit = numFrames / 4;
    if (samplesError > limit && renderBufferFrames > limit)
    {
        renderBufferFrames -= limit;
        currentError -= limit;
    }
    else if (-samplesError > limit)
    {
        renderBufferFrames += limit;
        currentError += limit;
    }

    // Ninguno de los dos debería alcanzarse, pero un host puede pedir cualquier
    // cosa.
    if (renderBufferFrames < 2)
    {
        stats.tooFewFrames++;
        RD_TRACE("engine: no hay muestras que generar (%d)", renderBufferFrames);
        return 0;
    }
    if (renderBufferFrames > 20000 || renderBufferFrames > emuCapacity)
    {
        stats.tooManyFrames++;
        RD_TRACE("engine: demasiadas muestras que generar (%d > %d)", renderBufferFrames, emuCapacity);
        return 0;
    }

    *blockError = currentError;
    return renderBufferFrames;
}

/**
 * @brief El bucle de síntesis: emulador, los dos efectos con sus rampas y el reparto del MIDI.
 *
 * Corre a `sourceRate` y escribe en `emuL`/`emuR`.
 *
 * @param emuFrames Muestras del emulador a generar.
 * @return Cuántos eventos de la cola se entregaron; los que quedan van más allá del último frame.
 */
int RdPianoEngine::synthesise(int emuFrames)
{
    const bool mode32khz = sourceRate == 32000;

    // `rate` es el incremento de fase del LFO por muestra del EMULADOR, que corre
    // a 20 o a 32 kHz según el parche: en los cinco parches de 32 kHz el chorus y
    // el phaser van 1,6x más rápidos con el mismo ajuste del panel. **Es
    // deliberado**: escalarlo por 20000/sourceRate se probó, se escuchó y se
    // descartó —sonaba peor—. Lo fija `engine_lfo_rate`, que falla si alguien lo
    // "arregla".
    spaceD.rate = spaceDRateFromMs(1000.0f / chorusRateToMsPeriod[clamp_index(params.chorusRate, 0, 14)] / 4.0f);
    spaceD.depth = spaceDDepth(clamp_index(params.chorusDepth, 0, 14) / 15.0f);

    phaser.rate = phaserRateTable[clamp_index((int)(params.efxPhaserRate * 0x7f), 0, 0x7f)];
    phaser.depth = phaserDepthTable[clamp_index((int)(params.efxPhaserDepth * 0x7f), 0, 0x7f)];

    // Reparto del MIDI: un solo recorrido con un índice que avanza en paralelo
    // al bucle de muestras, sin contenedor auxiliar ni búsqueda. `frame` va en
    // muestras del host e `i` en muestras del emulador, así que hay que
    // convertir. La cola llega ordenada; un evento fuera de orden no se pierde,
    // se entrega en el vaciado del final.
    const double hostToEmu = (double)sourceRate / hostRate;
    int nextEvent = 0;

    // `volume` se interpola dentro del bloque: leerlo una vez y saltar de golpe
    // es un escalón por bloque, y un mando movido rápido son decenas por
    // segundo. Al final del bucle se asigna el destino en vez de acumularlo, así
    // que con el mando quieto el paso es exactamente 0 y no hay deriva.
    const float volumeTarget = params.volume;
    const float volumeStep = (volumeTarget - volumeSmoothed) / (float)emuFrames;

    const float chorusTarget = params.chorusEnabled ? 1.0f : 0.0f;
    const float efxTarget = params.efxEnabled ? 1.0f : 0.0f;

    for (int i = 0; i < emuFrames; i++)
    {
        while (nextEvent < midiCount && (int)(midiQueue[nextEvent].frame * hostToEmu) <= i)
        {
            sendTracked(midiQueue[nextEvent].status, midiQueue[nextEvent].data1, midiQueue[nextEvent].data2);
            nextEvent++;
        }

        s32 sample = mcu->generate_next_sample(mode32khz);

        // Nivel de la salida cruda, en potencia: `levelSq` sigue el
        // decaimiento y `onsetSq` se queda con el ataque de la última nota.
        // Su razón es lo que decide con cuánta fuerza reentra un acorde al
        // cambiar de parche.
        const float magnitude = (float)sample;
        levelSq += (magnitude * magnitude - levelSq) * levelSmooth;
        if (onsetHold > 0)
        {
            if (levelSq > onsetSq)
                onsetSq = levelSq;
            onsetHold--;
        }

        // Los dos efectos corren SIEMPRE, encendidos o no: `process()` es lo
        // único que avanza sus líneas de retardo, y saltárselo las dejaba
        // congeladas con el audio de la última vez —que soltaban enteras al
        // reactivar el efecto, un estallido de −11 dBFS salido de la nada—. El
        // bypass es ahora la mezcla, y va en rampa para que tampoco haya salto.
        // Con la mezcla en 0 o en 1 la salida es bit a bit la de antes.
        const s32 dryL = sample << kEmuInputShift;
        const s32 dryR = sample << kEmuInputShift;

        spaceD.audioInL = dryL;
        spaceD.audioInR = dryR;
        spaceD.process();

        chorusMix += clamp_step(chorusTarget - chorusMix, -effectMixStep, effectMixStep);
        if (chorusMix <= 0.0f)
        {
            spaceD.audioOutL = dryL;
            spaceD.audioOutR = dryR;
        }
        else if (chorusMix < 1.0f)
        {
            spaceD.audioOutL = dryL + (s32)((float)(spaceD.audioOutL - dryL) * chorusMix);
            spaceD.audioOutR = dryR + (s32)((float)(spaceD.audioOutR - dryR) * chorusMix);
        }
        spaceD.audioOutL >>= kEmuOutputShift;
        spaceD.audioOutR >>= kEmuOutputShift;

        const s32 phaserDryL = spaceD.audioOutL;
        const s32 phaserDryR = spaceD.audioOutR;
        phaser.audioInL = phaserDryL << kEmuInputShift;
        phaser.audioInR = phaserDryR << kEmuInputShift;
        phaser.process();

        efxMix += clamp_step(efxTarget - efxMix, -effectMixStep, effectMixStep);
        if (efxMix >= 1.0f)
        {
            spaceD.audioOutL = phaser.audioOutL >> kEmuOutputShift;
            spaceD.audioOutR = phaser.audioOutR >> kEmuOutputShift;
        }
        else if (efxMix > 0.0f)
        {
            const s32 wetL = phaser.audioOutL >> kEmuOutputShift;
            const s32 wetR = phaser.audioOutR >> kEmuOutputShift;
            spaceD.audioOutL = phaserDryL + (s32)((float)(wetL - phaserDryL) * efxMix);
            spaceD.audioOutR = phaserDryR + (s32)((float)(wetR - phaserDryR) * efxMix);
        }

        emuL[i] = spaceD.audioOutL * kEmuToFloat * volumeSmoothed;
        emuR[i] = spaceD.audioOutR * kEmuToFloat * volumeSmoothed;
        volumeSmoothed += volumeStep;
    }
    volumeSmoothed = volumeTarget;

    return nextEvent;
}

/**
 * @brief De `sourceRate` a la tasa del host: `emuL`/`emuR` entran, `outL`/`outR` salen.
 * @param emuFrames Muestras del emulador disponibles.
 * @param numFrames Muestras del host a producir.
 * @param blockError Corrección de deriva que devolvió framesForBlock().
 */
void RdPianoEngine::resampleBlock(int emuFrames, int numFrames, double blockError)
{
    // `emuL`/`emuR` no se limpian: el bucle de síntesis les *asigna* las
    // `emuFrames` primeras posiciones y `resample_process()` no lee ninguna más.
    // `outL`/`outR` sí, porque el remuestreador puede devolver menos de
    // `numFrames` y la cola se lee igual.
    for (int i = 0; i < numFrames; i++)
    {
        outL[i] = 0.0f;
        outR[i] = 0.0f;
    }

    const double ratio = hostRate / sourceRate;

    int inUsed = 0;
    [[maybe_unused]] const int out = resample_process(resampleL, ratio, emuL, emuFrames, 0, &inUsed, outL, numFrames);
    resample_process(resampleR, ratio, emuR, emuFrames, 0, &inUsed, outR, numFrames);
    samplesError += blockError;
    if (inUsed == 0)
    {
        samplesError = 0;
        stats.clicks++;
        RD_TRACE("engine: click (%d)", out);
    }
}

/**
 * @brief Lo que va detrás del remuestreador: ganancia de parche, declick, trémolo y EQ.
 * @param left Canal izquierdo de salida.
 * @param right Canal derecho de salida.
 * @param numFrames Muestras del bloque.
 */
void RdPianoEngine::outputStage(float *left, float *right, int numFrames)
{
    // Trémolo: la fase avanza por muestra y se acota a 2 pi, en vez de
    // multiplicar un contador absoluto que acaba en cientos de miles de
    // radianes (y da un salto al desbordar). El canal derecho es el izquierdo
    // en oposición de fase —sen(pi + x) = -sen(x)—, así que sale del mismo
    // sen() sin una segunda llamada.
    const float depth = clamp_index(params.tremoloDepth, 0, 14) / 14.0f;
    const double tremoloHz = clamp_index(params.tremoloRate, 0, 14) / 2.0; // el dial son Hz/2
    const double tremoloStep = kTwoPi * tremoloHz / hostRate;

    // La compensación de headroom entra en la salida, no antes: el emulador y
    // lsp/ son aritmética entera transcrita del hardware y multiplicar dentro
    // movería el golden y los hashes de test_lsp.cpp. Interpolada dentro del
    // bloque, como el volumen: entre el parche más flojo y el más caliente hay
    // 12 dB y saltarlos de golpe es un escalón.
    const float outputGainTarget = kOutputScaling * patchOutputGain[currentPatch];
    const float outputGainStep = (outputGainTarget - outputGainSmoothed) / (float)numFrames;

    // El declick del cambio de parche y del de afinación: la salida baja a cero,
    // se cambia entre bloques (arriba, en serviceRequests) y vuelve a subir. Va
    // aquí, después del remuestreador, para que el cero sea cero de verdad y no
    // quede cola del parche viejo sonando bajo las tablas de onda nuevas.
    const float declickTarget = (declickPatch >= 0 || declickTune != kNoTuneRequest) ? 0.0f : 1.0f;

    for (int i = 0; i < numFrames; i++)
    {
        declickGain += clamp_step(declickTarget - declickGain, -declickDownStep, declickUpStep);

        const float declick = declick_shape(declickGain);

        left[i] = outL[i] * outputGainSmoothed * declick;
        right[i] = outR[i] * outputGainSmoothed * declick;
        outputGainSmoothed += outputGainStep;

        tremoloPhase += tremoloStep;
        if (tremoloPhase >= kTwoPi)
            tremoloPhase -= kTwoPi;
        if (params.tremoloEnabled)
        {
            const float half = 0.5f * (float)sin(tremoloPhase);
            left[i] *= (1.0f - depth) + ((0.5f + half) * depth);
            right[i] *= (1.0f - depth) + ((0.5f - half) * depth);
        }
    }
    outputGainSmoothed = outputGainTarget;

    for (int i = 0; i < numFrames; i++)
        left[i] = eqL.process(left[i]);
    for (int i = 0; i < numFrames; i++)
        right[i] = eqR.process(right[i]);
}

void RdPianoEngine::render(float *left, float *right, int numFrames)
{
    // Cambios de parche y de afinación pedidos desde fuera: aquí, entre
    // bloques, y no en el hilo que los pidió. Es lo que quita el cerrojo.
    serviceRequests();

    if (numFrames <= 0)
    {
        abortBlock(left, right, numFrames);
        return;
    }

    // Los búferes se dimensionaron en prepare(): un host que entregue un bloque
    // mayor que el anunciado no puede escribir fuera.
    if (numFrames > outCapacity)
    {
        stats.blockTooLarge++;
        abortBlock(left, right, numFrames);
        return;
    }

    double blockError = 0;
    const int emuFrames = framesForBlock(numFrames, &blockError);
    if (emuFrames == 0)
    {
        abortBlock(left, right, numFrames);
        return;
    }

    const int eventsSent = synthesise(emuFrames);
    resampleBlock(emuFrames, numFrames, blockError);
    outputStage(left, right, numFrames);

    // Lo que quedó más allá del último frame generado: eventos que el host situó
    // al final del bloque y que la conversión de tasas deja fuera. Se entregan
    // aquí en vez de perderse.
    for (int i = eventsSent; i < midiCount; i++)
        sendTracked(midiQueue[i].status, midiQueue[i].data1, midiQueue[i].data2);

    midiCount = 0;
}
