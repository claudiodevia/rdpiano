/**
 * @file test_engine_host.cpp
 * @brief Simulador de host: troceado de bloques, tasas, reservas, finitud, latencia y cola.
 */

#include <math.h>

#include <vector>

#include "alloc_counter.h"
#include "engine_fixture.h"
#include "patches.h"

/**
 * @brief El mismo stream pedido de una vez y a trozos irregulares: misma longitud, mismo nivel, mismas
 *        pendientes dentro del bloque y determinismo exacto.
 *
 * Bit a bit no puede ser igual: `renderBufferFrames` se redondea hacia arriba y el corrector de deriva resta
 * hasta numFrames/4, así que el troceado cambia cuántas muestras genera el emulador (con algún hueco corto
 * audible como clic; defecto conocido).
 */
TEST_SUITE(engine_block_invariance)
{
    const int TOTAL = 4096;

    auto a = make_engine(20000.0, TOTAL);
    auto b = make_engine(20000.0, TOTAL);
    auto c = make_engine(20000.0, TOTAL);
    REQUIRE_ENGINE(a);
    REQUIRE_ENGINE(b);
    REQUIRE_ENGINE(c);

    note_on(a.get(), 60, 100);
    note_on(b.get(), 60, 100);
    note_on(c.get(), 60, 100);

    const int one[] = {TOTAL};
    const int many[] = {7, 13, 1, 512, 256, 3, 1024, 64};

    Stereo whole = render_blocks(a.get(), TOTAL, one, 1);
    Stereo split = render_blocks(b.get(), TOTAL, many, 8);
    Stereo again = render_blocks(c.get(), TOTAL, many, 8);

    CHECK_EQ(whole.l.size(), (size_t)TOTAL);
    CHECK_EQ(split.l.size(), (size_t)TOTAL);

    // Y que no sea trivialmente cierto porque no suene nada.
    double levelWhole = rms(whole.l, 0, whole.l.size());
    double levelSplit = rms(split.l, 0, split.l.size());
    CHECK(levelWhole > kSoundingLevel);
    checks.add("bloque-mismo-nivel", fabs(levelSplit - levelWhole) < levelWhole * 0.005,
               check_fmt("rms entero %.6g, troceado %.6g", levelWhole, levelSplit));

    // Determinismo: el mismo troceado dos veces tiene que dar exactamente lo
    // mismo. Aquí sí se exige bit a bit.
    size_t firstDiff = (size_t)-1;
    for (size_t i = 0; i < split.l.size(); i++)
        if (split.l[i] != again.l[i] || split.r[i] != again.r[i])
        {
            firstDiff = i;
            break;
        }
    checks.add("bloque-determinista", firstDiff == (size_t)-1,
               check_fmt("primera diferencia en la muestra %zu", firstDiff));

    // La pendiente máxima dentro de un bloque no puede depender del troceado:
    // si el trémolo, el EQ o el corrector de deriva se reiniciasen por bloque,
    // esto se dispararía.
    double stepWhole = 0;
    for (size_t i = 1; i < whole.l.size(); i++)
    {
        double d = fabs((double)whole.l[i] - (double)whole.l[i - 1]);
        if (d > stepWhole)
            stepWhole = d;
    }

    std::vector<char> boundary(split.l.size(), 0);
    {
        int done = 0, bi = 0;
        while (done < TOTAL)
        {
            int n = many[bi++ % 8];
            if (n > TOTAL - done)
                n = TOTAL - done;
            done += n;
            if (done < TOTAL)
                boundary[(size_t)done] = 1;
        }
    }

    double stepInside = 0;
    for (size_t i = 1; i < split.l.size(); i++)
    {
        if (boundary[i])
            continue;
        double d = fabs((double)split.l[i] - (double)split.l[i - 1]);
        if (d > stepInside)
            stepInside = d;
    }
    checks.add("bloque-misma-pendiente", stepInside <= stepWhole * 1.01,
               check_fmt("pendiente entero %.6g, troceado %.6g", stepWhole, stepInside));

    // Caracterización del hueco descrito arriba, en muestras de silencio exacto
    // en 4.096 con una nota sonando. No se relaja; el día que el reparto de
    // muestras deje de perder cola, baja.
    const int kKnownDropouts = 23;
    int dropouts = 0;
    for (size_t i = 0; i < split.l.size(); i++)
        if (split.l[i] == 0.0f)
            dropouts++;
    checks.add("bloque-huecos-conocidos", dropouts <= kKnownDropouts,
               check_fmt("%d muestras de silencio exacto en %d", dropouts, TOTAL));
}

