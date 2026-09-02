#ifndef SOUND_CHIP_H
#define SOUND_CHIP_H

#include "mame_utils.h"
#include "sa_blocks.h"
#include "sa_tables.h"

class SoundChip
{
  public:
    // Ranuras de tablas de onda descifradas: una por juego de ROM, para que
    // cambiar de juego sea activar una ranura y no descifrar 768 KB.
    static constexpr unsigned NUM_WAVE_SLOTS = 3;

    SoundChip(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7);
    ~SoundChip();

    // Ver Mcu: no copiable ni movible, por el mismo motivo.
    SoundChip(const SoundChip &) = delete;
    SoundChip &operator=(const SoundChip &) = delete;
    SoundChip(SoundChip &&) = delete;
    SoundChip &operator=(SoundChip &&) = delete;

    u8 read(size_t offset);
    void write(size_t offset, u8 data);

    s32 update();

    // Estado de síntesis a cero: las 160 SA_Part y la IRQ pendiente. No toca las
    // tablas de onda —son la ROM descifrada, no estado— ni el juego de reserva
    // que espera publicación.
    void reset();

    // Carga de las ROM de onda, partida en dos por coste. `decode_samples()` es
    // la parte cara (~2,9 ms) y escribe en la ranura que se le diga, que no
    // tiene por qué ser la activa: se descifran todas al construir y desde ahí
    // cambiar de juego es `select_samples()`, un puntero.
    void decode_samples(unsigned slot, const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7);
    void select_samples(unsigned slot);

    // Descifra el juego en la ranura 0 y la activa: el camino de siempre para
    // quien solo usa un juego de ROM.
    void load_samples(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7);

    // if there is an IRQ currently waiting
    bool m_irq_triggered = false;

  private:
    static constexpr unsigned NUM_VOICES = 16;
    static constexpr unsigned PARTS_PER_VOICE = 10;
    static constexpr unsigned PARTS_PER_VOICE_MEM = 16;

    // Una dirección de la wave ROM ya descifrada. `exp` usa 14 bits y `delta` 9,
    // así que el signo —que antes ocupaba un `bool` de un byte por tabla— cabe
    // en el bit 15 de cada uno: 512 KB por ranura en vez de 768 KB, y una sola
    // línea de caché por lectura en vez de cuatro tablas paralelas.
    static constexpr uint16_t WAVE_SIGN = 0x8000;
    static constexpr uint16_t WAVE_SAMPLE = 0x7FFF;

    struct WaveEntry
    {
        uint16_t exp;
        uint16_t delta;
    };

    // Las tablas de onda ya descifradas, una ranura por juego de ROM. En el
    // montón y no en el objeto: son 512 KB por ranura.
    struct WaveTables
    {
        WaveEntry entries[0x20000];
    };

    WaveTables *wave_slots = nullptr;
    WaveTables *waves = nullptr;

    // LUT deterministas, compartidas por todas las instancias.
    const SaTables &tables;

    SA_Part m_parts[NUM_VOICES][PARTS_PER_VOICE_MEM]; // channel memory
    uint8_t m_irq_id = 0;                             // voice/part that triggered the IRQ
};

#endif
