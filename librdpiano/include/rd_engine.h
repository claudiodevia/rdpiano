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
 *   - Lo demás (params, pushMidi(), los getters de lo que suena) es del hilo de
 *     render(), o de cualquiera con el motor parado.
 */

#include <atomic>

#include "mame_utils.h"
#include "patches.h"

/** @brief Los cuatro chips de un juego de ROM, ya en memoria. El motor solo guarda los punteros. */
struct RdRomSet
{
    const u8 *ic5 = nullptr;  ///< ROM de onda IC5.
    const u8 *ic6 = nullptr;  ///< ROM de onda IC6.
    const u8 *ic7 = nullptr;  ///< ROM de onda IC7.
    const u8 *ic18 = nullptr; ///< ROM de parámetros.
};

/**
 * @brief Parámetros del motor. POD sin lógica.
 *
 * Se escribe en el hilo de render(), entre bloques; render() lo lee al empezar
 * cada uno. Los valores por defecto son los de fábrica del plugin.
 */
struct RdEngineParams
{
    float volume = 1.0f; ///< Ganancia de salida, 0..1.

    bool chorusEnabled = true; ///< Chorus encendido.
    int chorusRate = 5;        ///< Velocidad del LFO, posición del dial 0..14.
    int chorusDepth = 14;      ///< 0..14.

    bool tremoloEnabled = false; ///< Trémolo encendido.
    int tremoloRate = 6;         ///< 0..14; Hz = valor / 2, del reloj del host.
    int tremoloDepth = 6;        ///< 0..14.

    bool efxEnabled = false;     ///< Phaser encendido.
    float efxPhaserRate = 0.4f;  ///< 0..1.
    float efxPhaserDepth = 0.8f; ///< 0..1.
};

/**
 * @brief Contadores de diagnóstico.
 *
 * El núcleo no imprime desde el hilo de audio, así que lo que iría a un log es
 * un contador que la interfaz lee cuando quiere. Los escribe el hilo de
 * render() y se leen desde cualquiera.
 */
struct RdEngineStats
{
    std::atomic<unsigned long> tooFewFrames{0};   ///< El bloque pedía menos de 2 muestras.
    std::atomic<unsigned long> tooManyFrames{0};  ///< No cabían en el búfer del emulador.
    std::atomic<unsigned long> blockTooLarge{0};  ///< numFrames por encima del preparado.
    std::atomic<unsigned long> clicks{0};         ///< El remuestreador no consumió nada.
    std::atomic<unsigned long> midiDropped{0};    ///< La cola de eventos se llenó.
    std::atomic<unsigned long> programIgnored{0}; ///< Program change fuera de los 16 parches.

    /// resample_open() con éxito. Solo prepare() abre: libresample reserva con
    /// malloc, que `operator new` no ve, así que esto es lo que vigilan las pruebas.
    std::atomic<unsigned long> resamplerOpens{0};
};

/** @brief El motor: emulador, efectos, remuestreo y reparto del MIDI. La frontera con el plugin. */
class RdPianoEngine
{
  public:
    /**
     * @brief Construye el motor y descifra las ROM (~6 ms, 2 MB).
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
     * Idempotente: llamarla dos veces seguidas no fuga nada. Un cambio pedido y
     * sin atender se aplica antes de arrancar.
     *
     * @param hostSampleRate Tasa del anfitrión, en Hz.
     * @param maxBlockSize Bloque más grande que va a pedir el anfitrión.
     */
    void prepare(double hostSampleRate, int maxBlockSize);

    /** @brief Libera los búferes. prepare() la puede volver a llamar. */
    void release();

    /**
     * @brief Cambia de parche en el acto, corriendo el emulador.
     *
     * Manda sobre cualquier petición sin atender. No usar con render() vivo en
     * otro hilo: para eso está requestPatch().
     *
     * @param patch Parche, 0..NUM_PATCHES-1; fuera de rango no hace nada.
     */
    void setPatch(int patch);

