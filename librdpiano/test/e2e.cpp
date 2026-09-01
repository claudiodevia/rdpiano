// Harness de verificación end-to-end, headless y sin dependencias externas.
//
// Arranca el firmware real, carga cada uno de los 16 parches, le inyecta una
// secuencia MIDI fija y mide el audio resultante. Sirve para dos cosas:
//
//   1) comprobaciones de cordura (¿suena?, ¿se apaga la nota?, ¿voces colgadas?)
//   2) hash bit-exacto del stream por parche -> detecta cualquier cambio de
//      audio introducido por sound_chip.cpp, los UNSCRAMBLE_* o el MCU.
//
// Uso:
//   rdpiano_e2e [--roms DIR] [--patch N] [--wav-dir DIR]
//               [--write-golden FILE] [--golden FILE]

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "mame_utils.h"
#include "mcu.h"
#include "patches.h"
#include "rd_engine.h"

// ---------------------------------------------------------------- ROMs
//
// Los nombres de fichero, los tamaños y la tabla de juegos viven en patches.h:
// son los mismos que empotra el plugin.

static std::vector<u8> load_rom(const std::string &dir, const char *name, size_t len)
{
    std::string path = dir + "/" + name;
    std::vector<u8> data(len, 0);

    FILE *f = fopen(path.c_str(), "rb");
    if (f == NULL)
    {
        fprintf(stderr, "ERROR: no se puede abrir %s\n", path.c_str());
        exit(2);
    }
    size_t read = fread(data.data(), 1, len, f);
    fclose(f);

    if (read != len)
    {
        fprintf(stderr, "ERROR: %s tiene %zu bytes, se esperaban %zu\n", path.c_str(), read, len);
        exit(2);
    }

    return data;
}

struct RomBank
{
    std::map<std::string, std::vector<u8>> cache;
    std::string dir;

    const u8 *get(const char *name, size_t len)
    {
        auto it = cache.find(name);
        if (it == cache.end())
            it = cache.emplace(name, load_rom(dir, name, len)).first;
        return it->second.data();
    }
};

// ---------------------------------------------------------------- render

struct Accum
{
    double sumSquares = 0;
    size_t count = 0;
    s32 peak = 0;
    u64 hash = 0xcbf29ce484222325ull; // FNV-1a 64

    void add(s32 sample)
    {
        sumSquares += (double)sample * (double)sample;
        count++;
        s32 mag = sample < 0 ? -sample : sample;
        if (mag > peak)
            peak = mag;

        u32 bits = (u32)sample;
        for (int b = 0; b < 4; b++)
        {
            hash ^= (bits >> (b * 8)) & 0xff;
            hash *= 0x100000001b3ull;
        }
    }

    double rms() const { return count ? sqrt(sumSquares / count) : 0.0; }
};

// El plugin escala así la señal seca: (sample << 5 >> 6) / 65536 * 0.5, y
// después la compensación de headroom del parche (patches.h). Sólo afecta a
// los WAV de --wav-dir, que existen para escuchar lo que sale del producto: el
// hash y las comprobaciones van sobre la muestra cruda del emulador.
static float plugin_scale(s32 sample, float gain) { return (float)(sample / 2) / 65536.0f * 0.5f * gain; }

// Renderiza nSamples, acumulando en `total` y opcionalmente en `window`.
static void render(Mcu *mcu, bool rate32, size_t nSamples, Accum *total, Accum *window, std::vector<float> *wav,
                   float wavGain = 1.0f)
{
    for (size_t i = 0; i < nSamples; i++)
    {
        s32 sample = mcu->generate_next_sample(rate32);
        if (total)
            total->add(sample);
        if (window)
            window->add(sample);
        if (wav)
            wav->push_back(plugin_scale(sample, wavGain));
    }
}

// ---------------------------------------------------------------- WAV

static void write_u32(FILE *f, u32 v) { fwrite(&v, 4, 1, f); }
static void write_u16(FILE *f, u16 v) { fwrite(&v, 2, 1, f); }

