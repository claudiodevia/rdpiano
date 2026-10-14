#ifndef RDPIANO_ENGINE_FIXTURE_H
#define RDPIANO_ENGINE_FIXTURE_H

/**
 * @file engine_fixture.h
 * @brief Andamiaje de las suites `engine_*`: motores preparados, MIDI con nombre, métricas y umbrales.
 *
 * Las suites simulan un host: instancian RdPianoEngine, le mandan MIDI por bloques y miden lo que sale.
 * Ninguna necesita JUCE ni un DAW.
 */

#include <memory>
#include <vector>

#include "rd_engine.h"
#include "unit_test.h"

/// Sin motor —faltan las ROM— la suite falla y sale.
#define REQUIRE_ENGINE(e) ROMS_OR_FAIL((e) != nullptr)

/** @name Umbrales
 *  Niveles de muestra en coma flotante (fondo de escala = 1,0), con su valor en dBFS.
 *  @{
 */
constexpr double kAudibleRms = 1e-4;          ///< RMS por encima del cual algo suena: −80 dBFS.
constexpr double kSoundingLevel = 0.01;       ///< Pico o RMS de algo que de verdad suena: −40 dBFS.
constexpr double kNotePeak = 0.02;            ///< Pico de una nota redisparada que suena: −34 dBFS.
constexpr double kSilenceLevel = 1e-3;        ///< Pico o RMS por debajo del cual hay silencio: −60 dBFS.
constexpr double kBurstPeak = 5e-3;           ///< Pico máximo de un estallido al encender un efecto: −46 dBFS.
constexpr double kHungVoiceRms = 1e-5;        ///< RMS por debajo del cual no queda voz colgada: −100 dBFS.
constexpr double kClickStep = 0.25;           ///< Salto entre muestras que ya se oye como clic: −12 dBFS.
constexpr double kStepRatio = 2.0;            ///< Salto máximo en un transitorio frente al típico: +6 dB.
constexpr double kBypassStepRatio = 3.0;      ///< El mismo, al conmutar el bypass de un efecto: +9,5 dB.
constexpr double kRetriggerMaxJumpDb = 6.0;   ///< Reentrada: subida máxima del pico sobre lo que sonaba.
constexpr double kRetriggerMinJumpDb = -12.0; ///< Reentrada: bajada máxima, para que siga oyéndose.
/** @} */

/** @brief Una salida estéreo acumulada. */
struct Stereo
{
    std::vector<float> l, r;
};

/**
 * @brief Construye un motor preparado.
 *
 * El parche se selecciona **antes** de preparar, el orden del harness e2e y del plugin (trampa 8).
 *
 * @return nullptr si faltan las ROM.
 */
std::unique_ptr<RdPianoEngine> make_engine(double hostRate, int maxBlock, int patch = 0);

/** @brief Note-on por el camino del host. */
inline void note_on(RdPianoEngine *e, int note, int velocity, int frame = 0)
{
    e->pushMidi(frame, 0x90, (u8)note, (u8)velocity);
}

/** @brief Note-off por el camino del host. */
inline void note_off(RdPianoEngine *e, int note, int frame = 0) { e->pushMidi(frame, 0x80, (u8)note, 0); }

/** @brief Pedal de sostenido (CC 64) abajo o arriba. */
inline void sustain(RdPianoEngine *e, bool down, int frame = 0) { e->pushMidi(frame, 0xB0, 64, down ? 127 : 0); }

/** @brief Program change por el camino del host. */
inline void program_change(RdPianoEngine *e, int program, int frame = 0) { e->pushMidi(frame, 0xC0, (u8)program, 0); }

/** @brief Muestras de `secs` segundos a `rate` Hz. */
inline size_t frames_of(double secs, double rate) { return (size_t)(secs * rate); }

/** @brief RMS de [from, to); 0 si el tramo está vacío o se sale. */
double rms(const std::vector<float> &v, size_t from, size_t to);

/** @brief Pico absoluto. */
double peak(const std::vector<float> &v);

/** @brief Ni NaN ni infinito. */
bool all_finite(const std::vector<float> &v);

/** @brief El mayor salto entre muestras consecutivas de [from, to): la métrica de clic. */
double worst_step(const std::vector<float> &v, size_t from, size_t to);

/** @brief Renderiza `total` muestras en bloques del tamaño que diga `blocks`, que se recorre cíclicamente. */
Stereo render_blocks(RdPianoEngine *e, int total, const int *blocks, int numBlocks);

/** @brief Renderiza `blocks` bloques de `block` muestras y los añade a `out`. */
void render_into(RdPianoEngine *e, Stereo &out, int block, int blocks);

/**
 * @brief Periodo dominante de una envolvente muestreada cada `dt` segundos, por autocorrelación.
 * @return El periodo en segundos; -1 si no hay muestras suficientes o no hay periodo.
 */
double dominant_period(const std::vector<double> &env, double dt);

#endif
