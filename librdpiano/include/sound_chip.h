#ifndef SOUND_CHIP_H
#define SOUND_CHIP_H

#include "mame_utils.h"
#include "sa_blocks.h"
#include "sa_tables.h"

/**
 * @file sound_chip.h
 * @brief Los tres chips de síntesis (IC19, IC9, IC8) reimplementados a nivel de puertas.
 */

/**
 * @brief El motor de síntesis: 16 voces × 10 partes, mapeado en 0x1000-0x1FFF.
 *
 * IC19 lleva la envolvente y avisa por IRQ al final de cada segmento, IC9 la
 * fase y la dirección de la wave ROM, e IC8 suma volumen logarítmico y muestra.
 */
class SoundChip
{
  public:
    /// Ranuras de tablas de onda descifradas: una por juego de ROM, para que
    /// cambiar de juego sea activar una ranura y no descifrar 768 KB.
    static constexpr unsigned NUM_WAVE_SLOTS = 3;

    SoundChip(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7);
    ~SoundChip();

    /// Ver Mcu: no copiable ni movible, por el mismo motivo.
    SoundChip(const SoundChip &) = delete;
    SoundChip &operator=(const SoundChip &) = delete;
    SoundChip(SoundChip &&) = delete;
    SoundChip &operator=(SoundChip &&) = delete;

    /**
     * @brief Lee el bus del chip, que sólo publica un dato: quién disparó la IRQ.
     * @param offset Dirección relativa a 0x1000; el chip no la mira.
     * @return Voz y parte que dispararon la IRQ, empaquetadas como (voz << 4) | parte.
     */
    u8 read(size_t offset);

    /**
     * @brief Escribe un registro de parte.
     * @param offset Dirección relativa a 0x1000.
     * @param data Byte a escribir.
     */
    void write(size_t offset, u8 data);

    /**
     * @brief Avanza un tick de síntesis: 16 voces × 10 partes × 3 bloques.
     * @return La muestra sumada de todas las voces.
     */
    s32 update();

    /**
     * @brief Estado de síntesis a cero: las 160 SA_Part y la IRQ pendiente.
     *
     * No toca las tablas de onda —son la ROM descifrada, no estado— ni el juego
     * de reserva que espera publicación.
     */
    void reset();

    /**
     * @brief Descifra un juego de ROM de onda en una ranura (~2,9 ms).
     *
     * La ranura no tiene por qué ser la activa: se descifran todas al construir y
     * desde ahí cambiar de juego es select_samples(), un puntero.
     *
     * @param slot Ranura de destino, < NUM_WAVE_SLOTS.
     * @param temp_ic5 ROM de onda IC5.
     * @param temp_ic6 ROM de onda IC6.
     * @param temp_ic7 ROM de onda IC7.
     */
    void decode_samples(unsigned slot, const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7);

    /**
     * @brief Activa una ranura ya descifrada.
     * @param slot Ranura poblada por decode_samples().
     */
    void select_samples(unsigned slot);

    /**
     * @brief Descifra el juego en la ranura 0 y la activa.
     *
     * El camino de siempre para quien solo usa un juego de ROM.
     *
     * @param temp_ic5 ROM de onda IC5.
     * @param temp_ic6 ROM de onda IC6.
     * @param temp_ic7 ROM de onda IC7.
     */
    void load_samples(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7);

    bool m_irq_triggered = false; ///< Hay una IRQ esperando a que la CPU la atienda.

  private:
    static constexpr unsigned NUM_VOICES = 16;
    static constexpr unsigned PARTS_PER_VOICE = 10;
    static constexpr unsigned PARTS_PER_VOICE_MEM = 16;

    /// `exp` usa 14 bits y `delta` 9, así que el signo —que antes ocupaba un
    /// `bool` de un byte por tabla— cabe en el bit 15 de cada uno: 512 KB por
    /// ranura en vez de 768 KB, y una sola línea de caché por lectura en vez de
    /// cuatro tablas paralelas.
    static constexpr uint16_t WAVE_SIGN = 0x8000;
    static constexpr uint16_t WAVE_SAMPLE = 0x7FFF;

    /** @brief Una dirección de la wave ROM ya descifrada. */
    struct WaveEntry
    {
        uint16_t exp;
        uint16_t delta;
    };

    /** @brief Las tablas de un juego de ROM. En el montón y no en el objeto: 512 KB por ranura. */
    struct WaveTables
    {
        WaveEntry entries[0x20000];
    };

    WaveTables *wave_slots = nullptr;
    WaveTables *waves = nullptr;

    const SaTables &tables; ///< LUT deterministas, compartidas por todas las instancias.

    SA_Part m_parts[NUM_VOICES][PARTS_PER_VOICE_MEM];
    uint8_t m_irq_id = 0; ///< Voz/parte que disparó la IRQ.
};

#endif