static bool write_wav(const std::string &path, const std::vector<float> &samples, int sampleRate)
{
    FILE *f = fopen(path.c_str(), "wb");
    if (!f)
    {
        fprintf(stderr, "ERROR: no se puede escribir %s\n", path.c_str());
        return false;
    }

    u32 dataBytes = (u32)samples.size() * 2;
    fwrite("RIFF", 1, 4, f);
    write_u32(f, 36 + dataBytes);
    fwrite("WAVEfmt ", 1, 8, f);
    write_u32(f, 16); // tamaño del bloque fmt
    write_u16(f, 1);  // PCM
    write_u16(f, 1);  // mono
    write_u32(f, sampleRate);
    write_u32(f, sampleRate * 2); // byte rate
    write_u16(f, 2);              // block align
    write_u16(f, 16);             // bits
    fwrite("data", 1, 4, f);
    write_u32(f, dataBytes);

    for (float s : samples)
    {
        float clamped = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);
        write_u16(f, (u16)(s16)lrintf(clamped * 32767.0f));
    }

    fclose(f);
    return true;
}

// ---------------------------------------------------------------- escenario

// Duraciones en segundos de cada fase de la prueba.
static const double SILENCE_SECS = 0.10;
static const double NOTE_SECS = 0.50;
static const double CHORD_SECS = 0.50;
static const double RELEASE_SECS = 2.50;
static const double POLY_SECS = 0.40;
static const double POLY_RELEASE_SECS = 2.50;
static const double TAIL_WINDOW_SECS = 0.25; // final del release que se mide

struct PatchResult
{
    double silenceRms = 0;
    double noteRms = 0;
    double chordRms = 0;
    double releaseTailRms = 0;
    double polyRms = 0;
    double polyTailRms = 0;
    s32 peak = 0;
    u64 hash = 0;
    double emulatedSecs = 0;
};

static PatchResult run_patch(int patch, RomBank &roms, std::vector<float> *wav)
{
    const char *const *set = romSetFiles[patchToRomSetId[patch]];
    const u8 *ic5 = roms.get(set[ROM_IC5], WAVE_ROM_SIZE);
    const u8 *ic6 = roms.get(set[ROM_IC6], WAVE_ROM_SIZE);
    const u8 *ic7 = roms.get(set[ROM_IC7], WAVE_ROM_SIZE);
    const u8 *ic18 = roms.get(set[ROM_IC18], WAVE_ROM_SIZE);
    const u8 *prog = roms.get(PROG_ROM_FILE, PROG_ROM_SIZE);

    const int rate = patchSampleRates[patch];
    const bool rate32 = rate == 32000;
    const float wavGain = patchOutputGain[patch];

    Mcu *mcu = new Mcu(ic5, ic6, ic7, prog, ic18);
    mcu->loadSounds(ic5, ic6, ic7, ic18, patchToOffset[patch]);

    // El mismo arranque que el plugin, no una copia. El harness calienta al
    // ritmo del parche y el plugin siempre a 20 kHz: por esa divergencia
    // (trampa 7 de CLAUDE.md) boot() obliga a decir cuál se usa.
    mcu->boot(0, rate32);

    PatchResult r;
    Accum total;

    // 1. Silencio tras el arranque: no debería sonar nada todavía.
    {
        Accum w;
        render(mcu, rate32, (size_t)(SILENCE_SECS * rate), &total, &w, wav, wavGain);
        r.silenceRms = w.rms();
    }

    // 2. Nota sola.
    mcu->sendMidiCmd(0x90, 60, 100);
    {
        Accum w;
        render(mcu, rate32, (size_t)(NOTE_SECS * rate), &total, &w, wav, wavGain);
        r.noteRms = w.rms();
    }

    // 3. Acorde encima de la nota anterior.
    mcu->sendMidiCmd(0x90, 64, 100);
    mcu->sendMidiCmd(0x90, 67, 100);
    mcu->sendMidiCmd(0x90, 72, 100);
    {
        Accum w;
        render(mcu, rate32, (size_t)(CHORD_SECS * rate), &total, &w, wav, wavGain);
        r.chordRms = w.rms();
    }

    // 4. Todo apagado: la cola debe extinguirse (voces colgadas).
    mcu->sendMidiCmd(0x80, 60, 0);
    mcu->sendMidiCmd(0x80, 64, 0);
    mcu->sendMidiCmd(0x80, 67, 0);
    mcu->sendMidiCmd(0x80, 72, 0);
    {
        size_t frames = (size_t)(RELEASE_SECS * rate);
        size_t tailFrames = (size_t)(TAIL_WINDOW_SECS * rate);
        render(mcu, rate32, frames - tailFrames, &total, NULL, wav, wavGain);
        Accum w;
        render(mcu, rate32, tailFrames, &total, &w, wav, wavGain);
        r.releaseTailRms = w.rms();
    }

    // 5. Polifonía completa: 16 voces a la vez.
    for (int n = 0; n < 16; n++)
        mcu->sendMidiCmd(0x90, 48 + n, 100);
    {
        Accum w;
        render(mcu, rate32, (size_t)(POLY_SECS * rate), &total, &w, wav, wavGain);
        r.polyRms = w.rms();
    }

    for (int n = 0; n < 16; n++)
        mcu->sendMidiCmd(0x80, 48 + n, 0);
    {
        size_t frames = (size_t)(POLY_RELEASE_SECS * rate);
        size_t tailFrames = (size_t)(TAIL_WINDOW_SECS * rate);
        render(mcu, rate32, frames - tailFrames, &total, NULL, wav, wavGain);
        Accum w;
        render(mcu, rate32, tailFrames, &total, &w, wav, wavGain);
        r.polyTailRms = w.rms();
    }

    r.peak = total.peak;
    r.hash = total.hash;
    r.emulatedSecs = (double)total.count / rate;

    delete mcu;
    return r;
}

