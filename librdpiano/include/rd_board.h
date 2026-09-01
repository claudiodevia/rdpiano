#ifndef RD_BOARD_H
#define RD_BOARD_H

// La placa: todo lo que cuelga del bus —RAM, chip de sonido, latch de banco,
// ROMs y los dos puertos del bus de comandos—. Lo que está dentro del chip es
// de `Mcu`.
//
// Los dos únicos acoples, vía `RdBoardCpu`, son los del bus real: el handshake
// mira el contador de programa y escribir en el puerto 2 baja la línea TIN.

#include "command_port.h"
#include "mame_utils.h"
#include "rd_trace.h"
#include "rom_loader.h"
#include "sound_chip.h"

// Las líneas de interrupción del HD63701, tal como las numera el core de MAME.
enum
{
    M6800_IRQ_LINE = 0, // IRQ line number

    M6800_LINE_MAX
};
enum
{
    M6801_TIN_LINE =
        M6800_LINE_MAX, // P20/TIN Input Capture line (edge sense). Active edge is selectable by internal reg.
    M6801_IS3_LINE,     // SC1/IOS/IS3 (P54/IS on HD6301Y)
    M6801_STBY_LINE,    // STBY pin, or internal standby

    M6801_LINE_MAX
};

// Lo que la placa necesita de la CPU: el contador de programa (que el handshake
// compara contra direcciones fijas del firmware, trampa 1), las líneas de
// interrupción y los registros del chip dentro de 0x0000-0x001F.
class RdBoardCpu
{
  public:
    virtual ~RdBoardCpu() {}

    virtual u32 programCounter() const = 0;
    virtual void setInputLine(int line, int state) = 0;

    // Los registros de 0x0000-0x001F que son del chip y no de la placa
    // (temporizador y captura de entrada). Lo no reconocido devuelve 0xFF.
    virtual u8 readCpuRegister(u16 addr) = 0;
    virtual void writeCpuRegister(u16 addr, u8 data) = 0;
};

class RdBoard
{
  public:
    RdBoard(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7, const u8 *temp_progrom,
            const u8 *temp_paramsrom);

    // Igual que Mcu y SoundChip: casi 800 KB de estado, copiarlo por accidente
    // daría una placa divergente en silencio.
    RdBoard(const RdBoard &) = delete;
    RdBoard &operator=(const RdBoard &) = delete;
    RdBoard(RdBoard &&) = delete;
    RdBoard &operator=(RdBoard &&) = delete;

    // La CPU que cuelga de este bus. Se fija una vez, al construir el Mcu.
    void attach(RdBoardCpu *cpu) { this->cpu = cpu; }

    // El mapa de memoria. `inline` a propósito: camino caliente del emulador,
    // dos o tres accesos por instrucción y cien instrucciones por muestra.
    u8 read(u16 addr);
    void write(u16 addr, u8 data);

    // Carga de ROM, partida en dos por coste (~2,9 ms el juego de ROM, ~0,03 ms
    // el parche). Las ROM tienen que sobrevivir a la placa: se guarda el puntero
    // de la params para remapear la página.
    void loadRomSet(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7, const u8 *temp_paramsrom);
    void selectPatch(size_t from_addr);

    CommandPort &commandPort() { return command_port; }
    SoundChip &soundChip() { return sound_chip; }

    // Sólo para las pruebas del mapa: el latch de banco no tiene otro lector.
    u8 bankLatch() const { return latch_val; }

  private:
    RdBoardCpu *cpu = nullptr;

    SoundChip sound_chip;
    CommandPort command_port;

    u8 latch_val = 0x00;
    u8 program_rom[PROG_ROM_BYTES];
    u8 params_rom[PARAMS_ROM_BYTES];
    // ROM de params sin descifrar, propiedad del llamante de loadRomSet().
    const u8 *params_rom_src = nullptr;
    u8 ram[0x1000] = {0}; // el mapa solo direcciona 0x0000-0x0FFF
};

