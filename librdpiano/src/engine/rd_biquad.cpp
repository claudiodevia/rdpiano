#include "rd_biquad.h"

#include <math.h>

#include <numbers>

/**
 * @file rd_biquad.cpp
 * @brief Coeficientes del biquad y el EQ medio.
 */

// juce::dsp::ArrayCoefficients<float>::makePeakFilter, en float, con el mismo
// orden de operaciones; `assignImpl` divide después todo por a0.
void RdBiquad::setPeak(double sampleRate, float frequency, float q, float gainFactor)
{
    const float A = sqrtf(gainFactor);
    const float omega = (2.0f * std::numbers::pi_v<float> * (frequency > 2.0f ? frequency : 2.0f)) / (float)sampleRate;
    const float alpha = sinf(omega) / (q * 2.0f);
    const float c2 = -2.0f * cosf(omega);
    const float alphaTimesA = alpha * A;
    const float alphaOverA = alpha / A;

    const float rb0 = 1.0f + alphaTimesA;
    const float rb1 = c2;
    const float rb2 = 1.0f - alphaTimesA;
    const float ra0 = 1.0f + alphaOverA;
    const float ra1 = c2;
    const float ra2 = 1.0f - alphaOverA;

    const float inv = 1.0f / ra0;
    b0 = rb0 * inv;
    b1 = rb1 * inv;
    b2 = rb2 * inv;
    a1 = ra1 * inv;
    a2 = ra2 * inv;
}

void MidEq::prepare(double hostRate)
{
    const float gain = powf(10.0f, kGainDb * 0.05f);
    eqL.setPeak(hostRate, kFreq, kQ, gain);
    eqR.setPeak(hostRate, kFreq, kQ, gain);
    eqL.reset();
    eqR.reset();
}

void MidEq::process(float *left, float *right, int numFrames)
{
    for (int i = 0; i < numFrames; i++)
        left[i] = eqL.process(left[i]);
    for (int i = 0; i < numFrames; i++)
        right[i] = eqR.process(right[i]);
}
