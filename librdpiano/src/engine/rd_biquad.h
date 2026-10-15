#ifndef RD_BIQUAD_H
#define RD_BIQUAD_H

/**
 * @file rd_biquad.h
 * @brief El biquad de la salida y el EQ medio que se construye con él.
 */

/**
 * @brief Biquad en forma directa II traspuesta.
 *
 * Replica coeficientes y orden de operaciones de `juce::dsp::IIR::Filter<float>`,
 * salvo su `snapToZero`, que fuera de Intel es un no-op.
 */
struct RdBiquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; ///< Coeficientes, normalizados por a0.
    float s1 = 0.0f, s2 = 0.0f;                                  ///< Estado.

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

    /** @brief Filtra una muestra. */
    float process(float in)
    {
        float out = (in * b0) + s1;
        s1 = (in * b1) - (out * a1) + s2;
        s2 = (in * b2) - (out * a2);
        return out;
    }
};

/** @brief El EQ medio de la salida: una campana por canal, afinada de oído contra un MKS-20. */
class MidEq
{
  public:
    static constexpr float kFreq = 350.0f; ///< Centro de la campana, en Hz.
    static constexpr float kQ = 0.2f;      ///< Factor de calidad.
    static constexpr float kGainDb = 8.0f; ///< Ganancia en el centro.

    /** @brief Calcula los coeficientes para la tasa del host y vacía el estado. */
    void prepare(double hostRate);

    /** @brief Filtra en el sitio `numFrames` muestras de cada canal. */
    void process(float *left, float *right, int numFrames);

  private:
    RdBiquad eqL;
    RdBiquad eqR;
};

#endif
