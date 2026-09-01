// Mapa de memoria y latch de banco: se escribe y se lee de verdad —RAM, chip de
// sonido, latch, página de params, ROM de programa y los dos puertos del bus de
// comandos— sin CPU, sin firmware y sin audio. Basta una `RdBoard` con una CPU
// de mentira.

#include <stdio.h>

#include <string>
#include <vector>

#include "patches.h"
#include "rd_board.h"
#include "rom_loader.h"
#include "unit_test.h"

// La ROM de programa se acota sobre un array de 0x2000. Esta suite fija la
// equivalencia con la máscara `& 0xdfff` de antes en el único rango que el bus
// puede producir: addr >= 0xc000, luego offset 0..0x3fff.
TEST_SUITE(board_program_rom_mask)
{
    int mismatches = 0;
    int outOfRange = 0;

    for (int addr = 0xc000; addr <= 0xffff; addr++)
    {
        int offset = addr - 0xc000;
        if ((offset & 0xdfff) != (offset & 0x1fff))
            mismatches++;
        if ((offset & 0x1fff) >= 0x2000)
            outOfRange++;
    }

    CHECK_EQ(mismatches, 0);
    CHECK_EQ(outOfRange, 0);

    // La equivalencia es del rango, no de las máscaras: fuera de él discrepan.
    // Si algún día el bus entrega offsets mayores, `& 0xdfff` no habría sido
    // el equivalente inocente que parecía.
    CHECK((0x4000 & 0xdfff) != (0x4000 & 0x1fff));
}

// La página de params se direcciona con `(addr - 0x4000) | ((latch & 0b11) << 15)`
// sobre un array de 0x20000. El OR solo es una suma si el offset no invade los
// bits del banco: esto lo comprueba para todo el rango y los cuatro bancos.
TEST_SUITE(board_params_bank)
{
    size_t maxOffset = 0;
    size_t maxIndex = 0;

    for (int latch = 0; latch < 4; latch++)
    {
        for (int addr = 0x4000; addr <= 0xbfff; addr++)
        {
            size_t offset = (size_t)(addr - 0x4000);
            size_t index = offset | ((size_t)(latch & 0b11) << 15);

            if (offset > maxOffset)
                maxOffset = offset;
            if (index > maxIndex)
                maxIndex = index;

            // sin solape, el OR y la suma coinciden
            if (index != offset + ((size_t)(latch & 0b11) << 15))
                maxIndex = 0xffffffff;
        }
    }

    CHECK_EQ(maxOffset, 0x7fff);
    CHECK(maxIndex < 0x20000);
}

// ---------------------------------------------------------------------------
// El mapa de verdad, con una CPU de mentira.

namespace
{

    // Registra lo que la placa le pide a la CPU y devuelve lo que la CPU real
    // devolvería para los registros que no reconoce.
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

    std::vector<u8> read_rom(const std::string &name, size_t want)
    {
        std::vector<u8> buf;
        FILE *f = fopen((g_roms_dir + "/" + name).c_str(), "rb");
        if (!f)
            return buf;
        buf.resize(want);
        size_t got = fread(buf.data(), 1, want, f);
        fclose(f);
        if (got != want)
            buf.clear();
        return buf;
    }

    // Todo lo que hace falta para levantar una placa: las cuatro ROMs del juego
    // MK-80 más el firmware. Vive en un `static` porque construir una `RdBoard`
    // descifra 384 KB de wave ROM y las suites de abajo son varias.
    struct Fixture
    {
        std::vector<u8> ic5, ic6, ic7, ic18, prog;
        bool ok = false;