/**
 * @brief Bloques de 0, 1, 2 y 7 muestras y mayores que el preparado —JUCE no garantiza que
 *        `maximumExpectedSamplesPerBlock` lo sea—: salida finita, y un numFrames negativo no escribe nada.
 *        Un bloque que pide al emulador más de 20000 muestras se rinde si cabe en el búfer.
 */
TEST_SUITE(engine_edge_blocks)
{
    const int MAXB = 512;
    auto e = make_engine(48000.0, MAXB);
    REQUIRE_ENGINE(e);

    note_on(e.get(), 60, 100);

    // Un bloque mayor que el preparado: el contrato de JUCE no garantiza que
    // maximumExpectedSamplesPerBlock lo sea. El búfer es de verdad de ese
    // tamaño para que ASan vea cualquier desborde.
    const int sizes[] = {0, 1, 2, 7, MAXB - 1, MAXB, MAXB + 1, MAXB * 2};
    bool finite = true;
    for (int n : sizes)
    {
        std::vector<float> l((size_t)(n > 0 ? n : 1), 1234.0f);
        std::vector<float> r((size_t)(n > 0 ? n : 1), 1234.0f);
        e->render(l.data(), r.data(), n);
        for (int i = 0; i < n; i++)
            if (!isfinite(l[i]) || !isfinite(r[i]))
                finite = false;
    }
    CHECK(finite);

    // Un numFrames negativo, que un host no debería mandar, no escribe en ningún
    // sitio: lo vigila ASan, no un CHECK.
    std::vector<float> l(16, 0.0f), r(16, 0.0f);
    e->render(l.data(), r.data(), -1);

    // Un bloque que pide más de 20000 muestras al emulador y cabe en su búfer:
    // parche de 32 kHz con el host a 22,05 kHz y bloques de 16384 son 23 778.
    const int BIG = 16384;
    auto big = make_engine(22050.0, BIG, 3);
    REQUIRE_ENGINE(big);
    note_on(big.get(), 60, 100);
    Stereo out;
    render_into(big.get(), out, BIG, 2);
    checks.add("bloque-grande-se-rinde", big->stats.tooManyFrames.load() == 0,
               check_fmt("%lu bloques abortados", big->stats.tooManyFrames.load()));
    checks.add("bloque-grande-suena", rms(out.l, 0, out.l.size()) > kAudibleRms, "salió muda");
}

/**
 * @brief De 22,05 a 96 kHz, con un parche de 20 kHz y otro de 32 kHz y bloques irregulares: el motor
 *        devuelve siempre numFrames muestras, finitas, que suenan y sin bloques abortados.
 */
