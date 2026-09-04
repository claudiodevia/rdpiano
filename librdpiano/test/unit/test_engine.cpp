/**
 * @file test_engine.cpp
 * @brief El simulador de host: RdPianoEngine instanciado, no copiado.
 *
 * Una suite por riesgo: invariancia de bloque, bloques de borde, tasas del host, cero reservas en
 * render(), finitud, cambio de parche en caliente, temporización del MIDI y headroom. Ninguna
 * necesita JUCE ni un DAW.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "patches.h"
#include "rd_engine.h"
#include "unit_test.h"

// ---------------------------------------------------------------- reservas
//
// Sustituye el `operator new` global durante render(): si la cadena reserva
// una sola vez en el hilo de audio, el contador lo dice. libresample usa
// malloc y no pasa por aquí; ese lado lo vigila stats.resamplerOpens.

static bool g_countAllocs = false;
static long g_allocCount = 0;

void *operator new(size_t n)
{
    if (g_countAllocs)
        g_allocCount++;
    void *p = malloc(n ? n : 1);
    return p;
}

void *operator new[](size_t n) { return operator new(n); }

void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
void operator delete[](void *p, size_t) noexcept { free(p); }

// ---------------------------------------------------------------- ROMs

/**
 * @brief Las ROMs se leen una vez para todas las suites: cargarlas por prueba dominaría el tiempo de la suite
 *        unitaria.
 */
struct EngineRoms
{
    std::map<std::string, std::vector<u8>> files;
    RdRomSet sets[ROMSET_COUNT];
    const u8 *prog = nullptr;
    bool ok = false;
};

static const u8 *load(EngineRoms &r, const char *name, size_t want)
{
    auto it = r.files.find(name);
    if (it == r.files.end())
    {
        std::vector<u8> buf(want, 0);
        FILE *f = fopen((g_roms_dir + "/" + name).c_str(), "rb");
        if (!f)
            return nullptr;
        size_t got = fread(buf.data(), 1, want, f);
        fclose(f);
        if (got != want)
            return nullptr;
        it = r.files.emplace(name, std::move(buf)).first;
    }
    return it->second.data();
}

static EngineRoms &engine_roms()
{
    static EngineRoms r;
    static bool tried = false;
    if (tried)
        return r;
    tried = true;

    r.prog = load(r, PROG_ROM_FILE, PROG_ROM_SIZE);
    if (!r.prog)
        return r;

    for (int s = 0; s < ROMSET_COUNT; s++)
    {
        r.sets[s].ic5 = load(r, romSetFiles[s][ROM_IC5], WAVE_ROM_SIZE);
        r.sets[s].ic6 = load(r, romSetFiles[s][ROM_IC6], WAVE_ROM_SIZE);
        r.sets[s].ic7 = load(r, romSetFiles[s][ROM_IC7], WAVE_ROM_SIZE);
        r.sets[s].ic18 = load(r, romSetFiles[s][ROM_IC18], WAVE_ROM_SIZE);
        if (!r.sets[s].ic5 || !r.sets[s].ic6 || !r.sets[s].ic7 || !r.sets[s].ic18)
            return r;
    }

    r.ok = true;
    return r;
}

/**
 * @brief Construye un motor preparado. Devuelve NULL si faltan las ROMs: de que existan ya se queja
 *        patches_rom_files.
 */
static std::unique_ptr<RdPianoEngine> make_engine(double hostRate, int maxBlock, int patch = 0)
{
    EngineRoms &roms = engine_roms();
    if (!roms.ok)
        return nullptr;

    // El parche se selecciona ANTES de preparar, que es el orden del harness e2e
    // (loadSounds seguido de boot) y el que el plugin acaba teniendo: boot()
    // reinicia el firmware pero no el mapeo de la página de params, así que el
    // parche sobrevive al arranque.
    auto e = std::make_unique<RdPianoEngine>(roms.sets, roms.prog);
    if (patch != 0)
        e->setPatch(patch);
    e->prepare(hostRate, maxBlock);
    return e;
}

// ---------------------------------------------------------------- utilidades

struct Stereo
{
    std::vector<float> l, r;
};

static double rms(const std::vector<float> &v, size_t from, size_t to)
{
    if (to <= from || to > v.size())
        return 0.0;
    double sum = 0;
    for (size_t i = from; i < to; i++)
        sum += (double)v[i] * v[i];
    return sqrt(sum / (double)(to - from));
}

static double peak(const std::vector<float> &v)
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

static bool all_finite(const std::vector<float> &v)
{
    for (float x : v)
        if (!isfinite(x))
            return false;
    return true;
}

/**
 * @brief Renderiza `total` muestras en bloques del tamaño que diga `blocks`, que se recorre cíclicamente.
 */
static Stereo render_blocks(RdPianoEngine *e, int total, const int *blocks, int numBlocks)
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

/**
 * @brief ------------------------------------------------- invariancia de bloque
 */
TEST_SUITE(engine_block_invariance)
{
    // El mismo stream, pedido de una vez y pedido a trozos irregulares. No es
    // bit a bit el mismo y no puede serlo: `renderBufferFrames` se redondea
    // hacia arriba y el corrector de deriva resta hasta numFrames/4, así que el
    // troceado cambia cuántas muestras genera el emulador (con algún hueco
    // corto audible como clic; defecto conocido, docs/REFACTORIZACION.md §19).
    //
    // Lo que sí se fija: misma longitud, mismo nivel, mismo rango de pendientes
    // —que es lo que rompería un estado guardado en una variable local del
    // bloque— y determinismo exacto.
    const int TOTAL = 4096;

    auto a = make_engine(20000.0, TOTAL);
    auto b = make_engine(20000.0, TOTAL);
    auto c = make_engine(20000.0, TOTAL);
    if (!a || !b || !c)
    {
        CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
        return;
    }

    a->pushMidi(0, 0x90, 60, 100);
    b->pushMidi(0, 0x90, 60, 100);
    c->pushMidi(0, 0x90, 60, 100);

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
    CHECK(levelWhole > 1e-2);
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

    // Caracterización del hueco descrito arriba: hoy son 23 muestras de silencio
    // exacto en 4.096 con una nota sonando. No se relaja; el día que el reparto
    // de muestras deje de perder cola, baja.
    int dropouts = 0;
    for (size_t i = 0; i < split.l.size(); i++)
        if (split.l[i] == 0.0f)
            dropouts++;
    checks.add("bloque-huecos-conocidos", dropouts <= 32,
               check_fmt("%d muestras de silencio exacto en %d", dropouts, TOTAL));
}

