#ifndef RD_ENGINE_H
#define RD_ENGINE_H

/**
 * @file rd_engine.h
 * @brief La cadena de audio entera, como clase de C++ puro y sin JUCE.
 *
 * Emulador, chorus, phaser, trémolo, EQ, remuestreo y reparto del MIDI.
 *
 * Contrato de tiempo real:
 *   - prepare() reserva TODO. render() no reserva, no bloquea y no imprime.
 *   - Cambiar de parche o de afinación desde otro hilo se hace con
 *     requestPatch()/requestMasterTune(): publican la petición y la atiende
 *     render(), con una rampa alrededor. No hace falta cerrojo ninguno.
 *   - setPatch(), setMasterTune() y allNotesOff() corren el emulador en el acto:
 *     son para la puesta en marcha y las pruebas, no para tocar en vivo mientras
 *     render() está en marcha en otro hilo.
 */

#include <atomic>

#include "lsp/phaser.h"
#include "lsp/spaced.h"
#include "mame_utils.h"
#include "patches.h"

class Mcu;

/** @brief Los cuatro chips de un juego de ROM, ya en memoria. El motor solo guarda los punteros. */
struct RdRomSet
{
    const u8 *ic5 = nullptr;  ///< ROM de onda IC5.
    const u8 *ic6 = nullptr;  ///< ROM de onda IC6.
    const u8 *ic7 = nullptr;  ///< ROM de onda IC7.
    const u8 *ic18 = nullptr; ///< ROM de parámetros.
};

/**
 * @brief Parámetros del motor. POD sin lógica: el hilo de UI escribe, render() lee.
 *
 * Los valores por defecto son los de fábrica del plugin.
 */
struct RdEngineParams
{
    float volume = 1.0f; ///< Ganancia de salida, 0..1.

    bool chorusEnabled = true;
    int chorusRate = 5;   ///< 0..14, índice de chorusRateToMsPeriod.
    int chorusDepth = 14; ///< 0..14.

    bool tremoloEnabled = false;
    int tremoloRate = 6;  ///< 0..14, en Hz/2 del reloj del host.
    int tremoloDepth = 6; ///< 0..14.

    bool efxEnabled = false;
    float efxPhaserRate = 0.4f;  ///< 0..1.
    float efxPhaserDepth = 0.8f; ///< 0..1.
};

/** @brief Un evento MIDI con su posición dentro del bloque, en muestras del host. */
struct RdMidiEvent
{
    int frame = 0; ///< Posición dentro del bloque.
    u8 status = 0;
    u8 data1 = 0;
    u8 data2 = 0;
};

/**
 * @brief Biquad en forma directa II traspuesta: el EQ medio.
 *
 * Replica coeficientes y orden de operaciones de `juce::dsp::IIR::Filter<float>`,
 * salvo su `snapToZero`, que fuera de Intel es un no-op.
 */
struct RdBiquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float s1 = 0.0f, s2 = 0.0f;

    /** @brief Vacía el estado del filtro; los coeficientes no se tocan. */
    void reset()
    {
        s1 = 0.0f;
        s2 = 0.0f;
    }

    /**
     * @brief Coeficientes de `Coefficients<float>::makePeakFilter`, normalizados por a0.
     * @param sampleRate Tasa a la que va a correr el filtro.
     * @param frequency Centro de la campana, en Hz.
     * @param q Factor de calidad.
     * @param gainFactor Ganancia lineal en el centro.
     */
    void setPeak(double sampleRate, float frequency, float q, float gainFactor);

    /**
     * @brief Filtra una muestra.
     * @param in Muestra de entrada.
     * @return La muestra filtrada.
     */
    inline float process(float in)
    {
        float out = (in * b0) + s1;
        s1 = (in * b1) - (out * a1) + s2;
        s2 = (in * b2) - (out * a2);
        return out;
    }
};

/**
 * @brief Contadores de diagnóstico.
 *
 * El núcleo no imprime desde el hilo de audio, así que lo que iría a un log es
 * un contador que la interfaz lee cuando quiere.
 */
struct RdEngineStats
{
    unsigned long tooFewFrames = 0;  ///< El bloque pedía menos de 2 muestras.
    unsigned long tooManyFrames = 0; ///< No cabían en el búfer del emulador.
    unsigned long blockTooLarge = 0; ///< numFrames por encima del preparado.
    unsigned long clicks = 0;        ///< El remuestreador no consumió nada.
    unsigned long midiDropped = 0;   ///< La cola de eventos se llenó.

    /// resample_open() reserva ~600 KB y calcula un filtro Kaiser: 2,5 ms por
    /// handle, así que fuera de prepare() no se puede mover. test_engine lo
    /// vigila porque libresample usa malloc y `operator new` no lo vería.
    unsigned long resamplerOpens = 0;
};

