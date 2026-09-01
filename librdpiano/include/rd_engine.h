#ifndef RD_ENGINE_H
#define RD_ENGINE_H

// La cadena de audio entera —emulador, chorus, phaser, trémolo, EQ, resampling
// y reparto del MIDI— como clase de C++ puro, sin JUCE.
//
// Contrato de tiempo real:
//   - `prepare()` reserva TODO. `render()` no reserva, no bloquea y no imprime.
//   - `setPatch()`, `setMasterTune()` y `allNotesOff()` corren el emulador: los
//     llama el hilo de UI y el integrador tiene que serializarlos con
//     `render()` (en el plugin, `mcuLock`).

#include "lsp/phaser.h"
#include "lsp/spaced.h"
#include "mame_utils.h"
#include "patches.h"

class Mcu;

// Los cuatro chips de un juego de ROM, ya en memoria. El motor solo guarda los
// punteros: tienen que sobrevivirle.
struct RdRomSet
{
    const u8 *ic5 = nullptr;
    const u8 *ic6 = nullptr;
    const u8 *ic7 = nullptr;
    const u8 *ic18 = nullptr;
};

// Parámetros del motor. POD sin lógica: el hilo de UI escribe, `render()` lee.
// Los valores por defecto son los de fábrica del plugin.
struct RdEngineParams
{
    float volume = 1.0f;

    bool chorusEnabled = true;
    int chorusRate = 5;   // 0..14, índice de chorusRateToMsPeriod
    int chorusDepth = 14; // 0..14

    bool tremoloEnabled = false;
    int tremoloRate = 6;  // 0..14, en Hz/2
    int tremoloDepth = 6; // 0..14

    bool efxEnabled = false;
    float efxPhaserRate = 0.4f;  // 0..1
    float efxPhaserDepth = 0.8f; // 0..1
};

// Un evento MIDI con su posición dentro del bloque, en muestras del host.
struct RdMidiEvent
{
    int frame = 0;
    u8 status = 0;
    u8 data1 = 0;
    u8 data2 = 0;
};

// Biquad en forma directa II traspuesta (el EQ medio). Replica coeficientes y
// orden de operaciones de `juce::dsp::IIR::Filter<float>`, salvo su
// `snapToZero`, que fuera de Intel es un no-op.
struct RdBiquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float s1 = 0.0f, s2 = 0.0f;

    void reset()
    {
        s1 = 0.0f;
        s2 = 0.0f;
    }

    // Coeficientes de `Coefficients<float>::makePeakFilter`, normalizados por a0.
    void setPeak(double sampleRate, float frequency, float q, float gainFactor);

    inline float process(float in)
    {
        float out = (in * b0) + s1;
        s1 = (in * b1) - (out * a1) + s2;
        s2 = (in * b2) - (out * a2);
        return out;
    }
};

// Contadores de diagnóstico: el núcleo no imprime desde el hilo de audio, así
// que lo que iría a un log es un contador que la UI lee cuando quiere.
struct RdEngineStats
{
    unsigned long tooFewFrames = 0;  // el bloque pedía menos de 2 muestras
    unsigned long tooManyFrames = 0; // no cabían en el búfer del emulador
    unsigned long blockTooLarge = 0; // numFrames por encima del preparado
    unsigned long clicks = 0;        // el resampler no consumió nada
    unsigned long midiDropped = 0;   // la cola de eventos se llenó

    // resample_open() reserva ~600 KB y calcula un filtro Kaiser: 2,5 ms por
    // handle, así que fuera de prepare() no se puede mover. test_engine lo
    // vigila porque libresample usa malloc y `operator new` no lo vería.
    unsigned long resamplerOpens = 0;
};

class RdPianoEngine
{
  public:
    // `romSets` es un array de ROMSET_COUNT entradas, indexado por RomSetId.
    // Ni él ni las ROMs se copian.
    RdPianoEngine(const RdRomSet *romSets, const u8 *programRom);
    ~RdPianoEngine();

    RdPianoEngine(const RdPianoEngine &) = delete;
    RdPianoEngine &operator=(const RdPianoEngine &) = delete;
    RdPianoEngine(RdPianoEngine &&) = delete;
    RdPianoEngine &operator=(RdPianoEngine &&) = delete;

    // Reserva todo lo que `render()` va a necesitar y arranca el firmware.
    // Idempotente: llamarla dos veces seguidas no fuga nada.
    void prepare(double hostSampleRate, int maxBlockSize);

    // Libera los búferes. `prepare()` la puede volver a llamar.
    void release();

    // La parte cara de `setPatch()`: descifrar las tres ROM de onda del juego
    // del parche, ~2,9 ms. Escribe en el juego de reserva del chip, que
    // `render()` no lee, así que va FUERA del cerrojo del integrador; después
    // `setPatch()` sólo tiene que publicarlo (~0,03 ms). Es opcional: si nadie
    // la llamó, `setPatch()` hace el descifrado por su cuenta.
    void prepareRomSetFor(int patch);

    // Cambia de parche. Sólo recarga el juego de ROM si cambia de verdad; dentro
    // del mismo juego es remapear una página.
    void setPatch(int patch);
    int patch() const { return currentPatch; }

    void setMasterTune(int16_t tune);
    int16_t masterTune() const { return currentMasterTune; }

    // Pánico: pedal arriba y las 128 notas apagadas.
    void allNotesOff();

    // Encola un evento para el bloque que viene. `frame` es su posición dentro
    // del bloque, en muestras del host. RT-safe: cola fija, sin reservas.
    void pushMidi(int frame, u8 status, u8 data1, u8 data2);

    // Genera `numFrames` muestras estéreo. Sin reservas, sin locks, sin stdio.
    void render(float *left, float *right, int numFrames);

    double hostSampleRate() const { return hostRate; }
    int sourceSampleRate() const { return sourceRate; }
    int preparedBlockSize() const { return maxBlock; }

    RdEngineParams params;
    RdEngineStats stats;

  private:
    const RdRomSet *romSets = nullptr;
    const u8 *programRom = nullptr;

    Mcu *mcu = nullptr;

    SpaceD spaceD;
    Phaser phaser;
    RdBiquad eqL;
    RdBiquad eqR;

    // Abiertos en prepare() para todo el rango de factores de esta tasa de host
    // y cerrados en release(): render() no los toca.
    void *resampleL = nullptr;
    void *resampleR = nullptr;

    double hostRate = 0;
    int maxBlock = 0;
    int sourceRate = 20000;
    int currentPatch = 0;
    int16_t currentMasterTune = 0;

    // Juego de ROM que `prepareRomSetFor()` dejó descifrado y sin publicar, o
    // -1 si no hay nada esperando.
    int preparedRomSet = -1;

    // Búferes del emulador (a `sourceRate`) y su salida remuestreada (a
    // `hostRate`). Se reservan en `prepare()` y en ningún otro sitio.
    float *emuL = nullptr;
    float *emuR = nullptr;
    float *outL = nullptr;
    float *outR = nullptr;
    int emuCapacity = 0;
    int outCapacity = 0;

    double samplesError = 0;
    unsigned long tremoloPhase = 0;

    static const int kMidiQueueSize = 512;
    RdMidiEvent midiQueue[kMidiQueueSize];
    int midiCount = 0;
};

#endif
