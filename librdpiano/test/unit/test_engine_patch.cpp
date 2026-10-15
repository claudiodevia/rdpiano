/**
 * @file test_engine_patch.cpp
 * @brief Cambio de parche y de afinación en caliente: declick, espejo de lo pulsado y prepare() a medias.
 */

#include <math.h>

#include <vector>

#include "engine_fixture.h"

/**
 * @brief setPatch() entre bloques, cruzando juegos de ROM y tasas de emulador: cada parche suena y no hay
 *        saltos entre muestras de los que se oyen como clic.
 */
TEST_SUITE(engine_patch_change)
{
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    REQUIRE_ENGINE(e);

    // Parche 0 (20 kHz) -> parche 3 (32 kHz): el salto que cruza tasas de
    // emulador.
    const int sequence[] = {0, 3, 0, 11, 8};

    std::vector<float> l(BLOCK, 0.0f), r(BLOCK, 0.0f);
    double worstStep = 0;
    bool sounded = true;

    for (int patch : sequence)
    {
        e->setPatch(patch);
        note_on(e.get(), 60, 100);

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

        if (rms(out.l, out.l.size() / 2, out.l.size()) <= kAudibleRms)
            sounded = false;

        note_off(e.get(), 60);
        for (int b = 0; b < 8; b++)
            e->render(l.data(), r.data(), BLOCK);
    }

    checks.add("parche-suena", sounded, "algún parche se quedó mudo");
    checks.add("parche-sin-clic", worstStep < kClickStep, check_fmt("mayor salto entre muestras %.4f", worstStep));
}

/**
 * @brief Dos motores nuevos con la misma secuencia de parches, que cruza los tres juegos de ROM y vuelve, dan
 *        el mismo audio muestra a muestra: nada del cambio de parche depende de estado fuera del motor. Que la
 *        página cacheada sea la descifrada lo fija board_select_patch_page.
 */
TEST_SUITE(engine_patch_sequence)
{
    const int BLOCK = 256;

    // 0 y 2 comparten juego; 3, 8 y la vuelta a 0 lo cambian.
    const int sequence[] = {0, 2, 3, 8, 11, 0};

    Stereo runs[2];

    for (Stereo &out : runs)
    {
        auto e = make_engine(48000.0, BLOCK);
        REQUIRE_ENGINE(e);

        std::vector<float> l(BLOCK, 0.0f), r(BLOCK, 0.0f);

        for (int patch : sequence)
        {
            e->setPatch(patch);

            note_on(e.get(), 60, 100);
            for (int b = 0; b < 8; b++)
            {
                e->render(l.data(), r.data(), BLOCK);
                out.l.insert(out.l.end(), l.begin(), l.end());
                out.r.insert(out.r.end(), r.begin(), r.end());
            }

            note_off(e.get(), 60);
            for (int b = 0; b < 4; b++)
                e->render(l.data(), r.data(), BLOCK);
        }
    }

    const Stereo &first = runs[0];
    const Stereo &second = runs[1];
    checks.add("secuencia-misma-longitud", first.l.size() == second.l.size() && !first.l.empty(),
               check_fmt("%zu vs %zu", first.l.size(), second.l.size()));

    if (first.l.size() == second.l.size())
    {
        size_t diff = 0;
        for (size_t i = 0; i < first.l.size(); i++)
            if (first.l[i] != second.l[i] || first.r[i] != second.r[i])
                diff++;

        checks.add("secuencia-bit-exacta", diff == 0, check_fmt("%zu muestras distintas de %zu", diff, first.l.size()));
    }

    checks.add("secuencia-suena", rms(first.l, 0, first.l.size()) > kAudibleRms, "la secuencia salió muda");
}

/**
 * @brief requestPatch() con una voz decayendo: render() baja la salida antes de cambiar, así que durante el
 *        cambio no hay pico por encima de lo que sonaba ni salto, y el parche nuevo suena.
 */
