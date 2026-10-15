#ifndef RD_RETRIGGER_METER_H
#define RD_RETRIGGER_METER_H

/**
 * @file retrigger_meter.h
 * @brief Cuánto ha decaído lo que suena, para devolverlo al nivel alcanzado y no al del ataque.
 */

#include "mame_utils.h"

/**
 * @brief Seguidores de la salida cruda del emulador, en potencia (RMS al cuadrado).
 *
 * `onsetSq` es el nivel del último ataque y `levelSq` el de ahora: su razón es cuánto ha decaído la nota, y de
 * ahí sale la velocidad con la que reentra tras un cambio que apaga el firmware. Reentrar con el ataque entero
 * suma de +8 a +14 dB que nadie ha tocado.
 */
class RetriggerMeter
{
  public:
    /// Cuánto baja el ataque por unidad de velocidad: recta medida en los 16 parches de v16 a v120 (por debajo
    /// se aplana, de ahí el suelo de la corrección).
    static constexpr float kDbPerVelocityUnit = 0.228f;

    /// Tope de la corrección: más allá, la velocidad sale de la parte recta de la curva y el timbre deja de
    /// parecerse al que sonaba.
    static constexpr float kMaxRetriggerAttenDb = 30.0f;

    /// Constante del RMS. Un detector de pico daba varios dB de decaimiento que no existían: el ataque de un
    /// acorde suma en fase y el sustain no.
    static constexpr float kLevelTauMs = 25.0f;

    /// Ventana de medida del ataque; más larga que kLevelTauMs para que al RMS le dé tiempo a llenarse.
    static constexpr float kOnsetWindowMs = 80.0f;

    /** @brief Ajusta las dos constantes de tiempo a la tasa del emulador, que cambia con el parche. */
    void setSourceRate(int rate);

    /** @brief Sin nivel ni ataque medidos. */
    void reset();

    /** @brief Abre la ventana en la que se mide de qué nivel arranca una nota. */
    void startOnset();

    /** @brief Apunta una muestra cruda del emulador. */
    void feed(s32 sample)
    {
        const float magnitude = (float)sample;
        levelSq += (magnitude * magnitude - levelSq) * levelSmooth;
        if (onsetHold > 0)
        {
            if (levelSq > onsetSq)
                onsetSq = levelSq;
            onsetHold--;
        }
    }

    /** @brief Cuánto hay que bajar la velocidad de lo que reentra; 0 si no ha decaído o no hay medida. */
    int velocityDrop() const;

  private:
    float levelSq = 0.0f;
    float onsetSq = 0.0f;
    float levelSmooth = 0.0f;
    int onsetHold = 0;
    int onsetFrames = 0;
};

#endif
