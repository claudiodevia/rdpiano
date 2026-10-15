#ifndef RD_ENGINE_UTIL_H
#define RD_ENGINE_UTIL_H

/**
 * @file engine_util.h
 * @brief Conversiones de tiempo y relleno de búfer que comparten las piezas del motor.
 */

inline constexpr int kDialMax = 14; ///< Posición más alta de los diales de 15 pasos (velocidad y profundidad).

/**
 * @brief Paso por muestra de una rampa a partir de su duración.
 * @param ms Duración de la rampa.
 * @param rate Tasa a la que corre; puede ser 0 antes de prepare(), de ahí el suelo.
 * @return El paso, acotado a 1.
 */
inline float ramp_step(float ms, double rate)
{
    const double samples = ms * 0.001 * rate;
    return samples > 1.0 ? (float)(1.0 / samples) : 1.0f;
}

/** @brief Muestras enteras que dura `ms` a `rate` Hz, en `float` y truncando. */
inline int ms_to_frames(float ms, float rate) { return (int)(ms * 0.001f * rate); }

/** @brief Pone a cero `numFrames` muestras de los dos canales; con 0 o menos no escribe nada. */
inline void silence(float *left, float *right, int numFrames)
{
    for (int i = 0; i < numFrames; i++)
    {
        left[i] = 0.0f;
        right[i] = 0.0f;
    }
}

#endif