/**
 * @brief ------------------------------------------------- bloques de borde
 */
TEST_SUITE(engine_edge_blocks)
{
    const int MAXB = 512;
    auto e = make_engine(48000.0, MAXB);
    if (!e)
    {
        CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
        return;
    }

    e->pushMidi(0, 0x90, 60, 100);

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

    // Y con numFrames negativo, que un host no debería mandar pero que no puede
    // escribir en ningún sitio.
    std::vector<float> l(16, 0.0f), r(16, 0.0f);
    e->render(l.data(), r.data(), -1);
    CHECK(true); // llegar aquí sin caerse es la comprobación
}

/**
 * @brief ------------------------------------------------- tasas del host
 */
TEST_SUITE(engine_host_rates)
{
    // Longitud de salida exacta y sin deriva: el motor tiene que devolver
    // siempre numFrames muestras, sea cual sea la relación entre la tasa del
    // host y la del parche.
    const double rates[] = {22050.0, 44100.0, 48000.0, 88200.0, 96000.0};
    // Un parche de 20 kHz y otro de 32 kHz.
    const int patches[] = {0, 3};

    for (double hostRate : rates)
    {
        for (int patch : patches)
        {
            const int BLOCK = 512;
            auto e = make_engine(hostRate, BLOCK, patch);
            if (!e)
            {
                CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
                return;
            }

            e->pushMidi(0, 0x90, 60, 100);

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
            CHECK_MSG(level > 1e-4, "%.0f Hz parche %d: rms %.3g", hostRate, patch, level);

            // Y sin quedarse sin muestras: ni bloques vacíos ni desbordes.
            CHECK_MSG(e->stats.tooFewFrames == 0, "%.0f Hz parche %d: %lu", hostRate, patch, e->stats.tooFewFrames);
            CHECK_MSG(e->stats.tooManyFrames == 0, "%.0f Hz parche %d: %lu", hostRate, patch, e->stats.tooManyFrames);
        }
    }
}

/**
 * @brief ------------------------------------------------- sin reservas en RT
 */
TEST_SUITE(engine_no_alloc_in_render)
{
    const int BLOCK = 512;
    auto e = make_engine(48000.0, BLOCK);
    if (!e)
    {
        CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
        return;
    }

    std::vector<float> l(BLOCK, 0.0f), r(BLOCK, 0.0f);

    // Un bloque de calentamiento fuera de la cuenta: el primero puede abrir el
    // resampler, que es justo lo que la siguiente comprobación prohíbe.
    e->pushMidi(0, 0x90, 60, 100);
    e->render(l.data(), r.data(), BLOCK);

    unsigned long opensBefore = e->stats.resamplerOpens;

    e->params.chorusEnabled = true;
    e->params.efxEnabled = true;
    e->params.tremoloEnabled = true;

    g_allocCount = 0;
    g_countAllocs = true;
    for (int b = 0; b < 32; b++)
    {
        e->pushMidi(b % BLOCK, 0x90, 60 + (b % 12), 100);
        e->pushMidi(b % BLOCK, 0x80, 60 + (b % 12), 0);
        e->render(l.data(), r.data(), BLOCK);
    }
    g_countAllocs = false;

    checks.add("render-sin-operator-new", g_allocCount == 0, check_fmt("%ld reservas en 32 bloques", g_allocCount));

    // El otro lado: libresample reserva con malloc, así que el contador de
    // arriba no lo ve. resample_open() cuesta 2,5 ms y 600 KB y no puede
    // ocurrir con el bloque en marcha.
    checks.add("render-sin-resample-open", e->stats.resamplerOpens == opensBefore,
               check_fmt("%lu aperturas", e->stats.resamplerOpens - opensBefore));
}

/**
 * @brief ------------------------------------------------- finitud
 */
TEST_SUITE(engine_finite_at_extremes)
{
    // Los cuatro efectos activos y todos los parámetros en sus extremos: ni un
    // NaN ni un Inf.
    const int BLOCK = 256;
    auto e = make_engine(44100.0, BLOCK);
    if (!e)
    {
        CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
        return;
    }

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
            e->pushMidi(n * 8, 0x90, 40 + n * 3, 127);

        const int blocks[] = {BLOCK};
        Stereo out = render_blocks(e.get(), BLOCK * 40, blocks, 1);

        if (!all_finite(out.l) || !all_finite(out.r))
            finite = false;
        // Nada debería salir del rango de un float de audio por varios órdenes de
        // magnitud: si pasa, algo se está realimentando.
        if (peak(out.l) > 100.0 || peak(out.r) > 100.0)
            inRange = false;

        for (int n = 0; n < 16; n++)
            e->pushMidi(0, 0x80, 40 + n * 3, 0);
    }

    CHECK(finite);
    CHECK(inRange);
}

/**
 * @brief ------------------------------------------------- cambio de parche
 */
