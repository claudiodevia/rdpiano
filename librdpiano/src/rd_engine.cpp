#include "rd_engine.h"

#include <math.h>

#include "mcu.h"
#include "rd_trace.h"
#include "resample/libresample.h"

// El periodo del chorus por posición del dial, en milisegundos. Es parte de la
// cadena, no de la UI.
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

// El EQ medio, afinado de oído contra un MKS-20. Los coeficientes se calculan
// una vez en prepare().
static const float kMidEqFreq = 350.0f;
static const float kMidEqQ = 0.2f;
static const float kMidEqGainDb = 8.0f;

// El escalado seco: (sample << 5 >> 6) / 65536 * 0.5.
static const int kEmuInputShift = 5;
static const int kEmuOutputShift = 6;
static const float kEmuToFloat = 1.0f / 65536.0f;
static const float kOutputScaling = 0.5f;

static const float kPi = 3.14159265358979323846f;

static inline int clamp_index(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

// ---------------------------------------------------------------- biquad

void RdBiquad::setPeak(double sampleRate, float frequency, float q, float gainFactor)
{
    // juce::dsp::ArrayCoefficients<float>::makePeakFilter, en float, con el
    // mismo orden de operaciones.
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

    // assignImpl: todo dividido por a0.
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

    currentPatch = 0;
    sourceRate = patchSampleRates[0];

    spaceD.reset();
    phaser.reset();
    eqL.reset();
    eqR.reset();
}

RdPianoEngine::~RdPianoEngine()
{
    release();
    delete mcu;
}

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

    // El firmware arranca siempre a 20 kHz, también en los parches de 32 kHz. El
    // harness e2e calienta al ritmo del parche destino: la divergencia (trampa 7
    // de CLAUDE.md) sigue viva porque cerrarla movería el golden.
    mcu->boot(currentMasterTune, false);

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

void RdPianoEngine::prepareRomSetFor(int patch)
{
    if (patch < 0 || patch >= NUM_PATCHES)
        return;

    const int romSet = patchToRomSetId[patch];
    if (romSet == patchToRomSetId[currentPatch] || romSet == preparedRomSet)
        return;

    const RdRomSet &set = romSets[romSet];
    mcu->prepareRomSet(set.ic5, set.ic6, set.ic7);
    preparedRomSet = romSet;
}

void RdPianoEngine::setPatch(int patch)
{
    if (patch < 0 || patch >= NUM_PATCHES)
        return;

    const int romSet = patchToRomSetId[patch];
    if (romSet != patchToRomSetId[currentPatch])
    {
        // No-op si el integrador ya lo preparó fuera del cerrojo.
        prepareRomSetFor(patch);
        mcu->publishRomSet(romSets[romSet].ic18);
        preparedRomSet = -1;
    }

    mcu->selectPatch(patchToOffset[patch]);
    currentPatch = patch;
    mcu->reloadPatch();

    sourceRate = patchSampleRates[patch];
}

void RdPianoEngine::setMasterTune(int16_t tune)
{
    currentMasterTune = tune;
    mcu->setMasterTune(tune);
}

void RdPianoEngine::allNotesOff() { mcu->allNotesOff(); }

void RdPianoEngine::pushMidi(int frame, u8 status, u8 data1, u8 data2)
{
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

void RdPianoEngine::render(float *left, float *right, int numFrames)
{
    if (numFrames <= 0)
    {
        midiCount = 0;
        return;
    }

    // Los búferes se dimensionaron en prepare(): un host que entregue un bloque
    // mayor que el anunciado no puede escribir fuera.
    if (numFrames > outCapacity)
    {
        stats.blockTooLarge++;
        silence(left, right, numFrames);
        midiCount = 0;
        return;
    }

    const double destSampleRate = hostRate;
    const double renderBufferFramesFloat = (double)numFrames / destSampleRate * sourceRate;
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

    // Los dos retornos tempranos limpian la salida: dejarla intacta devolvería
    // al host la entrada o el bloque anterior en vez de silencio. No deberían
    // alcanzarse, pero un host puede pedir cualquier cosa.
    if (renderBufferFrames < 2)
    {
        stats.tooFewFrames++;
        RD_TRACE("engine: no hay muestras que generar (%d)", renderBufferFrames);
        silence(left, right, numFrames);
        midiCount = 0;
        return;
    }
    if (renderBufferFrames > 20000 || renderBufferFrames > emuCapacity)
    {
        stats.tooManyFrames++;
        RD_TRACE("engine: demasiadas muestras que generar (%d > %d)", renderBufferFrames, emuCapacity);
        silence(left, right, numFrames);
        midiCount = 0;
        return;
    }

    for (int i = 0; i < emuCapacity; i++)
    {
        emuL[i] = 0.0f;
        emuR[i] = 0.0f;
    }
    for (int i = 0; i < numFrames; i++)
    {
        outL[i] = 0.0f;
        outR[i] = 0.0f;
    }

    const bool mode32khz = sourceRate == 32000;

    spaceD.rate = spaceDRateFromMs(1000.0f / chorusRateToMsPeriod[clamp_index(params.chorusRate, 0, 14)] / 4.0f);
    spaceD.depth = spaceDDepth(clamp_index(params.chorusDepth, 0, 14) / 15.0f);

    phaser.rate = phaserRateTable[clamp_index((int)(params.efxPhaserRate * 0x7f), 0, 0x7f)];
    phaser.depth = phaserDepthTable[clamp_index((int)(params.efxPhaserDepth * 0x7f), 0, 0x7f)];

    // Reparto del MIDI: un solo recorrido con un índice que avanza en paralelo
    // al bucle de muestras, sin contenedor auxiliar ni búsqueda. `frame` va en
    // muestras del host e `i` en muestras del emulador, así que hay que
    // convertir. La cola llega ordenada; un evento fuera de orden no se pierde,
    // se entrega en el vaciado del final.
    const double hostToEmu = (double)sourceRate / destSampleRate;
    int nextEvent = 0;

    for (int i = 0; i < renderBufferFrames; i++)
    {
        while (nextEvent < midiCount && (int)(midiQueue[nextEvent].frame * hostToEmu) <= i)
        {
            mcu->sendMidiCmd(midiQueue[nextEvent].status, midiQueue[nextEvent].data1, midiQueue[nextEvent].data2);
            nextEvent++;
        }

        s32 sample = mcu->generate_next_sample(mode32khz);

        spaceD.audioInL = sample << kEmuInputShift;
        spaceD.audioInR = sample << kEmuInputShift;
        if (params.chorusEnabled)
        {
            spaceD.process();
        }
        else
        {
            spaceD.audioOutL = spaceD.audioInL;
            spaceD.audioOutR = spaceD.audioInR;
        }
        spaceD.audioOutL >>= kEmuOutputShift;
        spaceD.audioOutR >>= kEmuOutputShift;

        if (params.efxEnabled)
        {
            phaser.audioInL = spaceD.audioOutL << kEmuInputShift;
            phaser.audioInR = spaceD.audioOutR << kEmuInputShift;
            phaser.process();
            spaceD.audioOutL = phaser.audioOutL >> kEmuOutputShift;
            spaceD.audioOutR = phaser.audioOutR >> kEmuOutputShift;
        }

        emuL[i] = spaceD.audioOutL * kEmuToFloat * params.volume;
        emuR[i] = spaceD.audioOutR * kEmuToFloat * params.volume;
    }

    const double ratio = destSampleRate / sourceRate;

    int inUsed = 0;
    int out = resample_process(resampleL, ratio, emuL, renderBufferFrames, 0, &inUsed, outL, numFrames);
    resample_process(resampleR, ratio, emuR, renderBufferFrames, 0, &inUsed, outR, numFrames);
    samplesError += currentError;
    if (inUsed == 0)
    {
        samplesError = 0;
        stats.clicks++;
        RD_TRACE("engine: click (%d)", out);
    }

    const float depth = clamp_index(params.tremoloDepth, 0, 14) / 14.0f;
    const int tremRate = clamp_index(params.tremoloRate, 0, 14);

    // La compensación de headroom entra en la salida, no antes: el emulador y
    // lsp/ son aritmética entera transcrita del hardware y multiplicar dentro
    // movería el golden y los hashes de test_lsp.cpp.
    const float outputGain = kOutputScaling * patchOutputGain[currentPatch];

    for (int i = 0; i < numFrames; i++)
    {
        left[i] = outL[i] * outputGain;
        right[i] = outR[i] * outputGain;

        tremoloPhase = (tremoloPhase + 1) & 0xffffffff;
        if (params.tremoloEnabled)
        {
            float tremoloL = (float)(0.5 + 0.5 * sin(tremRate * 3.14159265359 * tremoloPhase / destSampleRate));
            float tremoloR =
                (float)(0.5 + 0.5 * sin(3.1415926535 + tremRate * 3.14159265359 * tremoloPhase / destSampleRate));
            left[i] *= (1.0f - depth) + (tremoloL * depth);
            right[i] *= (1.0f - depth) + (tremoloR * depth);
        }
    }

    for (int i = 0; i < numFrames; i++)
        left[i] = eqL.process(left[i]);
    for (int i = 0; i < numFrames; i++)
        right[i] = eqR.process(right[i]);

    // Lo que quedó más allá del último frame generado: eventos que el host situó
    // al final del bloque y que la conversión de tasas deja fuera. Se entregan
    // aquí en vez de perderse.
    for (; nextEvent < midiCount; nextEvent++)
        mcu->sendMidiCmd(midiQueue[nextEvent].status, midiQueue[nextEvent].data1, midiQueue[nextEvent].data2);

    midiCount = 0;
}
