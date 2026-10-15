/**
 * @file engine_fixture.cpp
 * @brief ROM compartidas por las suites `engine_*` y las métricas de engine_fixture.h.
 */

#include "engine_fixture.h"

#include <math.h>

#include "patches.h"
#include "rom_files.h"

namespace
{

    /** @brief Las ROM, leídas una vez para todas las suites del motor. */
    struct EngineRoms
    {
        RomCache cache;
        RdRomSet sets[ROMSET_COUNT];
        const u8 *prog = nullptr;
        bool ok = false;

        EngineRoms()
        {
            cache.dir = g_roms_dir;
            ok = cache.engineRoms(sets, prog);
        }
    };

    EngineRoms &engine_roms()
    {
        static EngineRoms r;
        return r;
    }

} // namespace

std::unique_ptr<RdPianoEngine> make_engine(double hostRate, int maxBlock, int patch)
{
    EngineRoms &roms = engine_roms();
    if (!roms.ok)
        return nullptr;

    auto e = std::make_unique<RdPianoEngine>(roms.sets, roms.prog);
    if (patch != 0)
        e->setPatch(patch);
    e->prepare(hostRate, maxBlock);
    return e;
}

double rms(const std::vector<float> &v, size_t from, size_t to)
{
    if (to <= from || to > v.size())
        return 0.0;
    double sum = 0;
    for (size_t i = from; i < to; i++)
        sum += (double)v[i] * v[i];
    return sqrt(sum / (double)(to - from));
}

double peak(const std::vector<float> &v)
{
    double p = 0;
    for (float x : v)
    {
        double m = fabs((double)x);
        if (m > p)
            p = m;
    }
    return p;
}

bool all_finite(const std::vector<float> &v)
{
    for (float x : v)
        if (!isfinite(x))
            return false;
    return true;
}

double worst_step(const std::vector<float> &v, size_t from, size_t to)
{
    if (to > v.size())
        to = v.size();

    double worst = 0;
    for (size_t i = from + 1; i < to; i++)
    {
        const double s = fabs((double)v[i] - (double)v[i - 1]);
        if (s > worst)
            worst = s;
    }
    return worst;
}

Stereo render_blocks(RdPianoEngine *e, int total, const int *blocks, int numBlocks)
{
    Stereo out;
    out.l.reserve(total);
    out.r.reserve(total);

    std::vector<float> bl, br;
    int done = 0;
    int b = 0;
    while (done < total)
    {
        int n = blocks[b++ % numBlocks];
        if (n > total - done)
            n = total - done;
        if (n <= 0)
            continue;

        bl.assign(n, 0.0f);
        br.assign(n, 0.0f);
        e->render(bl.data(), br.data(), n);

        out.l.insert(out.l.end(), bl.begin(), bl.end());
        out.r.insert(out.r.end(), br.begin(), br.end());
        done += n;
    }

    return out;
}

void render_into(RdPianoEngine *e, Stereo &out, int block, int blocks)
{
    std::vector<float> l(block, 0.0f), r(block, 0.0f);
    for (int b = 0; b < blocks; b++)
    {
        e->render(l.data(), r.data(), block);
        out.l.insert(out.l.end(), l.begin(), l.end());
        out.r.insert(out.r.end(), r.begin(), r.end());
    }
}

double dominant_period(const std::vector<double> &env, double dt)
{
    // Fuera el arranque —ataque y rampa de la mezcla— y fuera la media.
    if (env.size() < 40)
        return -1.0;
    std::vector<double> x(env.begin() + env.size() / 10, env.end());
    double mean = 0;
    for (double v : x)
        mean += v;
    mean /= (double)x.size();
    for (double &v : x)
        v -= mean;

    // Primer máximo de la autocorrelación después del primer cruce por cero.
    const size_t maxLag = x.size() / 2;
    std::vector<double> ac(maxLag, 0.0);
    for (size_t lag = 0; lag < maxLag; lag++)
    {
        double s = 0;
        for (size_t i = 0; i + lag < x.size(); i++)
            s += x[i] * x[i + lag];
        ac[lag] = s;
    }

    size_t lag = 1;
    while (lag < maxLag && ac[lag] > 0)
        lag++;

    size_t best = 0;
    double bestVal = 0;
    for (size_t i = lag; i < maxLag; i++)
        if (ac[i] > bestVal)
        {
            bestVal = ac[i];
            best = i;
        }

    return best == 0 ? -1.0 : best * dt;
}