TEST_SUITE(engine_patch_declick)
{
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    REQUIRE_ENGINE(e);

    e->params.chorusEnabled = false;
    note_on(e.get(), 60, 110);

    // Tecla soltada: la voz sigue en su decaimiento —que es lo que hace falta
    // para pillar el estallido— pero no queda nada pulsado, así que el cambio
    // no re-dispara nada y aquí se mide el declick solo. El re-disparo de lo
    // que sí sigue pulsado lo cubre engine_patch_held_notes.
    note_off(e.get(), 60, BLOCK / 2);

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
    checks.add("declick-sin-salto", stepDuring < stepBefore * kStepRatio,
               check_fmt("salto %.5f durante el cambio, %.5f antes", stepDuring, stepBefore));

    // Y el parche nuevo suena.
    note_on(e.get(), 60, 110);
    Stereo after;
    render_into(e.get(), after, BLOCK, 40);
    checks.add("declick-suena-despues", rms(after.l, 0, after.l.size()) > kAudibleRms, "el parche nuevo salió mudo");
}

/**
 * @brief El program change de reloadPatch() apaga voces y pedal dentro del firmware; el espejo de lo pulsado
 *        se los devuelve: el acorde y el pedal sobreviven al cambio, reentran sin golpe de tecla, una nota del
 *        mismo bloque que la petición suena y tras un pánico no se resucita nada.
 */
TEST_SUITE(engine_patch_held_notes)
{
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    REQUIRE_ENGINE(e);

    e->params.chorusEnabled = false;

    // Pedal abajo y un acorde aguantado.
    sustain(e.get(), true);
    note_on(e.get(), 60, 110);
    note_on(e.get(), 64, 110);
    note_on(e.get(), 67, 110);

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
    // había.
    std::vector<float> lastBefore(before.l.end() - frames_of(0.05, 48000.0), before.l.end());
    std::vector<float> onset(after.l.begin(), after.l.begin() + frames_of(0.35, 48000.0));
    const double peakBefore = peak(lastBefore);
    const double peakOnset = peak(onset);
    const double jumpDb = 20.0 * log10((peakOnset + 1e-9) / (peakBefore + 1e-9));
    checks.add("reentrada-sin-golpe", jumpDb < kRetriggerMaxJumpDb, check_fmt("pico de la reentrada %+.1f dB", jumpDb));
    checks.add("reentrada-audible", jumpDb > kRetriggerMinJumpDb, check_fmt("pico de la reentrada %+.1f dB", jumpDb));

    // Y el pedal sigue abajo: una nota corta tiene que dejar cola al soltarla.
    note_on(e.get(), 72, 110);
    note_off(e.get(), 72, BLOCK / 2);
    Stereo pedal;
    render_into(e.get(), pedal, BLOCK, 188);
    const double tail = rms(pedal.l, pedal.l.size() * 3 / 4, pedal.l.size());
    checks.add("pedal-sobrevive", tail > kSilenceLevel, check_fmt("cola %.6f tras soltar la tecla", tail));

    // Un note-on en el mismo bloque que la petición sale antes del program
    // change, que lo apaga: el espejo lo devuelve.
    auto f = make_engine(48000.0, BLOCK);
    REQUIRE_ENGINE(f);
    f->params.chorusEnabled = false;
    Stereo warm;
    render_into(f.get(), warm, BLOCK, 10);
    f->requestPatch(5);
    note_on(f.get(), 60, 110);
    Stereo sameBlock;
    render_into(f.get(), sameBlock, BLOCK, 150);
    checks.add("nota-del-bloque-del-cambio", peak(sameBlock.l) > kNotePeak, check_fmt("pico %.5f", peak(sameBlock.l)));

    // Tras un pánico no se resucita nada.
    auto g = make_engine(48000.0, BLOCK);
    REQUIRE_ENGINE(g);
    g->params.chorusEnabled = false;
    sustain(g.get(), true);
    note_on(g.get(), 60, 110);
    Stereo held;
    render_into(g.get(), held, BLOCK, 40);
    g->allNotesOff();
    Stereo quiet;
    render_into(g.get(), quiet, BLOCK, 188);
    g->requestPatch(5);
    Stereo afterPanic;
    render_into(g.get(), afterPanic, BLOCK, 188);
    checks.add("panico-no-resucita", peak(afterPanic.l) < kNotePeak,
               check_fmt("pico %.5f tras el cambio", peak(afterPanic.l)));
}

