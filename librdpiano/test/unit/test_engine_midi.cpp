/**
 * @file test_engine_midi.cpp
 * @brief MIDI del host: posición dentro del bloque, program change, ráfagas y nota repetida.
 */

#include <vector>

#include "engine_fixture.h"

/**
 * @brief Un evento suena en su frame dentro del bloque, no al principio, y dos eventos del mismo bloque se
 *        entregan en orden.
 */
TEST_SUITE(engine_midi_timing)
{
    const int BLOCK = 2048;
    const double HOST = 48000.0;

    auto e = make_engine(HOST, BLOCK);
    REQUIRE_ENGINE(e);

    std::vector<float> l(BLOCK, 0.0f), r(BLOCK, 0.0f);

    // Un bloque de silencio para asentar el arranque.
    e->render(l.data(), r.data(), BLOCK);

    const int onset = BLOCK / 2;
    note_on(e.get(), 60, 127, onset);
    e->render(l.data(), r.data(), BLOCK);

    // Nivel antes y después del frame pedido. Antes tiene que estar en
    // silencio; después, sonando.
    double before = rms(l, 0, (size_t)onset);
    double after = rms(l, (size_t)onset, (size_t)BLOCK);

    checks.add("midi-silencio-antes", before < after / 10.0, check_fmt("rms antes %.5g, después %.5g", before, after));
    checks.add("midi-suena-despues", after > kAudibleRms, check_fmt("rms después %.5g", after));

    // Y el frame exacto: la primera muestra que se separa del silencio no puede
    // caer antes de `onset`. El emulador tiene su propia latencia de ataque, así
    // que sólo se acota por abajo.
    size_t firstAudible = l.size();
    for (size_t i = 0; i < l.size(); i++)
        if (fabs((double)l[i]) > kAudibleRms)
        {
            firstAudible = i;
            break;
        }

    checks.add("midi-empieza-en-su-frame", firstAudible + 1 >= (size_t)onset,
               check_fmt("primera muestra audible %zu, pedida en %d", firstAudible, onset));

    // Dos eventos en el mismo bloque, en orden: el note-off tardío no puede
    // adelantarse al note-on.
    note_off(e.get(), 60);
    e->render(l.data(), r.data(), BLOCK);
    note_on(e.get(), 72, 127, 64);
    note_off(e.get(), 72, BLOCK - 64);
    e->render(l.data(), r.data(), BLOCK);
    CHECK(rms(l, 128, (size_t)BLOCK / 2) > kAudibleRms);
}

/**
 * @brief Un program change MIDI es un cambio de parche completo, página de parámetros incluida: el parche
 *        nuevo queda activo y suena.
 */
TEST_SUITE(engine_program_change)
{
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    REQUIRE_ENGINE(e);

    e->params.chorusEnabled = false;

    // Parche 5: otro juego de ROM que el 0.
    program_change(e.get(), 5);

    Stereo settle;
    render_into(e.get(), settle, BLOCK, 20);

    CHECK_EQ(e->patch(), 5);
    CHECK_EQ(e->activePatch(), 5);

    note_on(e.get(), 60, 110);

    Stereo out;
    render_into(e.get(), out, BLOCK, 60);

    const double level = rms(out.l, 0, out.l.size());
    checks.add("program-change-suena", level > kAudibleRms, check_fmt("rms %.6f tras el program change", level));
}

/**
 * @brief El número de programa es el parche, 0..15; uno mayor se ignora, deja el parche donde estaba y
 *        se cuenta en `stats.programIgnored`.
 */
TEST_SUITE(engine_program_change_range)
{
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    REQUIRE_ENGINE(e);
    e->params.chorusEnabled = false;

    Stereo out;

    // Dentro de rango: el programa es el parche. Entre uno y otro se deja pasar
    // la ventana de asentamiento, que es lo que colapsa las ráfagas.
    const int DENTRO[] = {5, 15, 0};
    for (int programa : DENTRO)
    {
        program_change(e.get(), programa);
        render_into(e.get(), out, BLOCK, 40); // ~213 ms
        checks.add(check_fmt("programa-%d-es-el-parche-%d", programa, programa), e->activePatch() == programa,
                   check_fmt("parche %d", e->activePatch()));
    }

    // Fuera de rango: se ignora, y el parche se queda donde estaba.
    const unsigned long ignoradosAntes = e->stats.programIgnored;
    const int FUERA[] = {16, 20, 127};
    for (int programa : FUERA)
    {
        program_change(e.get(), programa);
        render_into(e.get(), out, BLOCK, 40);
    }

    checks.add("programa-fuera-de-rango-no-cambia", e->activePatch() == 0 && e->patch() == 0,
               check_fmt("parche %d, pedido %d", e->activePatch(), e->patch()));
    checks.add("programa-fuera-de-rango-se-cuenta", e->stats.programIgnored - ignoradosAntes == 3,
               check_fmt("%lu ignorados", e->stats.programIgnored - ignoradosAntes));
}

/**
 * @brief Ventana de asentamiento del program change: el primero sale en el acto y de una ráfaga se cobra
 *        solo el último, porque cada cambio apaga el firmware y redispara lo pulsado.
 */
