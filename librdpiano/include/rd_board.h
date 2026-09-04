#ifndef RD_BOARD_H
#define RD_BOARD_H

/**
 * @file rd_board.h
 * @brief La placa: todo lo que cuelga del bus del HD63701.
 *
 * RAM, chip de sonido, latch de banco, ROMs y los dos puertos del bus de
 * comandos. Lo que está dentro del chip es de Mcu, y los dos únicos acoples
 * —vía RdBoardCpu— son los del bus real: el handshake mira el contador de
 * programa y escribir en el puerto 2 baja la línea TIN.
 */

#include "command_port.h"
#include "mame_utils.h"
#include "rd_trace.h"
#include "rom_loader.h"
#include "sound_chip.h"

/** @brief Líneas de interrupción del HD63701, tal como las numera el core de MAME. */
enum
{
    M6800_IRQ_LINE = 0, ///< Petición de interrupción enmascarable.

    M6800_LINE_MAX
};

/** @brief Líneas propias del HD6801/6301, a continuación de las del 6800. */
enum
{
    M6801_TIN_LINE = M6800_LINE_MAX, ///< P20/TIN, captura de entrada; el flanco activo lo elige un registro.
    M6801_IS3_LINE,                  ///< SC1/IOS/IS3 (P54/IS en el HD6301Y).
    M6801_STBY_LINE,                 ///< Patilla STBY, o el standby interno.

    M6801_LINE_MAX
};

/**
 * @brief Lo que la placa necesita de la CPU que cuelga de ella.
 *
 * El contador de programa —que el handshake compara contra direcciones fijas
 * del firmware, trampa 1—, las líneas de interrupción y los registros del chip
 * dentro de 0x0000-0x001F.
 */
class RdBoardCpu
{
  public:
    virtual ~RdBoardCpu() {}

    /** @brief Contador de programa actual, que es lo que mira el handshake. */
    virtual u32 programCounter() const = 0;

    /**
     * @brief Fija el estado de una línea de interrupción.
     * @param line Una de M6800_IRQ_LINE / M6801_TIN_LINE…
     * @param state ASSERT_LINE o CLEAR_LINE.
     */
    virtual void setInputLine(int line, int state) = 0;

    /**
     * @brief Lee uno de los registros de 0x0000-0x001F que son del chip, no de la placa.
     * @param addr Dirección dentro del bloque de registros.
     * @return El valor, o 0xFF si el registro no está reconocido.
     */
    virtual u8 readCpuRegister(u16 addr) = 0;

    /**
     * @brief Escribe uno de los registros del chip (temporizador, captura de entrada).
     * @param addr Dirección dentro del bloque de registros.
     * @param data Byte a escribir.
     */
    virtual void writeCpuRegister(u16 addr, u8 data) = 0;
};

/** @brief El bus y todo lo soldado a él: mapa de memoria, ROMs, chip de sonido y puerto de comandos. */
class RdBoard
{
  public:
    RdBoard(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7, const u8 *temp_progrom,
            const u8 *temp_paramsrom);

    /// Igual que Mcu y SoundChip: casi 800 KB de estado, copiarlo por accidente
    /// daría una placa divergente en silencio.
    RdBoard(const RdBoard &) = delete;
    RdBoard &operator=(const RdBoard &) = delete;
    RdBoard(RdBoard &&) = delete;
    RdBoard &operator=(RdBoard &&) = delete;

    /**
     * @brief Cuelga una CPU de este bus. Se fija una vez, al construir el Mcu.
     * @param cpu La CPU; tiene que sobrevivir a la placa.
     */
    void attach(RdBoardCpu *cpu) { this->cpu = cpu; }

    /**
     * @brief Lee del mapa de memoria.
     *
     * `inline` a propósito: camino caliente del emulador, dos o tres accesos por
     * instrucción y cien instrucciones por muestra.
     *
     * @param addr Dirección del bus.
     * @return El byte de quien responda; 0xFF si no responde nadie.
     */
    u8 read(u16 addr);

    /**
     * @brief Escribe en el mapa de memoria.
     * @param addr Dirección del bus.
     * @param data Byte a escribir.
     */
    void write(u16 addr, u8 data);