// ---------------------------------------------------------------------------
// El mapa, byte a byte. El orden de las ramas y cada máscara son los del
// hardware: el golden del harness mide exactamente esto.
//
//   0x0000-0x001F  registros del MCU (puerto1=0x02 datos, puerto2=0x03 control)
//   0x0000-0x0FFF  RAM
//   0x1000-0x1FFF  SoundChip
//   0x2000-0x3FFF  latch de banco (2 bits)
//   0x4000-0xBFFF  params ROM, bancada por latch_val & 0b11
//   0xC000-0xFFFF  program ROM (firmware, 8 KB)

inline u8 RdBoard::read(u16 addr)
{
    // program rom
    if (addr >= 0xc000)
        return program_rom[(addr - 0xc000) & 0x1fff];

    // port 1 DATA
    else if (addr == 0x0002)
    {
        u8 data_comm_bus = 0xff;

        // HACK: only works with the RD200 ROM (docs/FIRMWARE.md §2)
        const u32 pc = cpu->programCounter();
        CommandQueue &queue = command_port.queue();
        if (!queue.empty() && (pc == 0xE12B || pc == 0xE15E || pc == 0xE168))
        {
            data_comm_bus = queue.front();
            queue.pop();
        }

        RD_TRACE("%04x: read port1 %02x\n", pc, data_comm_bus);
        return data_comm_bus;
    }

    // port 2 CONTROL
    else if (addr == 0x0003)
    {
        RD_TRACE("%04x: read port2\n", cpu->programCounter());

        // HACK: only works with the RD200 ROM (docs/FIRMWARE.md §2)
        if (cpu->programCounter() == 0xE15A)
            return 0xFF;
        return 0x00;
    }

    // registros internos del chip (temporizador, captura); lo que no reconozca
    // devuelve 0xFF
    else if (addr < 0x20)
        return cpu->readCpuRegister(addr);

    // ram
    else if (addr < 0x1000)
        return ram[addr];

    // sound chip
    else if (addr < 0x2000)
        return sound_chip.read(addr - 0x1000);

    // params rom
    else if (addr >= 0x4000 && addr <= 0xbfff)
        return params_rom[(addr - 0x4000) | ((latch_val & 0b11) << 15)];

    RD_TRACE("%04x: unk read %04x\n", cpu->programCounter(), addr);
    return 0xFF;
}

inline void RdBoard::write(u16 addr, u8 data)
{
    // port dir
    if (addr == 0x0000 || addr == 0x0001)
    {
        // noop
    }

    // port 1 DATA
    else if (addr == 0x0002)
    {
        RD_TRACE("%04x: port1 write %04x=%02x\n", cpu->programCounter(), addr, data);
    }

    // port 2 CONTROL
    else if (addr == 0x0003)
    {
        RD_TRACE("%04x: port2 write %04x=%02x\n", cpu->programCounter(), addr, data);

        // El bit 2 selecciona la tasa de muestreo en la máquina real, pero aquí
        // nunca llegó a funcionar: la tasa sale de patchSampleRates[].
        // Ver docs/FIRMWARE.md §3.

        cpu->setInputLine(M6801_TIN_LINE, CLEAR_LINE);
    }

    // registros internos del chip
    else if (addr < 0x20)
    {
        cpu->writeCpuRegister(addr, data);
    }

    // ram
    else if (addr < 0x1000)
    {
        ram[addr] = data;
    }

    // sound chip
    else if (addr >= 0x1000 && addr < 0x2000)
    {
        sound_chip.write(addr - 0x1000, data);
        RD_TRACE("%04x: SA write %04x=%02x\n", cpu->programCounter(), addr, data);

        if (sound_chip.m_irq_triggered)
        {
            sound_chip.m_irq_triggered = false;
            cpu->setInputLine(0, CLEAR_LINE);
        }
    }

    // latch
    else
    {
        latch_val = data;
        RD_TRACE("latch write %04x=%02x\n", addr, data);
    }
}

#endif
