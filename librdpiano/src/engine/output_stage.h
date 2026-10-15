#ifndef RD_OUTPUT_STAGE_H
#define RD_OUTPUT_STAGE_H

/**
 * @file output_stage.h
 * @brief Lo que va detrás del remuestreador, a la tasa del host: ganancia de parche, declick, trémolo y EQ.
 */

#include "rd_biquad.h"

class ChangeScheduler;
struct RdEngineParams;

/**
 * @brief La salida a la tasa del host.
 *
 * La ganancia de parche entra aquí y no antes: el emulador y `lsp/` son aritmética entera transcrita del
 * hardware, y multiplicar dentro movería el golden y los hashes de test_lsp.cpp. El declick va también aquí,
 * tras el remuestreador, para que su cero sea cero de verdad y no quede cola del parche viejo.
 */
class OutputStage
{
  public:
    /// La velocidad del trémolo en el dial son Hz/2, del reloj del host con cualquier parche.
    static constexpr double kTremoloRateDivisor = 2.0;

    /// Con el dial a tope la profundidad del trémolo es 1 (la del chorus no: EffectsChain::kChorusDepthDivisor).
    static constexpr float kTremoloDepthDivisor = 14.0f;

    /**
     * @brief Arranca con la ganancia ya en su sitio, el trémolo en fase 0 y el EQ para la tasa del host.
     * @param gain La ganancia de parche de partida, sin rampa.
     */
    void prepare(double hostRate, float gain);

    /**
     * @brief Procesa un bloque.
     *
     * La ganancia de parche se interpola dentro del bloque hasta `gainTarget` (saltar de golpe entre parches es
     * un escalón de hasta 12 dB); quieta, la salida es la misma que sin interpolar.
     *
     * @param inL Canal izquierdo remuestreado.
     * @param inR Canal derecho remuestreado.
     * @param left Canal izquierdo de salida.
     * @param right Canal derecho de salida.
     * @param numFrames Muestras del bloque.
     * @param params Mandos del trémolo.
     * @param gainTarget Ganancia de parche al final del bloque.
     * @param changes De quien sale el declick, muestra a muestra.
     */
    void process(const float *inL, const float *inR, float *left, float *right, int numFrames,
                 const RdEngineParams &params, float gainTarget, ChangeScheduler &changes);

  private:
    double hostRate = 0;
    float gainSmoothed = 0.0f;
    double tremoloPhase = 0; ///< Acotada a 2π; en doble porque es un acumulador.
    MidEq eq;
};

#endif