TEST_SUITE(engine_patch_change)
{
    // Cambiar de parche entre bloques: tiene que seguir sonando, y sin una
    // discontinuidad de las que se oyen como un clic.
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    if (!e)
    {
        CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
        return;
    }

    // Parche 0 (20 kHz) -> parche 3 (32 kHz): el salto que cruza frecuencias,
    // que es el que reabría los dos resamplers en el hilo de audio.
    const int sequence[] = {0, 3, 0, 11, 8};

    std::vector<float> l(BLOCK, 0.0f), r(BLOCK, 0.0f);
    double worstStep = 0;
    bool sounded = true;

    for (int patch : sequence)
    {
        e->setPatch(patch);
        e->pushMidi(0, 0x90, 60, 100);

        Stereo out;
        for (int b = 0; b < 24; b++)
        {
            e->render(l.data(), r.data(), BLOCK);
            out.l.insert(out.l.end(), l.begin(), l.end());
        }

        // Detector de clics: el mayor salto entre muestras consecutivas, dentro
        // del bloque, después de dejar pasar el ataque.
        for (size_t i = BLOCK + 1; i < out.l.size(); i++)
        {
            double step = fabs((double)out.l[i] - (double)out.l[i - 1]);
            if (step > worstStep)
                worstStep = step;
        }

        if (rms(out.l, out.l.size() / 2, out.l.size()) <= 1e-4)
            sounded = false;

        e->pushMidi(0, 0x80, 60, 0);
        for (int b = 0; b < 8; b++)
            e->render(l.data(), r.data(), BLOCK);
    }

    checks.add("parche-suena", sounded, "algún parche se quedó mudo");
    checks.add("parche-sin-clic", worstStep < 0.25, check_fmt("mayor salto entre muestras %.4f", worstStep));
}

/**
 * @brief ------------------------------------------------- temporización del MIDI
 */
TEST_SUITE(engine_midi_timing)
{
    // pushMidi(frame, ...) -> la nota tiene que empezar a sonar en ese frame, no
    // al principio del bloque: con la comparación invertida, el bloque entero de
    // MIDI se consume en la primera muestra.
    const int BLOCK = 2048;
    const double HOST = 48000.0;

    auto e = make_engine(HOST, BLOCK);
    if (!e)
    {
        CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
        return;
    }

    std::vector<float> l(BLOCK, 0.0f), r(BLOCK, 0.0f);

    // Un bloque de silencio para asentar el arranque.
    e->render(l.data(), r.data(), BLOCK);

    const int onset = BLOCK / 2;
    e->pushMidi(onset, 0x90, 60, 127);
    e->render(l.data(), r.data(), BLOCK);

    // Nivel antes y después del frame pedido. Antes tiene que estar en
    // silencio; después, sonando.
    double before = rms(l, 0, (size_t)onset);
    double after = rms(l, (size_t)onset, (size_t)BLOCK);

    checks.add("midi-silencio-antes", before < after / 10.0, check_fmt("rms antes %.5g, después %.5g", before, after));
    checks.add("midi-suena-despues", after > 1e-4, check_fmt("rms después %.5g", after));

    // Y el frame exacto: la primera muestra que se separa del silencio no puede
    // caer antes de `onset`. El emulador tiene su propia latencia de ataque, así
    // que sólo se acota por abajo.
    size_t firstAudible = l.size();
    for (size_t i = 0; i < l.size(); i++)
        if (fabs((double)l[i]) > 1e-4)
        {
            firstAudible = i;
            break;
        }

    checks.add("midi-empieza-en-su-frame", firstAudible + 1 >= (size_t)onset,
               check_fmt("primera muestra audible %zu, pedida en %d", firstAudible, onset));

    // Dos eventos en el mismo bloque, en orden: el note-off tardío no puede
    // adelantarse al note-on.
    e->pushMidi(0, 0x80, 60, 0);
    e->render(l.data(), r.data(), BLOCK);
    e->pushMidi(64, 0x90, 72, 127);
    e->pushMidi(BLOCK - 64, 0x80, 72, 0);
    e->render(l.data(), r.data(), BLOCK);
    CHECK(rms(l, 128, (size_t)BLOCK / 2) > 1e-4);
}

/**
 * @brief ------------------------------------------------- headroom
 */
TEST_SUITE(engine_headroom)
{
    // Dieciséis voces a velocidad 127: lo que se fija es que los 16 parches
    // queden al MISMO nivel, el de `HEADROOM_TARGET_PEAK`. No se fija "no
    // recorta": el objetivo son +6 dBFS y pasa de fondo de escala a propósito
    // (patches.h); lo que no puede es dispararse muy por encima.
    const int BLOCK = 512;

    // El objetivo de la tabla, con el margen de lo que la medida se mueve al
    // acortar la ventana (el harness mide 1,5 s; aquí son 0,64).
    const double TARGET = (double)HEADROOM_TARGET_PEAK;

    // Parche 0 y parche 6: el segundo era el más caliente de los 16 (+13,7 dBFS
    // sin compensar), así que si los dos caen en el mismo pico la compensación
    // está haciendo su trabajo y no es una ganancia global disfrazada.
    const int probes[] = {0, 6};
    for (int pi = 0; pi < 2; pi++)
    {
        auto e = make_engine(48000.0, BLOCK, probes[pi]);
        if (!e)
        {
            CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
            return;
        }

        // Sin efectos: lo que se mide es el headroom de la cadena seca.
        e->params.chorusEnabled = false;
        e->params.efxEnabled = false;
        e->params.tremoloEnabled = false;
        e->params.volume = 1.0f;

        for (int n = 0; n < 16; n++)
            e->pushMidi(0, 0x90, 48 + n, 127);

        const int blocks[] = {BLOCK};
        Stereo out = render_blocks(e.get(), BLOCK * 60, blocks, 1);

        double p = peak(out.l);
        checks.add(check_fmt("headroom-suena-p%d", probes[pi]), p > 0.01, check_fmt("pico %.4f", p));

        checks.add(check_fmt("headroom-acotado-p%d", probes[pi]), p < TARGET * 1.5, check_fmt("pico %.4f", p));

        // Normalizado al objetivo de patches.h, no simplemente por debajo de 1.
        checks.add(check_fmt("headroom-normalizado-p%d", probes[pi]), p > TARGET * 0.9 && p < TARGET * 1.02,
                   check_fmt("pico %.4f, objetivo %.4f", p, TARGET));
    }

    // El peor caso medido de toda la cadena: el parche 5 con el chorus de
    // fábrica, +4,8 dB sobre la seca, o sea +10,8 dBFS. Recorta en la salida del
    // host —no hay limitador— y se acepta; lo que se fija es que no crezca.
    {
        auto e = make_engine(48000.0, BLOCK, 5);
        if (!e)
        {
            CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
            return;
        }

        e->params.chorusEnabled = true;
        e->params.efxEnabled = false;
        e->params.tremoloEnabled = false;
        e->params.volume = 1.0f;

        for (int n = 0; n < 16; n++)
            e->pushMidi(0, 0x90, 48 + n, 127);

        const int blocks[] = {BLOCK};
        Stereo out = render_blocks(e.get(), BLOCK * 60, blocks, 1);

        double p = peak(out.l);
        if (peak(out.r) > p)
            p = peak(out.r);

        checks.add("headroom-peor-caso-con-chorus", p < TARGET * 2.0,
                   check_fmt("pico %.4f (%.1f dBFS)", p, 20.0 * log10(p)));
    }
}

