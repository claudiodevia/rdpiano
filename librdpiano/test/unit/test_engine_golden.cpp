/**
 * @file test_engine_golden.cpp
 * @brief Hash bit-exacto de la salida del motor entero ante un guion fijo.
 *
 * Las demás suites `engine_*` comparan motores entre sí o miden propiedades; esta congela los bits. Cualquier
 * cambio en el orden de las operaciones en coma flotante de rd_engine.cpp la mueve. Como el golden de e2e: no
 * se regenera para ponerla en verde; un cambio de audio intencionado se escucha antes.
 */

#include <math.h>

#include <vector>

#include "engine_fixture.h"

namespace
{

    /** @brief Un paso del guion: qué se hace y en qué segundo. */
    struct Step
    {
        double at;                                  ///< Segundos desde el principio.
        void (*apply)(RdPianoEngine *e, int frame); ///< `frame` es la posición dentro del bloque.
    };

    /**
     * @brief Cruza los tres juegos de ROM y las dos tasas de emulador, afina, manda una ráfaga de program
     *        change, toca con pedal y mueve efectos, trémolo y volumen con notas sonando.
     */
    const Step kScript[] = {
        {0.00, [](RdPianoEngine *e, int f) { sustain(e, true, f); }},
        {0.00, [](RdPianoEngine *e, int f) { note_on(e, 48, 100, f); }},
        {0.01, [](RdPianoEngine *e, int f) { note_on(e, 60, 90, f); }},
        {0.30, [](RdPianoEngine *e, int f) { note_on(e, 64, 110, f); }},
        {0.30, [](RdPianoEngine *e, int) { e->params.tremoloEnabled = true; }},
        {0.50, [](RdPianoEngine *e, int) { e->requestPatch(3); }}, // MKS-20 B, 32 kHz
        {0.80, [](RdPianoEngine *e, int f) { note_off(e, 48, f); }},
        {0.80, [](RdPianoEngine *e, int f) { sustain(e, false, f); }},
        {0.85, [](RdPianoEngine *e, int f) { note_on(e, 67, 80, f); }},
        {1.00, [](RdPianoEngine *e, int) { e->params.efxEnabled = true; }},
        {1.00, [](RdPianoEngine *e, int) { e->params.chorusEnabled = false; }},
        {1.10, [](RdPianoEngine *e, int) { e->requestMasterTune(-12000); }},
        {1.40, [](RdPianoEngine *e, int f) { program_change(e, 9, f); }}, // MK-80
        {1.42, [](RdPianoEngine *e, int f) { program_change(e, 10, f); }},
        {1.44, [](RdPianoEngine *e, int f) { program_change(e, 11, f); }}, // 32 kHz
        {1.70, [](RdPianoEngine *e, int) { e->params.volume = 0.5f; }},
        {1.70, [](RdPianoEngine *e, int f) { note_on(e, 72, 127, f); }},
        {2.00, [](RdPianoEngine *e, int f) { note_off(e, 60, f); }},
        {2.00, [](RdPianoEngine *e, int f) { note_off(e, 64, f); }},
        {2.00, [](RdPianoEngine *e, int f) { note_off(e, 67, f); }},
        {2.00, [](RdPianoEngine *e, int f) { note_off(e, 72, f); }},
        {2.00, [](RdPianoEngine *e, int) { e->params.chorusEnabled = true; }},
        {2.00, [](RdPianoEngine *e, int) { e->requestPatch(0); }}, // MKS-20 A, 20 kHz
        {2.30, [](RdPianoEngine *e, int f) { sustain(e, true, f); }},
        {2.30, [](RdPianoEngine *e, int f) { note_on(e, 55, 100, f); }},
    };

    constexpr double kScriptSecs = 2.6;

    /** @brief Lo que deja el guion: el hash y con qué se puede comprobar que se ejecutó entero. */
    struct ScriptRun
    {
        bool ran = false;            ///< false si faltan las ROM.
        unsigned long long hash = 0; ///< FNV-1a de los bits de L y R, muestra a muestra.
        int finalPatch = -1;         ///< Parche activo al terminar.
        int finalTune = 0;           ///< Afinación activa al terminar.
        double level = 0;            ///< RMS del canal izquierdo entero.
    };

    /** @brief Corre el guion en bloques del tamaño que diga `blocks`, que se recorre cíclicamente. */
    ScriptRun run_script(double hostRate, const int *blocks, int numBlocks)
    {
        ScriptRun run;
        int maxBlock = 0;
        for (int i = 0; i < numBlocks; i++)
            if (blocks[i] > maxBlock)
                maxBlock = blocks[i];

        auto e = make_engine(hostRate, maxBlock);
        if (!e)
            return run;

        const int total = (int)frames_of(kScriptSecs, hostRate);
        const size_t numSteps = sizeof kScript / sizeof kScript[0];
        size_t next = 0;
        std::vector<float> l((size_t)maxBlock), r((size_t)maxBlock);
        Fnv1a h;
        double sumSquares = 0;

        int done = 0;
        for (int b = 0; done < total; b++)
        {
            int n = blocks[b % numBlocks];
            if (n > total - done)
                n = total - done;

            while (next < numSteps && (int)frames_of(kScript[next].at, hostRate) < done + n)
            {
                const int at = (int)frames_of(kScript[next].at, hostRate) - done;
                kScript[next].apply(e.get(), at > 0 ? at : 0);
                next++;
            }

            e->render(l.data(), r.data(), n);
            for (int i = 0; i < n; i++)
            {
                h.f32(l[(size_t)i]);
                h.f32(r[(size_t)i]);
                sumSquares += (double)l[(size_t)i] * l[(size_t)i];
            }
            done += n;
        }

        run.ran = true;
        run.hash = h.h;
        run.finalPatch = e->activePatch();
        run.finalTune = e->activeMasterTune();
        run.level = sqrt(sumSquares / total);
        return run;
    }

} // namespace

/**
 * @brief El guion a 44,1 y 48 kHz, en bloques regulares de 512 y en bloques irregulares, contra hashes
 *        capturados del motor.
 */
TEST_SUITE(engine_golden)
{
    const int regular[] = {512};
    const int irregular[] = {7, 300, 64, 512, 1, 129, 480, 33};

    struct Case
    {
        const char *name;
        double hostRate;
        const int *blocks;
        int numBlocks;
        unsigned long long want;
    };
    const Case cases[] = {
        {"44,1 kHz, bloques de 512", 44100.0, regular, 1, 0x3917cdd62e32560full},
        {"44,1 kHz, bloques irregulares", 44100.0, irregular, 8, 0xcdd2a2fb65882211ull},
        {"48 kHz, bloques de 512", 48000.0, regular, 1, 0x185645da1499b684ull},
        {"48 kHz, bloques irregulares", 48000.0, irregular, 8, 0xb778a514dd32a174ull},
    };

    for (const Case &c : cases)
    {
        const ScriptRun run = run_script(c.hostRate, c.blocks, c.numBlocks);
        ROMS_OR_FAIL(run.ran);

        // Que el hash no congele un guion a medias o un motor mudo.
        checks.add(check_fmt("%s: termina en el parche 0", c.name), run.finalPatch == 0,
                   check_fmt("parche %d", run.finalPatch));
        checks.add(check_fmt("%s: termina afinado", c.name), run.finalTune == -12000,
                   check_fmt("afinación %d", run.finalTune));
        checks.add(check_fmt("%s: suena", c.name), run.level > kSoundingLevel, check_fmt("rms %.5f", run.level));
        CHECK_HASH(c.name, run.hash, c.want);
    }
}