    /**
     * @brief El parche que el motor tiene puesto o va a poner. Cualquier hilo.
     * @return El de la petición sin atender si la hay; es lo que la interfaz debe enseñar.
     */
    int patch() const;

    /** @brief El parche que está sonando, con la petición ya atendida. Hilo de render() o motor parado. */
    int activePatch() const;

    /**
     * @brief Pide un cambio de parche desde cualquier hilo. RT-safe: un `store` atómico.
     *
     * La atiende render(): el primero sale en el acto y, de los que lleguen en
     * los 150 ms siguientes, solo el último. La salida baja a cero en 6 ms,
     * cambia y vuelve a subir.
     *
     * @param patch Parche, 0..NUM_PATCHES-1; fuera de rango no hace nada.
     */
    void requestPatch(int patch);

    /**
     * @brief Pide un cambio de afinación desde cualquier hilo. RT-safe.
     *
     * La atiende render() igual que la del parche: afinar apaga las voces del
     * firmware, así que va con declick y devolviéndole después las teclas y el
     * pedal que siguieran pulsados.
     *
     * @param tune Desviación con signo en el rango de un int16.
     */
    void requestMasterTune(int16_t tune);

    /**
     * @brief Afina en el acto, corriendo el emulador (~0,16 ms).
     *
     * Sin declick: sólo arranque y pruebas, nunca con render() vivo en otro hilo.
     *
     * @param tune Desviación con signo en el rango de un int16.
     */
    void setMasterTune(int16_t tune);

    /**
     * @brief La afinación puesta o pedida, igual que patch(). Cualquier hilo.
     * @return Lo que la interfaz debe enseñar aunque render() no haya atendido la petición.
     */
    int16_t masterTune() const;

    /** @brief La afinación que está sonando. Hilo de render() o motor parado. */
    int16_t activeMasterTune() const;

    /**
     * @brief Retardo de grupo del remuestreador, para declarárselo al anfitrión.
     *
     * Constante desde prepare(): es el peor caso (parche de 20 kHz), no el del
     * parche puesto, para no obligar al anfitrión a renegociar la latencia en
     * cada cambio de sonido.
     *
     * @return La latencia en muestras del host.
     */
    int latencySamples() const;

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
     * @brief Encola un evento MIDI para el bloque que viene: cola fija, sin reservas.
     *
     * Del hilo de render(), antes de render(). El program change no se encola:
     * es un cambio de parche completo, así que se convierte en requestPatch().
     * El número de programa es el parche, 0..15; uno mayor se ignora.
     *
     * @param frame Posición dentro del bloque, en muestras del host.
     * @param status Byte de estado MIDI.
     * @param data1 Primer byte de datos.
     * @param data2 Segundo byte de datos.
     */
    void pushMidi(int frame, u8 status, u8 data1, u8 data2);

    /**
     * @brief Genera un bloque estéreo. Sin reservas, sin cerrojos, sin stdio.
     *
     * Un bloque que no se puede rendir sale en silencio, y su MIDI se entrega
     * igual al firmware.
     *
     * @param left Canal izquierdo, numFrames muestras.
     * @param right Canal derecho, numFrames muestras.
     * @param numFrames Muestras a generar, hasta el maxBlockSize preparado.
     */
    void render(float *left, float *right, int numFrames);

    /** @brief La tasa del anfitrión de prepare(). */
    double hostSampleRate() const;

    /** @brief La tasa del emulador, que cambia con el parche. Hilo de render() o motor parado. */
    int sourceSampleRate() const;

    /** @brief El maxBlockSize de prepare(). */
    int preparedBlockSize() const;

    RdEngineParams params; ///< Mandos; el hilo, en RdEngineParams.
    RdEngineStats stats;   ///< Contadores; se leen desde cualquier hilo.

  private:
    class Impl;
    Impl *impl = nullptr;
};

#endif
