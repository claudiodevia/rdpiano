// Congela las dos transcripciones del DSP original —SpaceD (chorus) y Phaser—
// con un hash de la respuesta a un impulso y a un barrido, por efecto y por
// juego de parámetros. Los cuerpos `accA_NNN` son transcripción ciclo a ciclo y
// no se reescriben; si un hash se mueve, no se regenera: se revierte.

#include <string.h>

#include <vector>

#include "lsp/phaser.h"
#include "lsp/spaced.h"
#include "unit_test.h"

// SpaceD lleva 256 KB de ERAM: en la pila no cabe.
static std::vector<float> impulse_response_spaced(int32_t rate, int32_t depth, int n, bool sweep)
{
    SpaceD *fx = new SpaceD();
    fx->reset();
    fx->rate = rate;
    fx->depth = depth;

    std::vector<float> out;
    out.reserve(n * 2);

    for (int i = 0; i < n; i++)
    {
        // Impulso: una sola muestra a fondo de escala del bus de 24 bits.
        // Barrido: una rampa que recorre el rango, para excitar los clamps.
        int32_t in;
        if (sweep)
            in = (int32_t)(((int64_t)i * 0x7fffff * 2 / n) - 0x7fffff);
        else
            in = (i == 0) ? 0x400000 : 0;

        fx->audioInL = in;
        fx->audioInR = in;
        fx->process();
        out.push_back((float)fx->audioOutL);
        out.push_back((float)fx->audioOutR);
    }

    delete fx;
    return out;
}

static std::vector<float> impulse_response_phaser(int32_t rate, int32_t depth, int n, bool sweep)
{
    Phaser *fx = new Phaser();
    fx->reset();
    fx->rate = rate;
    fx->depth = depth;

    std::vector<float> out;
    out.reserve(n * 2);

    for (int i = 0; i < n; i++)
    {
        int32_t in;
        if (sweep)
            in = (int32_t)(((int64_t)i * 0x7fffff * 2 / n) - 0x7fffff);
        else
            in = (i == 0) ? 0x400000 : 0;

        fx->audioInL = in;
        fx->audioInR = in;
        fx->process();
        out.push_back((float)fx->audioOutL);
        out.push_back((float)fx->audioOutR);
    }

    delete fx;
    return out;
}

static unsigned long long hash_of(const std::vector<float> &v)
{
    Fnv1a h;
    for (float x : v)
        h.f32(x);
    return h.h;
}

// ---------------------------------------------------------------- tablas

TEST_SUITE(lsp_tables)
{
    // Las tablas de rate de spaced.cpp y phaser.cpp son la misma: que lo
    // compruebe el compilador y no un `diff` a mano.
    static_assert(sizeof(spaceDRateTable) == sizeof(phaserRateTable), "las dos tablas de rate tienen que ser la misma");
    CHECK_EQ(sizeof(spaceDRateTable) / sizeof(spaceDRateTable[0]), 128);
    CHECK_EQ(sizeof(spaceDDepthTable) / sizeof(spaceDDepthTable[0]), 128);
    CHECK_EQ(sizeof(phaserDepthTable) / sizeof(phaserDepthTable[0]), 128);
    CHECK_EQ(sizeof(phaserResonanceTable) / sizeof(phaserResonanceTable[0]), 128);

    bool same = true;
    for (int i = 0; i < 128; i++)
        if (spaceDRateTable[i] != phaserRateTable[i])
            same = false;
    CHECK(same);

    // No decrecientes: es lo que hace que el dial suba de forma monótona.
    bool rateMonotonic = true;
    bool depthMonotonic = true;
    for (int i = 1; i < 128; i++)
    {
        if (spaceDRateTable[i] < spaceDRateTable[i - 1])
            rateMonotonic = false;
        if (spaceDDepthTable[i] < spaceDDepthTable[i - 1] || phaserDepthTable[i] < phaserDepthTable[i - 1])
            depthMonotonic = false;
    }
    CHECK(rateMonotonic);
    CHECK(depthMonotonic);

    // spaceDDepth() recorta el índice: el motor puede pedir amount == 1, que
    // sin recorte indexaría la posición 128 de una tabla de 128.
    CHECK_EQ(spaceDDepth(0.0f), spaceDDepthTable[0]);
    CHECK_EQ(spaceDDepth(1.0f), spaceDDepthTable[0x7f]);
    CHECK_EQ(spaceDDepth(2.0f), spaceDDepthTable[0x7f]);
    CHECK_EQ(spaceDDepth(-1.0f), spaceDDepthTable[0]);
    CHECK_EQ(spaceDDepth(14.0f / 15.0f), spaceDDepthTable[119]);
}

