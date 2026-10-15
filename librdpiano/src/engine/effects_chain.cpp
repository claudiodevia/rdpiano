#include "effects_chain.h"

#include "engine_util.h"
#include "rd_engine.h"

/**
 * @file effects_chain.cpp
 * @brief Del panel a los parámetros de `lsp/`.
 */

namespace
{

    /// Periodo del LFO del chorus por posición del dial, en milisegundos.
    constexpr int chorusRateToMsPeriod[kDialMax + 1] = {2700, 1380, 910, 680, 540, 450, 385, 335,
                                                        300,  265,  245, 220, 205, 190, 175};

    /** @brief Índice de una tabla de `lsp/` para un mando de 0 a 1. */
    template <size_t N> int table_index(const int32_t (&)[N], float amount)
    {
        constexpr int last = (int)N - 1;
        return std::clamp((int)(amount * last), 0, last);
    }

} // namespace

int32_t EffectsChain::chorusRate(int dial)
{
    return spaceDRateFromMs(1000.0f / chorusRateToMsPeriod[std::clamp(dial, 0, kDialMax)] / kChorusRateDivisor);
}

int32_t EffectsChain::chorusDepth(int dial) { return spaceDDepth(std::clamp(dial, 0, kDialMax) / kChorusDepthDivisor); }

void EffectsChain::reset()
{
    spaceD.reset();
    phaser.reset();
}

void EffectsChain::setSourceRate(int rate) { mixStep = ramp_step(kMixRampMs, rate); }

void EffectsChain::snapTo(const RdEngineParams &params)
{
    chorusMix = params.chorusEnabled ? 1.0f : 0.0f;
    efxMix = params.efxEnabled ? 1.0f : 0.0f;
}

void EffectsChain::beginBlock(const RdEngineParams &params)
{
    spaceD.rate = chorusRate(params.chorusRate);
    spaceD.depth = chorusDepth(params.chorusDepth);
    phaser.rate = phaserRateTable[table_index(phaserRateTable, params.efxPhaserRate)];
    phaser.depth = phaserDepthTable[table_index(phaserDepthTable, params.efxPhaserDepth)];

    chorusTarget = params.chorusEnabled ? 1.0f : 0.0f;
    efxTarget = params.efxEnabled ? 1.0f : 0.0f;
}