// ---------------------------------------------------------------- umbrales

// Calibrados sobre el comportamiento actual del emulador; ver README del test.
static const double MIN_NOTE_RMS = 200.0;   // una nota tiene que sonar
static const double MAX_SILENCE_RMS = 50.0; // antes de tocar, casi nada
static const double MAX_TAIL_RMS = 200.0;   // la cola tiene que extinguirse
static const s32 MAX_PEAK = 1 << 24;        // cordura de rango

static void check_patch(const PatchResult &r, CheckRun &checks)
{
    checks.add("boot-silence", r.silenceRms < MAX_SILENCE_RMS,
               check_fmt("silence rms %.1f < %.1f", r.silenceRms, MAX_SILENCE_RMS));

    checks.add("note-sounds", r.noteRms > MIN_NOTE_RMS, check_fmt("note rms %.1f > %.1f", r.noteRms, MIN_NOTE_RMS));

    checks.add("chord-sounds", r.chordRms > r.noteRms / 2,
               check_fmt("chord rms %.1f > note rms/2 %.1f", r.chordRms, r.noteRms / 2));

    checks.add("release-decays", r.releaseTailRms < MAX_TAIL_RMS,
               check_fmt("release tail rms %.1f < %.1f", r.releaseTailRms, MAX_TAIL_RMS));

    checks.add("poly-sounds", r.polyRms > MIN_NOTE_RMS, check_fmt("poly rms %.1f > %.1f", r.polyRms, MIN_NOTE_RMS));

    checks.add("poly-decays", r.polyTailRms < MAX_TAIL_RMS,
               check_fmt("poly tail rms %.1f < %.1f", r.polyTailRms, MAX_TAIL_RMS));

    checks.add("peak-sane", r.peak > 0 && r.peak < MAX_PEAK, check_fmt("peak %d < %d", r.peak, MAX_PEAK));
}

// ---------------------------------------------------------------- golden

static std::map<int, u64> read_golden(const std::string &path)
{
    std::map<int, u64> golden;
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
    {
        fprintf(stderr, "ERROR: no se puede abrir el golden %s\n", path.c_str());
        exit(2);
    }
    char line[256];
    while (fgets(line, sizeof line, f))
    {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        int patch;
        unsigned long long hash;
        if (sscanf(line, "%d %llx", &patch, &hash) == 2)
            golden[patch] = hash;
    }
    fclose(f);
    return golden;
}

static void write_golden(const std::string &path, const std::map<int, PatchResult> &results)
{
    FILE *f = fopen(path.c_str(), "w");
    if (!f)
    {
        fprintf(stderr, "ERROR: no se puede escribir el golden %s\n", path.c_str());
        exit(2);
    }
    fprintf(f, "# rdpiano e2e golden hashes: <parche> <hash fnv1a64> <nombre>\n");
    for (const auto &kv : results)
        fprintf(f, "%2d %016llx  %s\n", kv.first, (unsigned long long)kv.second.hash, patchNames[kv.first]);
    fclose(f);
    printf("golden escrito en %s\n", path.c_str());
}