    /**
     * @brief Carga un juego de ROM entero y mapea la página de un parche.
     *
     * Las ROM tienen que sobrevivir a la placa: se guarda el puntero de la
     * params para poder remapear la página.
     *
     * @param temp_ic5 ROM de onda IC5.
     * @param temp_ic6 ROM de onda IC6.
     * @param temp_ic7 ROM de onda IC7.
     * @param temp_paramsrom ROM de parámetros sin descifrar.
     */
    void loadRomSet(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7, const u8 *temp_paramsrom);

    /**
     * @brief Descifra y mapea la página de parámetros de un parche (~0,03 ms).
     * @param from_addr Offset del parche dentro de la ROM de parámetros.
     */
    void selectPatch(size_t from_addr);

    /**
     * @brief Descifra un juego de ROM en una ranura que el emulador no lee (~2,9 ms).
     * @param slot Ranura de destino, < SoundChip::NUM_WAVE_SLOTS.
     * @param temp_ic5 ROM de onda IC5.
     * @param temp_ic6 ROM de onda IC6.
     * @param temp_ic7 ROM de onda IC7.
     */
    void decodeRomSet(unsigned slot, const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7);

    /**
     * @brief Activa un juego de ROM ya descifrado, en O(1).
     * @param slot Ranura ya poblada por decodeRomSet().
     * @param temp_paramsrom ROM de parámetros del juego, sin descifrar.
     */
    void selectRomSet(unsigned slot, const u8 *temp_paramsrom);

    /**
     * @brief Mapea una página de parámetros YA descifrada.
     *
     * Escribe exactamente los mismos bytes que selectPatch() sin descifrar nada:
     * el resto del espacio quedó a 0xff al construir y nadie más lo toca.
     *
     * @param page Página de PARAMS_PAGE_BYTES ya descifrada.
     * @param from_addr Offset del parche, para el remapeo del banco.
     */
    void selectPatchPage(const u8 *page, size_t from_addr);

    /**
     * @brief Todo lo que cuelga del bus a estado de arranque.
     *
     * RAM, latch de banco, chip de sonido y cola de comandos. Las ROM y la página
     * de parámetros ya mapeada no se tocan: son la configuración, no el estado
     * (por eso boot() no pierde el parche, trampa 8 de CLAUDE.md).
     */
    void reset();

    CommandPort &commandPort() { return command_port; }
    SoundChip &soundChip() { return sound_chip; }

    /** @brief Sólo para las pruebas del mapa: el latch de banco no tiene otro lector. */
    u8 bankLatch() const { return latch_val; }

  private:
    RdBoardCpu *cpu = nullptr;

    SoundChip sound_chip;
    CommandPort command_port;

    u8 latch_val = 0x00;
    u8 program_rom[PROG_ROM_BYTES];
    u8 params_rom[PARAMS_ROM_BYTES];
    const u8 *params_rom_src = nullptr; ///< ROM de params sin descifrar, propiedad del llamante de loadRomSet().
    u8 ram[0x1000] = {0};               ///< El mapa solo direcciona 0x0000-0x0FFF.
};

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

    else if (addr < 0x20)
        return cpu->readCpuRegister(addr);

    else if (addr < 0x1000)
        return ram[addr];

    else if (addr < 0x2000)
        return sound_chip.read(addr - 0x1000);

    else if (addr >= 0x4000 && addr <= 0xbfff)
        return params_rom[(addr - 0x4000) | ((latch_val & 0b11) << 15)];

    RD_TRACE("%04x: unk read %04x\n", cpu->programCounter(), addr);
    return 0xFF;
}

inline void RdBoard::write(u16 addr, u8 data)
{
    // 0x0000/0x0001: dirección de los puertos, sin emular
    if (addr == 0x0000 || addr == 0x0001)
    {
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

    else if (addr < 0x20)
    {
        cpu->writeCpuRegister(addr, data);
    }

    else if (addr < 0x1000)
    {
        ram[addr] = data;
    }

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

    else
    {
        latch_val = data;
        RD_TRACE("latch write %04x=%02x\n", addr, data);
    }
}

#endif
