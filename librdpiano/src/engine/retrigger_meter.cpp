#include "retrigger_meter.h"

#include <math.h>

#include "engine_util.h"

/**
 * @file retrigger_meter.cpp
 * @brief Constantes de tiempo del medidor y la conversión de decaimiento a velocidad.
 */

void RetriggerMeter::setSourceRate(int rate)
{
    levelSmooth = 1.0f - expf(-1.0f / (kLevelTauMs * 0.001f * (float)rate));
    onsetFrames = ms_to_frames(kOnsetWindowMs, (float)rate);
}

void RetriggerMeter::reset()
{
    levelSq = 0.0f;
    onsetSq = 0.0f;
    onsetHold = 0;
}

void RetriggerMeter::startOnset()
{
    onsetHold = onsetFrames;
    onsetSq = 0.0f;
}

int RetriggerMeter::velocityDrop() const
{
    if (!(onsetSq > 0.0f && levelSq > 0.0f && levelSq < onsetSq))
        return 0;

    // Los dos seguidores son potencias, de ahí el 10 y no el 20.
    float attenDb = -10.0f * log10f(levelSq / onsetSq);
    if (attenDb > kMaxRetriggerAttenDb)
        attenDb = kMaxRetriggerAttenDb;
    return (int)(attenDb / kDbPerVelocityUnit + 0.5f);
}
