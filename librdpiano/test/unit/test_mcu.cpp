/**
 * @file test_mcu.cpp
 * @brief Lo que Mcu pone en la cola de comandos: la traducción del MIDI y los tiempos de boot() y setMasterTune().
 *
 * Los bytes de cada mensaje los fija test_command_port; aquí se fija qué mensaje sale de cada evento MIDI y
 * cuánto de una secuencia ha recogido ya el firmware cuando Mcu devuelve el control.
 */

#include <memory>
#include <string>
#include <vector>

#include "mcu.h"
#include "patches.h"
#include "rom_files.h"
#include "unit_test.h"

namespace
{

    /** @brief Una copia de lo pendiente, de la cabeza a la cola, sin tocar la cola de verdad. */
    std::vector<u8> pending(const Mcu &mcu)
    {
        CommandQueue q = mcu.commandPort().queue();
        std::vector<u8> out;
        while (!q.empty())
        {
            out.push_back(q.front());
            q.pop();
        }
        return out;
    }

    std::string hex(const std::vector<u8> &bytes)
    {
        std::string s;
        for (u8 b : bytes)
            s += check_fmt("%02x ", b);
        return s;
    }

    void check_bytes(CheckRun &checks, const std::string &name, const std::vector<u8> &got, const std::vector<u8> &want)
    {
        checks.add(name, got == want, check_fmt("en cola [%s], esperado [%s]", hex(got).c_str(), hex(want).c_str()));
    }

    /** @brief Un Mcu con el MK-80 cargado; nullptr si faltan las ROM. */
    std::unique_ptr<Mcu> make_mcu()
    {
        static RomCache roms;
        roms.dir = g_roms_dir;
        RdRomSet set;
        const u8 *prog = roms.get(PROG_ROM_FILE, PROG_ROM_SIZE);
        if (!roms.romSet(ROMSET_MK80, set) || prog == nullptr)
            return nullptr;
        return std::make_unique<Mcu>(set.ic5, set.ic6, set.ic7, prog, set.ic18);
    }

    /** @brief Corre el firmware hasta que recoja todo lo pendiente; false si no lo recoge en `maxSamples`. */
    bool run_until_drained(Mcu &mcu, int maxSamples)
    {
        for (int i = 0; i < maxSamples && !mcu.commandPort().queue().empty(); i++)
            mcu.generate_next_sample(false);
        return mcu.commandPort().queue().empty();
    }

} // namespace

/**
 * @brief Cada evento MIDI y el mensaje del firmware que sale de él, sin correr la CPU: el canal no cuenta, la
 *        velocidad 0 es un note-off, el pedal cambia en 64 y lo que el firmware no entiende no se encola.
 */
TEST_SUITE(mcu_midi_translation)
{
    auto mcu = make_mcu();
    ROMS_OR_FAIL(mcu != nullptr);

    struct Case
    {
        const char *name;
        u8 status, data1, data2;
        std::vector<u8> want;
    };
    const Case cases[] = {
        {"note-on", 0x90, 60, 100, {0xC0, 60, 100}},
        {"note-on en otro canal", 0x9F, 60, 100, {0xC0, 60, 100}},
        {"note-on con velocidad 0", 0x90, 60, 0, {0xB0, 60, 0x00}},
        {"note-off", 0x80, 60, 64, {0xB0, 60, 0x00}},
        {"pedal a 127", 0xB0, 64, 127, {0x5F}},
        {"pedal a 64", 0xB0, 64, 64, {0x5F}},
        {"pedal a 63", 0xB0, 64, 63, {0x50}},
        {"program change 5", 0xC0, 5, 0, {0x35}},
        {"program change 20, nibble bajo", 0xC0, 20, 0, {0x34}},
        {"otro CC", 0xB0, 7, 100, {}},
        {"CC 123 (all notes off)", 0xB0, 123, 0, {}},
        {"pitch bend", 0xE0, 0, 64, {}},
        {"aftertouch", 0xA0, 60, 10, {}},
    };

    for (const Case &c : cases)
    {
        const size_t before = pending(*mcu).size();
        mcu->sendMidiCmd(c.status, c.data1, c.data2);
        const std::vector<u8> all = pending(*mcu);
        check_bytes(checks, c.name, std::vector<u8>(all.begin() + (long)before, all.end()), c.want);
    }
}

/**
 * @brief boot() deja recogido el prólogo —parche 0 y afinación— durante las 1024 muestras de margen, y la
 *        recarga del parche pendiente para después.
 */
TEST_SUITE(mcu_boot_timing)
{
    auto mcu = make_mcu();
    ROMS_OR_FAIL(mcu != nullptr);

    mcu->boot(0, false);
    check_bytes(checks, "tras boot() solo queda la recarga", pending(*mcu), {0x31, 0x30});

    // Y el firmware está vivo: la recoge en cuanto corre.
    CHECK(run_until_drained(*mcu, 1000));
}

/**
 * @brief El switcharoo de setMasterTune() (trampa 4): program change 0, afinación y program change 0, con el
 *        firmware corriendo entre medias. Al volver ya ha recogido el primer PC y la afinación; el segundo PC
 *        queda pendiente.
 */
TEST_SUITE(mcu_master_tune_switcharoo)
{
    auto mcu = make_mcu();
    ROMS_OR_FAIL(mcu != nullptr);

    mcu->boot(0, false);
    CHECK(run_until_drained(*mcu, 1000));

    const int16_t tunes[] = {-16384, 0, 32767};
    for (int16_t tune : tunes)
    {
        mcu->setMasterTune(tune);
        check_bytes(checks, check_fmt("afinación %d: solo queda el segundo program change", tune), pending(*mcu),
                    {0x30});
        checks.add(check_fmt("afinación %d: el firmware lo recoge", tune), run_until_drained(*mcu, 1000),
                   check_fmt("quedan %zu bytes", pending(*mcu).size()));
    }
}
