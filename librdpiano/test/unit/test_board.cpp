// Mapa de memoria y latch de banco.
//
// De momento cubre solo la aritmética de direcciones del bus, que es lo que
// hace falta para autorizar el cambio de máscara de la ROM de programa
// (REFACTORIZACION §2, §17.4). El mapa completo —RAM, SoundChip, latch, página
// de params— se prueba escribiendo y leyendo cuando exista `RdBoard` (fase 1):
// hoy `read_byte`/`write_byte` son privados de `Mcu`.

#include "unit_test.h"

// `read_byte` acota la ROM de programa sobre un array de 0x2000. La máscara
// era `& 0xdfff`: funciona —limpiar el bit 13 de 0..0x3fff da 0..0x1fff— pero
// se lee como un error. Esta suite fija la equivalencia en el único rango que
// el bus puede producir: addr >= 0xc000, luego offset 0..0x3fff.
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