// ---------------------------------------------------------------- SpaceD

TEST_SUITE(lsp_spaced)
{
    // Hashes capturados del código original. No se regeneran para poner algo en
    // verde.
    const int N = 2048;

    std::vector<float> impulseDefault = impulse_response_spaced(spaceDRateTable[8], spaceDDepthTable[0x7f], N, false);
    CHECK_HASH("spaced-impulso-por-defecto", hash_of(impulseDefault), 0x2fc294a596910eb9ull);

    // Los valores que pone el plugin con chorusRate=5 y chorusDepth=14.
    std::vector<float> impulsePlugin =
        impulse_response_spaced(spaceDRateFromMs(1000.0f / 450 / 4.0f), spaceDDepth(14 / 15.0f), N, false);
    CHECK_HASH("spaced-impulso-plugin", hash_of(impulsePlugin), 0x5c472a9f73a283f7ull);

    std::vector<float> sweep = impulse_response_spaced(spaceDRateTable[64], spaceDDepthTable[64], N, true);
    CHECK_HASH("spaced-barrido", hash_of(sweep), 0x6fd4d203d0542847ull);

    // Determinista: dos instancias reseteadas dan exactamente lo mismo.
    std::vector<float> again = impulse_response_spaced(spaceDRateTable[8], spaceDDepthTable[0x7f], N, false);
    CHECK_EQ(hash_of(again), hash_of(impulseDefault));

    // El impulso tiene que producir algo, y quedarse dentro del bus de 24 bits.
    bool nonZero = false;
    bool inRange = true;
    for (float x : sweep)
    {
        if (x != 0.0f)
            nonZero = true;
        if (x < -8388608.0f || x > 8388607.0f)
            inRange = false;
    }
    CHECK(nonZero);
    CHECK(inRange);
}

// ---------------------------------------------------------------- Phaser

TEST_SUITE(lsp_phaser)
{
    const int N = 2048;

    std::vector<float> impulseDefault = impulse_response_phaser(phaserRateTable[16], phaserDepthTable[64], N, false);
    CHECK_HASH("phaser-impulso-por-defecto", hash_of(impulseDefault), 0xb4b1594bb74b9afbull);

    // Los valores que pone el plugin con efxPhaserRate=0.4 y Depth=0.8.
    std::vector<float> impulsePlugin =
        impulse_response_phaser(phaserRateTable[(int)(0.4f * 0x7f)], phaserDepthTable[(int)(0.8f * 0x7f)], N, false);
    CHECK_HASH("phaser-impulso-plugin", hash_of(impulsePlugin), 0xd9f8cef230ceea54ull);

    std::vector<float> sweep = impulse_response_phaser(phaserRateTable[100], phaserDepthTable[127], N, true);
    CHECK_HASH("phaser-barrido", hash_of(sweep), 0x5c71e1d554c6bdc9ull);

    std::vector<float> again = impulse_response_phaser(phaserRateTable[16], phaserDepthTable[64], N, false);
    CHECK_EQ(hash_of(again), hash_of(impulseDefault));

    bool nonZero = false;
    bool inRange = true;
    for (float x : sweep)
    {
        if (x != 0.0f)
            nonZero = true;
        if (x < -8388608.0f || x > 8388607.0f)
            inRange = false;
    }
    CHECK(nonZero);
    CHECK(inRange);
}
