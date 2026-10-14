/**
 * @file test_engine_effects.cpp
 * @brief Efectos y salida: headroom, chorus y phaser (cola, bypass, LFO), volumen y trémolo.
 */

#include <math.h>

#include <vector>

#include "engine_fixture.h"
#include "patches.h"

/**
 * @brief Dieciséis voces a velocidad 127 dejan a los parches al mismo pico, el de `HEADROOM_TARGET_PEAK`
 *        (+6 dBFS, por encima del fondo de escala a propósito), y el peor caso con chorus no pasa de +12 dBFS.
 */
TEST_SUITE(engine_headroom)
{
    const int BLOCK = 512;

    // El objetivo de la tabla, con el margen de lo que la medida se mueve al
    // acortar la ventana (el harness mide 1,5 s; aquí son 0,64).
    const double TARGET = (double)HEADROOM_TARGET_PEAK;

    // Parche 0 y parche 6: el segundo es el más caliente de los 16 sin compensar
    // (+13,7 dBFS), así que si los dos caen en el mismo pico la compensación
    // está haciendo su trabajo y no es una ganancia global disfrazada.
    const int probes[] = {0, 6};
    for (int pi = 0; pi < 2; pi++)
    {
        auto e = make_engine(48000.0, BLOCK, probes[pi]);
        REQUIRE_ENGINE(e);

        // Sin efectos: lo que se mide es el headroom de la cadena seca.
        e->params.chorusEnabled = false;
        e->params.efxEnabled = false;
        e->params.tremoloEnabled = false;
        e->params.volume = 1.0f;

        for (int n = 0; n < 16; n++)
            note_on(e.get(), 48 + n, 127);

        const int blocks[] = {BLOCK};
        Stereo out = render_blocks(e.get(), BLOCK * 60, blocks, 1);

        double p = peak(out.l);
        checks.add(check_fmt("headroom-suena-p%d", probes[pi]), p > kSoundingLevel, check_fmt("pico %.4f", p));

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
        REQUIRE_ENGINE(e);

        e->params.chorusEnabled = true;
        e->params.efxEnabled = false;
        e->params.tremoloEnabled = false;
        e->params.volume = 1.0f;

        for (int n = 0; n < 16; n++)
            note_on(e.get(), 48 + n, 127);

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
 * @brief Encender un efecto en silencio absoluto da silencio: process() corre siempre, así que su línea de
 *        retardo no guarda audio viejo que soltar al reactivarlo.
 */
TEST_SUITE(engine_effect_tail)
{
    const int BLOCK = 512;
    const double RATE = 48000.0;

    for (int which = 0; which < 2; which++)
    {
        auto e = make_engine(RATE, BLOCK);
        REQUIRE_ENGINE(e);

        const bool chorus = which == 0;
        const char *name = chorus ? "chorus" : "phaser";

        e->params.chorusEnabled = chorus;
        e->params.efxEnabled = !chorus;

        // Un acorde con el efecto encendido: es lo que llena la línea.
        for (int n = 0; n < 6; n++)
            note_on(e.get(), 48 + n * 4, 127);

        Stereo loud;
        render_into(e.get(), loud, BLOCK, 60); // ~0,64 s

        // Se apaga el efecto y se sueltan las notas; luego, silencio de sobra.
        e->params.chorusEnabled = false;
        e->params.efxEnabled = false;
        for (int n = 0; n < 6; n++)
            note_off(e.get(), 48 + n * 4);

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

        checks.add(check_fmt("%s-suena-antes", name), peak(loud.l) > kSoundingLevel,
                   check_fmt("pico del acorde %.4f", peak(loud.l)));
        checks.add(check_fmt("%s-silencio-previo", name), before < kSilenceLevel,
                   check_fmt("pico %.8f en el silencio antes de encender", before));
        checks.add(check_fmt("%s-sin-cola-congelada", name), burst < kBurstPeak,
                   check_fmt("pico %.6f al encender en silencio", burst));
    }
}

/**
 * @brief Encender y apagar chorus y phaser con una nota sonando: la mezcla en rampa no deja un salto
 *        entre muestras que se despegue del de la señal.
 */
TEST_SUITE(engine_effect_bypass_ramp)
{
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    REQUIRE_ENGINE(e);

    e->params.chorusEnabled = false;
    e->params.efxEnabled = false;
    note_on(e.get(), 60, 110);

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
        checks.add(check_fmt("bypass-%s-sin-clic", t.name), step < typical * kBypassStepRatio,
                   check_fmt("salto %.5f frente a %.5f típico", step, typical));
    }
}

/**
 * @brief Bajar `volume` con una nota sonando: interpolado dentro del bloque, sin escalón, y el nivel llega
 *        al destino.
 */
TEST_SUITE(engine_volume_ramp)
{
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    REQUIRE_ENGINE(e);

    e->params.chorusEnabled = false;
    note_on(e.get(), 60, 110);

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

    checks.add("volumen-sin-escalon", stepRamp < stepBefore * kStepRatio,
               check_fmt("salto %.5f al bajar el volumen, %.5f antes", stepRamp, stepBefore));
    checks.add("volumen-llega", ratio > 0.1 && ratio < 0.35, check_fmt("nivel x%.3f con volume 1,0 -> 0,2", ratio));
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
        sustain(e.get(), true);
        note_on(e.get(), 48, 127);

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

/**
 * @brief El LFO del chorus va al ritmo del emulador: en un parche de 32 kHz modula 1,6 veces más rápido que
 *        en uno de 20 kHz con el mismo ajuste del panel.
 */
TEST_SUITE(engine_lfo_rate)
{
    // Esto NO es un fallo por corregir, aunque lo parezca: escalar `rate` por
    // 20000/sourceRate se implementó, se escuchó y se descartó —los cinco
    // parches sonaban peor con el LFO "corregido"—. Esta prueba está para que
    // volver a intentarlo falle y obligue a leer esto antes. Medido con el dial
    // de chorus a tope: 1,180 s a 20 kHz y 0,730 s a 32 kHz.
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
        sustain(e.get(), true);
        note_on(e.get(), 48, 127);

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

/**
 * @brief Las cuatro propiedades del trémolo: periodo de rate/2 Hz, al ritmo del host con cualquier parche
 *        (al contrario que el LFO de los efectos, engine_lfo_rate), canales en oposición de fase y profundidad
 *        lineal.
 */
TEST_SUITE(engine_tremolo)
{
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
