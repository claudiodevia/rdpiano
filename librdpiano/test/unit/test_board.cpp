/**
 * @file test_board.cpp
 * @brief Mapa de memoria y latch de banco, sin CPU ni firmware.
 *
 * Se escribe y se lee de verdad —RAM, chip de sonido, latch, página de params, ROM de programa y
 * los dos puertos del bus de comandos— sin CPU, sin firmware y sin audio. Basta una `RdBoard` con
 * una CPU de mentira.
 */

#include <vector>

#include "patches.h"
#include "rd_board.h"
#include "rom_files.h"
#include "rom_loader.h"
#include "unit_test.h"

namespace
{

    /**
     * @brief Una CPU de mentira: registra lo que la placa le pide.
     *
     * Devuelve lo que la CPU real devolvería para los registros que no reconoce.
     */
    struct FakeCpu : public RdBoardCpu
    {
        u32 pc = 0;
        int lastLine = -1;
        int lastState = -1;
        int lineCalls = 0;
        int registerReads = 0;
        int registerWrites = 0;
        u16 lastRegisterAddr = 0xffff;
        u8 lastRegisterData = 0;

        u32 programCounter() const override { return pc; }

        void setInputLine(int line, int state) override
        {
            lastLine = line;
            lastState = state;
            lineCalls++;
        }

        u8 readCpuRegister(u16 addr) override
        {
            registerReads++;
            lastRegisterAddr = addr;
            return 0xFF;
        }

        void writeCpuRegister(u16 addr, u8 data) override
        {
            registerWrites++;
            lastRegisterAddr = addr;
            lastRegisterData = data;
        }
    };

    /**
     * @brief Todo lo que hace falta para levantar una placa: las cuatro ROM del MK-80 y el firmware.
     *
     * Vive en un `static` porque construir una RdBoard descifra 384 KB de wave
     * ROM y las suites de abajo son varias.
     */
    struct Fixture
    {
        RomCache roms;
        RdRomSet set;
        const u8 *prog = nullptr;
        bool ok = false;

        Fixture()
        {
            roms.dir = g_roms_dir;
            prog = roms.get(PROG_ROM_FILE, PROG_ROM_SIZE);
            ok = roms.romSet(ROMSET_MK80, set) && prog != nullptr;
        }
    };

    const Fixture &fixture()
    {
        static Fixture f;
        return f;
    }

    /** @brief Una placa recién construida con la CPU de mentira enchufada. */
    struct BoardUnderTest
    {
        FakeCpu cpu;
        RdBoard board;

        explicit BoardUnderTest(const Fixture &f) : board(f.set.ic5, f.set.ic6, f.set.ic7, f.prog, f.set.ic18)
        {
            board.attach(&cpu);
        }
    };

} // namespace

/**
 * @brief Las tres regiones que son memoria de verdad: RAM, latch y sus fronteras.
 */
TEST_SUITE(board_memory_map)
{
    const Fixture &f = fixture();
    ROMS_OR_FAIL(f.ok);

    BoardUnderTest t(f);
    auto &[cpu, board] = t;

    // RAM: 0x0020-0x0FFF. Por debajo de 0x20 el bus son registros, no memoria.
    int ramMismatches = 0;
    for (u32 addr = 0x0020; addr < 0x1000; addr++)
    {
        board.write((u16)addr, (u8)(addr * 7 + 1));
        if (board.read((u16)addr) != (u8)(addr * 7 + 1))
            ramMismatches++;
    }
    CHECK_EQ(ramMismatches, 0);

    // Escribir en RAM no toca el latch, y viceversa.
    CHECK_EQ(board.bankLatch(), 0x00);

    // Latch: cualquier escritura por encima de 0x2000 que no sea RAM ni chip de
    // sonido cae aquí, incluido el rango de la ROM de programa. Es una rareza
    // del mapa original, no un descuido de esta prueba.
    board.write(0x2000, 0x03);
    CHECK_EQ(board.bankLatch(), 0x03);
    board.write(0x3fff, 0x01);
    CHECK_EQ(board.bankLatch(), 0x01);
    board.write(0xc000, 0x02);
    CHECK_EQ(board.bankLatch(), 0x02);

    // La RAM sobrevivió a todo lo anterior.
    CHECK_EQ(board.read(0x0020), (u8)(0x0020 * 7 + 1));
    CHECK_EQ(board.read(0x0fff), (u8)(0x0fff * 7 + 1));

    // Una lectura sin nada mapeado: 0x2000-0x3FFF sólo es escritura.
    CHECK_EQ(board.read(0x2000), 0xFF);
    CHECK_EQ(board.read(0x3fff), 0xFF);
}

