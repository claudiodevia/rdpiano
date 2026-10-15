#ifndef RD_EFFECTS_CHAIN_H
#define RD_EFFECTS_CHAIN_H

/**
 * @file effects_chain.h
 * @brief Chorus (SpaceD) y phaser a la tasa del emulador, con su mezcla en rampa.
 */

#include <algorithm>

#include "lsp/phaser.h"
#include "lsp/spaced.h"
#include "mame_utils.h"

struct RdEngineParams;

/**
 * @brief Los dos efectos de `lsp/`, en serie y en aritmética entera, como en el hardware.
 *
 * `process()` corre siempre, también en bypass: es lo único que avanza sus líneas de retardo, y saltárselo las
 * congela con audio viejo que sueltan al reactivar. El bypass es la mezcla (0 = seco, 1 = efecto), que va en
 * rampa; con 0 o 1 la salida es bit a bit la del efecto apagado o encendido.
 *
 * Los LFO van al ritmo del emulador, así que en los parches de 32 kHz modulan 1,6 veces más rápido. Deliberado:
 * lo fija `engine_lfo_rate`.
 */
class EffectsChain
{
  public:
    static constexpr int kEmuInputShift = 5;   ///< De muestra del emulador al bus de 24 bits de `lsp/`.
    static constexpr int kEmuOutputShift = 6;  ///< Del bus de 24 bits a la salida seca.
    static constexpr float kMixRampMs = 10.0f; ///< Rampa de la mezcla al conmutar un efecto.

    /// spaceDRateFromMs() recibe Hz/4, no milisegundos: el LFO se le pasa dividido por esto.
    static constexpr float kChorusRateDivisor = 4.0f;

    /// La profundidad del chorus se divide por 15 y la del trémolo por 14: con el dial a tope, el chorus queda
    /// en 14/15. Transcrito así; cambiarlo mueve el golden.
    static constexpr float kChorusDepthDivisor = 15.0f;

    /** @brief Velocidad del LFO del chorus para una posición del dial, 0..14. */
    static int32_t chorusRate(int dial);

    /** @brief Profundidad del chorus para una posición del dial, 0..14. */
    static int32_t chorusDepth(int dial);

    /** @brief Vacía las líneas de retardo de los dos efectos. */
    void reset();

    /** @brief Ajusta la rampa de la mezcla a la tasa del emulador, que cambia con el parche. */
    void setSourceRate(int rate);

    /** @brief Pone la mezcla donde dicen los mandos, sin rampa: para el primer bloque. */
    void snapTo(const RdEngineParams &params);

    /** @brief Lee de los mandos velocidades, profundidades y destino de la mezcla para el bloque que empieza. */
    void beginBlock(const RdEngineParams &params);

    /**
     * @brief Procesa una muestra del emulador.
     * @param sample La muestra cruda del chip de sonido.
     * @param outL Sale con el canal izquierdo, ya a escala seca.
     * @param outR Sale con el canal derecho.
     */
    void process(s32 sample, s32 &outL, s32 &outR)
    {
        const s32 dry = sample << kEmuInputShift;
        spaceD.audioInL = dry;
        spaceD.audioInR = dry;
        spaceD.process();

        chorusMix += std::clamp(chorusTarget - chorusMix, -mixStep, mixStep);
        s32 chorusL = spaceD.audioOutL;
        s32 chorusR = spaceD.audioOutR;
        if (chorusMix <= 0.0f)
        {
            chorusL = dry;
            chorusR = dry;
        }
        else if (chorusMix < 1.0f)
        {
            chorusL = dry + (s32)((float)(spaceD.audioOutL - dry) * chorusMix);
            chorusR = dry + (s32)((float)(spaceD.audioOutR - dry) * chorusMix);
        }
        chorusL >>= kEmuOutputShift;
        chorusR >>= kEmuOutputShift;

        phaser.audioInL = chorusL << kEmuInputShift;
        phaser.audioInR = chorusR << kEmuInputShift;
        phaser.process();

        efxMix += std::clamp(efxTarget - efxMix, -mixStep, mixStep);
        outL = chorusL;
        outR = chorusR;
        if (efxMix >= 1.0f)
        {
            outL = phaser.audioOutL >> kEmuOutputShift;
            outR = phaser.audioOutR >> kEmuOutputShift;
        }
        else if (efxMix > 0.0f)
        {
            const s32 wetL = phaser.audioOutL >> kEmuOutputShift;
            const s32 wetR = phaser.audioOutR >> kEmuOutputShift;
            outL = chorusL + (s32)((float)(wetL - chorusL) * efxMix);
            outR = chorusR + (s32)((float)(wetR - chorusR) * efxMix);
        }
    }

  private:
    SpaceD spaceD;
    Phaser phaser;

    float chorusMix = 0.0f;
    float efxMix = 0.0f;
    float chorusTarget = 0.0f;
    float efxTarget = 0.0f;
    float mixStep = 1.0f;
};

#endif
