#ifndef RD_CHANGE_SCHEDULER_H
#define RD_CHANGE_SCHEDULER_H

/**
 * @file change_scheduler.h
 * @brief Peticiones de parche y afinación: de otro hilo al hilo de render(), con ventana de asentamiento y
 *        declick.
 */

#include <stdint.h>

#include <algorithm>
#include <atomic>

/**
 * @brief Curva de la rampa de declick: smoothstep, derivada cero en 0 y en 1.
 *
 * El arranque plano esconde el transitorio de la nota que reentra; el final plano evita el bombeo.
 */
inline float declick_shape(float g) { return g * g * (3.0f - 2.0f * g); }

/**
 * @brief Cuándo se aplica un cambio de parche o de afinación.
 *
 * Las peticiones llegan por atómicas desde cualquier hilo (lo único de aquí que no es del hilo de render()).
 * Un cambio de parche pasa por la ventana de asentamiento: el primero sale en el acto y, de los que llegan con
 * ella abierta, se cobra solo el último (cada cambio apaga el firmware y redispara lo pulsado). Después, la
 * salida baja a cero, el motor aplica el cambio entre bloques —la tasa del emulador cambia con el parche— y
 * vuelve a subir.
 */
class ChangeScheduler
{
  public:
    static constexpr int kNoTune = 0x7fffffff; ///< Sin afinación pendiente.

    /// Bajada del declick: rápida, que es como menos se oye.
    static constexpr float kFadeOutMs = 6.0f;
    /// Subida del declick sin nada que reentre.
    static constexpr float kFadeInMs = 15.0f;
    /// Subida si el cambio redisparó lo pulsado: esconde el golpe de martillo de las notas que reentran.
    static constexpr float kFadeInHeldMs = 80.0f;
    /// Ventana de asentamiento del parche: una ráfaga de program change se cobra un cambio.
    static constexpr float kPatchSettleMs = 150.0f;

    /** @brief Lo que toca aplicar con la salida ya en cero. */
    struct Due
    {
        int patch = -1;     ///< Parche nuevo, o -1.
        int tune = kNoTune; ///< Afinación nueva, o kNoTune.
    };

    /** @brief Pide un parche ya validado. Cualquier hilo. */
    void request(int patch)
    {
        latestPatchValue.store(patch, std::memory_order_relaxed);
        patchRequest.store(patch, std::memory_order_release);
    }

    /** @brief Pide una afinación. Cualquier hilo. */
    void requestTune(int16_t tune)
    {
        latestTuneValue.store((int)tune, std::memory_order_relaxed);
        tuneRequest.store((int)tune, std::memory_order_release);
    }

    /** @brief El parche pedido o puesto, lo que debe enseñar la interfaz. Cualquier hilo. */
    int latestPatch() const { return latestPatchValue.load(std::memory_order_relaxed); }

    /** @brief La afinación pedida o puesta. Cualquier hilo. */
    int16_t latestTune() const { return (int16_t)latestTuneValue.load(std::memory_order_relaxed); }

    /** @brief Lo que suena pasa a ser también lo que se enseña. */
    void publishPatch(int patch) { latestPatchValue.store(patch, std::memory_order_relaxed); }

    /** @brief Igual que publishPatch(), para la afinación. */
    void publishTune(int16_t tune) { latestTuneValue.store((int)tune, std::memory_order_relaxed); }

    /**
     * @brief Recoge todo lo pendiente, esté en las atómicas o ya en la ventana o en la rampa.
     *
     * Para prepare(), que lo aplica antes de arrancar el firmware (trampa 8 de CLAUDE.md). Si hay en los dos
     * sitios manda la atómica, que es la más reciente.
     */
    Due takePending();

    /** @brief Rampas y ventana a la tasa del host, sin nada pendiente y la salida arriba. Va tras takePending(). */
    void prepare(double hostRate);

    /** @brief Descarta el parche pendiente: un setPatch() inmediato manda sobre él. */
    void cancelPatch();

    /** @brief Descarta la afinación pendiente: un setMasterTune() inmediato manda sobre ella. */
    void cancelTune();

    /**
     * @brief Avanza la ventana y dice si toca aplicar algo ya.
     * @param numFrames Muestras del bloque que empieza.
     * @param currentPatch Parche que suena: pedirlo otra vez no es un cambio.
     * @param currentTune Afinación que suena, ídem.
     * @param due Sale con lo que hay que aplicar, afinación primero.
     * @return true si la salida ya está en cero y hay algo que aplicar; el motor termina con finish().
     */
    bool service(int numFrames, int currentPatch, int16_t currentTune, Due &due);

    /** @brief Elige la subida tras aplicar un cambio: más larga si algo ha reentrado. */
    void finish(bool retriggered);

    /** @brief Destino del declick en este bloque: 0 con un cambio esperando, 1 sin él. */
    float gainTarget() const { return (declickPatch >= 0 || declickTune != kNoTune) ? 0.0f : 1.0f; }

    /** @brief Avanza el declick una muestra del host hacia `target` y devuelve la ganancia ya curvada. */
    float nextGain(float target)
    {
        declickGain += std::clamp(target - declickGain, -declickDownStep, declickUpStep);
        return declick_shape(declickGain);
    }

  private:
    std::atomic<int> patchRequest{-1};
    std::atomic<int> tuneRequest{kNoTune};
    std::atomic<int> latestPatchValue{0};
    std::atomic<int> latestTuneValue{0};

    double hostRate = 0;

    float declickGain = 1.0f;
    float declickDownStep = 1.0f;
    float declickUpStep = 1.0f;
    int declickPatch = -1;     ///< Parche que espera a que la rampa toque fondo.
    int declickTune = kNoTune; ///< Afinación que espera a que la rampa toque fondo.

    int settlePatch = -1;      ///< Último parche llegado con la ventana abierta.
    int settleFrames = 0;      ///< Lo que le queda abierto a la ventana.
    int patchSettleFrames = 0; ///< Duración de la ventana, en muestras del host.
};

#endif