/**
 * @brief La ROM de programa: los 8 KB descifrados, espejados cuatro veces en 0xC000-0xFFFF.
 */
TEST_SUITE(board_program_rom)
{
    const Fixture &f = fixture();
    ROMS_OR_FAIL(f.ok);

    BoardUnderTest t(f);
    auto &[cpu, board] = t;

    // Lo que la placa entrega es exactamente lo que `decode_program_rom` produce
    // del dump: si el mapa se equivocara de offset, esto lo vería.
    std::vector<u8> expected(PROG_ROM_BYTES);
    decode_program_rom(expected.data(), f.prog);

    int mismatches = 0;
    for (size_t i = 0; i < PROG_ROM_BYTES; i++)
        if (board.read((u16)(0xc000 + i)) != expected[i])
            mismatches++;
    CHECK_EQ(mismatches, 0);

    // El espejo: 0xC000+i y 0xE000+i son el mismo byte.
    int aliasMismatches = 0;
    for (size_t i = 0; i < PROG_ROM_BYTES; i++)
        if (board.read((u16)(0xc000 + i)) != board.read((u16)(0xe000 + i)))
            aliasMismatches++;
    CHECK_EQ(aliasMismatches, 0);

    // El vector de reset vive en 0xFFFE y no puede salir en blanco: es lo
    // primero que lee `Mcu::reset()`.
    u16 resetVector = (board.read(0xfffe) << 8) | board.read(0xffff);
    CHECK(resetVector >= 0xc000);
}

/**
 * @brief La página de params, bancada por los dos bits bajos del latch.
 */
TEST_SUITE(board_params_page)
{
    const Fixture &f = fixture();
    ROMS_OR_FAIL(f.ok);

    BoardUnderTest t(f);
    auto &[cpu, board] = t;

    // Con el parche 8 mapeado, se compara contra la página que
    // `decode_params_page` produce por su cuenta para el mismo offset.
    std::vector<u8> expected(PARAMS_ROM_BYTES);
    decode_params_page(expected.data(), f.set.ic18, patchToOffset[8]);
    board.selectPatch(patchToOffset[8]);

    int mismatches = 0;
    for (int latch = 0; latch < 4; latch++)
    {
        board.write(0x2000, (u8)latch);

        // Un muestreo denso de los cuatro bancos, no los 128 KB enteros.
        for (u32 addr = 0x4000; addr <= 0xbfff; addr += 17)
        {
            size_t index = (addr - 0x4000) | ((size_t)latch << 15);
            if (board.read((u16)addr) != expected[index])
                mismatches++;
        }
    }
    CHECK_EQ(mismatches, 0);

    // Sólo cuentan los dos bits bajos del latch: 0x04 y 0x00 son el mismo banco.
    board.write(0x2000, 0x00);
    u8 bank0 = board.read(0x4000);
    board.write(0x2000, 0x04);
    CHECK_EQ(board.read(0x4000), bank0);
    board.write(0x2000, 0x05);
    CHECK_EQ(board.read(0x4000), expected[1 << 15]);
}

/**
 * @brief Los dos puertos del bus de comandos: el handshake depende de direcciones fijas del firmware RD200
 *        (trampa 1 de CLAUDE.md), y aquí se puede poner el contador de programa donde haga falta sin ejecutar
 *        una sola instrucción.
 */
