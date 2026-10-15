#include "output_stage.h"

#include <math.h>

#include <algorithm>
#include <numbers>

#include "change_scheduler.h"
#include "engine_util.h"
#include "rd_engine.h"

/**
 * @file output_stage.cpp
 * @brief El bucle de salida: el orden de las operaciones es audio y lo congela `engine_golden`.
 */

namespace
{

    constexpr double kTwoPi = 2.0 * std::numbers::pi;

} // namespace

void OutputStage::prepare(double newHostRate, float gain)
{
    hostRate = newHostRate;
    gainSmoothed = gain;
    tremoloPhase = 0;
    eq.prepare(hostRate);
}

void OutputStage::process(const float *inL, const float *inR, float *left, float *right, int numFrames,
                          const RdEngineParams &params, float gainTarget, ChangeScheduler &changes)
{
    const float depth = std::clamp(params.tremoloDepth, 0, kDialMax) / kTremoloDepthDivisor;
    const double tremoloHz = std::clamp(params.tremoloRate, 0, kDialMax) / kTremoloRateDivisor;
    const double tremoloStep = kTwoPi * tremoloHz / hostRate;

    const float gainStep = (gainTarget - gainSmoothed) / (float)numFrames;
    const float declickTarget = changes.gainTarget();

    for (int i = 0; i < numFrames; i++)
    {
        const float declick = changes.nextGain(declickTarget);

        left[i] = inL[i] * gainSmoothed * declick;
        right[i] = inR[i] * gainSmoothed * declick;
        gainSmoothed += gainStep;

        // El canal derecho es el izquierdo en oposición de fase:
        // sen(π + x) = −sen(x), del mismo sin().
        tremoloPhase += tremoloStep;
        if (tremoloPhase >= kTwoPi)
            tremoloPhase -= kTwoPi;
        if (params.tremoloEnabled)
        {
            const float half = 0.5f * (float)sin(tremoloPhase);
            left[i] *= (1.0f - depth) + ((0.5f + half) * depth);
            right[i] *= (1.0f - depth) + ((0.5f - half) * depth);
        }
    }
    gainSmoothed = gainTarget;

    eq.process(left, right, numFrames);
}