TEST_SUITE(engine_host_rates)
{
    const double rates[] = {22050.0, 44100.0, 48000.0, 88200.0, 96000.0};
    // Un parche de 20 kHz y otro de 32 kHz.
    const int patches[] = {0, 3};

    for (double hostRate : rates)
    {
        for (int patch : patches)
        {
            const int BLOCK = 512;
            auto e = make_engine(hostRate, BLOCK, patch);
            REQUIRE_ENGINE(e);

            note_on(e.get(), 60, 100);

            // Un segundo simulado, en bloques irregulares.
            const int blocks[] = {BLOCK, 128, 480, 64};
            int total = (int)hostRate;
            Stereo out = render_blocks(e.get(), total, blocks, 4);

            CHECK_MSG(out.l.size() == (size_t)total, "%.0f Hz parche %d: %zu de %d", hostRate, patch, out.l.size(),
                      total);
            CHECK_MSG(all_finite(out.l) && all_finite(out.r), "%.0f Hz parche %d", hostRate, patch);

            // Suena: caza el silencio total por debajo de 32 kHz que da un
            // búfer intermedio dimensionado al revés.
            double level = rms(out.l, out.l.size() / 4, out.l.size());
            CHECK_MSG(level > kAudibleRms, "%.0f Hz parche %d: rms %.3g", hostRate, patch, level);

            // Y sin quedarse sin muestras: ni bloques vacíos ni desbordes.
            CHECK_MSG(e->stats.tooFewFrames == 0, "%.0f Hz parche %d: %lu", hostRate, patch,
                      e->stats.tooFewFrames.load());
            CHECK_MSG(e->stats.tooManyFrames == 0, "%.0f Hz parche %d: %lu", hostRate, patch,
                      e->stats.tooManyFrames.load());
        }
    }
}

/**
 * @brief render() no llama a `operator new` ni a `resample_open()`, con los tres efectos encendidos y
 *        MIDI en cada bloque.
 */
TEST_SUITE(engine_no_alloc_in_render)
{
    const int BLOCK = 512;
    auto e = make_engine(48000.0, BLOCK);
    REQUIRE_ENGINE(e);

    std::vector<float> l(BLOCK, 0.0f), r(BLOCK, 0.0f);

    // Un bloque de calentamiento fuera de la cuenta: el primero puede abrir el
    // resampler, que es justo lo que la siguiente comprobación prohíbe.
    note_on(e.get(), 60, 100);
    e->render(l.data(), r.data(), BLOCK);

    unsigned long opensBefore = e->stats.resamplerOpens;

    e->params.chorusEnabled = true;
    e->params.efxEnabled = true;
    e->params.tremoloEnabled = true;

    alloc_count_start();
    for (int b = 0; b < 32; b++)
    {
        note_on(e.get(), 60 + (b % 12), 100, b % BLOCK);
        note_off(e.get(), 60 + (b % 12), b % BLOCK);
        e->render(l.data(), r.data(), BLOCK);
    }
    const long allocs = alloc_count_stop();

    checks.add("render-sin-operator-new", allocs == 0, check_fmt("%ld reservas en 32 bloques", allocs));

    // El otro lado: libresample reserva con malloc, así que el contador de
    // arriba no lo ve. resample_open() cuesta 2,5 ms y 600 KB y no puede
    // ocurrir con el bloque en marcha.
    checks.add("render-sin-resample-open", e->stats.resamplerOpens == opensBefore,
               check_fmt("%lu aperturas", e->stats.resamplerOpens - opensBefore));
}

/**
 * @brief Chorus, trémolo y phaser encendidos con todos los parámetros en sus extremos: ni NaN ni Inf, y
 *        nada que se realimente.
 */
