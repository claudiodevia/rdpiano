#include "patch_bank.h"

#include "mcu.h"
#include "patches.h"
#include "rd_engine.h"
#include "rom_loader.h"

/**
 * @file patch_bank.cpp
 * @brief Descifrado de los juegos de ROM y las páginas al construir; activación de un parche.
 */

// La ranura s guarda el juego s, y el constructor de Mcu ya descifró en la 0
// el del parche 0.
static_assert(ROMSET_COUNT <= (int)SoundChip::NUM_WAVE_SLOTS);
static_assert(patchToRomSetId[0] == 0);

PatchBank::PatchBank(const RdRomSet *romSets, Mcu &mcu)
{
    for (int s = 1; s < ROMSET_COUNT; s++)
        mcu.decodeRomSet((unsigned)s, romSets[s].ic5, romSets[s].ic6, romSets[s].ic7);

    pages = new u8[(size_t)NUM_PATCHES * PARAMS_PAGE_BYTES];
    for (int p = 0; p < NUM_PATCHES; p++)
        decode_params_window(pages + (size_t)p * PARAMS_PAGE_BYTES, romSets[patchToRomSetId[p]].ic18, patchToOffset[p]);
}

PatchBank::~PatchBank() { delete[] pages; }

const u8 *PatchBank::page(int patch) const { return pages + (size_t)patch * PARAMS_PAGE_BYTES; }

void PatchBank::activate(Mcu &mcu, int patch) const
{
    mcu.activatePatch((unsigned)patchToRomSetId[patch], page(patch), patchToOffset[patch]);
    mcu.setSampleRate(patchSampleRates[patch]);
}