TEST_SUITE(board_comm_ports)
{
    const Fixture &f = fixture();
    ROMS_OR_FAIL(f.ok);

    BoardUnderTest t(f);
    auto &[cpu, board] = t;

    // Con la cola vacía, el puerto 1 lee 0xFF esté donde esté el PC.
    cpu.pc = 0xE12B;
    CHECK_EQ(board.read(0x0002), 0xFF);

    // Con la cola llena pero el PC en otro sitio, tampoco entrega nada: y lo que
    // importa es que **no consume** el byte.
    board.commandPort().programChange(5);
    cpu.pc = 0x1234;
    CHECK_EQ(board.read(0x0002), 0xFF);
    CHECK_EQ(board.commandPort().queue().size(), 1);

    // En cualquiera de las tres direcciones del handshake, sí.
    const u32 handshake[] = {0xE12B, 0xE15E, 0xE168};
    for (u32 pc : handshake)
    {
        board.commandPort().queue().clear();
        board.commandPort().programChange(5);
        cpu.pc = pc;
        checks.add(check_fmt("puerto 1 entrega en %04x", pc), board.read(0x0002) == 0x35, check_fmt("PC %04x", pc));
        checks.add(check_fmt("puerto 1 consume en %04x", pc), board.commandPort().queue().empty(),
                   "la cola no se vació");
    }

    // El puerto 2 sólo devuelve 0xFF en su dirección; en cualquier otra, 0x00.
    cpu.pc = 0xE15A;
    CHECK_EQ(board.read(0x0003), 0xFF);
    cpu.pc = 0xE15B;
    CHECK_EQ(board.read(0x0003), 0x00);

    // Escribir en el puerto 2 baja la línea TIN: es el acuse de recibo del
    // firmware y el único camino por el que la placa toca la CPU en cada byte.
    cpu.lineCalls = 0;
    board.write(0x0003, 0x00);
    CHECK_EQ(cpu.lineCalls, 1);
    CHECK_EQ(cpu.lastLine, M6801_TIN_LINE);
    CHECK_EQ(cpu.lastState, CLEAR_LINE);

    // Los registros de dirección de puerto no hacen nada, ni siquiera llegan a
    // la CPU.
    cpu.registerWrites = 0;
    board.write(0x0000, 0xff);
    board.write(0x0001, 0xff);
    CHECK_EQ(cpu.registerWrites, 0);

    // El resto de 0x0000-0x001F sí es de la CPU.
    cpu.registerReads = 0;
    board.read(0x0008);
    CHECK_EQ(cpu.registerReads, 1);
    CHECK_EQ(cpu.lastRegisterAddr, 0x0008);

    cpu.registerWrites = 0;
    board.write(0x0008, 0x42);
    CHECK_EQ(cpu.registerWrites, 1);
    CHECK_EQ(cpu.lastRegisterData, 0x42);

    // Y 0x0020 ya es RAM, no registro.
    cpu.registerWrites = 0;
    board.write(0x0020, 0x5a);
    CHECK_EQ(cpu.registerWrites, 0);
    CHECK_EQ(board.read(0x0020), 0x5a);
}

/**
 * @brief El chip de sonido ocupa 0x1000-0x1FFF. Su `read()` ignora el offset y devuelve siempre el
 *        identificador de la última IRQ: rareza conocida y sin resolver, escrita aquí para que el día que se
 *        arregle salte aquí y no en el golden.
 */
TEST_SUITE(board_sound_chip_window)
{
    const Fixture &f = fixture();
    ROMS_OR_FAIL(f.ok);

    BoardUnderTest t(f);
    auto &[cpu, board] = t;

    u8 first = board.read(0x1000);
    int differing = 0;
    for (u32 addr = 0x1000; addr < 0x2000; addr += 37)
        if (board.read((u16)addr) != first)
            differing++;
    CHECK_EQ(differing, 0);

    // Escribir en la ventana no toca la RAM contigua ni el latch.
    board.write(0x0fff, 0x11);
    board.write(0x1000, 0x22);
    CHECK_EQ(board.read(0x0fff), 0x11);
    CHECK_EQ(board.bankLatch(), 0x00);
}

/**
 * @brief Cada part ocupa 16 bytes del mapa, pero sólo tiene 8 registros: +8..+F se descartan, no se pliegan sobre
 *        +0..+7. La red es la respuesta del propio chip —armar una voz y hashear 256 muestras—, porque los
 *        registros no se pueden releer.
 */
TEST_SUITE(board_sound_chip_part_fields)
{
    const Fixture &f = fixture();
    ROMS_OR_FAIL(f.ok);

    BoardUnderTest t(f);
    auto &[cpu, board] = t;

    // Voz 0, part 0 en marcha, desde cero.
    auto arm = [&]() {
        board.reset();
        board.write(0x1000, 0x20); // +0 pitch_lut_i alto
        board.write(0x1001, 0x00); // +1 pitch_lut_i bajo
        board.write(0x1002, 0x00); // +2 wave_addr_loop
        board.write(0x1003, 0x10); // +3 wave_addr_high
        board.write(0x1005, 0x40); // +5 env_speed
        board.write(0x1007, 0x20); // +7 env_offset
        board.write(0x1004, 0x60); // +4 env_dest
        board.write(0x1006, 0x03); // +6 flags
    };

    auto hash256 = [&]() {
        Fnv1a h;
        for (int i = 0; i < 256; i++)
            h.i32(board.soundChip().update());
        return h.h;
    };

    auto armAndHash = [&](int offset, u8 value) {
        arm();
        if (offset >= 0)
            board.write((u16)(0x1000 + offset), value);
        return hash256();
    };

    const unsigned long long armed = armAndHash(-1, 0);

    // La mitad baja son registros de verdad: mueven la salida.
    CHECK(armAndHash(0x0, 0x40) != armed);
    CHECK(armAndHash(0x3, 0x3f) != armed);

    // La alta no: +8 no se pliega sobre +0 ni +B sobre +3, que darían los hashes
    // de las dos líneas anteriores.
    CHECK_HASH("+8 no escribe en +0", armAndHash(0x8, 0x40), armed);
    CHECK_HASH("+B no escribe en +3", armAndHash(0xb, 0x3f), armed);

    // Y la guarda de voz: un offset de 0x10000 no cae en la voz 0. La ventana del
    // bus lo enmascara a 16 bits, pero el chip es alcanzable por su cuenta.
    arm();
    board.soundChip().write(0x10000 + 0x3, 0x3f);
    CHECK_HASH("offset 0x10000 descartado", hash256(), armed);
}