/**
 * @brief ------------------------------------------------- carga de ROM en dos fases
 */
TEST_SUITE(engine_patch_prepare)
{
    // `prepareRomSetFor()` saca de `setPatch()` la parte cara —descifrar las
    // tres ROM de onda, ~2,9 ms— para que el integrador la corra FUERA del
    // cerrojo que serializa con render(). Lo que hay que fijar es que partirlo
    // no cambie NADA: la misma secuencia de parches tiene que dar el mismo
    // audio, muestra a muestra, se prepare antes o no.
    const int BLOCK = 256;

    // Cruza los tres juegos de ROM y vuelve: 0 y 2 comparten juego (preparar
    // ahí no tiene nada que hacer), 3 y 8 lo cambian.
    const int sequence[] = {0, 2, 3, 8, 11, 0};

    Stereo direct, split;

    for (int pass = 0; pass < 2; pass++)
    {
        auto e = make_engine(48000.0, BLOCK);
        if (!e)
        {
            CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
            return;
        }

        Stereo &out = pass == 0 ? direct : split;
        std::vector<float> l(BLOCK, 0.0f), r(BLOCK, 0.0f);

        for (int patch : sequence)
        {
            if (pass == 1)
            {
                // Lo que haría el hilo de UI antes de tomar el cerrojo. Repetido
                // a propósito: preparar dos veces, o preparar el juego que ya
                // está puesto, tiene que ser inocuo.
                e->prepareRomSetFor(patch);
                e->prepareRomSetFor(patch);
            }
            e->setPatch(patch);

            e->pushMidi(0, 0x90, 60, 100);
            for (int b = 0; b < 8; b++)
            {
                e->render(l.data(), r.data(), BLOCK);
                out.l.insert(out.l.end(), l.begin(), l.end());
                out.r.insert(out.r.end(), r.begin(), r.end());
            }

            e->pushMidi(0, 0x80, 60, 0);
            for (int b = 0; b < 4; b++)
                e->render(l.data(), r.data(), BLOCK);
        }
    }

    checks.add("preparar-misma-longitud", direct.l.size() == split.l.size() && !direct.l.empty(),
               check_fmt("%zu vs %zu", direct.l.size(), split.l.size()));

    if (direct.l.size() == split.l.size())
    {
        size_t diff = 0;
        for (size_t i = 0; i < direct.l.size(); i++)
            if (direct.l[i] != split.l[i] || direct.r[i] != split.r[i])
                diff++;

        checks.add("preparar-bit-exacto", diff == 0, check_fmt("%zu muestras distintas de %zu", diff, direct.l.size()));
    }

    checks.add("preparar-suena", rms(direct.l, 0, direct.l.size()) > 1e-4, "la secuencia salió muda");
}

// ------------------------------------------------- efectos: bypass y cola

/**
 * @brief El mayor salto entre muestras consecutivas de un tramo: la métrica de clic que usa docs/RENDIMIENTO-
 *        DIRECTO.md.
 */
static double worst_step(const std::vector<float> &v, size_t from, size_t to)
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

static void render_into(RdPianoEngine *e, Stereo &out, int block, int blocks)
{
    std::vector<float> l(block, 0.0f), r(block, 0.0f);
    for (int b = 0; b < blocks; b++)
    {
        e->render(l.data(), r.data(), block);
        out.l.insert(out.l.end(), l.begin(), l.end());
        out.r.insert(out.r.end(), r.begin(), r.end());
    }
}

TEST_SUITE(engine_effect_tail)
{
    // Apagar un efecto dejaba de llamar a su process(), que es lo único que
    // avanza la línea de retardo: se quedaba congelada con el último audio que
    // pasó por ella y lo soltaba ENTERO al reactivarla, un estallido de −11 dBFS
    // sin tocar una sola tecla. Con process() corriendo siempre, encender un
    // efecto en silencio absoluto tiene que seguir dando silencio.
    const int BLOCK = 512;
    const double RATE = 48000.0;

    for (int which = 0; which < 2; which++)
    {
        auto e = make_engine(RATE, BLOCK);
        if (!e)
        {
            CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
            return;
        }

        const bool chorus = which == 0;
        const char *name = chorus ? "chorus" : "phaser";

        e->params.chorusEnabled = chorus;
        e->params.efxEnabled = !chorus;

        // Un acorde con el efecto encendido: es lo que llena la línea.
        for (int n = 0; n < 6; n++)
            e->pushMidi(0, 0x90, 48 + n * 4, 127);

        Stereo loud;
        render_into(e.get(), loud, BLOCK, 60); // ~0,64 s

        // Se apaga el efecto y se sueltan las notas; luego, silencio de sobra.
        e->params.chorusEnabled = false;
        e->params.efxEnabled = false;
        for (int n = 0; n < 6; n++)
            e->pushMidi(0, 0x80, 48 + n * 4, 0);

        Stereo quiet;
        render_into(e.get(), quiet, BLOCK, 280); // ~3 s

        // Sólo la cola: al principio de este tramo todavía se está apagando el
        // acorde. Lo que interesa es que al final no queda absolutamente nada.
        double before = 0;
        for (size_t i = quiet.l.size() - (size_t)BLOCK * 40; i < quiet.l.size(); i++)
            if (fabs((double)quiet.l[i]) > before)
                before = fabs((double)quiet.l[i]);

        // Y ahora se enciende, sin tocar nada.
        e->params.chorusEnabled = chorus;
        e->params.efxEnabled = !chorus;

        Stereo after;
        render_into(e.get(), after, BLOCK, 40); // ~0,43 s

        const double burst = peak(after.l);

        checks.add(check_fmt("%s-suena-antes", name), peak(loud.l) > 0.01,
                   check_fmt("pico del acorde %.4f", peak(loud.l)));
        checks.add(check_fmt("%s-silencio-previo", name), before < 1e-3,
                   check_fmt("pico %.8f en el silencio antes de encender", before));
        checks.add(check_fmt("%s-sin-cola-congelada", name), burst < 5e-3,
                   check_fmt("pico %.6f al encender en silencio", burst));
    }
}

