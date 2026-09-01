#ifndef RD_ENGINE_H
#define RD_ENGINE_H

// El motor de audio completo, sin JUCE (REFACTORIZACION §1).
//
// Hasta la fase 2 toda la cadena —escalados, chorus, phaser, trémolo, EQ,
// resampling y reparto temporal del MIDI— vivía dentro de `processBlock`,
// mezclada con `juce::AudioBuffer`. Consecuencia: la mitad del riesgo real del
// producto no era alcanzable desde ninguna prueba, y el "simulador de host"
// que pedía FIABILIDAD §17.1 sólo se podía escribir copiando 130 líneas.
//
// Aquí está esa cadena entera como una clase de C++ puro. `processBlock` queda
// en volcar el MIDI a `pushMidi()` y llamar a `render()`; `test_engine.cpp` la
// instancia en vez de copiarla.
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

// Los cuatro chips de un juego de ROM, ya en memoria. El motor guarda los
// punteros: tienen que sobrevivirle (BinaryData en el plugin, ficheros
// mapeados en las pruebas).
struct RdRomSet
{
    const u8 *ic5 = nullptr;
    const u8 *ic6 = nullptr;
    const u8 *ic7 = nullptr;
    const u8 *ic18 = nullptr;
};

// Parámetros del motor. POD y sin lógica: el hilo de UI escribe, `render()`
// lee. Los valores por defecto son los del plugin.
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

// Biquad en forma directa II traspuesta, con los mismos coeficientes y el
// mismo orden de operaciones que `juce::dsp::IIR::Filter<float>`: el EQ medio
// del plugin tiene que sonar igual después de salir de JUCE.
//
// Lo único que no se replica es el `snapToZero` de JUCE, que sólo existe en
// x86 (`JUCE_SNAP_TO_ZERO` es un no-op fuera de Intel) y que en un build ARM
// —el objetivo del plugin— tampoco se ejecutaba.
struct RdBiquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float s1 = 0.0f, s2 = 0.0f;

    void reset()
    {
        s1 = 0.0f;
        s2 = 0.0f;
    }

    // Coeficientes de `Coefficients<float>::makePeakFilter`, normalizados por a0
    // como hace `assignImpl`.
    void setPeak(double sampleRate, float frequency, float q, float gainFactor);

    inline float process(float in)
    {
        float out = (in * b0) + s1;
        s1 = (in * b1) - (out * a1) + s2;
        s2 = (in * b2) - (out * a2);
        return out;
    }
};

// Contadores de diagnóstico. El núcleo no imprime desde el hilo de audio
// (AUDITORIA §7): lo que antes era un `printf` por bloque es un contador que
// la UI puede leer cuando quiera.
struct RdEngineStats
{
    unsigned long tooFewFrames = 0;  // el bloque pedía menos de 2 muestras
    unsigned long tooManyFrames = 0; // no cabían en el búfer del emulador
    unsigned long blockTooLarge = 0; // numFrames por encima del preparado
    unsigned long clicks = 0;        // el resampler no consumió nada
    unsigned long midiDropped = 0;   // la cola de eventos se llenó

    // resample_open() reserva ~600 KB con malloc y calcula un filtro Kaiser de
    // ~70.000 coeficientes: 2,5 ms por handle (AUDITORIA §2). Fuera de prepare()
    // este contador no se puede mover, y test_engine lo vigila — es la mitad de
    // la prueba "sin reservas en RT" que un contador de `operator new` no ve,
    // porque libresample usa malloc.
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

    // Cambia de parche. Sólo recarga el juego de ROM si cambia de verdad; dentro
    // del mismo juego es remapear una página (REFACTORIZACION §6).
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
    void reloadRomsFor(int patch);

    const RdRomSet *romSets = nullptr;
    const u8 *programRom = nullptr;

    Mcu *mcu = nullptr;

    SpaceD spaceD;
    Phaser phaser;
    RdBiquad eqL;
    RdBiquad eqR;

    // Abiertos en prepare() para todo el rango de factores de esta tasa de host
    // y cerrados en release(): render() no los toca (AUDITORIA §2 y §9).
    void *resampleL = nullptr;
    void *resampleR = nullptr;

    double hostRate = 0;
    int maxBlock = 0;
    int sourceRate = 20000;
    int currentPatch = 0;
    int16_t currentMasterTune = 0;

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
