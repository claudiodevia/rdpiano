/**
 * @file test_biquad.cpp
 * @brief El biquad de la salida y el EQ medio, fuera de la cadena.
 */

#include <math.h>

#include <vector>

#include "engine/rd_biquad.h"
#include "unit_test.h"

namespace
{

    constexpr double kRate = 48000.0;

    /** @brief Amplitud, en dB, de la respuesta estacionaria a un seno de `freq` Hz y amplitud 1. */
    double steady_gain_db(RdBiquad f, double freq)
    {
        const int total = (int)kRate;
        double peak = 0;
        for (int i = 0; i < total; i++)
        {
            const float out = f.process((float)sin(2.0 * M_PI * freq * i / kRate));
            if (i > total / 2 && fabs(out) > peak)
                peak = fabs(out);
        }
        return 20.0 * log10(peak);
    }

    /** @brief Respuesta estacionaria a una entrada constante. */
    double steady_dc(RdBiquad f)
    {
        float out = 0.0f;
        for (int i = 0; i < (int)kRate; i++)
            out = f.process(1.0f);
        return out;
    }

    /** @brief Respuesta estacionaria a la alternancia ±1 (Nyquist), en valor absoluto. */
    double steady_nyquist(RdBiquad f)
    {
        float out = 0.0f;
        for (int i = 0; i < (int)kRate; i++)
            out = f.process(i % 2 ? -1.0f : 1.0f);
        return fabs(out);
    }

    std::vector<float> impulse_response(RdBiquad &f, int n)
    {
        std::vector<float> out((size_t)n);
        for (int i = 0; i < n; i++)
            out[(size_t)i] = f.process(i == 0 ? 1.0f : 0.0f);
        return out;
    }

} // namespace

/**
 * @brief La campana del EQ medio: +8 dB en 350 Hz, ganancia 1 en continua y en Nyquist, transparente con
 *        ganancia 1, y reset() vuelve al estado de un filtro nuevo.
 */
TEST_SUITE(rd_biquad)
{
    const float gain = powf(10.0f, MidEq::kGainDb * 0.05f);

    RdBiquad peak;
    peak.setPeak(kRate, MidEq::kFreq, MidEq::kQ, gain);

    const double center = steady_gain_db(peak, MidEq::kFreq);
    checks.add("centro-a-8-dB", fabs(center - MidEq::kGainDb) < 0.05, check_fmt("%.3f dB en 350 Hz", center));

    const double dc = steady_dc(peak);
    checks.add("continua-a-0-dB", fabs(dc - 1.0) < 1e-4, check_fmt("%.6f", dc));

    const double nyquist = steady_nyquist(peak);
    checks.add("nyquist-a-0-dB", fabs(nyquist - 1.0) < 1e-3, check_fmt("%.6f", nyquist));

    // Con A = 1 numerador y denominador coinciden: la salida es la entrada, salvo
    // el redondeo de normalizar por a0 que se acumula en el estado (−100 dB).
    RdBiquad flat;
    flat.setPeak(kRate, MidEq::kFreq, MidEq::kQ, 1.0f);
    double worst = 0;
    for (int i = 0; i < 4096; i++)
    {
        const float in = (float)sin(0.37 * i) * 0.8f;
        worst = fmax(worst, fabs((double)flat.process(in) - in));
    }
    checks.add("ganancia-1-transparente", worst < 1e-5, check_fmt("error máximo %.3g", worst));

    RdBiquad used = peak;
    for (int i = 0; i < 1000; i++)
        used.process((float)(i % 7) - 3.0f);
    used.reset();
    RdBiquad fresh = peak;
    CHECK(impulse_response(used, 256) == impulse_response(fresh, 256));
}

/**
 * @brief MidEq filtra los dos canales con la misma campana, a la tasa de prepare().
 */
TEST_SUITE(rd_biquad_mid_eq)
{
    const int N = 512;
    MidEq eq;
    eq.prepare(kRate);

    std::vector<float> l((size_t)N, 0.0f), r((size_t)N, 0.0f);
    l[0] = 1.0f;
    r[0] = 1.0f;
    eq.process(l.data(), r.data(), N);
    CHECK(l == r);

    RdBiquad reference;
    reference.setPeak(kRate, MidEq::kFreq, MidEq::kQ, powf(10.0f, MidEq::kGainDb * 0.05f));
    CHECK(l == impulse_response(reference, N));
}
