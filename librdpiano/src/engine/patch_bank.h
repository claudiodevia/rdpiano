#ifndef RD_PATCH_BANK_H
#define RD_PATCH_BANK_H

/**
 * @file patch_bank.h
 * @brief Los 16 parches ya descifrados: cambiar de parche cuesta microsegundos y cabe en el hilo de audio.
 */

#include "mame_utils.h"

class Mcu;
struct RdRomSet;

/**
 * @brief Las tablas de onda de los tres juegos de ROM, en sus ranuras del emulador, y las 16 páginas de
 *        parámetros.
 *
 * Todo lo caro de un cambio de parche se hace al construir (~1,2 ms y 512 KB por juego de onda, 32 KB por
 * página): después, cambiar es activar una ranura y copiar una página.
 */
class PatchBank
{
  public:
    /**
     * @brief Descifra en `mcu` los juegos de ROM que no descifró su constructor y las 16 páginas.
     * @param romSets ROMSET_COUNT juegos, indexados por RomSetId. No se copian ni se guardan.
     * @param mcu Construido con el juego del parche 0, que ya ocupa la ranura 0.
     */
    PatchBank(const RdRomSet *romSets, Mcu &mcu);
    ~PatchBank();

    PatchBank(const PatchBank &) = delete;
    PatchBank &operator=(const PatchBank &) = delete;

    /** @brief La página de parámetros ya descifrada de un parche, PARAMS_PAGE_BYTES. */
    const u8 *page(int patch) const;

    /** @brief Pone un parche en el emulador: juego de onda, página, relectura del firmware y ritmo de la CPU. */
    void activate(Mcu &mcu, int patch) const;

  private:
    u8 *pages = nullptr;
};

#endif
