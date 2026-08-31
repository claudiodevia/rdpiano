#ifndef SOUND_CHIP_H
#define SOUND_CHIP_H

#include "mame_utils.h"
#include "sa_blocks.h"
#include "sa_tables.h"

class SoundChip {
public:
  SoundChip(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7);

  // Ver Mcu: no copiable ni movible, por el mismo motivo.
  SoundChip(const SoundChip &) = delete;
  SoundChip &operator=(const SoundChip &) = delete;
  SoundChip(SoundChip &&) = delete;
  SoundChip &operator=(SoundChip &&) = delete;

  u8 read(size_t offset);
  void write(size_t offset, u8 data);

  s32 update();

  void load_samples(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7);

  // if there is an IRQ currently waiting
  bool m_irq_triggered = false;

private:
  static constexpr unsigned NUM_VOICES = 16;
  static constexpr unsigned PARTS_PER_VOICE = 10;
  static constexpr unsigned PARTS_PER_VOICE_MEM = 16;

  uint16_t samples_exp[0x20000];
  bool samples_exp_sign[0x20000];
  uint16_t samples_delta[0x20000];
  bool samples_delta_sign[0x20000];

  // LUT deterministas, compartidas por todas las instancias (§4).
  const SaTables &tables;

  SA_Part m_parts[NUM_VOICES][PARTS_PER_VOICE_MEM];    // channel memory
  uint8_t m_irq_id = 0;						                     // voice/part that triggered the IRQ
};

#endif