TEST_SUITE(engine_effect_bypass_ramp)
{
    // El bypass conmutaba de una muestra a la siguiente entre dos señales
    // distintas: un clic. Ahora es una mezcla en rampa, así que el mayor salto
    // en la conmutación no puede despegarse del de la señal normal.
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    if (!e)
    {
        CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
        return;
    }

    e->params.chorusEnabled = false;
    e->params.efxEnabled = false;
    e->pushMidi(0, 0x90, 60, 110);

    Stereo warm;
    render_into(e.get(), warm, BLOCK, 20);

    const double typical = worst_step(warm.l, warm.l.size() / 2, warm.l.size());

    struct Toggle
    {
        const char *name;
        bool chorus;
        bool efx;
    };
    const Toggle toggles[4] = {
        {"chorus-on", true, false}, {"chorus-off", false, false}, {"efx-on", false, true}, {"efx-off", false, false}};

    for (const Toggle &t : toggles)
    {
        e->params.chorusEnabled = t.chorus;
        e->params.efxEnabled = t.efx;

        Stereo out;
        render_into(e.get(), out, BLOCK, 20); // ~107 ms, la rampa son 10 ms

        const double step = worst_step(out.l, 0, out.l.size());
        checks.add(check_fmt("bypass-%s-sin-clic", t.name), step < typical * 3.0,
                   check_fmt("salto %.5f frente a %.5f típico", step, typical));
    }
}

/**
 * @brief ------------------------------------------------- program change
 */
TEST_SUITE(engine_program_change)
{
    // El program change se reenviaba al firmware tal cual y dejaba el motor
    // MUDO: cambiaba el número de parche pero no la página de parámetros, que
    // seguía siendo la del anterior. Ahora es un cambio de parche completo.
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    if (!e)
    {
        CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
        return;
    }

    e->params.chorusEnabled = false;

    // Parche 5: otro juego de ROM que el 0, que es el caso que se rompía.
    e->pushMidi(0, 0xC0, 5, 0);

    Stereo settle;
    render_into(e.get(), settle, BLOCK, 20);

    CHECK_EQ(e->patch(), 5);
    CHECK_EQ(e->activePatch(), 5);

    e->pushMidi(0, 0x90, 60, 110);

    Stereo out;
    render_into(e.get(), out, BLOCK, 60);

    const double level = rms(out.l, 0, out.l.size());
    checks.add("program-change-suena", level > 1e-4, check_fmt("rms %.6f tras el program change", level));
}

/**
 * @brief ------------------------------------------------- declick del cambio de parche
 */
TEST_SUITE(engine_patch_declick)
{
    // Cambiar de parche cortaba en seco lo que estuviera sonando, con un pico de
    // +3,5 a +6,5 dB sobre la propia nota: voces del parche viejo leyendo las
    // tablas de onda del nuevo. Pedido con requestPatch(), render() baja la
    // salida antes de cambiar, así que ni pico ni salto.
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    if (!e)
    {
        CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
        return;
    }

    e->params.chorusEnabled = false;
    e->pushMidi(0, 0x90, 60, 110);

    // Tecla soltada: la voz sigue en su decaimiento —que es lo que hace falta
    // para pillar el estallido— pero no queda nada pulsado, así que el cambio
    // no re-dispara nada y aquí se mide el declick solo. El re-disparo de lo
    // que sí sigue pulsado lo cubre engine_patch_held_notes.
    e->pushMidi(BLOCK / 2, 0x80, 60, 0);

    Stereo before;
    render_into(e.get(), before, BLOCK, 24);

    const double levelBefore = peak(before.l);
    const double stepBefore = worst_step(before.l, before.l.size() / 2, before.l.size());

    // Al otro juego de ROM y a otra tasa de emulador: el salto más largo.
    e->requestPatch(11);

    Stereo during;
    render_into(e.get(), during, BLOCK, 24);

    const double levelDuring = peak(during.l);
    const double stepDuring = worst_step(during.l, 0, during.l.size());

    checks.add("declick-aplicado", e->activePatch() == 11, check_fmt("parche activo %d", e->activePatch()));
    checks.add("declick-sin-pico", levelDuring <= levelBefore,
               check_fmt("pico %.4f durante el cambio, %.4f antes", levelDuring, levelBefore));
    checks.add("declick-sin-salto", stepDuring < stepBefore * 2.0,
               check_fmt("salto %.5f durante el cambio, %.5f antes", stepDuring, stepBefore));

    // Y el parche nuevo suena.
    e->pushMidi(0, 0x90, 60, 110);
    Stereo after;
    render_into(e.get(), after, BLOCK, 40);
    checks.add("declick-suena-despues", rms(after.l, 0, after.l.size()) > 1e-4, "el parche nuevo salió mudo");
}

/**
 * @brief ------------------------------------------- notas y pedal en el cambio
 */
