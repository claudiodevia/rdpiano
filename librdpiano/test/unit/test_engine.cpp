// El simulador de host: RdPianoEngine instanciado, no copiado.
//
// Una suite por riesgo: invariancia de bloque, bloques de borde, tasas del
// host, cero reservas en render(), finitud, cambio de parche en caliente,
// temporización del MIDI y headroom. Ninguna necesita JUCE ni un DAW.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <map>
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

// Las ROMs se leen una vez para todas las suites: cargarlas por prueba
// dominaría el tiempo de la suite unitaria.
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

// Construye un motor preparado. Devuelve NULL si faltan las ROMs: de que
// existan ya se queja patches_rom_files.
static RdPianoEngine *make_engine(double hostRate, int maxBlock, int patch = 0)
{
    EngineRoms &roms = engine_roms();
    if (!roms.ok)
        return nullptr;

    // El parche se selecciona ANTES de preparar, que es el orden del harness e2e
    // (loadSounds seguido de boot) y el que el plugin acaba teniendo: boot()
    // reinicia el firmware pero no el mapeo de la página de params, así que el
    // parche sobrevive al arranque.
    RdPianoEngine *e = new RdPianoEngine(roms.sets, roms.prog);
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

// Renderiza `total` muestras en bloques del tamaño que diga `blocks`, que se
// recorre cíclicamente.
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

// ------------------------------------------------- invariancia de bloque

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

    RdPianoEngine *a = make_engine(20000.0, TOTAL);
    RdPianoEngine *b = make_engine(20000.0, TOTAL);
    RdPianoEngine *c = make_engine(20000.0, TOTAL);
    if (!a || !b || !c)
    {
        CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
        delete a;
        delete b;
        delete c;
        return;
    }

    a->pushMidi(0, 0x90, 60, 100);
    b->pushMidi(0, 0x90, 60, 100);
    c->pushMidi(0, 0x90, 60, 100);

    const int one[] = {TOTAL};
    const int many[] = {7, 13, 1, 512, 256, 3, 1024, 64};

    Stereo whole = render_blocks(a, TOTAL, one, 1);
    Stereo split = render_blocks(b, TOTAL, many, 8);
    Stereo again = render_blocks(c, TOTAL, many, 8);

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

    delete a;
    delete b;
    delete c;
}

// ------------------------------------------------- bloques de borde

TEST_SUITE(engine_edge_blocks)
{
    const int MAXB = 512;
    RdPianoEngine *e = make_engine(48000.0, MAXB);
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

    delete e;
}

// ------------------------------------------------- tasas del host

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
            RdPianoEngine *e = make_engine(hostRate, BLOCK, patch);
            if (!e)
            {
                CHECK_MSG(false, "sin ROMs en %s", g_roms_dir.c_str());
                return;
            }

            e->pushMidi(0, 0x90, 60, 100);

            // Un segundo simulado, en bloques irregulares.
            const int blocks[] = {BLOCK, 128, 480, 64};
            int total = (int)hostRate;
            Stereo out = render_blocks(e, total, blocks, 4);

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

            delete e;
        }
    }
}

// ------------------------------------------------- sin reservas en RT

TEST_SUITE(engine_no_alloc_in_render)
{
    const int BLOCK = 512;
    RdPianoEngine *e = make_engine(48000.0, BLOCK);
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

    delete e;
}

// ------------------------------------------------- finitud

TEST_SUITE(engine_finite_at_extremes)
{
    // Los cuatro efectos activos y todos los parámetros en sus extremos: ni un
    // NaN ni un Inf.
    const int BLOCK = 256;
    RdPianoEngine *e = make_engine(44100.0, BLOCK);
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
        Stereo out = render_blocks(e, BLOCK * 40, blocks, 1);

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

    delete e;
}

// ------------------------------------------------- cambio de parche

TEST_SUITE(engine_patch_change)
{
    // Cambiar de parche entre bloques: tiene que seguir sonando, y sin una
    // discontinuidad de las que se oyen como un clic.
    const int BLOCK = 256;
    RdPianoEngine *e = make_engine(48000.0, BLOCK);
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

    delete e;
}

// ------------------------------------------------- temporización del MIDI

TEST_SUITE(engine_midi_timing)
{
    // pushMidi(frame, ...) -> la nota tiene que empezar a sonar en ese frame, no
    // al principio del bloque: con la comparación invertida, el bloque entero de
    // MIDI se consume en la primera muestra.
    const int BLOCK = 2048;
    const double HOST = 48000.0;

    RdPianoEngine *e = make_engine(HOST, BLOCK);
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

    delete e;
}

// ------------------------------------------------- headroom

TEST_SUITE(engine_headroom)
{
    // Dieciséis voces a velocidad 127: lo que se fija es que los 16 parches
    // queden al MISMO nivel, el de `HEADROOM_TARGET_PEAK`. No se fija "no
    // recorta": el objetivo son +3 dBFS y pasa de fondo de escala a propósito
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
        RdPianoEngine *e = make_engine(48000.0, BLOCK, probes[pi]);
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
        Stereo out = render_blocks(e, BLOCK * 60, blocks, 1);

        double p = peak(out.l);
        checks.add(check_fmt("headroom-suena-p%d", probes[pi]), p > 0.01, check_fmt("pico %.4f", p));

        checks.add(check_fmt("headroom-acotado-p%d", probes[pi]), p < TARGET * 1.5, check_fmt("pico %.4f", p));

        // Normalizado al objetivo de patches.h, no simplemente por debajo de 1.
        checks.add(check_fmt("headroom-normalizado-p%d", probes[pi]), p > TARGET * 0.9 && p < TARGET * 1.02,
                   check_fmt("pico %.4f, objetivo %.4f", p, TARGET));

        delete e;
    }

    // El peor caso medido de toda la cadena: el parche 5 con el chorus de
    // fábrica, +4,9 dB sobre la seca, o sea +7,9 dBFS. Recorta en la salida del
    // host —no hay limitador— y se acepta; lo que se fija es que no crezca.
    {
        RdPianoEngine *e = make_engine(48000.0, BLOCK, 5);
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
        Stereo out = render_blocks(e, BLOCK * 60, blocks, 1);

        double p = peak(out.l);
        if (peak(out.r) > p)
            p = peak(out.r);

        checks.add("headroom-peor-caso-con-chorus", p < TARGET * 2.0,
                   check_fmt("pico %.4f (%.1f dBFS)", p, 20.0 * log10(p)));

        delete e;
    }
}

// ------------------------------------------------- carga de ROM en dos fases

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
        RdPianoEngine *e = make_engine(48000.0, BLOCK);
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

        delete e;
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