/**
 * @brief Afinar corre el switcharoo de la trampa 4, que apaga voces y pedal como un cambio de parche: pasa
 *        por el mismo declick y el mismo espejo, así que el acorde y el pedal sobreviven sin salto ni golpe.
 */
TEST_SUITE(engine_tune_held_notes)
{
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    REQUIRE_ENGINE(e);

    e->params.chorusEnabled = false;

    sustain(e.get(), true);
    note_on(e.get(), 60, 110);
    note_on(e.get(), 64, 110);
    note_on(e.get(), 67, 110);

    Stereo before;
    render_into(e.get(), before, BLOCK, 94); // ~0,5 s
    const double levelBefore = rms(before.l, before.l.size() / 2, before.l.size());
    const double stepBefore = worst_step(before.l, before.l.size() / 2, before.l.size());

    // Todo el recorrido del dial de una vez: el peor caso.
    e->requestMasterTune(32767);

    Stereo during;
    render_into(e.get(), during, BLOCK, 24);
    const double stepDuring = worst_step(during.l, 0, during.l.size());

    Stereo after;
    render_into(e.get(), after, BLOCK, 164);
    const double levelAfter = rms(after.l, after.l.size() / 2, after.l.size());

    checks.add("afinacion-aplicada", e->masterTune() == 32767, check_fmt("tune %d", (int)e->masterTune()));
    checks.add("acorde-sobrevive", levelAfter > levelBefore * 0.1,
               check_fmt("RMS %.5f tras afinar, %.5f antes", levelAfter, levelBefore));
    checks.add("sin-salto", stepDuring < stepBefore * kStepRatio,
               check_fmt("salto %.5f al afinar, %.5f antes", stepDuring, stepBefore));

    // Y reentra al nivel al que había llegado, no al de su ataque.
    std::vector<float> lastBefore(before.l.end() - frames_of(0.05, 48000.0), before.l.end());
    std::vector<float> onset(during.l.begin(), during.l.end());
    const double jumpDb = 20.0 * log10((peak(onset) + 1e-9) / (peak(lastBefore) + 1e-9));
    checks.add("reentrada-sin-golpe", jumpDb < kRetriggerMaxJumpDb, check_fmt("pico de la reentrada %+.1f dB", jumpDb));

    // El pedal sigue abajo: una nota corta tiene que dejar cola al soltarla.
    note_on(e.get(), 72, 110);
    note_off(e.get(), 72, BLOCK / 2);
    Stereo pedal;
    render_into(e.get(), pedal, BLOCK, 188);
    const double tail = rms(pedal.l, pedal.l.size() * 3 / 4, pedal.l.size());
    checks.add("pedal-sobrevive", tail > kSilenceLevel, check_fmt("cola %.6f tras soltar la tecla", tail));
}

/**
 * @brief prepare() con un cambio de parche o de afinación a medio atender (en la bajada del declick) lo
 *        aplica en vez de tirarlo: lo que suena y lo que enseña la interfaz coinciden, y manda lo más reciente.
 */