TEST_SUITE(engine_patch_held_notes)
{
    // El cambio de parche manda un program change al firmware —única forma de
    // que relea la página de parámetros recién mapeada— y eso apaga las voces y
    // suelta el pedal dentro del firmware: lo que estuvieras tocando se quedaba
    // mudo hasta soltar las teclas y volver a pulsar. El motor guarda un espejo
    // de lo pulsado y se lo devuelve al firmware tras cambiar.
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    if (!e)
    {
        CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
        return;
    }

    e->params.chorusEnabled = false;

    // Pedal abajo y un acorde aguantado.
    e->pushMidi(0, 0xb0, 64, 127);
    e->pushMidi(0, 0x90, 60, 110);
    e->pushMidi(0, 0x90, 64, 110);
    e->pushMidi(0, 0x90, 67, 110);

    Stereo before;
    render_into(e.get(), before, BLOCK, 94); // ~0,5 s
    const double levelBefore = rms(before.l, before.l.size() / 2, before.l.size());

    // Al otro juego de ROM y a otra tasa de emulador, como en engine_patch_declick.
    e->requestPatch(11);

    Stereo after;
    render_into(e.get(), after, BLOCK, 188); // 1 s
    const double levelAfter = rms(after.l, after.l.size() / 2, after.l.size());

    checks.add("acorde-sobrevive", levelAfter > levelBefore * 0.1,
               check_fmt("RMS %.5f tras el cambio, %.5f antes", levelAfter, levelBefore));

    // Y reentra sin golpe de tecla: la velocidad se escala con lo que la nota
    // llevaba decaído, así que el pico de la reentrada se queda en el nivel que
    // había. Disparando con la velocidad original eran +10 dB de golpe.
    std::vector<float> lastBefore(before.l.end() - (size_t)(0.05 * 48000.0), before.l.end());
    std::vector<float> onset(after.l.begin(), after.l.begin() + (size_t)(0.35 * 48000.0));
    const double peakBefore = peak(lastBefore);
    const double peakOnset = peak(onset);
    const double jumpDb = 20.0 * log10((peakOnset + 1e-9) / (peakBefore + 1e-9));
    checks.add("reentrada-sin-golpe", jumpDb < 6.0, check_fmt("pico de la reentrada %+.1f dB", jumpDb));
    checks.add("reentrada-audible", jumpDb > -12.0, check_fmt("pico de la reentrada %+.1f dB", jumpDb));

    // Y el pedal sigue abajo: una nota corta tiene que dejar cola al soltarla.
    e->pushMidi(0, 0x90, 72, 110);
    e->pushMidi(BLOCK / 2, 0x80, 72, 0);
    Stereo pedal;
    render_into(e.get(), pedal, BLOCK, 188);
    const double tail = rms(pedal.l, pedal.l.size() * 3 / 4, pedal.l.size());
    checks.add("pedal-sobrevive", tail > 1e-3, check_fmt("cola %.6f tras soltar la tecla", tail));

    // Un note-on que caiga en el mismo bloque que la petición se enviaba antes
    // del program change y lo mataba: ahora el espejo lo revive.
    auto f = make_engine(48000.0, BLOCK);
    f->params.chorusEnabled = false;
    Stereo warm;
    render_into(f.get(), warm, BLOCK, 10);
    f->requestPatch(5);
    f->pushMidi(0, 0x90, 60, 110);
    Stereo sameBlock;
    render_into(f.get(), sameBlock, BLOCK, 150);
    checks.add("nota-del-bloque-del-cambio", peak(sameBlock.l) > 0.02, check_fmt("pico %.5f", peak(sameBlock.l)));

    // Tras un pánico no se resucita nada.
    auto g = make_engine(48000.0, BLOCK);
    g->params.chorusEnabled = false;
    g->pushMidi(0, 0xb0, 64, 127);
    g->pushMidi(0, 0x90, 60, 110);
    Stereo held;
    render_into(g.get(), held, BLOCK, 40);
    g->allNotesOff();
    Stereo quiet;
    render_into(g.get(), quiet, BLOCK, 188);
    g->requestPatch(5);
    Stereo afterPanic;
    render_into(g.get(), afterPanic, BLOCK, 188);
    checks.add("panico-no-resucita", peak(afterPanic.l) < 0.02,
               check_fmt("pico %.5f tras el cambio", peak(afterPanic.l)));
}

/**
 * @brief ------------------------------------------------- rampa de volumen
 */
TEST_SUITE(engine_volume_ramp)
{
    // `volume` se leía una vez por bloque y saltaba: un mando movido rápido son
    // decenas de escalones por segundo. Interpolado dentro del bloque, el salto
    // no se despega del de la señal y el destino se alcanza igual.
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    if (!e)
    {
        CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
        return;
    }

    e->params.chorusEnabled = false;
    e->pushMidi(0, 0x90, 60, 110);

    Stereo loud;
    render_into(e.get(), loud, BLOCK, 24);

    const double stepBefore = worst_step(loud.l, loud.l.size() / 2, loud.l.size());
    const double levelBefore = rms(loud.l, loud.l.size() - BLOCK, loud.l.size());

    e->params.volume = 0.2f;

    Stereo ramp;
    render_into(e.get(), ramp, BLOCK, 1);

    Stereo quiet;
    render_into(e.get(), quiet, BLOCK, 4);

    const double stepRamp = worst_step(ramp.l, 0, ramp.l.size());
    const double levelAfter = rms(quiet.l, 0, quiet.l.size());
    const double ratio = levelBefore > 0 ? levelAfter / levelBefore : 0;

    checks.add("volumen-sin-escalon", stepRamp < stepBefore * 2.0,
               check_fmt("salto %.5f al bajar el volumen, %.5f antes", stepRamp, stepBefore));
    checks.add("volumen-llega", ratio > 0.1 && ratio < 0.35, check_fmt("nivel x%.3f con volume 1,0 -> 0,2", ratio));
}

/**
 * @brief ------------------------------------------------- latencia declarada
 */