TEST_SUITE(engine_patch_settle)
{
    const int BLOCK = 256;

    {
        auto e = make_engine(48000.0, BLOCK);
        REQUIRE_ENGINE(e);
        e->params.chorusEnabled = false;

        // Uno suelto no espera: si esperase, la nota que viene detrás en la
        // secuencia sonaría con el parche viejo.
        Stereo out;
        program_change(e.get(), 7);
        render_into(e.get(), out, BLOCK, 6); // ~32 ms, la rampa de bajada son 6
        checks.add("uno-suelto-no-espera", e->activePatch() == 7, check_fmt("parche %d a los 32 ms", e->activePatch()));
    }

    {
        auto e = make_engine(48000.0, BLOCK);
        REQUIRE_ENGINE(e);
        e->params.chorusEnabled = false;

        // Ráfaga: veinte program change distintos, uno por bloque (~107 ms).
        std::vector<float> l(BLOCK, 0.0f), r(BLOCK, 0.0f);
        int cambios = 0;
        int anterior = e->activePatch();
        int ultimo = 0;

        for (int i = 0; i < 20; i++)
        {
            ultimo = 1 + (i % 15);
            program_change(e.get(), ultimo);
            e->render(l.data(), r.data(), BLOCK);
            if (e->activePatch() != anterior)
            {
                cambios++;
                anterior = e->activePatch();
            }
        }

        // Y lo que falte de ventana, para que salga el que quedó esperando.
        for (int i = 0; i < 60; i++)
        {
            e->render(l.data(), r.data(), BLOCK);
            if (e->activePatch() != anterior)
            {
                cambios++;
                anterior = e->activePatch();
            }
        }

        checks.add("rafaga-se-colapsa", cambios <= 2, check_fmt("%d cambios aplicados de 20 mensajes", cambios));
        checks.add("rafaga-gana-el-ultimo", e->activePatch() == ultimo,
                   check_fmt("parche %d, último pedido %d", e->activePatch(), ultimo));
    }
}

/**
 * @brief Trampa 14: un note-on repetido en la misma tecla sin note-off redispara y no deja ninguna voz
 *        colgada, con y sin pedal, en notas graves, medias y agudas.
 */
TEST_SUITE(engine_repeated_note)
{
    const int BLOCK = 256;
    const int NOTAS[] = {36, 60, 84};

    for (int conPedal = 0; conPedal < 2; conPedal++)
    {
        for (int nota : NOTAS)
        {
            auto e = make_engine(48000.0, BLOCK);
            REQUIRE_ENGINE(e);
            e->params.chorusEnabled = false;

            if (conPedal)
                sustain(e.get(), true);
            note_on(e.get(), nota, 100);

            Stereo primera;
            render_into(e.get(), primera, BLOCK, 8);

            // El segundo ataque sin note-off en medio: lo que colgaba la voz.
            note_on(e.get(), nota, 100, 10);
            Stereo segunda;
            render_into(e.get(), segunda, BLOCK, 8);

            // Redispara de verdad, no se queda muda de tanto protegerla.
            const double nivel = rms(segunda.l, 0, segunda.l.size());
            checks.add(check_fmt("repetida-suena-n%d-%s", nota, conPedal ? "pedal" : "seco"), nivel > kSilenceLevel,
                       check_fmt("rms %.6f tras el segundo ataque", nivel));

            // Y se apaga: pedal arriba y las 128 notas, como el pánico del host.
            sustain(e.get(), false);
            for (int n = 0; n < 128; n++)
                note_off(e.get(), n);

            Stereo cola;
            render_into(e.get(), cola, BLOCK, (int)(48000.0 * 3 / BLOCK));

            const size_t ultimo = cola.l.size() - (size_t)(48000.0 * 0.5);
            const double colaRms = rms(cola.l, ultimo, cola.l.size());
            checks.add(check_fmt("repetida-no-cuelga-n%d-%s", nota, conPedal ? "pedal" : "seco"),
                       colaRms < kHungVoiceRms,
                       check_fmt("rms %.6f dos segundos y medio después de apagarlo todo", colaRms));
        }
    }
}

/**
 * @brief El MIDI de un bloque que no se puede rendir llega igual al firmware: un note-off en él apaga la nota y
 *        el siguiente cambio de parche no la resucita.
 */
TEST_SUITE(engine_abort_delivers_midi)
{
    const int BLOCK = 256;
    auto e = make_engine(48000.0, BLOCK);
    REQUIRE_ENGINE(e);
    e->params.chorusEnabled = false;

    note_on(e.get(), 60, 100);
    Stereo ataque;
    render_into(e.get(), ataque, BLOCK, 16);

    // Un bloque de una muestra a 48 kHz son 0,4 muestras del emulador: se aborta.
    const unsigned long abortadosAntes = e->stats.tooFewFrames;
    note_off(e.get(), 60);
    std::vector<float> l(1, 0.0f), r(1, 0.0f);
    e->render(l.data(), r.data(), 1);
    checks.add("bloque-abortado", e->stats.tooFewFrames - abortadosAntes == 1, "el bloque de una muestra se rindió");

    Stereo cola;
    render_into(e.get(), cola, BLOCK, (int)(48000.0 * 1.5 / BLOCK));

    // Con el note-off perdido, el espejo seguiría con la tecla pulsada y el cambio la redispararía.
    e->setPatch(2);
    Stereo tras;
    render_into(e.get(), tras, BLOCK, (int)(48000.0 * 0.3 / BLOCK));
    const double nivel = rms(tras.l, 0, tras.l.size());
    checks.add("note-off-abortado-no-resucita", nivel < kSilenceLevel,
               check_fmt("rms %.5f tras el cambio de parche", nivel));
}