// ---------------------------------------------------------------- headroom
//
// De aquí sale `patchOutputGain[]`. No es una comprobación: no falla nunca y no
// entra en el ctest.
//
// Mide el motor entero (EQ incluido), no el emulador desnudo, y normaliza sobre
// la cadena seca porque el chorus modula y su pico depende de la fase del LFO;
// lo que el chorus añade va aparte, en la columna `c/efx`. Es idempotente:
// aplica la ganancia que ya está en la tabla y propone
// `ganancia_actual x objetivo / pico_medido`.

static const double HEADROOM_HOST_RATE = 48000.0;
static const int HEADROOM_BLOCK = 512;
static const double HEADROOM_SECS = 1.5;

static double measure_headroom(int patch, const RdRomSet *sets, const u8 *prog, bool chorus, bool efx)
{
    RdPianoEngine engine(sets, prog);
    if (patch != 0)
        engine.setPatch(patch);
    engine.prepare(HEADROOM_HOST_RATE, HEADROOM_BLOCK);

    engine.params.chorusEnabled = chorus;
    engine.params.efxEnabled = efx;
    engine.params.tremoloEnabled = false;
    engine.params.volume = 1.0f;

    // El peor caso razonable: las 16 voces a la vez, a velocity 127.
    for (int n = 0; n < 16; n++)
        engine.pushMidi(0, 0x90, 48 + n, 127);

    std::vector<float> l(HEADROOM_BLOCK), r(HEADROOM_BLOCK);
    const int blocks = (int)(HEADROOM_SECS * HEADROOM_HOST_RATE) / HEADROOM_BLOCK;
    double peak = 0;
    for (int b = 0; b < blocks; b++)
    {
        engine.render(l.data(), r.data(), HEADROOM_BLOCK);
        for (int i = 0; i < HEADROOM_BLOCK; i++)
        {
            double a = fabs((double)l[i]);
            if (a > peak)
                peak = a;
            a = fabs((double)r[i]);
            if (a > peak)
                peak = a;
        }
    }
    return peak;
}

static int run_headroom(RomBank &roms)
{
    RdRomSet sets[ROMSET_COUNT];
    for (int s = 0; s < ROMSET_COUNT; s++)
    {
        sets[s].ic5 = roms.get(romSetFiles[s][ROM_IC5], WAVE_ROM_SIZE);
        sets[s].ic6 = roms.get(romSetFiles[s][ROM_IC6], WAVE_ROM_SIZE);
        sets[s].ic7 = roms.get(romSetFiles[s][ROM_IC7], WAVE_ROM_SIZE);
        sets[s].ic18 = roms.get(romSetFiles[s][ROM_IC18], WAVE_ROM_SIZE);
    }
    const u8 *prog = roms.get(PROG_ROM_FILE, PROG_ROM_SIZE);

    printf("headroom: acorde de 16 notas a velocity 127, cadena seca con EQ,\n"
           "          %.0f Hz. Objetivo: pico %.5f (%.1f dBFS)\n\n",
           HEADROOM_HOST_RATE, (double)HEADROOM_TARGET_PEAK, 20.0 * log10((double)HEADROOM_TARGET_PEAK));
    printf("%-3s %-22s %9s %8s %10s %10s %9s\n", "#", "parche", "ganancia", "pico", "dBFS", "propuesta", "c/efx");

    std::vector<double> proposed(NUM_PATCHES, 1.0);
    double worstFx = 0;
    for (int patch = 0; patch < NUM_PATCHES; patch++)
    {
        double peak = measure_headroom(patch, sets, prog, false, false);
        double gain = patchOutputGain[patch];
        proposed[patch] = peak > 0 ? gain * HEADROOM_TARGET_PEAK / peak : gain;

        // La misma medida con efectos. El peor caso no es tenerlos todos: el
        // phaser ATENÚA, así que chorus solo —el ajuste de fábrica— pega más
        // fuerte que chorus + phaser.
        double fxPeak = measure_headroom(patch, sets, prog, true, false);
        double fxBoth = measure_headroom(patch, sets, prog, true, true);
        if (fxBoth > fxPeak)
            fxPeak = fxBoth;
        if (fxPeak > worstFx)
            worstFx = fxPeak;

        printf("%-3d %-22s %9.5f %8.4f %+10.2f %10.5f %9.4f\n", patch, patchNames[patch], gain, peak,
               20.0 * log10(peak > 0 ? peak : 1e-9), proposed[patch], fxPeak);
        fflush(stdout);
    }

    printf("\npeor caso con efectos: %.4f (%+.2f dBFS)\n", worstFx, 20.0 * log10(worstFx > 0 ? worstFx : 1e-9));

    printf("\npara patches.h:\n");
    for (int patch = 0; patch < NUM_PATCHES; patch++)
        printf("    %.5ff, // %s\n", proposed[patch], patchNames[patch]);
    return 0;
}