TEST_SUITE(engine_latency)
{
    // El retardo de grupo del remuestreador, para que el anfitrión lo compense
    // al grabar. Es `Xoff` (muestras de ENTRADA) llevado a la tasa del host, y
    // se declara el peor caso —parche de 20 kHz— para no renegociarlo en cada
    // cambio de sonido.
    auto a = make_engine(48000.0, 512);
    auto b = make_engine(96000.0, 512);
    if (!a || !b)
    {
        CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
        return;
    }

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

// ------------------------------------------------- velocidad del LFO

/**
 * @brief Periodo dominante de una envolvente muestreada cada `dt` segundos, por autocorrelación. Devuelve -1
 *        si no hay muestras suficientes o no hay periodo.
 */
static double dominant_period(const std::vector<double> &env, double dt)
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

/**
 * @brief Periodo dominante de la modulación del chorus, en segundos.
 *
 * El observable es la diferencia wet-dry dividida por el nivel seco: el emulador es determinista, así que dos
 * motores idénticos —uno con chorus y otro sin él— dan el mismo seco muestra a muestra, y normalizar quita la
 * caída de la nota. Sobre esa envolvente, autocorrelación. Devuelve -1 si no encuentra periodo.
 */
static double chorus_lfo_period(int patch, double hostRate, int block, double secs)
{
    std::vector<float> wet, dry;

    for (int pass = 0; pass < 2; pass++)
    {
        auto e = make_engine(hostRate, block, patch);
        if (!e)
            return -1.0;

        e->params.chorusEnabled = pass == 0;
        e->params.efxEnabled = false;
        e->params.tremoloEnabled = false;
        e->params.chorusRate = 14; // el más rápido del dial: cabe más de un ciclo
        e->params.chorusDepth = 14;

        // Pedal abajo para que la nota no se apague antes de tiempo.
        e->pushMidi(0, 0xB0, 64, 127);
        e->pushMidi(0, 0x90, 48, 127);

        Stereo out;
        render_into(e.get(), out, block, (int)(secs * hostRate) / block);
        (pass == 0 ? wet : dry) = out.l;
    }

    const int w = (int)(0.010 * hostRate); // ventanas de 10 ms
    std::vector<double> env;
    for (size_t i = 0; i + (size_t)w <= wet.size(); i += (size_t)w)
    {
        double sd = 0, sw = 0;
        for (int k = 0; k < w; k++)
        {
            const double d = dry[i + k];
            const double diff = (double)wet[i + k] - d;
            sd += d * d;
            sw += diff * diff;
        }
        sd = sqrt(sd / w);
        sw = sqrt(sw / w);
        env.push_back(sd > 1e-6 ? sw / sd : 0.0);
    }

    return dominant_period(env, 0.010);
}

TEST_SUITE(engine_lfo_rate)
{
    // El LFO de los efectos avanza una vez por muestra del EMULADOR, que corre a
    // 20 o a 32 kHz según el parche, así que los cinco parches de 32 kHz modulan
    // 32/20 = 1,6x más rápido con el mismo ajuste del panel. Medido con el dial
    // de chorus a tope: 1,180 s a 20 kHz y 0,730 s a 32 kHz.
    //
    // Esto NO es un fallo por corregir, aunque lo parezca y aunque
    // docs/RENDIMIENTO-DIRECTO.md §10.1 lo proponga: escalar `rate` por
    // 20000/sourceRate se implementó, se escuchó y se descartó —los cinco
    // parches sonaban peor con el LFO "corregido"—. Esta prueba está para que
    // volver a intentarlo falle y obligue a leer esto antes.
    const double p20 = chorus_lfo_period(0, 48000.0, 512, 4.0); // Piano 1, 20 kHz
    const double p32 = chorus_lfo_period(3, 48000.0, 512, 4.0); // Harpsichord, 32 kHz

    if (p20 < 0 || p32 < 0)
    {
        CHECK_MSG(false, "sin ROMs en %s, o LFO no medible (%.3f, %.3f)", g_roms_dir.c_str(), p20, p32);
        return;
    }

    const double expected = p20 * 20000.0 / 32000.0;
    checks.add("lfo-al-ritmo-del-emulador", fabs(p32 - expected) < p20 * 0.1,
               check_fmt("%.3f s a 20 kHz, %.3f s a 32 kHz (x%.2f, se esperaba x0,63)", p20, p32, p32 / p20));
}

// ------------------------------------------------- trémolo

/**
 * @brief La ganancia del trémolo, ventana a ventana y por canal. El emulador es determinista, así que dos
 *        motores idénticos —uno con trémolo y otro sin él— dan el mismo seco muestra a muestra y la razón de
 *        rms entre los dos ES la ganancia, ya sin la caída de la nota. El EQ va detrás del trémolo y suaviza
 *        un poco la medida: por eso las tolerancias no son estrechas.
 */
struct TremoloEnvelope
{
    std::vector<double> l, r;
    static constexpr double kWindow = 0.002; // segundos por muestra de la envolvente
};

static bool tremolo_envelope(TremoloEnvelope &env, int patch, double hostRate, int rate, int depth, double secs)
{
    const int BLOCK = 256;
    Stereo wet, dry;

    for (int pass = 0; pass < 2; pass++)
    {
        auto e = make_engine(hostRate, BLOCK, patch);
        if (!e)
            return false;

        e->params.chorusEnabled = false;
        e->params.efxEnabled = false;
        e->params.tremoloEnabled = pass == 0;
        e->params.tremoloRate = rate;
        e->params.tremoloDepth = depth;

        // Pedal abajo para que la nota no se apague antes de tiempo.
        e->pushMidi(0, 0xB0, 64, 127);
        e->pushMidi(0, 0x90, 48, 127);

        render_into(e.get(), pass == 0 ? wet : dry, BLOCK, (int)(secs * hostRate) / BLOCK);
    }

    const size_t w = (size_t)(TremoloEnvelope::kWindow * hostRate);
    env.l.clear();
    env.r.clear();
    for (size_t i = 0; i + w <= wet.l.size(); i += w)
    {
        const double dl = rms(dry.l, i, i + w), wl = rms(wet.l, i, i + w);
        const double dr = rms(dry.r, i, i + w), wr = rms(wet.r, i, i + w);
        env.l.push_back(dl > 1e-6 ? wl / dl : 0.0);
        env.r.push_back(dr > 1e-6 ? wr / dr : 0.0);
    }

    return env.l.size() >= 40;
}

/**
 * @brief Recorrido de la envolvente, saltándose el arranque (ataque de la nota y rampas de volumen y de
 *        headroom).
 */
static void envelope_range(const std::vector<double> &v, double &lo, double &hi)
{
    lo = 1e9;
    hi = -1e9;
    for (size_t i = v.size() / 5; i < v.size(); i++)
    {
        if (v[i] < lo)
            lo = v[i];
        if (v[i] > hi)
            hi = v[i];
    }
}

TEST_SUITE(engine_tremolo)
{
    // El trémolo es lo último de la cadena antes del EQ y lo único que modula
    // los dos canales en oposición de fase. Se fijan sus cuatro propiedades:
    // el periodo (rate/2 Hz), que ese periodo va al ritmo del HOST —y no al del
    // emulador, como el LFO de los efectos: ver engine_lfo_rate—, la oposición
    // de fase entre canales y la profundidad.
    const double HOST = 48000.0;
    const double SECS = 1.2;

    TremoloEnvelope full;
    if (!tremolo_envelope(full, 0, HOST, 14, 14, SECS))
    {
        CHECK_MSG(false, "sin ROMs en %s, o envolvente no medible", g_roms_dir.c_str());
        return;
    }

    // rate 14 son 7 Hz, o sea 0,143 s de periodo.
    const double period = dominant_period(full.l, TremoloEnvelope::kWindow);
    checks.add("tremolo-periodo", period > 0 && fabs(period - 2.0 / 14.0) < 0.15 * (2.0 / 14.0),
               check_fmt("%.4f s medidos, %.4f s esperados", period, 2.0 / 14.0));

    // Oposición de fase: gL = (1-d) + d*(0,5+0,5 sen x) y gR lo mismo con
    // sen(pi+x) = -sen(x), así que gL + gR = 2 - d en todo momento. Con d = 1
    // (depth 14) la suma vale 1 y cada canal recorre el rango entero.
    double loL, hiL, loSum = 1e9, hiSum = -1e9;
    envelope_range(full.l, loL, hiL);
    {
        std::vector<double> sum;
        sum.reserve(full.l.size());
        for (size_t i = 0; i < full.l.size(); i++)
            sum.push_back(full.l[i] + full.r[i]);
        envelope_range(sum, loSum, hiSum);
    }

    checks.add("tremolo-canales-en-oposicion", hiSum - loSum < 0.25 && fabs((hiSum + loSum) / 2 - 1.0) < 0.2,
               check_fmt("suma L+R entre %.3f y %.3f, se esperaba 1,0 constante", loSum, hiSum));
    checks.add("tremolo-profundidad-14", hiL - loL > 0.75, check_fmt("ganancia entre %.3f y %.3f", loL, hiL));

    // La mitad de rate, el doble de periodo: el dial es lineal en frecuencia.
    TremoloEnvelope half;
    if (!tremolo_envelope(half, 0, HOST, 7, 14, SECS))
    {
        CHECK_MSG(false, "envolvente no medible con rate 7");
        return;
    }
    const double periodHalf = dominant_period(half.l, TremoloEnvelope::kWindow);
    checks.add("tremolo-rate-lineal", periodHalf > 0 && fabs(periodHalf - 2 * period) < 0.2 * periodHalf,
               check_fmt("%.4f s con rate 7, %.4f s con rate 14", periodHalf, period));

    // Y a mitad de profundidad, la mitad de recorrido.
    TremoloEnvelope shallow;
    if (!tremolo_envelope(shallow, 0, HOST, 14, 7, SECS))
    {
        CHECK_MSG(false, "envolvente no medible con depth 7");
        return;
    }
    double loS, hiS;
    envelope_range(shallow.l, loS, hiS);
    checks.add("tremolo-profundidad-7", fabs((hiS - loS) - 7.0 / 14.0) < 0.2,
               check_fmt("recorrido %.3f, se esperaba 0,5", hiS - loS));

    // Parche de 32 kHz: el trémolo va detrás del remuestreador, así que su
    // periodo no puede moverse con la tasa del emulador. Justo lo contrario del
    // LFO de los efectos, que sí se mueve (engine_lfo_rate).
    TremoloEnvelope fast;
    if (!tremolo_envelope(fast, 3, HOST, 14, 14, SECS))
    {
        CHECK_MSG(false, "envolvente no medible en el parche 3");
        return;
    }
    const double period32 = dominant_period(fast.l, TremoloEnvelope::kWindow);
    checks.add("tremolo-al-ritmo-del-host", period32 > 0 && fabs(period32 - period) < 0.15 * period,
               check_fmt("%.4f s a 20 kHz, %.4f s a 32 kHz", period, period32));
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
        e->pushMidi(0, 0x90, n, 127);

    Stereo held;
    render_into(e.get(), held, BLOCK, (int)(0.5 * hostRate) / BLOCK);
    if (peak(held.l) < 0.01)
        return -1.0; // no llegó a sonar: la medida no diría nada

    for (int n : notes)
        e->pushMidi(0, 0x80, n, 0);

    const double kSilence = 1e-3; // -60 dBFS
    const int chunks = (int)(0.050 * hostRate) / BLOCK;
    double tail = 0;
    int quiet = 0;
    for (double t = 0; t < maxSecs && quiet < 4; t += chunks * BLOCK / hostRate)
    {
        Stereo out;
        render_into(e.get(), out, BLOCK, chunks);
        const double p = peak(out.l) > peak(out.r) ? peak(out.l) : peak(out.r);
        if (p > kSilence)
        {
            tail = t + chunks * BLOCK / hostRate;
            quiet = 0;
        }
        else
            quiet++;
    }

    return tail;
}

TEST_SUITE(engine_tail_length)
{
    // `tailLengthSeconds()` es lo que el plugin le declara al anfitrión, y un
    // anfitrión se lo cree: al exportar deja de pedir bloques cuando pasa ese
    // tiempo sin eventos. Si la cola real de algún parche lo pasa, el final de
    // la última nota se corta en el fichero exportado.
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