TEST_SUITE(engine_finite_at_extremes)
{
    const int BLOCK = 256;
    auto e = make_engine(44100.0, BLOCK);
    REQUIRE_ENGINE(e);

    struct Extreme
    {
        float volume;
        int chorusRate, chorusDepth;
        int tremoloRate, tremoloDepth;
        float phaserRate, phaserDepth;
    };
    const Extreme extremes[] = {
        {1.0f, 0, 0, 0, 0, 0.0f, 0.0f},
        {1.0f, 14, 14, 14, 14, 1.0f, 1.0f},
        {0.0f, 14, 0, 0, 14, 1.0f, 0.0f},
        {1.0f, 0, 14, 14, 0, 0.0f, 1.0f},
    };

    bool finite = true;
    bool inRange = true;

    for (const Extreme &x : extremes)
    {
        e->params.volume = x.volume;
        e->params.chorusEnabled = true;
        e->params.chorusRate = x.chorusRate;
        e->params.chorusDepth = x.chorusDepth;
        e->params.tremoloEnabled = true;
        e->params.tremoloRate = x.tremoloRate;
        e->params.tremoloDepth = x.tremoloDepth;
        e->params.efxEnabled = true;
        e->params.efxPhaserRate = x.phaserRate;
        e->params.efxPhaserDepth = x.phaserDepth;

        for (int n = 0; n < 16; n++)
            note_on(e.get(), 40 + n * 3, 127, n * 8);

        const int blocks[] = {BLOCK};
        Stereo out = render_blocks(e.get(), BLOCK * 40, blocks, 1);

        if (!all_finite(out.l) || !all_finite(out.r))
            finite = false;
        // Nada debería salir del rango de un float de audio por varios órdenes de
        // magnitud: si pasa, algo se está realimentando.
        if (peak(out.l) > 100.0 || peak(out.r) > 100.0)
            inRange = false;

        for (int n = 0; n < 16; n++)
            note_off(e.get(), 40 + n * 3);
    }

    CHECK(finite);
    CHECK(inRange);
}

/**
 * @brief La latencia declarada es el retardo de grupo del remuestreador (`Xoff`, en muestras de entrada,
 *        llevado a la tasa del host) en el peor caso, el parche de 20 kHz: positiva, menor de 5 ms,
 *        proporcional a la tasa del host e independiente del parche.
 */
TEST_SUITE(engine_latency)
{
    auto a = make_engine(48000.0, 512);
    auto b = make_engine(96000.0, 512);
    REQUIRE_ENGINE(a);
    REQUIRE_ENGINE(b);

    checks.add("latencia-declarada", a->latencySamples() > 0, check_fmt("%d muestras a 48 kHz", a->latencySamples()));
    checks.add("latencia-razonable", a->latencySamples() < (int)(0.005 * 48000.0),
               check_fmt("%d muestras a 48 kHz son %.2f ms", a->latencySamples(), a->latencySamples() / 48.0));

    // El retardo es un tiempo fijo: al doble de tasa, el doble de muestras.
    checks.add("latencia-escala-con-el-host", b->latencySamples() == a->latencySamples() * 2,
               check_fmt("%d a 48 kHz, %d a 96 kHz", a->latencySamples(), b->latencySamples()));

    // No depende del parche: cambiarlo no puede mover lo que ya se declaró.
    const int declared = a->latencySamples();
    a->setPatch(3);
    checks.add("latencia-constante-entre-parches", a->latencySamples() == declared,
               check_fmt("%d -> %d", declared, a->latencySamples()));
}

// ------------------------------------------------- cola tras el note-off

/**
 * @brief Segundos desde el último note-off hasta que la salida se queda por debajo de -60 dBFS para no
 *        volver. Renderiza a trozos y para en cuanto hay silencio, que es lo que hace barata la medida de los
 *        16 parches.
 */
static double measure_tail(int patch, double hostRate, double maxSecs)
{
    const int BLOCK = 512;
    auto e = make_engine(hostRate, BLOCK, patch);
    if (!e)
        return -1.0;

    // Sin efectos: lo que se mide es la cola del emulador. Las líneas de retardo
    // del chorus y del phaser son de milisegundos y no mueven el resultado.
    e->params.chorusEnabled = false;
    e->params.efxEnabled = false;
    e->params.tremoloEnabled = false;

    const int notes[] = {48, 55, 60, 64};
    for (int n : notes)
        note_on(e.get(), n, 127);

    Stereo held;
    render_into(e.get(), held, BLOCK, (int)(0.5 * hostRate) / BLOCK);
    if (peak(held.l) < kSoundingLevel)
        return -1.0; // no llegó a sonar: la medida no diría nada

    for (int n : notes)
        note_off(e.get(), n);

    const int chunks = (int)(0.050 * hostRate) / BLOCK;
    double tail = 0;
    int quiet = 0;
    for (double t = 0; t < maxSecs && quiet < 4; t += chunks * BLOCK / hostRate)
    {
        Stereo out;
        render_into(e.get(), out, BLOCK, chunks);
        const double p = peak(out.l) > peak(out.r) ? peak(out.l) : peak(out.r);
        if (p > kSilenceLevel)
        {
            tail = t + chunks * BLOCK / hostRate;
            quiet = 0;
        }
        else
            quiet++;
    }

    return tail;
}