// ---------------------------------------------------------------- main

int main(int argc, char **argv)
{
    std::string romsDir = "roms";
    std::string wavDir;
    std::string goldenPath;
    std::string writeGoldenPath;
    int onlyPatch = -1;
    bool headroom = false;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (arg == "--roms" && next)
            romsDir = argv[++i];
        else if (arg == "--wav-dir" && next)
            wavDir = argv[++i];
        else if (arg == "--golden" && next)
            goldenPath = argv[++i];
        else if (arg == "--write-golden" && next)
            writeGoldenPath = argv[++i];
        else if (arg == "--patch" && next)
            onlyPatch = atoi(argv[++i]);
        else if (arg == "--headroom")
            headroom = true;
        else
        {
            fprintf(stderr,
                    "uso: %s [--roms DIR] [--patch N] [--wav-dir DIR]\n"
                    "        [--write-golden FILE] [--golden FILE] [--headroom]\n",
                    argv[0]);
            return 2;
        }
    }

    if (onlyPatch >= NUM_PATCHES)
    {
        fprintf(stderr, "ERROR: parche %d fuera de rango (0..%d)\n", onlyPatch, NUM_PATCHES - 1);
        return 2;
    }

    RomBank roms;
    roms.dir = romsDir;

    if (headroom)
        return run_headroom(roms);

    std::map<int, u64> golden;
    if (!goldenPath.empty())
        golden = read_golden(goldenPath);

    std::map<int, PatchResult> results;
    int failed = 0;
    int hashMismatches = 0;

    printf("%-3s %-22s %6s %8s %8s %8s %8s %8s %9s %-16s\n", "#", "parche", "kHz", "silen", "nota", "acorde", "cola",
           "poly", "peak", "hash");

    for (int patch = 0; patch < NUM_PATCHES; patch++)
    {
        if (onlyPatch >= 0 && patch != onlyPatch)
            continue;

        std::vector<float> wav;
        PatchResult r = run_patch(patch, roms, wavDir.empty() ? NULL : &wav);
        results[patch] = r;

        printf("%-3d %-22s %6d %8.1f %8.1f %8.1f %8.1f %8.1f %9d %016llx\n", patch, patchNames[patch],
               patchSampleRates[patch], r.silenceRms, r.noteRms, r.chordRms, r.releaseTailRms, r.polyRms, r.peak,
               (unsigned long long)r.hash);

        CheckRun checks;
        check_patch(r, checks);
        checks.print_failures();
        failed += checks.failed();

        if (!golden.empty())
        {
            auto it = golden.find(patch);
            if (it == golden.end())
            {
                printf("    WARN sin golden para el parche %d\n", patch);
            }
            else if (it->second != r.hash)
            {
                printf("    FAIL golden-hash esperado %016llx, obtenido %016llx\n", (unsigned long long)it->second,
                       (unsigned long long)r.hash);
                hashMismatches++;
            }
        }

        if (!wavDir.empty())
        {
            char path[512];
            snprintf(path, sizeof path, "%s/patch%02d.wav", wavDir.c_str(), patch);
            if (write_wav(path, wav, patchSampleRates[patch]))
                printf("    wav %s (%.1fs)\n", path, r.emulatedSecs);
        }

        fflush(stdout);
    }

    if (!writeGoldenPath.empty())
        write_golden(writeGoldenPath, results);

    printf("\n%zu parche(s), %d comprobacion(es) fallida(s), %d hash(es) "
           "distinto(s) del golden\n",
           results.size(), failed, hashMismatches);

    if (hashMismatches)
        printf("El audio cambió respecto al golden. Si el cambio es intencionado, "
               "escucha los WAV y regenera con --write-golden.\n");

    return (failed || hashMismatches) ? 1 : 0;
}