/** @brief El motor: emulador, efectos, remuestreo y reparto del MIDI. La frontera con el plugin. */
class RdPianoEngine
{
  public:
    /**
     * @brief Construye el motor y descifra las ROM (~9 ms, 2 MB).
     * @param romSets Array de ROMSET_COUNT entradas, indexado por RomSetId. Ni él ni las ROM se copian.
     * @param programRom ROM de programa (firmware) sin descifrar.
     */
    RdPianoEngine(const RdRomSet *romSets, const u8 *programRom);
    ~RdPianoEngine();

    RdPianoEngine(const RdPianoEngine &) = delete;
    RdPianoEngine &operator=(const RdPianoEngine &) = delete;
    RdPianoEngine(RdPianoEngine &&) = delete;
    RdPianoEngine &operator=(RdPianoEngine &&) = delete;

    /**
     * @brief Reserva todo lo que render() va a necesitar y arranca el firmware.
     *
     * Idempotente: llamarla dos veces seguidas no fuga nada.
     *
     * @param hostSampleRate Tasa del anfitrión, en Hz.
     * @param maxBlockSize Bloque más grande que va a pedir el anfitrión.
     */
    void prepare(double hostSampleRate, int maxBlockSize);

    /** @brief Libera los búferes. prepare() la puede volver a llamar. */
    void release();

    /**
     * @brief No hace nada.
     *
     * Los tres juegos de ROM se descifran al construir y las 16 páginas de
     * parámetros también, así que ya no hay parte cara que adelantar. Se mantiene
     * para que un integrador antiguo siga compilando.
     *
     * @param patch Parche que se iba a preparar; se ignora.
     */
    void prepareRomSetFor(int patch);

    /**
     * @brief Cambia de parche en el acto, corriendo el emulador.
     *
     * Sólo reactiva el juego de ROM si cambia de verdad; dentro del mismo juego
     * es mapear otra página ya descifrada. No usar con render() vivo en otro
     * hilo: para eso está requestPatch().
     *
     * @param patch Parche, 0..NUM_PATCHES-1.
     */
    void setPatch(int patch);

    /**
     * @brief El parche que el motor tiene puesto o va a poner.
     * @return El de la petición sin atender si la hay; es lo que la interfaz debe enseñar.
     */
    int patch() const { return latestPatch.load(std::memory_order_relaxed); }

    /** @brief El parche que está sonando ahora mismo, con la petición ya atendida. */
    int activePatch() const { return currentPatch; }

    /**
     * @brief Pide un cambio de parche desde cualquier hilo.
     *
     * La petición la atiende render(): baja la salida a cero en 6 ms, cambia y
     * vuelve a subir. Las repeticiones se colapsan, así que barrer el dial no
     * encadena cambios. RT-safe: un `exchange` y nada más.
     *
     * @param patch Parche, 0..NUM_PATCHES-1.
     */
    void requestPatch(int patch);

    /**
     * @brief Pide un cambio de afinación desde cualquier hilo.
     * @param tune Desviación con signo en el rango de un int16.
     */
    void requestMasterTune(int16_t tune);

    /**
     * @brief Afina en el acto, corriendo el emulador (~0,16 ms).
     * @param tune Desviación con signo en el rango de un int16.
     */
    void setMasterTune(int16_t tune);

    /**
     * @brief La afinación puesta o pedida, igual que patch().
     * @return Lo que la interfaz debe enseñar aunque render() no haya atendido la petición.
     */
    int16_t masterTune() const { return (int16_t)latestTune.load(std::memory_order_relaxed); }

    /**
     * @brief Retardo de grupo del remuestreador, para declarárselo al anfitrión.
     *
     * Constante desde prepare(): es el peor caso (parche de 20 kHz), no el del
     * parche puesto, para no obligar al anfitrión a renegociar la latencia en
     * cada cambio de sonido.
     *
     * @return La latencia en muestras del host.
     */
    int latencySamples() const { return latencyFrames; }

    /// Peor caso de los 16 parches con margen; lo mide `engine_tail_length`.
    static constexpr double kTailSeconds = 3.0;

    /**
     * @brief Cuánto sigue sonando después del último note-off, para declarárselo al anfitrión.
     *
     * Con cero, el host se cree que puede dejar de pedir bloques al soltar la
     * tecla y corta el final de la nota al exportar o al congelar la pista.
     *
     * @return La cola declarada, en segundos.
     */
    double tailLengthSeconds() const { return kTailSeconds; }

    /** @brief Pánico: pedal arriba y las 128 notas apagadas. Corre el emulador. */
    void allNotesOff();

    /**
     * @brief Encola un evento MIDI para el bloque que viene. RT-safe: cola fija, sin reservas.
     * @param frame Posición dentro del bloque, en muestras del host.
     * @param status Byte de estado MIDI.
     * @param data1 Primer byte de datos.
     * @param data2 Segundo byte de datos.
     */
    void pushMidi(int frame, u8 status, u8 data1, u8 data2);