        Fixture()
        {
            ic5 = read_rom(romSetFiles[ROMSET_MK80][ROM_IC5], WAVE_ROM_SIZE);
            ic6 = read_rom(romSetFiles[ROMSET_MK80][ROM_IC6], WAVE_ROM_SIZE);
            ic7 = read_rom(romSetFiles[ROMSET_MK80][ROM_IC7], WAVE_ROM_SIZE);
            ic18 = read_rom(romSetFiles[ROMSET_MK80][ROM_IC18], WAVE_ROM_SIZE);
            prog = read_rom(PROG_ROM_FILE, PROG_ROM_SIZE);
            ok = !ic5.empty() && !ic6.empty() && !ic7.empty() && !ic18.empty() && !prog.empty();
        }
    };

    const Fixture &fixture()
    {
        static Fixture f;
        return f;
    }

} // namespace

// Las tres regiones que son memoria de verdad: RAM, latch y sus fronteras.
TEST_SUITE(board_memory_map)
{
    const Fixture &f = fixture();
    if (!f.ok)
    {
        printf("    (nota: ROMs no encontradas en %s, se omite)\n", g_roms_dir.c_str());
        return;
    }

    FakeCpu cpu;
    RdBoard board(f.ic5.data(), f.ic6.data(), f.ic7.data(), f.prog.data(), f.ic18.data());
    board.attach(&cpu);

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

// La ROM de programa: los 8 KB descifrados, espejados cuatro veces en
// 0xC000-0xFFFF.
TEST_SUITE(board_program_rom)
{
    const Fixture &f = fixture();
    if (!f.ok)
        return;

    FakeCpu cpu;
    RdBoard board(f.ic5.data(), f.ic6.data(), f.ic7.data(), f.prog.data(), f.ic18.data());
    board.attach(&cpu);

    // Lo que la placa entrega es exactamente lo que `decode_program_rom` produce
    // del dump: si el mapa se equivocara de offset, esto lo vería.
    std::vector<u8> expected(PROG_ROM_BYTES);
    decode_program_rom(expected.data(), f.prog.data());

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

// La página de params, bancada por los dos bits bajos del latch.
TEST_SUITE(board_params_page)
{
    const Fixture &f = fixture();
    if (!f.ok)
        return;

    FakeCpu cpu;
    RdBoard board(f.ic5.data(), f.ic6.data(), f.ic7.data(), f.prog.data(), f.ic18.data());
    board.attach(&cpu);

    // La placa arranca con el parche 0 mapeado; se compara contra la página que
    // `decode_params_page` produce por su cuenta para el mismo offset.
    std::vector<u8> expected(PARAMS_ROM_BYTES);
    decode_params_page(expected.data(), f.ic18.data(), patchToOffset[8]);
    board.selectPatch(patchToOffset[8]);

    int mismatches = 0;
    for (int latch = 0; latch < 4; latch++)
    {
        board.write(0x2000, (u8)latch);

        // Un muestreo denso, no los 128 KB: la aritmética completa ya la fija
        // board_params_bank y aquí lo que se comprueba es el camino real.
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

// Los dos puertos del bus de comandos: el handshake depende de direcciones
// fijas del firmware RD200 (trampa 1 de CLAUDE.md), y aquí se puede poner el
// contador de programa donde haga falta sin ejecutar una sola instrucción.
TEST_SUITE(board_comm_ports)
{
    const Fixture &f = fixture();
    if (!f.ok)
        return;

    FakeCpu cpu;
    RdBoard board(f.ic5.data(), f.ic6.data(), f.ic7.data(), f.prog.data(), f.ic18.data());
    board.attach(&cpu);

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

// El chip de sonido ocupa 0x1000-0x1FFF. Su `read()` ignora el offset y
// devuelve siempre el identificador de la última IRQ: rareza conocida y sin
// resolver, escrita aquí para que el día que se arregle salte aquí y no en el
// golden.
TEST_SUITE(board_sound_chip_window)
{
    const Fixture &f = fixture();
    if (!f.ok)
        return;

    FakeCpu cpu;
    RdBoard board(f.ic5.data(), f.ic6.data(), f.ic7.data(), f.prog.data(), f.ic18.data());
    board.attach(&cpu);

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