/**
 * @brief La cola declarada cubre la cola real más larga de los 16 parches sin exagerarla más de tres veces:
 *        el anfitrión deja de pedir bloques al pasar ese tiempo sin eventos y cortaría el final al exportar.
 */
TEST_SUITE(engine_tail_length)
{
    double worst = 0;
    int worstPatch = -1;
    int measured = 0;

    for (int patch = 0; patch < NUM_PATCHES; patch++)
    {
        const double tail = measure_tail(patch, 48000.0, 6.0);
        if (tail < 0)
            continue;
        measured++;
        if (tail > worst)
        {
            worst = tail;
            worstPatch = patch;
        }
    }

    if (measured != NUM_PATCHES)
    {
        CHECK_MSG(false, "sin ROMs en %s (%d de %d parches medidos)", g_roms_dir.c_str(), measured, NUM_PATCHES);
        return;
    }

    checks.add("cola-declarada-cubre-la-real", worst < RdPianoEngine::kTailSeconds,
               check_fmt("%.2f s en el parche %d, %.2f s declarados", worst, worstPatch, RdPianoEngine::kTailSeconds));

    // Y que no sea una cifra inventada por lo alto: declarar de más obliga al
    // anfitrión a renderizar silencio en cada exportación.
    checks.add("cola-declarada-sin-exagerar", RdPianoEngine::kTailSeconds < worst * 3.0,
               check_fmt("%.2f s declarados para una cola de %.2f s", RdPianoEngine::kTailSeconds, worst));
}

/**
 * @brief prepare() dos veces seguidas es lo mismo que una, y tras release() prepare() deja el motor como
 *        recién construido: el mismo audio, muestra a muestra, que un motor nuevo.
 */
TEST_SUITE(engine_prepare_release_cycle)
{
    const int BLOCK = 512;
    auto play = [&](RdPianoEngine *e) {
        sustain(e, true);
        note_on(e, 60, 100);
        note_on(e, 67, 90, 100);
        Stereo out;
        render_into(e, out, BLOCK, 40);
        return out;
    };

    auto fresh = make_engine(48000.0, BLOCK);
    auto twice = make_engine(48000.0, BLOCK);
    auto cycled = make_engine(48000.0, BLOCK);
    REQUIRE_ENGINE(fresh);
    REQUIRE_ENGINE(twice);
    REQUIRE_ENGINE(cycled);

    twice->prepare(48000.0, BLOCK);

    // Un rato de uso con otra tasa y otro bloque antes de soltarlo todo.
    play(cycled.get());
    cycled->release();
    cycled->prepare(44100.0, 128);
    cycled->release();
    cycled->prepare(48000.0, BLOCK);

    const Stereo want = play(fresh.get());
    const Stereo gotTwice = play(twice.get());
    const Stereo gotCycled = play(cycled.get());

    checks.add("referencia-suena", rms(want.l, 0, want.l.size()) > kAudibleRms, "el motor nuevo salió mudo");
    checks.add("prepare-dos-veces-igual", gotTwice.l == want.l && gotTwice.r == want.r,
               "la salida difiere de la de un motor preparado una vez");
    checks.add("release-prepare-igual", gotCycled.l == want.l && gotCycled.r == want.r,
               "la salida difiere de la de un motor nuevo");
}