/**
 * @brief `reset()` reinicia, además de los registros de la CPU, RAM, latch, chip de sonido y cola de comandos (la
 *        cola es la delicada: el handshake la consume en direcciones fijas del firmware, trampa 1); las ROM y la
 *        página de params sobreviven (trampa 8).
 */
TEST_SUITE(board_reset)
{
    const Fixture &f = fixture();
    ROMS_OR_FAIL(f.ok);

    BoardUnderTest t(f);
    auto &[cpu, board] = t;

    const u8 paramsBefore = board.read(0x4000);
    const u8 progBefore = board.read(0xc000);

    board.write(0x0020, 0x5a);
    board.write(0x0fff, 0xa5);
    board.write(0x2000, 0x03);
    board.commandPort().programChange(5);
    board.commandPort().masterTune(0);
    board.write(0x1003, 0x3f); // deja la voz 0 con wave_addr_high sucio

    CHECK_EQ(board.read(0x0020), 0x5a);
    CHECK_EQ(board.bankLatch(), 0x03);
    CHECK_EQ(board.commandPort().queue().size(), 4);

    board.reset();

    CHECK_EQ(board.read(0x0020), 0x00);
    CHECK_EQ(board.read(0x0fff), 0x00);
    CHECK_EQ(board.bankLatch(), 0x00);
    CHECK(board.commandPort().queue().empty());

    // El chip de sonido, en el estado con el que nace: mismas muestras.
    Fnv1a fresh;
    for (int i = 0; i < 64; i++)
        fresh.i32(board.soundChip().update());

    BoardUnderTest virgin(f);
    Fnv1a reference;
    for (int i = 0; i < 64; i++)
        reference.i32(virgin.board.soundChip().update());
    CHECK_HASH("chip de sonido reiniciado", fresh.h, reference.h);

    // Lo que no es estado sigue en pie: las dos ROM y la página ya mapeada.
    CHECK_EQ(board.read(0x4000), paramsBefore);
    CHECK_EQ(board.read(0xc000), progBefore);
}

/**
 * @brief La página que el motor descifra al construir y mapea con selectPatchPage() es, byte a byte en los
 *        cuatro bancos de 0x4000-0xBFFF, la que selectPatch() descifra en el acto, para los 16 parches y
 *        arrastrando en la placa el parche anterior.
 */
TEST_SUITE(board_select_patch_page)
{
    const Fixture &f = fixture();
    ROMS_OR_FAIL(f.ok);

    RomCache roms;
    roms.dir = g_roms_dir;
    RdRomSet sets[ROMSET_COUNT];
    for (int s = 0; s < ROMSET_COUNT; s++)
        ROMS_OR_FAIL(roms.romSet(s, sets[s]));

    BoardUnderTest cached(f);
    std::vector<u8> page(PARAMS_PAGE_BYTES);

    for (int patch = 0; patch < NUM_PATCHES; patch++)
    {
        const u8 *ic18 = sets[patchToRomSetId[patch]].ic18;

        BoardUnderTest decoded(f);
        decoded.board.selectRomSet(0, ic18);
        decoded.board.selectPatch(patchToOffset[patch]);

        // Lo mismo que hace el constructor de RdPianoEngine con cada parche.
        decode_params_window(page.data(), ic18, patchToOffset[patch]);
        cached.board.selectPatchPage(page.data(), patchToOffset[patch]);

        int mismatches = 0;
        for (int latch = 0; latch < 4; latch++)
        {
            decoded.board.write(0x2000, (u8)latch);
            cached.board.write(0x2000, (u8)latch);
            for (u32 addr = 0x4000; addr <= 0xbfff; addr++)
                if (decoded.board.read((u16)addr) != cached.board.read((u16)addr))
                    mismatches++;
        }
        checks.add(check_fmt("página cacheada = descifrada, parche %d", patch), mismatches == 0,
                   check_fmt("%d bytes distintos", mismatches));
    }
}
