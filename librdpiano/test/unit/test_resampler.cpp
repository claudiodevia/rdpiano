/**
 * @file test_resampler.cpp
 * @brief libresample: lo que el motor necesita saber de él.
 *
 * Longitud de salida por ratio, ausencia de NaN en los bordes, y que abrir y cerrar mil veces no
 * acumule handles.
 *
 * El código de resample/ es de terceros y no se toca: esto congela cómo lo usa el motor, no cómo
 * está escrito.
 */

#include <math.h>

#include <vector>

#if defined(__APPLE__)
#include <sys/resource.h>
#define RD_CAN_MEASURE_RSS 1
#endif

// Bajo AddressSanitizer el pico de RSS lo domina la instrumentación, no el
// código: mil ciclos correctos crecen 204 MB. Con ASan el bucle se ejecuta
// igual —eso detecta un uso después de cerrar— pero no se mide.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#undef RD_CAN_MEASURE_RSS
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#undef RD_CAN_MEASURE_RSS
#endif

#include "resample/libresample.h"
#include "unit_test.h"

/**
 * @brief Los ratios que puede pedir el plugin: tasa del host / tasa del parche.
 */
struct RatioCase
{
    int hostRate;
    int patchRate;
};

static const RatioCase kRatios[] = {
    {44100, 20000}, {44100, 32000}, {48000, 20000}, {48000, 32000},
    {88200, 20000}, {96000, 32000}, {22050, 20000}, {8000, 32000},
};

/**
 * @brief Un handle abierto como lo abre el motor: un rango de factores que cubre todos los parches a esa tasa
 *        de host, no un factor fijo.
 */
static void *open_for_host(int hostRate)
{
    double minFactor = (double)hostRate / 32000.0;
    double maxFactor = (double)hostRate / 20000.0;
    return resample_open(1, minFactor, maxFactor);
}

TEST_SUITE(resampler_output_length)
{
    for (const RatioCase &rc : kRatios)
    {
        void *h = open_for_host(rc.hostRate);
        CHECK_MSG(h != NULL, "host %d", rc.hostRate);
        if (!h)
            continue;

        const double factor = (double)rc.hostRate / rc.patchRate;
        const int inLen = 1024;
        std::vector<float> in(inLen, 0.0f);
        std::vector<float> out((size_t)(inLen * factor) + 64, 0.0f);

        // Una senoide, para que haya algo que medir.
        for (int i = 0; i < inLen; i++)
            in[i] = 0.25f * sinf(2.0f * 3.14159265358979f * 440.0f * i / rc.patchRate);

        // El primer bloque paga el retardo del filtro; se miden los siguientes,
        // que es el régimen en el que vive el plugin.
        long long totalIn = 0;
        long long totalOut = 0;
        for (int block = 0; block < 8; block++)
        {
            int inUsed = 0;
            int got = resample_process(h, factor, in.data(), inLen, 0, &inUsed, out.data(), (int)out.size());
            CHECK_MSG(got >= 0, "host %d parche %d: got %d", rc.hostRate, rc.patchRate, got);
            if (block > 0)
            {
                totalIn += inUsed;
                totalOut += got;
            }
        }

        // La relación salida/entrada tiene que ser el factor pedido, con el
        // margen de una muestra por bloque.
        double measured = totalIn ? (double)totalOut / (double)totalIn : 0.0;
        CHECK_NEAR(measured, factor, 0.01);

        resample_close(h);
    }
}

TEST_SUITE(resampler_finite)
{
    // Bordes: bloque vacío, bloque de una muestra, fondo de escala y el flag de
    // último bloque. Nada de esto puede producir NaN ni Inf.
    for (const RatioCase &rc : kRatios)
    {
        void *h = open_for_host(rc.hostRate);
        if (!h)
            continue;

        const double factor = (double)rc.hostRate / rc.patchRate;
        std::vector<float> out(4096, 0.0f);
        bool finite = true;

        const int lens[] = {0, 1, 2, 7, 1024};
        for (int len : lens)
        {
            std::vector<float> in((size_t)(len ? len : 1), 0.0f);
            for (int i = 0; i < len; i++)
                in[i] = (i & 1) ? 1.0f : -1.0f; // fondo de escala, alternando

            int inUsed = 0;
            int got = resample_process(h, factor, in.data(), len, 0, &inUsed, out.data(), (int)out.size());
            for (int i = 0; i < got; i++)
                if (!isfinite(out[i]))
                    finite = false;
        }

        // Último bloque: vacía el estado interno del filtro.
        int inUsed = 0;
        std::vector<float> in(1, 0.0f);
        int got = resample_process(h, factor, in.data(), 1, 1, &inUsed, out.data(), (int)out.size());
        for (int i = 0; i < got; i++)
            if (!isfinite(out[i]))
                finite = false;

        CHECK_MSG(finite, "host %d parche %d", rc.hostRate, rc.patchRate);
        resample_close(h);
    }
}

TEST_SUITE(resampler_no_leak)
{
    // Cada handle con highQuality=1 reserva ~600 KB: mil sin cerrar son ~600 MB,
    // mil abiertos y cerrados no deben mover el pico del proceso. `ru_maxrss`
    // nunca baja, así que una fuga se vería en el delta.
#ifdef RD_CAN_MEASURE_RSS
    struct rusage before;
    getrusage(RUSAGE_SELF, &before);

    int opened = 0;
    for (int i = 0; i < 1000; i++)
    {
        void *h = resample_open(1, 1.0, 2.5);
        if (!h)
            break;
        opened++;
        resample_close(h);
    }
    CHECK_EQ(opened, 1000);

    struct rusage after;
    getrusage(RUSAGE_SELF, &after);

    // En Darwin ru_maxrss va en bytes.
    double grewMB = (double)(after.ru_maxrss - before.ru_maxrss) / (1024 * 1024);
    CHECK_MSG(grewMB < 64.0, "el pico de RSS creció %.1f MB en 1000 ciclos", grewMB);
#else
    // Sin una medida de pico fiable, el bucle se ejecuta igual pero no se mide.
    int opened = 0;
    for (int i = 0; i < 1000; i++)
    {
        void *h = resample_open(1, 1.0, 2.5);
        if (!h)
            break;
        opened++;
        resample_close(h);
    }
    CHECK_EQ(opened, 1000);
#endif
}