TEST_SUITE(engine_prepare_pending_change)
{
    // El bloque es corto a propósito: 64 muestras a 48 kHz son ~1,3 ms, menos de
    // los 6 ms de la bajada, así que el cambio queda pendiente de verdad.
    const int BLOCK = 64;
    std::vector<float> l(BLOCK, 0.0f), r(BLOCK, 0.0f);

    {
        auto e = make_engine(48000.0, BLOCK);
        REQUIRE_ENGINE(e);

        e->requestPatch(5);
        e->render(l.data(), r.data(), BLOCK);
        checks.add("parche-queda-pendiente", e->activePatch() != 5,
                   check_fmt("el cambio ya se aplicó en el primer bloque: parche %d", e->activePatch()));

        e->prepare(48000.0, BLOCK);
        checks.add("parche-pendiente-no-se-pierde", e->activePatch() == e->patch(),
                   check_fmt("suena %d, la interfaz enseña %d", e->activePatch(), e->patch()));
        checks.add("parche-pendiente-es-el-pedido", e->activePatch() == 5,
                   check_fmt("parche activo %d", e->activePatch()));

        // Y el parche repescado suena.
        note_on(e.get(), 60, 110);
        Stereo after;
        render_into(e.get(), after, BLOCK, 200);
        checks.add("parche-pendiente-suena", rms(after.l, 0, after.l.size()) > kAudibleRms,
                   "el parche repescado salió mudo");
    }

    {
        auto e = make_engine(48000.0, BLOCK);
        REQUIRE_ENGINE(e);

        e->requestMasterTune(1234);
        e->render(l.data(), r.data(), BLOCK);
        checks.add("afinacion-queda-pendiente", e->activeMasterTune() != 1234,
                   check_fmt("el afinado ya se aplicó en el primer bloque: %d", (int)e->activeMasterTune()));

        e->prepare(48000.0, BLOCK);
        checks.add("afinacion-pendiente-no-se-pierde", e->activeMasterTune() == e->masterTune(),
                   check_fmt("suena %d, la interfaz enseña %d", (int)e->activeMasterTune(), (int)e->masterTune()));
        checks.add("afinacion-pendiente-es-la-pedida", e->activeMasterTune() == 1234,
                   check_fmt("afinación activa %d", (int)e->activeMasterTune()));
    }

    // Con una petición nueva encima de la que ya espera en la rampa, manda la
    // nueva: es la más reciente.
    {
        auto e = make_engine(48000.0, BLOCK);
        REQUIRE_ENGINE(e);

        e->requestPatch(5);
        e->render(l.data(), r.data(), BLOCK);
        e->requestPatch(7);
        e->requestMasterTune(1234);

        e->prepare(48000.0, BLOCK);
        checks.add("manda-lo-mas-reciente", e->activePatch() == 7 && e->activePatch() == e->patch(),
                   check_fmt("suena %d, la interfaz enseña %d", e->activePatch(), e->patch()));
        checks.add("manda-lo-mas-reciente-afinacion", e->activeMasterTune() == e->masterTune(),
                   check_fmt("suena %d, la interfaz enseña %d", (int)e->activeMasterTune(), (int)e->masterTune()));
    }
}

/**
 * @brief Lo que solo sostiene el pedal no se devuelve al firmware tras el cambio de parche: resucitarlo serían
 *        decenas de ataques a la vez. El pedal sí vuelve (lo fija engine_patch_held_notes).
 */
TEST_SUITE(engine_patch_pedal_only_not_restored)
{
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    REQUIRE_ENGINE(e);
    e->params.chorusEnabled = false;

    sustain(e.get(), true);
    note_on(e.get(), 60, 110);
    note_on(e.get(), 64, 110);
    Stereo warm;
    render_into(e.get(), warm, BLOCK, 20);
    note_off(e.get(), 60);
    note_off(e.get(), 64);

    Stereo held;
    render_into(e.get(), held, BLOCK, 40);
    const double levelHeld = rms(held.l, held.l.size() / 2, held.l.size());
    checks.add("el-pedal-sostiene", levelHeld > kSilenceLevel, check_fmt("rms %.6f con las teclas sueltas", levelHeld));

    e->requestPatch(11);
    Stereo after;
    render_into(e.get(), after, BLOCK, 188);

    // Pasado el declick, nada: las voces del parche viejo las apagó el firmware y nadie las redisparó.
    const double tail = peak(std::vector<float>(after.l.begin() + (long)frames_of(0.2, 48000.0), after.l.end()));
    checks.add("pedal-solo-no-resucita", tail < kSilenceLevel, check_fmt("pico %.6f tras el cambio", tail));
}