    /**
     * @brief Genera un bloque estéreo. Sin reservas, sin cerrojos, sin stdio.
     * @param left Canal izquierdo, numFrames muestras.
     * @param right Canal derecho, numFrames muestras.
     * @param numFrames Muestras a generar, hasta el maxBlockSize preparado.
     */
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

    /// Abiertos en prepare() para todo el rango de factores de esta tasa de host
    /// y cerrados en release(): render() no los toca.
    void *resampleL = nullptr;
    void *resampleR = nullptr;

    double hostRate = 0;
    int maxBlock = 0;
    int latencyFrames = 0;
    int sourceRate = 20000;
    int currentPatch = 0;
    int16_t currentMasterTune = 0;

    /// Las 16 páginas de parámetros ya descifradas (32 KB cada una), en el orden
    /// de los parches: cambiar de parche es copiar una, no descifrarla.
    u8 *paramPages = nullptr;
    const u8 *paramPage(int patch) const;

    void applyPatch(int patch);
    void serviceRequests();

    /// Espejo de lo que el firmware cree pulsado, y su reenvío tras un cambio de
    /// parche: el program change que hace falta para releer la página de
    /// parámetros apaga las voces y suelta el pedal dentro del firmware.
    void trackMidi(u8 status, u8 data1, u8 data2);
    void sendTracked(u8 status, u8 data1, u8 data2);
    int restoreHeldNotes();

    /// Las fases de `render()`, en el orden en que corren. El orden de
    /// operaciones dentro de cada una es audio: no se toca.
    void abortBlock(float *left, float *right, int numFrames);
    int framesForBlock(int numFrames, double *blockError);
    int synthesise(int emuFrames);
    void resampleBlock(int emuFrames, int numFrames, double blockError);
    void outputStage(float *left, float *right, int numFrames);

    /// Peticiones pendientes. -1 y kNoTuneRequest = no hay nada que atender.
    static const int kNoTuneRequest = 0x7fffffff;
    std::atomic<int> patchRequest{-1};
    std::atomic<int> tuneRequest{kNoTuneRequest};
    std::atomic<int> latestPatch{0};
    std::atomic<int> latestTune{0};

    /// Declick del cambio de parche: la salida baja a cero antes de cambiar y
    /// sube después. `declickPatch` es el parche que espera a que la rampa toque
    /// fondo; el cambio se aplica entre bloques, nunca a mitad de uno, porque la
    /// tasa del emulador cambia con él.
    float declickGain = 1.0f;
    float declickDownStep = 1.0f;
    float declickUpStep = 1.0f;
    int declickPatch = -1;

    /// Mezcla de los dos efectos: 0 = seco, 1 = efecto. Se mueve en rampa, y
    /// `process()` corre siempre —también en bypass— para que la línea de retardo
    /// no se congele y suelte lo que tenía dentro al reactivarla.
    float chorusMix = 0.0f;
    float efxMix = 0.0f;
    float effectMixStep = 1.0f;

    /// Ganancias interpoladas dentro del bloque: sin esto, mover el volumen es un
    /// escalón por bloque (zíper) y cambiar de parche, un salto de hasta 12 dB.
    float volumeSmoothed = 1.0f;
    float outputGainSmoothed = 0.0f;

    /// Búferes del emulador (a `sourceRate`) y su salida remuestreada (a
    /// `hostRate`). Se reservan en `prepare()` y en ningún otro sitio.
    float *emuL = nullptr;
    float *emuR = nullptr;
    float *outL = nullptr;
    float *outR = nullptr;
    int emuCapacity = 0;
    int outCapacity = 0;

    double samplesError = 0;
    double tremoloPhase = 0;

    /// Velocidad de cada nota pulsada (0 = suelta) y el pedal, tal y como los
    /// recibió el firmware. Es lo que se le devuelve tras cambiar de parche.
    u8 heldVelocity[128] = {0};
    bool sustainDown = false;

    /// Seguidores de la salida del emulador, en potencia (RMS al cuadrado), con
    /// los que se decide con cuánta fuerza reentra lo que se estaba tocando:
    /// `onsetSq` es el nivel del último ataque y `levelSq` el de ahora, así que
    /// su razón es cuánto ha decaído la nota. Sin esto reentraba con su ataque
    /// entero, hasta +14 dB sobre lo que sonaba: el golpe de tecla que no debe
    /// oírse al cambiar de sonido.
    float levelSq = 0.0f;
    float onsetSq = 0.0f;
    float levelSmooth = 0.0f;
    int onsetHold = 0;

    static const int kMidiQueueSize = 512;
    RdMidiEvent midiQueue[kMidiQueueSize];
    int midiCount = 0;
};

#endif
